/**
 * @file        bsp_flash_ram.c
 * @brief       在ITCM中执行STM32F746单bank内部Flash的裸寄存器编程与扇区擦除。
 * @details     F746单bank在编程/擦除期间无法从Flash取指，而HAL的Flash函数本身位于Flash。
 *              本文件把唯一的寄存器操作函数放在可复制的itcm_code_src输入段（链接在Flash），
 *              启动阶段把该段字节复制到ITCM的NOLOAD缓冲，之后只通过Thumb函数指针执行。
 *              命令位与STM32F7参考手册及HAL实现一致；错误标志操作前后由FLASH_SR清除和检查。
 *              编程使用x32字访问：本板无外部VPP，x64双字编程会置PGPERR。
 *              忙等待按操作区分预算：单字编程用短上限，128 KiB扇区擦除用覆盖≥1.5 s的上限。
 * @note        复制后的函数为位置无关的相对分支/字面量，不得调用任何Flash内函数或依赖中断；
 *              调用者必须先EnsureLoaded并在调用期间关闭中断。
 */
#include "bsp_flash_ram.h"
#include "main.h"
#include <string.h>

/* 可复制源段由链接器为孤儿段itcm_code_src自动提供；目标缓冲由链接脚本放在ITCM。 */
extern uint8_t __start_itcm_code_src[];
extern uint8_t __stop_itcm_code_src[];
extern uint8_t __itcm_code_start__[];
extern uint8_t __itcm_code_end__[];

#define FLASH_RAM_REG_BASE UINT32_C(0x40023C00) /* FLASH_R/ACR起始地址。 */
/* STM32F746的FLASH寄存器顺序为ACR,KEYR,OPTKEYR,SR(0x0C),CR(0x10),OPTCR,OPTCR1。 */
#define FLASH_RAM_SR_INDEX 3U                  /* FLASH_SR偏移0x0C。 */
#define FLASH_RAM_CR_INDEX 4U                  /* FLASH_CR偏移0x10。 */
#define FLASH_RAM_SR_BSY UINT32_C(0x00010000)
#define FLASH_RAM_SR_CLEAR UINT32_C(0x000000F3) /* EOP/OPERR/WRPERR/PGAERR/PGPERR/ERSERR写1清除。 */
#define FLASH_RAM_SR_ERRORS UINT32_C(0x000000FE) /* OPERR..ERSERR位，不含EOP。 */
#define FLASH_RAM_CR_PSIZE_MASK UINT32_C(0x00000300)
#define FLASH_RAM_CR_PSIZE_WORD UINT32_C(0x00000200)
#define FLASH_RAM_CR_PG UINT32_C(0x00000001)
#define FLASH_RAM_CR_SER UINT32_C(0x00000002)
#define FLASH_RAM_CR_SNB_MASK UINT32_C(0x00000078)
#define FLASH_RAM_CR_STRT UINT32_C(0x00010000)
#define FLASH_RAM_ACTION_PROGRAM 0U
#define FLASH_RAM_ACTION_ERASE 1U

typedef bool (*BSP_FlashRamOperation)(uint32_t action, uint32_t target, uint32_t data);

_Static_assert(BSP_FLASH_RAM_ERASE_WAIT_LIMIT > BSP_FLASH_RAM_PROGRAM_WAIT_LIMIT,
               "erase wait budget must exceed program budget");

static bool flash_ram_loaded;

/**
 * @brief   在Flash忙时由ITCM执行的唯一低层操作函数。
 * @param   action 0为单字编程，1为扇区擦除。
 * @param   target 编程地址或扇区编号。
 * @param   data 编程数据，擦除时忽略。
 * @return  操作完成且无Flash错误为true。
 * @note    不得调用外部函数；所有等待为有界轮询，擦除与编程分别使用各自的计数上限。
 */
