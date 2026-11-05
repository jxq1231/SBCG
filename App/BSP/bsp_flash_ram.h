/**
 * @file        bsp_flash_ram.h
 * @brief       在ITCM RAM中执行STM32F746内部Flash编程与扇区擦除的底层接口。
 * @details     单bank器件在Flash编程/擦除期间无法从Flash取指；本模块的寄存器序列
 *              位于.ItcmCode段，启动时由BSP_FlashRam_EnsureLoaded从Flash镜像复制到ITCM。
 * @note        调用者必须先EnsureLoaded，并在调用期间关闭中断；只允许操作已解锁的Flash。
 */
#ifndef SBCG_BSP_FLASH_RAM_H
#define SBCG_BSP_FLASH_RAM_H

#include <stdbool.h>
#include <stdint.h>

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
 * @note    调用前需HAL_FLASH_Unlock并关闭中断；128KiB扇区擦除期间CPU从ITCM执行。
 */
bool BSP_FlashRam_EraseSector(uint32_t sector);

#endif
