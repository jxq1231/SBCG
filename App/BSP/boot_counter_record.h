/**
 * @file        boot_counter_record.h
 * @brief       定义跨掉电启动代次的Flash记录格式、扇区扫描与双扇区轮转决策。
 * @details     记录为固定32字节大端无关的小端字段；扫描和轮转只做纯数据运算，
 *              不访问Flash寄存器、HAL、中断或全局状态，便于主机夹具验证掉电边界。
 * @note        硬件读写由bsp_boot_counter.c完成；本模块假定槽按32字节对齐且扇区长度固定。
 */
#ifndef SBCG_BOOT_COUNTER_RECORD_H
#define SBCG_BOOT_COUNTER_RECORD_H

#include <stdbool.h>
#include <stdint.h>

/** @brief 单条记录的固定字节数，必须与Flash双字编程粒度8字节对齐。 */
#define BOOT_COUNTER_RECORD_BYTES 32U

/** @brief 单个保留扇区的固定字节数，当前板级布局为128 KiB。 */
#define BOOT_COUNTER_SECTOR_BYTES (128U * 1024U)

/** @brief 扇区内可容纳的记录槽数。 */
#define BOOT_COUNTER_SLOTS (BOOT_COUNTER_SECTOR_BYTES / BOOT_COUNTER_RECORD_BYTES)

/** @brief 无可用空槽时的槽下标。 */
#define BOOT_COUNTER_NO_SLOT UINT32_MAX

/** @brief 单个扇区的扫描结果，所有字段由BootCounterSector_Scan写满。 */
typedef struct
{
    uint64_t highest;    /* 本扇区最大的有效代次，无有效记录时为0。 */
    uint32_t first_free; /* 首个全擦除槽下标，无空槽时为BOOT_COUNTER_NO_SLOT。 */
    bool has_valid;      /* 至少存在一条完整有效记录。 */
    bool has_partial;    /* 存在非擦除且解码失败的残写记录。 */
} BootCounterSector;

/** @brief 双扇区轮转决策动作。 */
typedef enum
{
    BOOT_COUNTER_ACTION_APPEND = 0,          /* 直接在目标槽追加。 */
    BOOT_COUNTER_ACTION_ERASE_AND_APPEND = 1, /* 先擦除目标扇区再追加。 */
    BOOT_COUNTER_ACTION_FAIL = 2             /* 代次耗尽或布局非法。 */
} BootCounterAction;

/** @brief 由两个扇区扫描结果得出的写入计划。 */
typedef struct
{
    BootCounterAction action;
    uint32_t sector;   /* 目标扇区下标0或1。 */
    uint32_t slot;     /* 目标记录槽下标。 */
    uint64_t next_value; /* 待写入的非零代次。 */
} BootCounterPlan;

/**
 * @brief   编码一条启动代次记录。
 * @param   value 待记录的非零代次，不得为0。
 * @param   out 非NULL的32字节输出，调用方持有。
 * @return  无返回值；value为0时输出全0xFF并由解码拒绝。
 */
void BootCounterRecord_Encode(uint64_t value, uint8_t out[BOOT_COUNTER_RECORD_BYTES]);

/**
 * @brief   校验并解码一条记录。
 * @param   in 非NULL只读32字节，仅本次借用。
 * @param   value 可为NULL的输出；非NULL时仅在成功时写入。
 * @return  格式、标记、CRC与补码全部一致且代次非零为true。
 */
bool BootCounterRecord_Decode(const uint8_t in[BOOT_COUNTER_RECORD_BYTES], uint64_t *value);

/**
 * @brief   判断槽是否为全擦除状态。
 * @param   in 非NULL只读32字节，仅本次借用。
 * @return  32字节均为0xFF时为true。
 */
bool BootCounterRecord_IsErased(const uint8_t in[BOOT_COUNTER_RECORD_BYTES]);

/**
 * @brief   扫描一个扇区的全部记录槽。
 * @param   sector 非NULL只读扇区首地址，长度至少BOOT_COUNTER_SECTOR_BYTES。
 * @param   sector_bytes 扇区字节数，必须为BOOT_COUNTER_SECTOR_BYTES的整数倍且非零。
 * @param   out 非NULL输出，调用方持有；失败时清零。
 * @return  参数合法时为true，否则false且out清零。
 * @note    不因首个空槽停止，以兼容擦除/残写造成的空洞；不修改输入。
 */
bool BootCounterSector_Scan(const uint8_t *sector, uint32_t sector_bytes, BootCounterSector *out);

/**
 * @brief   根据两个扇区扫描结果选择追加位置并计算下一代次。
 * @param   scans 非NULL只读两扇区扫描结果，下标0/1对应硬件布局。
 * @param   plan 非NULL输出，调用方持有；失败时action为FAIL。
 * @return  可写时为true；代次耗尽、目标扇区仍是最大值所在扇区等非法组合为false。
 * @note    永不擦除保存全局最大代次的扇区；无有效记录时从代次1重新开始。
 */
bool BootCounter_Plan(const BootCounterSector scans[2], BootCounterPlan *plan);

#endif
