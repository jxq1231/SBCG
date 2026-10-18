# M0.1 单板系统时钟启动验收

验收日期：2026-10-18。结论：**当前连接的一块STM32F746通过调试复位启动与短时运行检查**。不将此结论推广到另一块板、断电冷启动、绝对频率精度或后续测距功能。

## 1. 授权与变更范围

沿用用户允许的连接、复位/暂停和读取权限。此次未调用Flash编程、擦除或选项字节修改命令，也未通过F5的launch流程下载固件。只设置临时硬件断点，读取Flash、SRAM和寄存器；结束时删除断点并恢复运行。

未修改固件、`.ioc`、CubeMX初始化、调试配置、IRQ、DMA或缓存策略。本次提交只保存验收结论和同步文档状态。

## 2. 工程、实物与固件身份

| 项目 | 记录 |
| --- | --- |
| 工程 | SBCG / STM32F746NGH6 / TFBGA216 |
| 验收前仓库提交 | f9967229fbc2c27d96e96649307a9383c3d04c31 |
| 时钟生成代码最近提交 | 792ba7c，未在本轮改动 |
| 构建 | `python -B Tools/Monitor/autodebug.py build`，Debug配置/构建退出0，Ninja无待构建项 |
| ELF | `build/Debug/SBCG.elf` |
| ELF SHA256 | `ab320042b76f0aeed5aad7b0da343e8bd662a199c5a67b7715c258d082637060` |
| 探针 | ST-LINK V2J33M25，目标电压约3.235 V |
| OpenOCD | xPack 0.12.0+dev-02228-ge5888bda3，安装包0.12.0-7 |
| 连接配置 | `.vscode/openocd-stm32f746.cfg`，1800 kHz，硬件复位下连接 |
| MCU UIDw0 | `0x001A002D` |
| MCU UIDw1 | `0x30365105` |
| MCU UIDw2 | `0x36363631` |
| 板卡身份边界 | PCB修订和A/B角色尚未指定；上述为真实MCU UID，不是ST-LINK USB序列号 |

先解析ELF的Flash加载段，再用OpenOCD的`dump_image`只读回0x08000000起的5500字节，逐段对照ELF数据。未使用可能在目标RAM运行校验算法的验收路径。

| Flash加载地址 | 长度 | 比对 |
| --- | --- | --- |
| 0x08000000 | 5488字节 | 全部相同 |
| 0x08001570 | 12字节，SRAM初始化数据的Flash加载副本 | 全部相同 |

读回5500字节的SHA256为`d44805b91952003d08cb559545ffea36f875862f4e9a7b2e2ce4ba0a4ba73dab`。这是Flash字节的哈希，与包含调试信息的ELF文件哈希不同。

## 3. 启动与运行观察

使用ELF符号和反汇编确认断点地址，均为硬件断点：

- `MX_GPIO_Init()`入口0x08001064：证明`SystemClock_Config()`已返回。
- 主循环0x08000F84：证明GPIO初始化返回后已进入循环。
- 同时监视`Error_Handler`、HardFault、MemManage、BusFault、UsageFault入口。未命中这些断点；各次快照CFSR/HFSR均为0。

| 观察阶段 | PC | SystemCoreClock | uwTick（ms） | CFSR / HFSR |
| --- | --- | --- | --- | --- |
| 时钟初始化返回 | 0x08001064 | 200000000 | 0 | 0 / 0 |
| 首次进入主循环 | 0x08000F84 | 200000000 | 0 | 0 / 0 |
| 继续运行约1秒后暂停 | 0x08000F84 | 200000000 | 1002 | 0 / 0 |
| 再运行约2秒后暂停 | 0x08000F84 | 200000000 | 3005 | 0 / 0 |
| 再运行约5秒后暂停 | 0x08000F84 | 200000000 | 8006 | 0 / 0 |
| 再次复位并运行约1秒 | 0x08000F84 | 200000000 | 1131 | 0 / 0 |

