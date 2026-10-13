# 分布式声波测距系统：STM32F746G-DISCO 硬件资源与 CubeMX 配置约定

日期：2026-10-13。适用：课程提供的两块 STM32F746G-DISCO / 32F746GDISCOVERY，MCU 为 **STM32F746NGH6，TFBGA216**。

**本文件已核查四份用户提供的 PDF；引脚映射以 UM1907 Rev 4 中的 MB1191 B-04 原理图及 I/O 分配表为依据。用户已确认温度传感器为DHT11；实际两板的PCB修订、焊桥/电阻状态、DHT11小板引脚顺序及板载上拉尚未现场确认。以下“推荐参数”是团队统一配置建议，不是已经在两块实物上验证通过的配置。**

## 0. SBCG工程现状与本次范围

项目根目录：`D:/electronic_design/SBCG`。本次里程碑为 **M0——硬件资源规划修订**，交付本目录下的 `Docs/Hardware_Resources.md`；本轮按用户要求在SBCG内提交更新后的工程源码及构建配置基线，并补充第一步指南。Git通过.gitignore排除build、.codex、compile_commands.json、对象文件、固件二进制及其它编译副产物，使其不出现在常规更改列表；保留Core/Drivers源码、.ioc、启动/链接脚本、CMake构建配置、共享VS Code配置及设计文档。Git作者/提交者按项目AGENTS.md使用jxq，邮箱由用户指定；不改全局身份。

已只读核对 `SBCG.ioc`、`Core/Inc/main.h`、`Core/Src/main.c`、顶层CMake及链接脚本，发现下列差异。**下文表格是目标分配，不代表当前工程已经启用这些外设。**

| 项目 | SBCG当前配置 | 目标/处理 |
| --- | --- | --- |
| MCU | **STM32F746NGH6 / STM32F746NGHx，TFBGA216** | 用户已修正，当前与开发板官方目标一致 |
| Flash | **链接脚本1024K，即1 MiB** | 已与NG目标一致；本轮未改链接脚本 |
| 软件版本 | CubeMX 6.18.1、DB.6.0.181、CubeF7 V1.17.4 | 两板固定同一版本；本文件BSP参数须对照该版本核查 |
| 显示格式约定 | **用户指定RGB565，每像素16位/2字节** | 后续CubeMX的LTDC图层、帧缓冲和绘图代码按第5.2节统一；当前未启用LTDC |
| 已启用资源 | SYS/RCC/NVIC/CORTEX_M7；SWD+SWO，PB3/PA13/PA14及HSE脚 | 未启用LTDC/FMC/ETH/USART1/I2C3/TIM2；DHT11的PG7未分配 |
| 时钟 | HCLK216 MHz、PCLK1 54 MHz、APB1定时器108 MHz；HSE_ON、25 MHz | 第5.1节200 MHz为建议目标，须通过CubeMX调整；不能照抄PSC=99到当前216 MHz时钟 |
| 生成GPIO | 仅开启GPIOA/B/H时钟，未定义DHT11/PPS标签 | CubeMX分配PG7及其他目标引脚后再生成GPIO初始化和标签宏 |

第一步操作见同目录 `CubeMX_Step1.md`。本地AGENTS.md第4节要求硬件变化由CubeMX完成，并在未经授权操作该流程时等待用户重新生成。因此本次没有改 `.ioc`、`SystemClock_Config()`、`MX_GPIO_Init()`、链接脚本、DMA/NVIC或固件调用链；配置差异和预期生成变化均写入第6节。

## 1. 先统一的结论

| 需求 | 两板统一采用的接口 | 结论与范围 |
| --- | --- | --- |
| 4.3英寸 LCD | 板载U19，480×272；**图层/帧缓冲RGB565** | 屏的RGB888物理连线完整保留；每像素2字节；不按SPI屏配置 |
| 显示缓冲 | 板载 U4 SDRAM，FMC SDRAM Bank1，16位数据总线 | 芯片标称128 Mbit，板上可访问64 Mbit，即8 MiB；地址基准0xC0000000 |
| 电容触控 | PH7/PH8 的 I2C3，PI13 中断 | 与音频编解码器共用 I2C3；LCD/触控复位接 NRST，没有独立的触控复位 GPIO |
| 交换机网络 | CN9 RJ45 → U9 LAN8742A → ETH RMII | 50 MHz RMII_REF_CLK 从 PHY 输入 PA1；不是 MCU MCO 输出 |
| 整秒脉冲 | **PA15 → CN7-2 / Arduino D9** | 推荐 TIM2_CH1（AF1）输出 `PPS_OUT`，3.3 V、正脉冲、建议100 μs；需跟随两板同步时间基准 |
| 观察/设参串口 | **USART1：PA9 TX、PB7 RX → ST-LINK → CN14 USB** | 115200、8数据位、无校验、1停止位、无流控；PC6/PC7 的 Arduino 串口不是 ST-LINK VCP |
| 温度补偿基础部分 | 串口或屏幕输入环境温度 | 无需外接传感器，满足任务书基础部分的接口要求 |
| 自动环境测温提高部分 | **课程DHT11：PG7 → CN4-5 / Arduino D4** | 单线数字温湿度接口；按用户确认采用DHT11，DATA开漏释放、上拉到3.3 V；无需I2C/ADC |

证据：资料[S1]第19～23、30、34～41、42～48页，[S2]复用表，[S3]第2～4页。方括号编号见文末。

## 2. 课程约束与验收目标

- 两板验收必须通过**交换机**组网；网线直连仅允许开发自测。上电后30秒内收敛到统一时间基准。
- 每板输出整秒脉冲，脉宽不小于1 μs；收敛后连续10次测量两路上升沿差，最大偏差不大于±10 μs。本文建议100 μs便于触发和读数，脉宽可由团队另行统一。[S3，第2页]
- 基础温度补偿允许串口命令、按键或界面设置环境温度；提高部分才要求自动获取环境温度。[S3，第3页]
- 不使用课程提供的开发板、模块和连接线之外的模块或电路。温度方案采用用户确认的课程DHT11；不能自行增加USB转串口、电平转换板或额外上拉电路并默认允许使用。[S3，第4页]
- 计算机不参与采集、计算和结果判定；串口只承担观察输出及设置参数。网络时钟同步须由板端完成。[S3，第2、5页]
- 两板距离真值取**麦克风开孔之间**的距离，验收前标记位置。[S3，第4页]
- `.ioc` 只能通过 CubeMX/CubeIDE 图形界面设置并保存，**不手改 `.ioc` 文本**。本文不生成或修改 `.ioc`。
- 遵循本次用户提供的 AGENTS.md：仓库未初始化先初始化；交付文档按约定使用 `docs` 类型提交。

## 3. 表格与命名约定

表内 MCU Pin 使用端口名，例如 `PA15`；括号内是 **MCU 的 TFBGA216 球位**，不是排针序号。`CN7-2` 表示板上 CN7 的2号针，`D9` 表示 Arduino 信号名。LCD 的 `U19-n` 表示原理图屏模块的端子号，不是可随意插线的扩展排针。

两板使用完全相同的 User Label。GPIO 标签采用 `LCD_DISP`、`LCD_BL_CTRL`、`PPS_OUT` 等；业务变量采用 `environment_temperature_c`、`relative_humidity_pct`、`temperature_valid`、`temperature_sample_age_ms`、`sound_speed_m_s`、`board_role`。外设句柄保持 CubeMX 生成的 `huart1`、`hltdc`、`hi2c3`、`heth`、`htim2`、`hsdram1` 等实际名称；不通过改引脚标签来重命名句柄。

普通 GPIO 的 User Label 通常生成 `<Label>_Pin` / `<Label>_GPIO_Port`；AF 引脚是否生成同样的宏取决于生成器。标签用于定位与团队沟通，不保证生成变量。外围 BSP 自带标签/句柄时应统一使用一套初始化，避免 CubeMX 与 BSP 重复改同一外设。

