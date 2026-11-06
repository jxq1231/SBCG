/**
 * @file        bsp_flash_ram.h
 * @brief       在ITCM RAM中执行STM32F746内部Flash编程与扇区擦除的底层接口。
 * @details     单bank器件在Flash编程/擦除期间无法从Flash取指；本模块的寄存器序列
 *              位于.ItcmCode段，启动时由BSP_FlashRam_EnsureLoaded从Flash镜像复制到ITCM。
 *              忙等待用有界轮询计数实现，不依赖SysTick（该阶段已关闭中断）。
 * @note        调用者必须先EnsureLoaded，并在调用期间关闭中断；只允许操作已解锁的Flash。
 */
#ifndef SBCG_BSP_FLASH_RAM_H
#define SBCG_BSP_FLASH_RAM_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief   单字编程的忙等待轮询上限；216 MHz下约0.08～0.23 s。
 * @note    单字编程典型仅几十微秒，该上限只用于硬件异常时结束等待并返回false。
 */
#define BSP_FLASH_RAM_PROGRAM_WAIT_LIMIT UINT32_C(2000000)

/**
 * @brief   128 KiB扇区擦除的忙等待轮询上限；216 MHz下约1.8～5.6 s。
 * @details 轮询循环在ITCM执行且此时中断关闭，SysTick不推进，HAL_GetTick不能作为截止；
 *          编译实测循环体为5条指令(-Os)至11条(-O0)，按每次8～25周期保守估算：
 *          48,000,000次即48e6×8/216e6≈1.78 s，覆盖数据手册1 s量级的扇区擦除并留裕量。
 * @note    只放大等待上限，不改变擦除序列；超时仍返回false，由调用者报ERASE错误。
 */
#define BSP_FLASH_RAM_ERASE_WAIT_LIMIT UINT32_C(48000000)

/**
 * @brief   将ITCM中的Flash操作代码从Flash镜像复制到ITCM并同步指令流。
 * @return  无返回值；重复调用不重复复制。
 * @note    必须在任何Flash编程/擦除前、且只在未进行Flash操作时调用。
 */
void BSP_FlashRam_EnsureLoaded(void);

/**
 * @brief   在ITCM中编程一个32位字。
 * @param   address 目标Flash地址，必须4字节对齐且已擦除。
 * @param   data 待写入的32位小端数据。
 * @return  操作完成且无Flash错误为true。
 * @note    调用前需HAL_FLASH_Unlock并关闭中断；函数返回后Flash已不忙。
 *          故意使用x32字编程：x64双字编程需要外部VPP，本板未提供。
 */
bool BSP_FlashRam_ProgramWord(uint32_t address, uint32_t data);

/**
 * @brief   在ITCM中擦除一个Flash扇区。
 * @param   sector 扇区编号，当前布局为0..7。
 * @return  操作完成且无Flash错误为true。
 * @note    调用前需HAL_FLASH_Unlock并关闭中断；128KiB扇区擦除期间CPU从ITCM执行，
 *          忙等待上限为BSP_FLASH_RAM_ERASE_WAIT_LIMIT（216 MHz下≥1.5 s）。
 */
bool BSP_FlashRam_EraseSector(uint32_t sector);

#endif