分段tick增量为1002、2003、5001 ms；暂停期间不用于计时。最后一行是独立复位后的读数，不能与前一行求连续增量。主机等待、命令和复位开销均存在，以上不是时钟精度测量。各次xPSR为0x01000000、PRIMASK为0，快照均在Thread模式。

## 4. 寄存器交叉核对

| 寄存器 | 观测值 | 核对结果 |
| --- | --- | --- |
| RCC_CR | 0x03078A83 | HSE使能、就绪、Bypass；PLL使能、就绪 |
| RCC_PLLCFGR | 0x22406419 | HSE源，M25、N400、P2、Q2 |
| RCC_CFGR | 0x0000940A | 当前系统源PLL，AHB /1，APB1 /4，APB2 /2 |
| FLASH_ACR | 0x00000006 | 6个等待周期 |
| PWR_CR1 / CSR1 | 0x0003C000 / 0x00034000 | Scale1、OverDrive启用且就绪 |
| SysTick_CTRL | 初次0x7，后续0x10007 | ENABLE/TICKINT/CLKSOURCE均为1；后续COUNTFLAG为1 |
| SysTick_LOAD | 0x00030D3F，即199999 | 与200 MHz、1 ms HAL时基配置一致 |
| SCB_CCR | 0x00040200 | I-Cache/D-Cache使能位均为0；本轮没有改变缓存配置 |
| SCB_VTOR | 0x00200000 | 原始观测值；本轮未修改向量表位置 |

按工程声明的25 MHz HSE与实测PLL/分频寄存器，HCLK计算为200 MHz、PCLK1为50 MHz、PCLK2为100 MHz。该交叉核对与`SystemCoreClock`一致，但不替代示波器或频率计的独立测频。

## 5. 调用链、IRQ及DMA

`main → MPU_Config → HAL_Init → SystemClock_Config → MX_GPIO_Init → 空主循环`。

现有SysTick IRQ调用`HAL_IncTick()`维护`uwTick`；异常入口仅用于硬件断点观察，没有修改ISR、回调或优先级。无新增IRQ和DMA关系，无新增共享缓冲、缓存维护或外设功能。

## 6. 证据位置、结果与后续

本地原始记录位于`Logs/Startup/20261018T085237Z/`，按现有规则由Git忽略：

- `image-readback.log`、`flash-readback.bin`、`image-report.json`：只读Flash比对证据。
- `startup-readonly.tcl`、`startup-readonly.log`、`runtime-report.json`：实际观察命令、输出及解析结果。
- 构建报告：`Logs/AutoDebug/20261018T085152-1791357a/report.json`。

日志中的两条`no breakpoint ... found`来自对已删除的时钟/主循环断点重复清理，非目标异常；其余监视断点清理完成，`FINAL_RESUME_OK`和`STARTUP_SCRIPT_COMPLETE`均已记录，OpenOCD退出0。初次临时脚本缺少右括号的解析失败发生在该脚本的目标初始化前，保留在`startup-script-parse-error.log`，不计为固件启动失败。

验收标记：`PASS_SINGLE_BOARD_DEBUG_RESET_STARTUP`。当前只关闭已连接板卡的调试复位启动检查；两板共用基线、第二块板UID/启动、A/B指定、PCB修订及冷启动验证仍待完成。VS Code Build界面消除匹配器提示仍需用户确认，主机构建不能代替该界面结果。

停在当前增量。后续另行确认独立目标；若继续显示路线，先通过CubeMX配置FMC/SDRAM及所需MPU区域并完成读写验收，再让LTDC访问帧缓冲。本轮未开展这些外设配置。

命令语义依据：[OpenOCD通用命令](https://openocd.org/doc/html/General-Commands.html)、[OpenOCD寄存器读取](https://openocd.org/doc-release/html/CPU-Configuration.html)。实际地址、寄存器位及结果以当前ELF、CMSIS头文件和上述本机日志为准。
