/**
 * @file        bsp_boot_counter.c
 * @brief       在STM32F746内部Flash的两个保留128KiB扇区中维护跨掉电启动代次。
 * @details     扇区6(0x080C0000)与扇区7(0x080E0000)位于应用镜像之后，每槽32字节，
 *              先扫描两扇区有效记录的最大值，再按双扇区轮转计划追加；
 *              目标槽必须全擦除，需要时只擦除不保存全局最大值的另一扇区。
 *              单bank器件编程/擦除期间不能从Flash取指，因此实际寄存器序列由
 *              bsp_flash_ram.c的ITCM副本执行；随后回读整条记录校验，成功才把值交给模式服务。
 * @note        只在App_Init的启动诊断阶段调用一次；顺序执行会阻塞CPU且短暂暂停Flash取指，
 *              因此不得在DMA/音频/网络运行后调用。不改变时钟、选项字节或擦除范围之外的内容。
 */
#include "bsp_boot_counter.h"
#include "boot_counter_record.h"
#include "bsp_flash_ram.h"
#include "main.h"
#include <string.h>

/* 扇区6(0x080C0000)/7(0x080E0000)紧跟应用镜像：镜像代码与初始化数据装载镜像必须止于
   0x080C0000之前，由cmake/linker/stm32f746_eth.ld的链接期ASSERT强制；越过时擦除扇区6
   会删除正在执行的代码，下一次复位将无法启动。 */
#define BOOT_COUNTER_SECTOR_COUNT 2U
#define BOOT_COUNTER_FIRST_SECTOR_INDEX 6U

static const uint32_t boot_counter_addresses[BOOT_COUNTER_SECTOR_COUNT] = {
    UINT32_C(0x080C0000), UINT32_C(0x080E0000)
};
static const uint32_t boot_counter_sector_ids[BOOT_COUNTER_SECTOR_COUNT] = {
    FLASH_SECTOR_6, FLASH_SECTOR_7
};

_Static_assert(BOOT_COUNTER_RECORD_BYTES % 8U == 0U, "Flash双字编程要求记录8字节对齐");
_Static_assert(BOOT_COUNTER_SECTOR_BYTES == (128U * 1024U), "保留扇区必须为128KiB");
_Static_assert(BOOT_COUNTER_SLOTS == 4096U, "启动代次扇区槽数必须为4096");
_Static_assert((BOOT_COUNTER_FIRST_SECTOR_INDEX + 1U) < 8U, "保留扇区必须位于8扇区器件内");

/** @brief 从记录字节按小端取出一个32位字。 @param bytes 非NULL只读4字节。 @return 字值。 */
static uint32_t LoadWord(const uint8_t *bytes)
{
    uint32_t value = 0U;
    for (unsigned i = 0U; i < 4U; ++i)
    {
        value |= (uint32_t)bytes[i] << (8U * i);
    }
    return value;
}

/**
 * @brief   擦除一个保留扇区。
 * @param   sector_index 保留扇区下标0或1。
 * @return  擦除完成且无Flash错误为true。
 * @note    实际擦除序列在ITCM副本中执行。
 */
static bool EraseSector(uint32_t sector_index)
{
    if (sector_index >= BOOT_COUNTER_SECTOR_COUNT)
    {
        return false;
    }
    return BSP_FlashRam_EraseSector(boot_counter_sector_ids[sector_index]);
}

/**
 * @brief   把32字节记录按八个字写入指定Flash地址并回读校验。
 * @param   address 目标槽地址，必须4字节对齐且已擦除。
 * @param   record 非NULL只读32字节记录。
 * @return  编程与回读全部成功为true。
 * @note    每个字的编程序列在ITCM副本中执行；使用x32字编程避免外部VPP要求。
 */
static bool ProgramRecord(uint32_t address, const uint8_t record[BOOT_COUNTER_RECORD_BYTES])
{
    if ((address % 4U) != 0U)
    {
        return false;
    }
    for (uint32_t word = 0U; word < (BOOT_COUNTER_RECORD_BYTES / 4U); ++word)
    {
        const uint32_t data = LoadWord(&record[word * 4U]);
        if (!BSP_FlashRam_ProgramWord(address + (word * 4U), data))
        {
            return false;
        }
    }
    return memcmp((const void *)address, record, BOOT_COUNTER_RECORD_BYTES) == 0;
}

/**
 * @brief   读取历史最大代次并持久化下一代次。
 * @param   out 非NULL输出；成功前保持清零。
 * @return  结果状态码。
 */