参数组：`L1`=LTDC；`L2`=LCD控制；`T1`=触控；`S1`=SDRAM；`E1`=ETH；`U1`=USART；`P1`=PPS；`X1`=DHT11温湿度接口。具体数值见第5节。“占用”同时记录板上硬连线和本方案分配；未启用某外设不等于其焊接的引脚成为空闲。

## 4. 完整引脚清单

### 4.1 LCD RGB与时序：28个 LTDC 引脚

依据[S1]图29、图33、表11及[S2]表12；由[S5] `BSP_LCD_MspInit()` 交叉核对。RGB888 是屏幕物理接线；帧缓冲可以选 RGB565，仍保留这一组物理引脚。

| 原始 MCU Pin（球位） | 板级物理接口/网络 | 复用功能 | 推荐 User Label | 参数 | 占用与待验证项 |
| --- | --- | --- | --- | --- | --- |
| PI15（G4） | U19-5 / LCD_R0 | LTDC_R0 / AF14 | LCD_R0 | L1 | 已硬连屏，需保留 |
| PJ0（R6） | U19-6 / LCD_R1 | LTDC_R1 / AF14 | LCD_R1 | L1 | 已硬连屏，需保留 |
| PJ1（R7） | U19-7 / LCD_R2 | LTDC_R2 / AF14 | LCD_R2 | L1 | 已硬连屏，需保留 |
| PJ2（P7） | U19-8 / LCD_R3 | LTDC_R3 / AF14 | LCD_R3 | L1 | 已硬连屏，需保留 |
| PJ3（N8） | U19-9 / LCD_R4 | LTDC_R4 / AF14 | LCD_R4 | L1 | 已硬连屏，需保留 |
| PJ4（M9） | U19-10 / LCD_R5 | LTDC_R5 / AF14 | LCD_R5 | L1 | 已硬连屏，需保留 |
| PJ5（M14） | U19-11 / LCD_R6 | LTDC_R6 / AF14 | LCD_R6 | L1 | 已硬连屏，需保留 |
| PJ6（K12） | U19-12 / LCD_R7 | LTDC_R7 / AF14 | LCD_R7 | L1 | 已硬连屏，需保留 |
| PJ7（J12） | U19-13 / LCD_G0 | LTDC_G0 / AF14 | LCD_G0 | L1 | 已硬连屏，需保留 |
| PJ8（H12） | U19-14 / LCD_G1 | LTDC_G1 / AF14 | LCD_G1 | L1 | 已硬连屏，需保留 |
| PJ9（J13） | U19-15 / LCD_G2 | LTDC_G2 / AF14 | LCD_G2 | L1 | 已硬连屏，需保留 |
| PJ10（H13） | U19-16 / LCD_G3 | LTDC_G3 / AF14 | LCD_G3 | L1 | 已硬连屏，需保留 |
| PJ11（G12） | U19-17 / LCD_G4 | LTDC_G4 / AF14 | LCD_G4 | L1 | 已硬连屏，需保留 |
| PK0（G13） | U19-18 / LCD_G5 | LTDC_G5 / AF14 | LCD_G5 | L1 | 已硬连屏，需保留 |
| PK1（F12） | U19-19 / LCD_G6 | LTDC_G6 / AF14 | LCD_G6 | L1 | 已硬连屏，需保留 |
| PK2（F13） | U19-20 / LCD_G7 | LTDC_G7 / AF14 | LCD_G7 | L1 | 已硬连屏，需保留 |
| PE4（A1） | U19-21 / LCD_B0 | LTDC_B0 / AF14 | LCD_B0 | L1 | 已硬连屏，需保留 |
| PJ13（B9） | U19-22 / LCD_B1 | LTDC_B1 / AF14 | LCD_B1 | L1 | 已硬连屏，需保留 |
| PJ14（C9） | U19-23 / LCD_B2 | LTDC_B2 / AF14 | LCD_B2 | L1 | 已硬连屏，需保留 |
| PJ15（D10） | U19-24 / LCD_B3 | LTDC_B3 / AF14 | LCD_B3 | L1 | 已硬连屏，需保留 |
| **PG12（C7）** | U19-25 / LCD_B4 | **LTDC_B4 / AF9** | LCD_B4 | L1 | 不能统一套用AF14 |
| PK4（D7） | U19-26 / LCD_B5 | LTDC_B5 / AF14 | LCD_B5 | L1 | 已硬连屏，需保留 |
| PK5（C6） | U19-27 / LCD_B6 | LTDC_B6 / AF14 | LCD_B6 | L1 | 已硬连屏，需保留 |
| PK6（C5） | U19-28 / LCD_B7 | LTDC_B7 / AF14 | LCD_B7 | L1 | 已硬连屏，需保留 |
| PI14（H3） | U19-30 / LCD_CLK | LTDC_CLK / AF14 | LCD_CLK | L1 | 屏幕像素时钟 |
| PI10（D5） | U19-32 / LCD_HSYNC | LTDC_HSYNC / AF14 | LCD_HSYNC | L1 | 已硬连屏，需保留 |
| PI9（E4） | U19-33 / LCD_VSYNC | LTDC_VSYNC / AF14 | LCD_VSYNC | L1 | 已硬连屏，需保留 |
| PK7（C4） | U19-34 / LCD_DE | LTDC_DE / AF14 | LCD_DE | L1 | 已硬连屏，需保留 |

### 4.2 LCD控制与电容触控

依据[S1]图29、图33及[S5]板级头文件、LCD/触控驱动。B-04 的屏为 RK043FN48H-CT672B；BSP 触控驱动使用 FT5336。若实物换过屏，先确认面板和触控芯片。

| 原始 MCU Pin（球位） | 板级物理接口/网络 | 复用功能 | 推荐 User Label/别名 | 参数 | 占用与待验证项 |
| --- | --- | --- | --- | --- | --- |
| PI12（E3） | U19-31 / LCD_DISP | GPIO_Output | LCD_DISP | L2 | 板载；高电平显示使能 |
| PK3（D8） | U10-7 EN / LCD_BL_CTRL | GPIO_Output | LCD_BL_CTRL | L2 | 板载背光驱动；高电平使能 |
| PH7（N13） | U19-39 / LCD_SCL；同网AUDIO_SCL、DCMI_SCL | I2C3_SCL / AF4 | BOARD_I2C3_SCL | T1 | 板载共享总线，不能另设成GPIO或FMC |
| PH8（P14） | U19-40 / LCD_SDA；同网AUDIO_SDA、DCMI_SDA | I2C3_SDA / AF4 | BOARD_I2C3_SDA | T1 | 板载共享总线；检查设备地址 |
| PI13（G3） | U19-38 / LCD_INT | GPIO_Input，或EXTI13 | TOUCH_INT | T1 | 轮询可保留输入；中断模式确认FT5336触发配置 |
| NRST（J1） | U19-37 / LCD_RST；同网DCMI_NRST及PHY nRST | 专用复位，不是GPIO | 原理图名LCD_RST；不创建TOUCH_RST_Pin | 系统复位 | **无独立MCU触控复位脚**；不能捏造PH/PI复位GPIO |

### 4.3 板载SDRAM：38个FMC引脚

依据[S1]图27、图29、表11和[S5] SDRAM驱动。所有下列引脚为 **AF12 / FMC**，均已硬连 U4，本方案全部保留；参数统一为 `S1`。板级接口是内焊 SDRAM，不是 Arduino 排针。

