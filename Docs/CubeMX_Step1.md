# CubeMX第一步：现有系统时钟基线复核

<!-- doc-status: completed-baseline-reference; reviewed: 2026-10-23 -->

> 已核对当前代码及分版实测范围。现行状态只见[Project_State](Project_State.md)；本页早期实验按原日期理解，后续设计不等于已配置。

复核：2026-10-23。工程为现有`SBCG.ioc`，不要新建替代工程。本板此步骤已生成并通过调试复位启动验收；下文用于复核和后续协作，不要求重新修改已通过的配置。

## 1. 当前基线

| 项目 | 当前值 |
| --- | --- |
| MCU / 封装 | STM32F746NGH6 / TFBGA216 |
| 工具与固件包 | CubeMX 6.18.1 / CubeF7 V1.17.4 |
| 构建 | GCC/CMake/Ninja；`STM32F746xx_FLASH.ld`为1024 KiB |
| SYS Debug | Trace Asynchronous Sw，保留PA13/SWDIO、PA14/SWCLK、PB3/SWO |
| HAL时基 | SysTick；选择跟踪模式不自动产生SWO日志，当前未配置SWO接收 |
| HSE | Bypass Clock Source，工程输入25 MHz |
| PLL | HSE源，M25、N400、P2；当前Q2 |
| 总线 | SYSCLK/HCLK200 MHz；AHB /1；APB1 /4为50 MHz；APB2 /2为100 MHz |
| 电源与Flash | Scale1、OverDrive、FLASH_LATENCY_6 |
| 当前GPIO | 当前生成代码开启A/B/C/G/H/I端口时钟；PA15未分配，保留后续PPS |
| 已启用/未启用 | USART1含RX IRQ、ETH/LwIP/PTP、I2C3、SAI2/DMA2及PG7-DHT11已配置；FMC/LTDC/TIM2未启用，见[当前状态](Project_State.md) |

按此前核查的MB1191 B-04原理图，有源25 MHz时钟进入PH0，因此选择Bypass。本板实测RCC状态与之相符，但PCB修订和绝对输入频率尚未独立测量；其它实物不能直接套用结论。

当前PLLQ输出200 MHz。USB/RNG/SDMMC未使用，后续需要这些时钟域时重新规划；现有Q2不提供48 MHz输出。ST-LINK VCP由独立ST-LINK芯片处理，无需为它启用主MCU USB。

## 2. 在CubeMX中复核

1. 打开现有`SBCG.ioc`，确认器件、封装、软件包与生成工具。
2. **System Core → SYS**：Debug为Trace Asynchronous Sw，HAL时基为SysTick。
3. **System Core → RCC**：HSE为Bypass Clock Source，输入频率25 MHz。
4. **Clock Configuration**：核对M25/N400/P2、SYSCLK/HCLK200 MHz、PCLK1 50 MHz、PCLK2 100 MHz及各分频，没有未解释的时钟冲突。
5. 只有本次目标确需配置变化时才保存并Generate Code；检查生成差异与USER CODE保留情况，不手改`.ioc`或生成初始化。
6. 复用现有构建：

```powershell
cmake --preset Debug
cmake --build --preset Debug --parallel
```

本指南只覆盖系统基线。后续外设参数集中在[Hardware_Resources.md](Hardware_Resources.md)，开发顺序在[Project_Roadmap.md](Project_Roadmap.md)；不要在复核SYS/RCC时一次启用全部外设。

## 3. M0.1已归档实物验收结果

| 检查 | 实际结果 |
| --- | --- |
| Flash与Debug ELF | 两个加载段5500字节全部一致 |
| 时钟初始化返回 | 命中MX_GPIO_Init入口0x08001064 |
| 主循环 | 命中并多次观察0x08000F84 |
| SystemCoreClock | 200000000，与PLL/总线寄存器核对一致 |
| HAL tick | 分段运行uwTick=0→1002→3005→8006，再次复位后1131 |
| 异常 | 未命中所监视错误/异常入口；快照CFSR/HFSR均为0 |
| 当前UIDw0/w1/w2 | 001A002D / 30365105 / 36363631；未指定A/B |

地址与数值对应本次已比对ELF；完整哈希、寄存器、日志及限制见[M0_1_Startup.md](Milestones/M0_1_Startup.md)。当前板通过的是调试复位和短时运行，不是第二板、冷启动或独立测频。

上述实测ELF调用链为`main → MPU_Config → HAL_Init → SystemClock_Config → MX_GPIO_Init → 空主循环`。后续M0.2工作区已在GPIO之后加入USART1初始化、App_Init和主循环App_Process调用；启动/心跳新ELF已构建，用户日志已确认本板发送通过，见[M0.2报告](Milestones/M0_2_Serial.md)。现有SysTick仅维护HAL tick，无新增IRQ、DMA或缓存维护。

## 4. 以后复验时的操作边界

- 使用符号与Flash一致的ELF，在SystemClock_Config之后观察，确认进入主循环；继续运行两段时间后再暂停比较uwTick，暂停期间不要求增加。
- SystemCoreClock是软件值，需与RCC配置核对；它本身不等于独立测频。
- F5现为launch，会下载ELF；仅允许连接诊断时使用不下载固件的方式，不能把F5当只读attach。
- UID按w0/w1/w2记录，每字8位十六进制；当前CMSIS的UID_BASE为0x1FF0F420，三个32位地址依次为0x1FF0F420/424/428。ST-LINK USB序列号不能代替MCU UID。
- 当前用户要求不检查第二板。后续两板共用配置、UID角色和终验由相应协作阶段完成，不阻断当前板继续开发，也不提前标记为两板通过。

## 5. 本步状态

- [x] MCU、封装、1024 KiB链接设置已核对。
- [x] SYS/RCC生成值与当前板寄存器相符。
- [x] Debug构建及用户VS Code Build复验通过。
- [x] 用户烧录校验成功；后续只读Flash比对与单板启动观察通过。
- [ ] 冷启动、独立测频未执行；其它硬件性能按其里程碑单独验收。

本步SYS/RCC已完成，勿重复配置。后续USART1收发、网络/PTP观测、温度及音频均有各版实测；显示、双板同步和测距仍未实现。当前目标只见[项目状态](Project_State.md)。