BSP_BootCounterStatus BSP_BootCounter_Next(BSP_BootCounterResult *out)
{
    if (out == NULL)
    {
        return BSP_BOOT_COUNTER_ERROR_ARGUMENT;
    }
    *out = (BSP_BootCounterResult){0};

    BootCounterSector scans[BOOT_COUNTER_SECTOR_COUNT];
    for (uint32_t i = 0U; i < BOOT_COUNTER_SECTOR_COUNT; ++i)
    {
        if (!BootCounterSector_Scan((const uint8_t *)boot_counter_addresses[i],
                                    BOOT_COUNTER_SECTOR_BYTES, &scans[i]))
        {
            return BSP_BOOT_COUNTER_ERROR_LAYOUT;
        }
    }

    BootCounterPlan plan;
    if (!BootCounter_Plan(scans, &plan) || plan.action == BOOT_COUNTER_ACTION_FAIL ||
        plan.sector >= BOOT_COUNTER_SECTOR_COUNT || plan.slot >= BOOT_COUNTER_SLOTS)
    {
        return BSP_BOOT_COUNTER_ERROR_EXHAUSTED;
    }

    const uint32_t target_address = boot_counter_addresses[plan.sector] +
        (plan.slot * BOOT_COUNTER_RECORD_BYTES);
    if (!BootCounterRecord_IsErased((const uint8_t *)target_address) &&
        plan.action == BOOT_COUNTER_ACTION_APPEND)
    {
        /* 扫描说该槽为空但实际操作前发现非空：布局或并发写入异常，拒绝继续。 */
        return BSP_BOOT_COUNTER_ERROR_LAYOUT;
    }

    uint8_t record[BOOT_COUNTER_RECORD_BYTES];
    BootCounterRecord_Encode(plan.next_value, record);

    BSP_BootCounterStatus status = BSP_BOOT_COUNTER_OK;
    /* STM32F7单bank在编程期间不能可靠取指：关中断并暂时关闭预取/ART，
       操作完成并回读验证后再复位ART缓存、恢复原ACR和中断。 */
    BSP_FlashRam_EnsureLoaded();
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint32_t saved_acr = FLASH->ACR;
    FLASH->ACR = saved_acr & ~(FLASH_ACR_PRFTEN | FLASH_ACR_ARTEN);
    __DSB();
    __ISB();

    HAL_FLASH_Unlock();
    if (plan.action == BOOT_COUNTER_ACTION_ERASE_AND_APPEND && !EraseSector(plan.sector))
    {
        status = BSP_BOOT_COUNTER_ERROR_ERASE;
    }
    else if (!ProgramRecord(target_address, record))
    {
        status = BSP_BOOT_COUNTER_ERROR_PROGRAM;
    }
    HAL_FLASH_Lock();

    if (status == BSP_BOOT_COUNTER_OK)
    {
        /* 回读完整记录，确认提交标记、CRC和补码一致后才对外登记。 */
        BootCounterSector verify = {0};
        if (!BootCounterSector_Scan((const uint8_t *)boot_counter_addresses[plan.sector],
                                    BOOT_COUNTER_SECTOR_BYTES, &verify) ||
            verify.highest != plan.next_value)
        {
            status = BSP_BOOT_COUNTER_ERROR_VERIFY;
        }
    }

    __HAL_FLASH_ART_RESET();
    __DSB();
    FLASH->ACR = saved_acr;
    __DSB();
    __ISB();
    if ((primask & 1U) == 0U)
    {
        __enable_irq();
    }
    if (status != BSP_BOOT_COUNTER_OK)
    {
        return status;
    }

    out->value = plan.next_value;
    out->sector = (uint8_t)plan.sector;
    out->slot = (uint16_t)plan.slot;
    out->erased = plan.action == BOOT_COUNTER_ACTION_ERASE_AND_APPEND;
    return BSP_BOOT_COUNTER_OK;
}

/**
 * @brief   将状态码转换为稳定短字符串。
 * @param   status 待转换状态。
 * @return  非NULL只读常量字符串。
 */
const char *BSP_BootCounter_StatusName(BSP_BootCounterStatus status)
{
    switch (status)
    {
    case BSP_BOOT_COUNTER_OK: return "OK";
    case BSP_BOOT_COUNTER_ERROR_ARGUMENT: return "ARGUMENT";
    case BSP_BOOT_COUNTER_ERROR_LAYOUT: return "LAYOUT";
    case BSP_BOOT_COUNTER_ERROR_EXHAUSTED: return "EXHAUSTED";
    case BSP_BOOT_COUNTER_ERROR_ERASE: return "ERASE";
    case BSP_BOOT_COUNTER_ERROR_PROGRAM: return "PROGRAM";
    case BSP_BOOT_COUNTER_ERROR_VERIFY: return "VERIFY";
    default: return "UNKNOWN";
    }
}