| 原始 MCU Pin（球位） | 板级物理接口/网络 | 复用功能 | 推荐 User Label | 参数 | 占用与待验证项 |
| --- | --- | --- | --- | --- | --- |
| PF0（D2） | U4 / FMC_A0 | FMC_A0 / AF12 | SDRAM_A0 | S1 | 板载占用 |
| PF1（E2） | U4 / FMC_A1 | FMC_A1 / AF12 | SDRAM_A1 | S1 | 板载占用 |
| PF2（G2） | U4 / FMC_A2 | FMC_A2 / AF12 | SDRAM_A2 | S1 | 板载占用 |
| PF3（H2） | U4 / FMC_A3 | FMC_A3 / AF12 | SDRAM_A3 | S1 | 板载占用 |
| PF4（J2） | U4 / FMC_A4 | FMC_A4 / AF12 | SDRAM_A4 | S1 | 板载占用 |
| PF5（K3） | U4 / FMC_A5 | FMC_A5 / AF12 | SDRAM_A5 | S1 | 板载占用 |
| PF12（M6） | U4 / FMC_A6 | FMC_A6 / AF12 | SDRAM_A6 | S1 | 板载占用 |
| PF13（N6） | U4 / FMC_A7 | FMC_A7 / AF12 | SDRAM_A7 | S1 | 板载占用 |
| PF14（P6） | U4 / FMC_A8 | FMC_A8 / AF12 | SDRAM_A8 | S1 | 板载占用 |
| PF15（M8） | U4 / FMC_A9 | FMC_A9 / AF12 | SDRAM_A9 | S1 | 板载占用 |
| PG0（N7） | U4 / FMC_A10 | FMC_A10 / AF12 | SDRAM_A10 | S1 | 板载占用 |
| PG1（M7） | U4 / FMC_A11 | FMC_A11 / AF12 | SDRAM_A11 | S1 | 板载占用 |
| PG4（N12） | U4 / FMC_BA0 | FMC_BA0（A14）/ AF12 | SDRAM_BA0 | S1 | 板载占用 |
| PG5（N11） | U4 / FMC_BA1 | FMC_BA1（A15）/ AF12 | SDRAM_BA1 | S1 | 板载占用 |
| PD14（L12） | U4 / FMC_D0 | FMC_D0 / AF12 | SDRAM_D0 | S1 | 板载占用 |
| PD15（K13） | U4 / FMC_D1 | FMC_D1 / AF12 | SDRAM_D1 | S1 | 板载占用 |
| PD0（B12） | U4 / FMC_D2 | FMC_D2 / AF12 | SDRAM_D2 | S1 | 板载占用 |
| PD1（C12） | U4 / FMC_D3 | FMC_D3 / AF12 | SDRAM_D3 | S1 | 板载占用 |
| PE7（R8） | U4 / FMC_D4 | FMC_D4 / AF12 | SDRAM_D4 | S1 | 板载占用 |
| PE8（N9） | U4 / FMC_D5 | FMC_D5 / AF12 | SDRAM_D5 | S1 | 板载占用 |
| PE9（P9） | U4 / FMC_D6 | FMC_D6 / AF12 | SDRAM_D6 | S1 | 板载占用 |
| PE10（R9） | U4 / FMC_D7 | FMC_D7 / AF12 | SDRAM_D7 | S1 | 板载占用 |
| PE11（P10） | U4 / FMC_D8 | FMC_D8 / AF12 | SDRAM_D8 | S1 | 板载占用 |
| PE12（R10） | U4 / FMC_D9 | FMC_D9 / AF12 | SDRAM_D9 | S1 | 板载占用 |
| PE13（R11） | U4 / FMC_D10 | FMC_D10 / AF12 | SDRAM_D10 | S1 | 板载占用 |
| PE14（P11） | U4 / FMC_D11 | FMC_D11 / AF12 | SDRAM_D11 | S1 | 板载占用 |
| PE15（R12） | U4 / FMC_D12 | FMC_D12 / AF12 | SDRAM_D12 | S1 | 板载占用 |
| PD8（L15） | U4 / FMC_D13 | FMC_D13 / AF12 | SDRAM_D13 | S1 | 板载占用 |
| PD9（L14） | U4 / FMC_D14 | FMC_D14 / AF12 | SDRAM_D14 | S1 | 板载占用 |
| PD10（K15） | U4 / FMC_D15 | FMC_D15 / AF12 | SDRAM_D15 | S1 | 板载占用 |
| PE0（A6） | U4 / FMC_NBL0（DQM0） | FMC_NBL0 / AF12 | SDRAM_NBL0 | S1 | 板载占用 |
| PE1（A5） | U4 / FMC_NBL1（DQM1） | FMC_NBL1 / AF12 | SDRAM_NBL1 | S1 | 板载占用 |
| PC3（L4） | U4 / FMC_SDCKE0 | FMC_SDCKE0 / AF12 | SDRAM_CKE | S1 | Bank1；不能改用PH7 |
| PH3（J4） | U4 / FMC_SDNE0 | FMC_SDNE0 / AF12 | SDRAM_NE | S1 | Bank1片选 |
| PH5（J3） | U4 / FMC_SDNWE | FMC_SDNWE / AF12 | SDRAM_NWE | S1 | 板载占用 |
| PF11（P8） | U4 / FMC_SDNRAS | FMC_SDNRAS / AF12 | SDRAM_NRAS | S1 | 板载占用 |
| PG15（B7） | U4 / FMC_SDNCAS | FMC_SDNCAS / AF12 | SDRAM_NCAS | S1 | 板载占用 |
| PG8（H14） | U4 / FMC_SDCLK | FMC_SDCLK / AF12 | SDRAM_CLK | S1 | 需校验时钟频率及波形 |

### 4.4 以太网RMII及管理线

依据[S1]图29、图31、表11，[S2]表12及[S6] Ethernet示例。RMII总线在 MCU 与 PHY 之间；网线接 **CN9 RJ45**，不把 RMII 引脚直接接到交换机。

| 原始 MCU Pin（球位） | 板级物理接口/网络 | 复用功能 | 推荐 User Label | 参数 | 占用与待验证项 |
| --- | --- | --- | --- | --- | --- |
| PA1（N2） | U9-14 REFCLKO / RMII_REF_CLK | ETH_RMII_REF_CLK / AF11 | ETH_REF_CLK | E1 | 板载50 MHz时钟输入，检查PHY输出 |
| PA2（P2） | U9-12 / RMII_MDIO | ETH_MDIO / AF11 | ETH_MDIO | E1 | 板载管理总线 |
| PC1（M3） | U9-13 / RMII_MDC | ETH_MDC / AF11 | ETH_MDC | E1 | 板载管理时钟 |
| PA7（R3） | U9-11 / RMII_CRS_DV | ETH_RMII_CRS_DV / AF11 | ETH_CRS_DV | E1 | 板载占用 |
| PC4（N5） | U9-8 / RMII_RXD0 | ETH_RMII_RXD0 / AF11 | ETH_RXD0 | E1 | 板载占用 |
| PC5（P5） | U9-7 / RMII_RXD1 | ETH_RMII_RXD1 / AF11 | ETH_RXD1 | E1 | 板载占用 |
| PG11（B8） | U9-16 / RMII_TX_EN | ETH_RMII_TX_EN / AF11 | ETH_TX_EN | E1 | 板载占用 |
| PG13（B3） | U9-17 / RMII_TXD0 | ETH_RMII_TXD0 / AF11 | ETH_TXD0 | E1 | 板载占用；不要用PB12替代板级连线 |
| PG14（A4） | U9-18 / RMII_TXD1 | ETH_RMII_TXD1 / AF11 | ETH_TXD1 | E1 | 板载占用；不要用PB13替代板级连线 |
| PG2（M13） | U9-10 / RMII_RXER、PHYAD0 | ETH_MII_RX_ER / AF11；板网名RMII_RXER | ETH_RX_ER | E1 | 官方示例设AF11；有些CubeMX RMII界面未显示此项，仍不可分配给其他输出 |
| NRST（J1） | U9-15 / nRST | 共用硬件复位 | 不创建ETH_RESET_GPIO | 系统复位 | 不是独立PHY复位GPIO |