__attribute__((section("itcm_code_src"),noinline,used))
static bool ItcmFlashOperation(uint32_t action, uint32_t target, uint32_t data)
{
    volatile uint32_t *regs = (volatile uint32_t *)FLASH_RAM_REG_BASE;
    /* 擦除等待必须覆盖数百毫秒到1 s量级；编程只需覆盖微秒级。 */
    const uint32_t wait_limit = action == FLASH_RAM_ACTION_ERASE ?
        BSP_FLASH_RAM_ERASE_WAIT_LIMIT : BSP_FLASH_RAM_PROGRAM_WAIT_LIMIT;
    bool ready = false;
    for (uint32_t i = 0U; i < wait_limit; ++i)
    {
        if ((regs[FLASH_RAM_SR_INDEX] & FLASH_RAM_SR_BSY) == 0U)
        {
            ready = true;
            break;
        }
    }
    if (!ready)
    {
        return false;
    }
    regs[FLASH_RAM_SR_INDEX] = FLASH_RAM_SR_CLEAR;
    if (action == FLASH_RAM_ACTION_PROGRAM)
    {
        regs[FLASH_RAM_CR_INDEX] &= ~FLASH_RAM_CR_PSIZE_MASK;
        regs[FLASH_RAM_CR_INDEX] |= FLASH_RAM_CR_PSIZE_WORD | FLASH_RAM_CR_PG;
        *(volatile uint32_t *)target = data;
        __DSB();
    }
    else
    {
        regs[FLASH_RAM_CR_INDEX] &= ~(FLASH_RAM_CR_PSIZE_MASK | FLASH_RAM_CR_SER | FLASH_RAM_CR_SNB_MASK);
        regs[FLASH_RAM_CR_INDEX] |= FLASH_RAM_CR_PSIZE_WORD | FLASH_RAM_CR_SER |
            ((target & 0xFU) << 3U);
        regs[FLASH_RAM_CR_INDEX] |= FLASH_RAM_CR_STRT;
    }

    /* 等待擦除/编程结束：超时同样清除SER/PG并按false上交错误，由调用方放弃本次登记；
       残余扇区状态由下一次启动的扇区扫描重新判定，不在本函数内重试。 */
    ready = false;
    for (uint32_t i = 0U; i < wait_limit; ++i)
    {
        if ((regs[FLASH_RAM_SR_INDEX] & FLASH_RAM_SR_BSY) == 0U)
        {
            ready = true;
            break;
        }
    }
    if (action == FLASH_RAM_ACTION_PROGRAM)
    {
        regs[FLASH_RAM_CR_INDEX] &= ~FLASH_RAM_CR_PG;
    }
    else
    {
        regs[FLASH_RAM_CR_INDEX] &= ~(FLASH_RAM_CR_SER | FLASH_RAM_CR_SNB_MASK);
    }
    return ready && (regs[FLASH_RAM_SR_INDEX] & FLASH_RAM_SR_ERRORS) == 0U;
}

/** @brief 读取复制到ITCM的Thumb入口，bit0置位以满足BX状态切换。 @return 函数指针。 */
static BSP_FlashRamOperation ItcmOperation(void)
{
    return (BSP_FlashRamOperation)((uintptr_t)__itcm_code_start__ | 1U);
}

/**
 * @brief   将ITCM中的Flash操作代码从Flash镜像复制到ITCM。
 * @return  无返回值。
 */
void BSP_FlashRam_EnsureLoaded(void)
{
    if (flash_ram_loaded)
    {
        return;
    }
    const uint32_t source_bytes = (uint32_t)(__stop_itcm_code_src - __start_itcm_code_src);
    const uint32_t target_bytes = (uint32_t)(__itcm_code_end__ - __itcm_code_start__);
    if (source_bytes == 0U || source_bytes > target_bytes)
    {
        return;
    }
    memcpy(__itcm_code_start__, __start_itcm_code_src, source_bytes);
    __DSB();
    __ISB();
    flash_ram_loaded = true;
}

/**
 * @brief   在ITCM中编程一个32位字。
 * @param   address 目标Flash地址。
 * @param   data 待写入数据。
 * @return  成功为true。
 */
bool BSP_FlashRam_ProgramWord(uint32_t address, uint32_t data)
{
    if (!flash_ram_loaded)
    {
        return false;
    }
    return ItcmOperation()(FLASH_RAM_ACTION_PROGRAM, address, data);
}

/**
 * @brief   在ITCM中擦除一个Flash扇区。
 * @param   sector 扇区编号。
 * @return  成功为true。
 */
bool BSP_FlashRam_EraseSector(uint32_t sector)
{
    if (!flash_ram_loaded)
    {
        return false;
    }
    return ItcmOperation()(FLASH_RAM_ACTION_ERASE, sector, 0U);
}