### 4.5 ST-LINK VCP、1PPS、调试与基准时钟

| 原始 MCU Pin（球位） | 板级物理接口/网络 | 复用功能 | 推荐 User Label/别名 | 参数 | 占用与待验证项 |
| --- | --- | --- | --- | --- | --- |
| PA9（E15） | VCP_TX → STLINK_RX → CN14 USB | USART1_TX / AF7 | DEBUG_UART_TX；huart1 | U1 | 默认已连VCP；核对R64/R63/R58 |
| PB7（B5） | VCP_RX ← STLINK_TX ← CN14 USB | USART1_RX / AF7 | DEBUG_UART_RX；huart1 | U1 | 已连VCP；不是PB6/PA10默认组合 |
| **PA15（A13）** | **CN7-2 / Arduino D9** | **TIM2_CH1 / AF1** | **PPS_OUT；htim2** | P1 | 原为扩展脚，本方案占用；不要再用于JTAG/SPI/其他模块 |
| PG6（J15） | CN4-3 / Arduino D2 | GPIO_Output（备选） | PPS_OUT（仅选备选时） | P1备选 | 未默认分配；若要求严格GPIO_Output可改此脚，需实测中断抖动 |
| PA13（A15） | SWDIO，板载ST-LINK | SYS_SWDIO | 保留SYS原名 | Serial Wire | 已占用，不改为通用GPIO |
| PA14（A14） | SWCLK，板载ST-LINK | SYS_SWCLK | 保留SYS原名 | Serial Wire | 已占用，不改为通用GPIO |
| PH0（G1） | X2 OSC_25M经R30输入 | RCC_OSC_IN | 保留RCC原名 | 第5.1节 | 已接25 MHz有源时钟；核对实物修订 |
| PH1（H1） | OSC_OUT；B-04图中未用 | RCC_OSC_OUT/保留 | 不作为外接接口 | 第5.1节 | 不以“无连接”推定能在扩展口接入 |

`PA15` 方案与 `PG6` 方案二选一，不给两个引脚同时命名为 `PPS_OUT`。本方案用一个 MCU I/O 引脚的定时器复用功能输出脉冲；若教师要求模式必须为 GPIO_Output，再采用 PG6 备选并重新验收。

### 4.6 DHT11温湿度接口：统一用PG7 / Arduino D4

**DHT11为外接课程温湿度传感器，不是DISCO板载器件。** 未发现板载独立环境温度传感器；MCU内部温度传感器测的是芯片温度，不代替DHT11的环境测温。[S1、S2] 本轮依据用户确认将原来的候选温度接口收敛为PG7；DHT11的单线协议不是I2C，也不需要ADC。[S8]

| 原始 MCU Pin（球位） | 板级物理接口 | 复用功能 | 推荐 User Label/变量别名 | 初始化参数 | 占用与待验证项 |
| --- | --- | --- | --- | --- | --- |
| **PG7（J14）** | **CN4-5 / Arduino D4 → DHT11 DATA** | **GPIO_Output，Open Drain** | **DHT11_DATA** | X1：初始High（释放）、No Pull、Low Speed | 本方案分配；当前SBCG.ioc未配置；确认模块自带上拉到3.3 V |
| 无MCU信号引脚 | **CN6-4 / 3V3 → DHT11 VCC** | 电源 | 无GPIO别名 | 3.3 V供电，短线 | 核对实际模块支持3.3 V；不默认接5 V |
| 无MCU信号引脚 | **CN6-6或CN6-7 / GND → DHT11 GND** | 地 | 无GPIO别名 | 共地 | 只接地针，不把VIN/5V当GND |
| 无MCU信号引脚 | 裸DHT11的3号脚 | NC | 无 | 不连接 | 仅适用于4脚裸器件；三针小板按其丝印接，不能套用裸件脚序 |
| 无外部引脚 | 串口或触控设置 | 软件设温/故障回退 | environment_temperature_c、temperature_source | 基础功能保留，UI标记MANUAL/DHT11 | 不把失效或超龄的读数标成自动有效温度 |
| 无外部引脚 | MCU内部 | ADC1_IN18 / Temperature Sensor | mcu_die_temperature_c | 仅诊断，不默认启用 | 不作为环境温度；与VBAT共用内部通道 |

PG7在[S1]表11和图26中只接Arduino D4；与第4.1～4.5节的LTDC、FMC、RMII、VCP和PA15/PPS没有引脚重叠。此前给I2C温度模块预留的PB8/PB9、模拟温度预留的PA0不再为DHT11占用；PH7/PH8继续服务触控/音频I2C3，TIM2继续留给PPS/同步。

**接线前核对上拉。** 奥松典型电路使用约4.7 kΩ上拉；若课程三针DHT11小板已带电阻且上拉到其3.3 V电源，直接使用该小板，不额外加电路。若是没有上拉的裸件，PG7开漏方案尚不满足通信条件，不能用MCU内部弱上拉冒充外部上拉，也不能自行补非课程提供电阻。此时可进一步核查课程提供的上拉资源，或评估已硬连板上2.7 kΩ上拉的PB9/CN7-9：该脚与CN2、可能的A4焊桥共网，必须确认总线无其他设备、DHT11低电平驱动和波形合格后，再通过CubeMX迁移；**PB9不是本方案第二个同时使用的DATA脚**。

裸器件正视栅格面的脚序以[S8]图1为准：1=VDD、2=DATA、3=NC、4=GND。三针小板的VCC/DATA/GND顺序由其自身丝印或资料确定，目前没有照片，不能写死第几针为电源。旧版/不同厂家DHT11的电压和数据格式也须与实际发放模块匹配。

## 5. 两板统一的初始化参数

### 5.1 时钟基线（建议值，不是唯一可行方案）

为使 SDRAM 初始化和刷新数与官方 BSP 的100 MHz配置一致，建议两板先采用 **SYSCLK/HCLK = 200 MHz，SDCLK = HCLK/2 = 100 MHz**；APB1 = 50 MHz，APB2 = 100 MHz，采用常规定时器倍频设置，使 TIM2 时钟为100 MHz。参数必须以 CubeMX Clock Configuration 显示值及生成代码为准。

| 设置 | 建议 |
| --- | --- |
| RCC HSE | Bypass Clock Source，输入25 MHz；B-04原理图X2为有源振荡器，经R30接PH0，并给PHY供25 MHz参考 |
| 主PLL | HSE，PLLM=25、PLLN=400、PLLP=2；其它输出只按实际使用外设配置 |
| 总线 | AHB /1，APB1 /4，APB2 /2；TIMPRE采用常规倍频路径并核对TIM2时钟 |
| LTDC | PLLSAIN=192、PLLSAIR=5、PLLSAI DivR=4：1 MHz×192/5/4=9.6 MHz |
| 电源/Flash | 在CubeMX按实际3.3 V和200 MHz配置电压档位、必要的OverDrive及Flash延时；不凭本表跳过时钟校验 |

ST部分示例代码使用 `HSE_ON`；这里依据上传B-04原理图选择有源时钟的 Bypass。实物若为其他修订，先确认其时钟电路。200 MHz主PLL不能自然提供常见的48 MHz USB时钟；ST-LINK VCP由独立ST-LINK芯片处理，**不需要启用主MCU USB_OTG_FS/HS**。后续若启用USB/SDMMC，重新核查48 MHz域。

不要直接用216 MHz HCLK、FMC /2 得到108 MHz SDCLK。上传[S2，第186页]规定3.0～3.6 V、指定负载下SDCLK最高100 MHz；若采用216 MHz系统时钟，应选合适分频并重新计算SDRAM时序/刷新计数，而不是照抄下面100 MHz数值。100 MHz也仍需确认板负载、实物SDRAM与电压条件。

### 5.2 L1/L2：LCD-LTDC与控制GPIO

| 项目 | CubeMX/初始化建议 |
| --- | --- |
| LTDC物理接口 | RGB888；按第4.1节固定引脚，AF推挽、No Pull、高/很高速度；PG12为AF9 |
| 面板有效区 | 480×272 |
| 水平参数（实际长度） | HSYNC=41，HBP=13，HFP=32 |
| 垂直参数（实际长度） | VSYNC=10，VBP=2，VFP=2 |
| HAL寄存器字段核对 | HorizontalSync=40；VerticalSync=9；AccumulatedHBP=53；AccumulatedVBP=11；AccumulatedActiveW=533；AccumulatedActiveH=283；TotalWidth=565；TotalHeigh=285 |
| 极性 | HS/VS/DE：Active Low；Pixel Clock：IPC，按BSP选择输入像素时钟极性（通常界面对应正常/上升沿），核对生成的 `LTDC_PCPOLARITY_IPC` |
| 图层固定约定 | 用户指定一层RGB565、480×272；起址0xC0000000、Alpha=255、背景黑；HAL字段必须为LTDC_PIXEL_FORMAT_RGB565。图层内存布局与绘图代码一致 |
| 缓冲大小 | RGB565每帧480×272×2=261120字节；双缓冲522240字节；可将第二缓冲放0xC0040000，两块按256 KiB间距预留 |
| GPIO控制 | PI12=LCD_DISP，PK3=LCD_BL_CTRL；推挽、No Pull、低速，初始低；SDRAM和LTDC图层准备好后再拉高 |

CubeMX有的界面填写实际长度，有的显示累计值；以表中两套值区分，避免重复减1。时序来源[S5] LCD BSP及[S7] RK043FN48H组件参数，不是仅凭MCU数据手册推测。

RGB565描述的是帧缓冲格式，板载28个LTDC引脚完整保留。ST BSP的 `BSP_LCD_LayerDefaultInit()` 默认ARGB8888；当前公开BSP另有 `BSP_LCD_LayerRgb565Init()`，若采用BSP须核对团队版本并统一初始化所有权。不得在CubeMX设置RGB565后又调用ARGB8888默认初始化覆盖。原始RGB565颜色：黑0x0000、白0xFFFF、红0xF800、绿0x07E0、蓝0x001F；内存像素使用uint16_t。BSP/DMA2D不同绘图函数对颜色参数的约定可能不同，须确认是否需要RGB888到RGB565转换，不能直接混用32位LCD_COLOR_*常量。详细颜色和图层设置见 `CubeMX_Step1.md`。

### 5.3 S1：FMC SDRAM

| 项目 | 100 MHz SDCLK下的建议/官方BSP基线 |
| --- | --- |
| 存储器/控制器 | SDRAM Bank1；SDNE0、SDCKE0；8列地址位、12行地址位、4个内部Bank |
| 总线/地址 | 16位；基址0xC0000000，可用范围0xC0000000～0xC07FFFFF |
| 工作参数 | CAS=2；Write Protection Disable；SDClock=HCLK/2；Read Burst Enable；Read Pipe Delay=0 |
| 时序单位 | 以下为SDCLK周期数，不是纳秒 |
| LoadToActiveDelay / tMRD | 2 |
| ExitSelfRefreshDelay / tXSR | 7 |
| SelfRefreshTime / tRAS | 4 |
| RowCycleDelay / tRC | 7 |
| WriteRecoveryTime / tWR | 2 |
| RPDelay / tRP | 2 |
| RCDDelay / tRCD | 2 |
| Refresh Count | 0x0603（1539），官方BSP的100 MHz值；更改SDCLK或芯片型号后重新核算 |
| 模式寄存器 | Burst Length=1，Sequential，CAS2，Standard，Write Burst Single |
| GPIO | 第4.3节全部AF12、推挽、No Pull、高/很高速度 |

上述周期数来自ST BSP，属于可复核的参考基线，不代替实物U4对应速度等级的数据手册。实施前各项应满足 `周期数 ≥ ceil(器件要求的最小时间 × SDCLK频率)`，特别复核tRAS、tRC和CAS允许的频率；不满足时增加周期数并确认组合约束。当前四份上传资料没有独立的U4完整器件数据手册，需在实物型号确认后补做该项核对。

**`MX_FMC_Init()` 初始化控制器并不等于SDRAM芯片已完成上电命令序列。** 固件还须发送：Clock Enable → 等待至少100 μs（BSP用1 ms）→ Precharge All → 8次Auto Refresh → Load Mode Register → Program Refresh Rate。[S5]

须先完成SDRAM初始化和读写验证，再让LTDC读取帧缓冲。启动代码/链接脚本不要在此之前自动清零未初始化的外部SDRAM。外部帧缓冲与DMA2D/LTDC共用时，应确定MPU属性和D-Cache一致性策略；开启缓存后的“读写成功”不能只验证CPU自己的缓存副本。

### 5.4 T1：触控与共享I2C3

- I2C3：PH7 SCL、PH8 SDA，AF4开漏，No Pull；B-04已有板上拉，检查实物，不额外接未经课程许可的电路。先用100 kHz、7位寻址，CubeMX按实际I2C核时钟计算TIMINGR；不要复制另一时钟下的固定十六进制TIMINGR。
- FT5336的**7位地址为0x38**；ST BSP/HAL传统移位地址为`0x70`。使用API时确认它要求7位地址还是左移后的地址，不能二次左移。[S5]
- I2C3还连WM8994音频控制（BSP移位地址0x34，即7位0x1A）及相机接口；可共用总线，但要共用管理、避免重复初始化及并发访问冲突。
- 第一阶段轮询触控：PI13输入、No Pull；确认坐标/方向后，再选择中断方式。中断方式需配置触控控制器的中断模式、有效电平与EXTI13边沿，并启用EXTI15_10；不能只凭“触控INT”名称认定边沿。
- 屏幕/触控硬件复位来自NRST，不配置独立 `TOUCH_RST` GPIO。

### 5.5 E1：ETH RMII、PHY与交换机

- ETH选择 **RMII**；逐一核对第4.4节，AF11、推挽、No Pull、高/很高速度。PHY为LAN8742A，开启自动协商，MAC速度/双工跟随协商结果。
- PG2板级网为RMII_RXER；DS/CubeMX功能名可能显示 `ETH_MII_RX_ER`。官方示例配置该脚AF11。若当前CubeMX版本在RMII模式下没有此选择，保持主接口为RMII，记录差异并参照所用版本的官方驱动处理，不切成MII、不手改 `.ioc`、不把PG2作为其他输出。
- B-04图31的PHYAD0/RXER有上拉，预期PHY地址1；上电读取PHY标识/地址实际确认。采用官方LAN8742驱动的地址探测能力，或记录MDIO读出的实际地址；不能盲用LAN8720驱动/地址0。
- PA1接收PHY输出的50 MHz；X2供PHY的25 MHz是另一段时钟路径。PHY复位共用NRST，不新建 `ETH_RST` GPIO。
- 两板MAC必须不同。建议本地管理MAC `02:00:00:00:00:01` / `02:00:00:00:00:02`；网络团队可另行统一。
- 无DHCP服务器的独立交换机测试网络，可约定静态IP `192.168.10.1` / `192.168.10.2`、掩码 `255.255.255.0`；均为团队建议，不是板卡固有地址。不要把普通交换机当DHCP服务器。
- 需要ETH/LwIP中断及正确的DMA描述符/缓冲内存配置。M7 DTCM不能默认作为ETH DMA缓冲；参考当前CubeF7示例选DMA可访问区域，并处理32字节缓存行对齐与Cache维护/MPU配置。
- RMII的50 MHz时钟不等于两板时间已经同步。满足±10 μs仍须实现板端时间同步、时间戳和偏差控制；是否采用ETH硬件时间戳、协议和收敛策略由同步模块单独设计。Ping通只证明连通性。

### 5.6 U1/P1：调试串口与1PPS

**USART1**：Asynchronous、115200 bit/s、Word Length=8 Bits、Parity=None、Stop Bits=1、Hardware Flow Control=None、TX/RX、Oversampling=16；PA9/PB7均AF7。接CN14的ST-LINK USB，PC侧两块板对应的COM号分别记录，不能硬编码都用同一个COM号。串口接收若使用中断，启用USART1 IRQ；长日志不要在同步/采集的关键中断中阻塞发送。

默认板级路由为R64接通、R63断开、R58接通，保留PA9→VCP_TX、PJ12→OTG_FS_VBUS。若把PA9改为USB_OTG_FS_VBUS，就失去VCP_TX。[S1，表11脚注，第48页] 板载VCP不需要外接USB转串口，也不需要主MCU的USB CDC。

**1PPS首选PA15/TIM2_CH1**：SYS Debug选Serial Wire，释放PA15的JTAG功能；AF1推挽、No Pull、低/中速度，从CN7-2 / D9接示波器。建议使用硬件输出比较/定时器边沿，避免任务调度决定上升沿。

| 项目 | 配置/核对 |
| --- | --- |
| 定时器 | TIM2，Internal Clock，32位；独占用于PPS/同步时间安排，不能同时改作HAL时基或不兼容的输入捕获 |
| 输出 | CH1 PWM Generation，PWM Mode1、Active High，用于第一阶段输出自测；正式同步按统一时间基准安排边沿 |
| 自测计数参数 | 目标200 MHz HCLK时TIM2=100 MHz：PSC=99；若保留当前216 MHz HCLK且常规倍频，TIM2=108 MHz：PSC=107。两者均得到1 MHz；ARR=999999、CCR1=100 → 1秒周期、100 μs高电平 |
| 通用公式 | fCNT=fTIM/(PSC+1)；周期=(ARR+1)/fCNT；PWM1高电平时长=CCR1/fCNT |
| 信号别名 | PPS_OUT；业务侧可使用pps_timer指向实际htim2，不改生成句柄 |
| 启动状态 | 未同步时保持低或明确标注未收敛；输出启动/首次脉冲不能被误认为共同整秒 |

以上固定PWM只用于验证接口、频率和脉宽。正式验收时必须把**同步后的共同整秒时刻**转换成各板硬件计数值，安排上升沿和下降沿，并随同步算法校正相位/频率。两板从各自复位时刻自由跑1 Hz，即使频率准确，也不满足同步要求。不得用 `HAL_Delay(1000)` 或普通RTOS延时生成验收脉冲。

若教师要求严格GPIO_Output，PG6/CN4-3可作为备选，初始低、推挽、No Pull，由同一同步时基的定时器中断/允许的硬件路径控制；但ISR延迟会进入误差预算，必须在网络、LCD和音频同时运行时验证。PA15和PG6两个方案不能混用为默认。

**示波器接入**：CH1探头尖端接A板CN7-2，CH2接B板CN7-2；两探头地夹分别接本板CN6-6或CN6-7的GND。确认两板供电/测量地允许共地；不把地夹接3V3、5V或VIN。用高阻/10×探头，观察0～3.3 V正脉冲，以A板上升沿触发。两板PPS信号线不互接；示波器只观察，不作为同步时钟源。

### 5.7 X1：DHT11 GPIO与时序设置

| 项目 | 两板约定 |
| --- | --- |
| GPIO | PG7，User Label=`DHT11_DATA`，Output Open Drain、初始High、No Pull、Low Speed |
| 外设占用 | 不启用I2C1/ADC3；不占TIM2通道；基础读取方案不新增IRQ/DMA，微秒测时可独立使用DWT核心计数器 |
| 总线状态 | 主机写Low发起请求；写High是关闭开漏下拉、释放线路，由外部上拉产生高电平；释放后读取IDR判断传感器波形，不主动推挽拉高 |
| 上电等待 | 至少1秒后首次发请求 |
| 起始低电平 | 建议20 ms，满足官方18～30 ms范围；按时间状态机安排，不能把整段等待放在ISR中 |
| 读取节奏 | 建议每5秒读取一次，所有重试也保持间隔>2秒；不为快速显示而每100 ms读取 |
| 响应/位宽 | 验证响应与40位波形，设置超时；按实际版本识别短/长高电平位，不能假设每个厂家的典型值完全相同 |
| 帧校验 | 5字节，高位先出；前4字节之和取低8位等于第5字节才接受 |
| 变量 | environment_temperature_c、relative_humidity_pct、temperature_valid、temperature_sample_age_ms、temperature_source |
| 数据时效 | 返回的是上一次测量；长时间停读后按正常>2秒间隔预读/再读，不用无间隔的两连读绕过采样限制 |

默认保持GPIO开漏输出模式即可释放总线并读取引脚输入，不需要反复把生成的GPIO初始化改成输入/推挽。若后续选EXTI或定时器捕获，先明确新的CubeMX配置和驱动所有权，不能在现有生成代码里偷偷追加AF或IRQ初始化。

微秒计时不能用 `HAL_Delay()`；若使用DWT，其周期计数换算依据实际 `SystemCoreClock`，目标200 MHz和当前216 MHz不可混用。**读取DHT11时不全局关闭中断数毫秒**，以免破坏±10 μs同步、ETH服务或音频DMA。带中断的短轮询在高负载下仍可能漏掉DHT11位边沿；按响应、位宽、校验和超时检测失败，延后重试，不为温度读取阻塞时间同步。正式联调应同时刷新LCD、收发网络并采集音频；若失败率不可接受，再经CubeMX设计独立捕获方案。

课程自动声速补偿使用环境温度；传感器应放在通风处，避开MCU、背光和稳压器热源。手动设温路径保留，传感器失效/超龄时显示状态并由策略决定保持最后有效值或回退，不能悄悄写成0℃。单只DHT11时，两板共享同一有效温度及时间戳；两只时也统一选择/融合策略，避免同次发声两板用不同声速。尚未确认实际提供数量。

奥松V1.3手册给出约±2℃温度精度、秒级响应，测试条件包含25℃和5 V；3.3 V使用时仍需实测。因此自动获得读数不等于课程的测距温补指标已经通过。温度解码按实际器件版本确定，新版本可能含小数及符号；不要一概把第4字节忽略。[S8]

内部温度只可作为诊断：ADC1_IN18与VBAT共用内部通道，不能同时启用两者并把VBAT当温度；如启用，等待至少10 μs、采样至少10 μs，并按VDDA及出厂校准值换算。[S2，第43、151页]

## 6. 分步CubeMX配置指南

1. **记录两板实物。** MCU型号、MB1191修订、屏幕型号、ST-LINK连接与焊接改动；记录CubeMX/CubeIDE版本和STM32CubeF7包版本。若实物与B-04不同，先用对应ST原理图核对差异；核对课程DHT11小板的引脚丝印、电压、上拉和数量。
2. **建立一份共享硬件基线。** 优先Board Selector选STM32F746G-DISCO；若选MCU Selector，必须选STM32F746NGH6/TFBGA216。SBCG现有工程已选NG型号、链接Flash为1024K，保持这一基线；不新建替代项目，也不直接改 `.ioc` 或链接脚本。已有项目通过图形界面编辑。板默认初始化可作参考，但逐项检查默认启用的外设；不能只依赖Board Selector自动结果。
3. **SYS/RCC/Clock Configuration。** Debug选Serial Wire，保留PA13/PA14；配置HSE25 MHz及第5.1节基线。确认SYSCLK/HCLK、APB时钟、TIM2、SDCLK和LTDC时钟实际显示值，无时钟错误。暂不启用主MCU USB。
4. **固定GPIO和标签。** 按第4节逐脚选择功能和User Label；对自动分配到错误焊接位置的引脚，在图形界面重新选择、必要时锁定。重点检查PG12/AF9、SDCKE0/PC3、SDNE0/PH3、USART1_RX/PB7、ETH_TXD0/PG13和ETH_TXD1/PG14。
5. **FMC。** 启用SDRAM Bank1，16位、8列、12行、4内部Bank和第5.3节参数；确认全部38个FMC引脚。生成后的应用还需SDRAM命令序列和刷新设置，不能只打开FMC就使用外部内存。
6. **LTDC。** 配置28个RGB/时序引脚，输入面板时序和极性；设置一层RGB565及缓冲地址。添加PI12/PK3两个控制GPIO。先验证屏幕纯色和文字，之后再考虑DMA2D、双缓冲或波形显示。
7. **I2C3/触控。** 选择PH7/PH8，100 kHz、7位寻址；PI13先输入轮询。接入与当前软件包相符的FT5336/BSP驱动；屏幕触控方向实测后再启用EXTI。复位不增加GPIO。
8. **USART1。** Asynchronous，手动核对PA9 TX和PB7 RX，填入U1参数；选择需要的RX中断。两块板分别经CN14接电脑，测试发送和设温命令。不要选择PC6/PC7来代替VCP。
9. **ETH/LwIP。** RMII、LAN8742、E1参数，核对全部固定引脚和PG2占用；设置不同MAC/IP，选择适合应用的LwIP运行方式及NVIC。按所用CubeF7/HAL版本处理描述符、链接脚本和Cache，保留同步模块的时间戳设计位置。
10. **TIM2/PPS。** PA15→TIM2_CH1并标PPS_OUT；按第5.6节选择与实际TIM2时钟匹配的PSC，当前108 MHz用107、建议100 MHz用99。先测1秒周期和100 μs脉宽，再接入同步算法。DHT11不得修改TIM2频率或占PPS通道。
11. **DHT11。** Pinout选择PG7为GPIO_Output；在System Core → GPIO配置Output Open Drain、Output Level High、No Pull、Low Speed、User Label=`DHT11_DATA`，保存并Generate Code。预期main.h生成DHT11_DATA_Pin/DHT11_DATA_GPIO_Port，GPIO初始化开启GPIOG并设置PG7开漏/初始释放；独立GPIO文件模式时对应gpio.c/gpio.h。不用打开I2C1/ADC3，不更改TIM2/NVIC/DMA。基础手动设温保留。
12. **保存与生成。** 仅点击CubeMX保存`.ioc`、Generate Code；配置保留USER CODE区。检查生成的GPIO AF、外设初始化与时钟值，业务逻辑放USER CODE区或独立文件。SDRAM命令、触控驱动、PHY管理、时间同步并非全部由CubeMX自动完成。
13. **队友共用。** 两板使用同一已审核`.ioc`基线与软件包版本；仅在业务配置中区分board_role、MAC/IP和实测校准，不改物理引脚表。运行验收前核对第8节清单。

本轮记录用户已完成的NG器件/1024K工程基线并交付可审核配置指南；PG7、目标时钟、FMC和LTDC尚未在CubeMX配置。按SBCG/AGENTS.md第4节，用户在CubeMX完成本里程碑的设置并重新生成后，再核查生成代码、开展DHT11驱动实现和硬件测试。此处不通过手写HAL初始化替代生成过程。

## 7. 冲突与易错项

| 冲突/问题 | 处理规则 |
| --- | --- |
| 通用MCU AF可选脚与PCB实际连线不一致 | AF可选不等于板上已接到屏/PHY；以第4节和实物原理图为准 |
| PA9：VCP_TX与USB_FS_VBUS | 保留默认VCP电阻路由；主MCU USB功能需另行设计，不能占用PA9后仍承诺VCP发送 |
| PB6：USART1_TX候选与QSPI_NCS | 板上PB6接QSPI；VCP用PA9；不能因USART自动分配而改用PB6 |
| PG7/DHT11与PA15/PPS | 两脚分开使用；DHT11不需要I2C/ADC，TIM2只由同步模块管理 |
| DHT11开漏与5 V模块上拉 | 使用课程模块的3.3 V接法，核对上拉；PG7上无默认外部上拉，不用内部弱上拉代替典型4.7 kΩ |
| DHT11读取与实时中断 | 不全局屏蔽中断数毫秒；失败延后重试，网络、PPS和音频保持服务 |
| PH7/PH8：I2C3与FMC其他候选 | 板上用于触控/音频/相机控制；SDRAM使用PC3/PH3等实际连线 |
| PG12：LTDC_B4 | AF9，其他所选LCD脚大多AF14；逐脚核对 |
| PG2：PHY RXER/地址strap | 有硬连线，即使RMII界面只显示9根常用线，也不可当空闲GPIO输出 |
| PA15：PPS与JTAG/SPI | SYS只选Serial Wire；不再选JTAG、SPI片选或另一课程模块同脚 |
| TIM2：PPS与采集/系统时基 | 一个团队成员负责分配；不能分别修改PSC/ARR。音频时间戳若共用TIM2，须有统一计数和比较设计 |
| PB5：ETH_PPS_OUT候选与USB_HS_ULPI_D7 | 板上PB5连USB PHY，且不是已核实的Arduino输出口；不默认用PB5替代本方案PPS接口 |
| PI1：Arduino D13与板载LD1 | D13带LED负载；不是优先选择的验收PPS接口 |
| Arduino A4/A5与D14/D15 | CN5-5/6通过SB4/SB5、SB1/SB3在PF7/PF6与PB9/PB8之间选择；检查[S1]图26和桥位实物，不能同时视为独立信号 |
| 上传引脚图“CN8电源排针” | 官方电源排针为**CN6**；CN8是SWD相关连接器。按官方针号和实物丝印接地/取电 |
| 上传引脚图“数字接口0～5 V” | STM32逻辑输出为3.3 V；部分FT引脚输入耐压有条件，不能概括为全部5 V可用；模拟输入仅0～3.3 V。本文接线统一3.3 V逻辑 |
| 216 MHz系统时钟与SDRAM /2 | SDCLK108 MHz不能照抄100 MHzBSP配置；本文件建议200 MHz基线，其他时钟需重新校核 |
| LTDC与SAI音频时钟 | PLLSAI/PLLI2S资源须共同规划；改变SAI时钟不能无意改变LCD像素时钟，反之亦然 |
| CubeMX与BSP各自初始化同一外设 | 统一所有权、句柄和时序；触控/音频共用I2C3时不能互相重新初始化或混用两个锁 |
| Cache、ETH DMA、音频DMA、LTDC同时使用 | 明确DMA可访问内存、MPU属性、对齐及Cache维护；仅关掉报错并不能证明数据一致 |
| 两板有相同硬件配置 | 相同配置不自动实现同步；MAC/IP唯一，共同时间基准、协议、温度/声速参数一致 |

**为完整课程任务提前保留音频资源**：[S1]图24、表11中PI4/PI5/PI6/PI7/PG10为SAI2相关、PD6为Audio_INT，PH7/PH8用于音频控制；板载U20/U21麦克风的MIC_DATA/MIC_CLK经WM8994编解码器路径处理，不是可随意指定为ADC输入的麦克风接口。本文件未替采集模块确定采样率/SAI子块/DMA配置，但不把上述脚列为空闲，避免显示和温度配置占用后无法采集声音。

## 8. 两板联调与待验证清单

### 8.1 配置/接线检查

- [ ] A/B板PCB修订与B-04图一致，或已对照对应ST原理图更新差异。
- [ ] 对照实物确认CN7-2是D9/PA15，CN6-6/7是GND；不是根据PDF图的观看方向猜针序。
- [ ] 确认PA9 VCP路由电阻状态、两块板各自COM号。
- [ ] 固定外设合计：LCD/触控33个普通I/O、SDRAM38个、ETH10个、VCP2个、PPS1个、DHT11 1个；共85个**不同**I/O，不含NRST、SWD、RCC。按生成代码核对没有重复分配。
- [ ] 记录实际HSE、HCLK、SDCLK、LTDC像素时钟及TIM2计数时钟。
- [ ] SDRAM上电命令、刷新、内存区域和Cache方案已统一；LCD图层像素格式与绘图程序一致。
- [ ] 触控型号、7位地址及方向已确认；共享I2C3访问不存在重复初始化。
- [ ] PHY地址/标识、50 MHz参考时钟、自动协商和拔插网线恢复已确认。
- [ ] 已保留SAI2/音频控制资源，TIM2所有权与声音到达时间戳方案已统一。

### 8.2 验收相关测量

- [ ] 单板PPS高电平脉宽≥1 μs，建议100 μs；信号0～3.3 V，无启动异常脉冲误判。
- [ ] 双板通过交换机，在LCD刷新、串口输出、音频采集同时运行时，上电30秒内收敛。
- [ ] 收敛后连续记录10个上升沿差 `Δt=tB−tA`，保存符号及最大绝对值，要求每次 `abs(Δt)≤10 μs`；不只取平均值。
- [ ] 显示/串口能区分未同步、正在收敛、已收敛，示波器结果与软件偏差记录一致。
- [ ] 基础功能改变设定环境温度，两板显示当前温度/声速，测距随温度变化符合任务书要求。
- [ ] DHT11为课程实际提供器件；PG7/CN4-5数据线、CN6-4电源、CN6-6/7地线及3.3 V上拉已确认。
- [ ] 上电至少1秒、起始18～30 ms、读取间隔>2秒、完整40位及校验符合实际模块版本。
- [ ] 同时运行网络、LCD和音频时，DHT11读取不屏蔽实时中断；PPS仍满足±10 μs，失败重试和超龄状态可观察。
- [x] 当前SBCG的NG型号/1024K链接设置已只读核对一致。
- [ ] 按第一步指南在CubeMX调整HSE/目标时钟，并核对生成代码；随后再配置FMC、RGB565 LTDC和PPS。
- [ ] 离线测量不依赖电脑辅助程序；示波器和串口仅观察/设参。

### 8.3 尚未确定、需要补录的信息

| 待确认事项 | 当前状态 | 影响/补录方式 |
| --- | --- | --- |
| 两板实际PCB版本及改动 | 未看到实物丝印/焊桥 | 提供版本和路由状态后按对应原理图核查 |
| 实物U4速度等级/最小时序 | 现有表格取ST BSP参考值 | 获取对应SDRAM器件资料，复核tRAS/tRC/CAS及实际SDCLK |
| 课程DHT11小板及数量 | 型号由用户确认；三针/四针形态、上拉、厂家版本、数量未知 | 核对丝印和上拉；决定各板独立读取或共享有效温度 |
| CubeMX系统时钟与显示配置 | 已读到6.18.1 / CubeF7 V1.17.4；NG/1024K已一致，当前仍216 MHz/HSE_ON且无LTDC | 按第一步指南核对系统时钟；显示图层固定RGB565，完成FMC后再启用帧缓冲读取 |
| 音频采样/时钟/DMA | 后续采集模块设计 | 决定SAI时钟、DMA和内存带宽，与显示/同步联调 |
| 时间同步协议与TIM2所有权 | 尚未实现 | 本文给接口，不承诺硬件自测PWM达到±10 μs同步 |
| DHT11驱动、温度时效及共享方式 | 本轮规划未实现 | 统一>2秒间隔、校验/超时、失效回退及两板声速；实测温补误差 |
| 允许的供电和示波器连接线 | 待现场器材确认 | 只使用课程提供器材，不增加未允许的电路 |

## 9. 资料来源与核查定位

本次直接读取了下列四份用户提供文件；页码按PDF页面及印刷页码一致编号。用户先前提供的AGENTS.md已应用；本轮另外完整读取SBCG项目根目录的AGENTS.md，并只读核查当前工程。原对话中的文字建议不作为板级接线证据。

| 编号 | 文件/官方来源 | 本文用到的位置 |
| --- | --- | --- |
| S1 | 用户提供 `en.DM00190424.pdf`，UM1907 / DocID027942 **Rev 4，June 2017，51页** | 第19页ETH/SDRAM；第21～23页连接器；图22第30页ST-LINK；图24第32页音频；图26第34页Arduino；图27第35页SDRAM；图29第37页MCU连接；图31第39页PHY；图33第41页LCD；表11第42～48页球位/网络及VCP路由 |
| S2 | 用户提供 `DS10916.pdf`，DocID027590 **Rev 4，227页** | 第43页内部温度；第4章Pinouts/表12第75～88页AF映射（PA15在第77页、PG12在第84页）；第151页温度采样参数；第186页SDRAM时钟条件 |
| S3 | 用户提供《嵌入式系统课程设计-分布式声波测距系统-设计任务书（V1.0）》5页 | 第2页交换机/1PPS/同步验收；第3页手动与自动温度补偿；第4页器材限制与麦克风距离；第5页电脑限制 |
| S4 | 用户提供 `STM32F746G引脚对应图.pdf`，1页 | Arduino信号与MCU端口交叉对照；CN8及5 V说明存在上述需纠正项 |
| S5 | [ST官方32F746GDISCOVERY BSP](https://github.com/STMicroelectronics/32f746gdiscovery-bsp) | [LCD驱动](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_lcd.c)、[SDRAM驱动](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_sdram.c)、[SDRAM参数](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_sdram.h)、[板级引脚/地址](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery.h)、[FT5336触控驱动](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_ts.c) |
| S6 | [ST官方CubeF7 Ethernet示例](https://github.com/STMicroelectronics/STM32CubeF7/blob/master/Projects/STM32746G-Discovery/Applications/LwIP/LwIP_HTTP_Server_Netconn_RTOS/Src/ethernetif.c) | HAL_ETH_MspInit引脚含PG2；LAN8742管理与协商；DMA/Cache结构参考，需匹配团队实际软件包版本 |
| S7 | [ST官方RK043FN48H组件参数](https://github.com/STMicroelectronics/stm32-rk043fn48h/blob/main/rk043fn48h.h) | 480×272及HSYNC/HBP/HFP/VSYNC/VBP/VFP面板参数 |
| S8 | [奥松DHT11官方产品手册V1.3_20170331](https://www.aosong.com/userfiles/files/media/DHT11-V1_3%E8%AF%B4%E6%98%8E%E4%B9%A6%EF%BC%88%E8%AF%A6%E7%BB%86%E7%89%88%EF%BC%89.pdf) | PDF第2～6页：脚序、电压、典型4.7 kΩ上拉、>2秒采样、18～30 ms起始、40位帧及版本数据格式；实际模块仍需确认 |

公开资料入口：[ST产品及对应PCB原理图](https://www.st.com/en/evaluation-tools/32f746gdiscovery.html)、[UM1907公开PDF](https://www.st.com/resource/en/user_manual/dm00190424-discovery-kit-for-stm32f7-series-with-stm32f746ng-mcu-stmicroelectronics.pdf)、[DS10916公开PDF](https://www.st.com/resource/en/datasheet/stm32f746ng.pdf)。在线PDF修订可能不同于上述用户文件；不能将在线新版页码套用到上传Rev 4。BSP链接指向可更新分支，团队实施时应固定所用CubeF7版本。

本次交付为**资料核查与配置指南**，未连接实物、未运行CubeMX生成工程、未修改`.ioc`，也未声称硬件或验收已经通过。所有需实测的事项已列入第8节。
