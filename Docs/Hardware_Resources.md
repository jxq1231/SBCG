# 分布式声波测距系统：STM32F746G-DISCO 硬件资源与 CubeMX 配置约定

修订日期：2026-10-16。适用：课程提供的两块 STM32F746G-DISCO / 32F746GDISCOVERY，MCU 为 **STM32F746NGH6，TFBGA216**。

**本文件已核查四份用户提供的 PDF；引脚映射以 UM1907 Rev 4 中的 MB1191 B-04 原理图及 I/O 分配表为依据。用户已确认温度传感器为DHT11；实际两板的PCB修订、焊桥/电阻状态、DHT11小板引脚顺序及板载上拉尚未现场确认。以下“推荐参数”是团队统一配置建议，不是已经在两块实物上验证通过的配置。**

## 0. SBCG工程现状与本次范围

项目根目录：`D:/electronic_design/SBCG`。本次里程碑为 **M0——硬件资源规划修订**，更新 `Docs/Hardware_Resources.md` 与第一步指南 `Docs/CubeMX_Step1.md`。工程源码和构建配置基线已在前序提交记录；本次只提交这两份文档。Git通过.gitignore排除build、.codex、compile_commands.json、对象文件、固件二进制及其它编译副产物，使其不出现在常规更改列表；保留Core/Drivers源码、.ioc、启动/链接脚本、CMake构建配置、共享VS Code配置及设计文档。Git作者/提交者按项目AGENTS.md使用jxq，邮箱由用户指定；不改全局身份。

已只读核对 `SBCG.ioc`、`Core/Inc/main.h`、`Core/Src/main.c`、顶层CMake及链接脚本，发现下列差异。**下文表格是目标分配，不代表当前工程已经启用这些外设。**

| 项目 | SBCG当前配置 | 目标/处理 |
| --- | --- | --- |
| MCU | **STM32F746NGH6 / STM32F746NGHx，TFBGA216** | 用户已修正，当前与开发板官方目标一致 |
| Flash | **链接脚本1024K，即1 MiB** | 已与NG目标一致；本轮未改链接脚本 |
| 软件版本 | CubeMX 6.18.1、DB.6.0.181、CubeF7 V1.17.4 | 两板固定同一版本；本文件BSP参数须对照该版本核查 |
| 显示格式约定 | **用户指定RGB565，每像素16位/2字节** | 后续CubeMX的LTDC图层、帧缓冲和绘图代码按第5.2节统一；当前未启用LTDC；目标LTDC模式和DMA2D同为RGB565 |
| 已启用资源 | SYS/RCC/NVIC/CORTEX_M7；SWD+SWO，PB3/PA13/PA14及HSE脚 | 未启用LTDC/FMC/ETH/USART1/I2C3/TIM2；DHT11的PG7未分配 |
| 时钟 | HCLK216 MHz、PCLK1 54 MHz、APB1定时器108 MHz；HSE_ON、25 MHz | 第5.1节200 MHz为建议目标，须通过CubeMX调整；不能照抄PSC=99到当前216 MHz时钟 |
| 生成GPIO | 仅开启GPIOA/B/H时钟，未定义DHT11/PPS标签 | CubeMX分配PG7及其他目标引脚后再生成GPIO初始化和标签宏 |

第一步操作见同目录 `CubeMX_Step1.md`。本地AGENTS.md第4节要求硬件变化由CubeMX完成，并在未经授权操作该流程时等待用户重新生成。因此本次没有改 `.ioc`、`SystemClock_Config()`、`MX_GPIO_Init()`、链接脚本、DMA/NVIC或固件调用链；配置差异和预期生成变化均写入第6节。

**本轮推进边界：下一次CubeMX操作只做Serial Wire、HSE Bypass 25 MHz、HCLK 200 MHz和相应总线分频。** 屏幕、网口、TIM2及音频DMA先不启用；第4～6节是后续分阶段目标，不是本轮一次完成的配置清单。

**共用工程尚未落实。** 本机只读确认的是SBCG / CubeMX 6.18.1 / CubeF7 V1.17.4 / CMake。用户审查提到另一侧SBCJ / 6.15 / CubeIDE，但该工程尚未取得；不能写成两侧已共用同一.ioc或同一固件。配置前先约定主工程和工具/固件包版本，并读取两板真实UID、指定A/B。详细记录方法见第一步指南第1.2节；不把ST-LINK USB序列号当目标MCU UID。PTP到TIM2由SBCG负责人先验证单板，再同步已验证方案到队友工程。

## 1. 先统一的结论

| 需求 | 两板统一采用的接口 | 结论与范围 |
| --- | --- | --- |
| 4.3英寸 LCD | **LTDC引脚模式、图层、帧缓冲和DMA2D均RGB565**，480×272 | 20个LTDC AF信号（16颜色+4时序）；8个未用低位颜色脚由原MCU脚固定输出0；板上连线不改 |
| 显示缓冲 | 板载 U4 SDRAM，FMC SDRAM Bank1，16位数据总线 | 芯片标称128 Mbit，板上可访问64 Mbit，即8 MiB；地址基准0xC0000000 |
| 电容触控 | PH7/PH8 的 I2C3，PI13 中断 | 与音频编解码器共用 I2C3；LCD/触控复位接 NRST，没有独立的触控复位 GPIO |
| 交换机网络 | CN9 RJ45 → U9 LAN8742A → ETH RMII | 50 MHz RMII_REF_CLK 从 PHY 输入 PA1；不是 MCU MCO 输出 |
| 整秒脉冲 | **PA15 → CN7-2 / Arduino D9** | 推荐 TIM2_CH1（AF1）输出 `PPS_OUT`，3.3 V、正脉冲、建议100 μs；由ETH/PTP秒事件硬件驱动TIM2；TIM2不作为测量时间源 |
| 观察/设参串口 | **USART1：PA9 TX、PB7 RX → ST-LINK → CN14 USB** | 115200、8数据位、无校验、1停止位、无流控；PC6/PC7 的 Arduino 串口不是 ST-LINK VCP |
| 温度补偿基础部分 | 串口或屏幕输入环境温度 | 无需外接传感器，满足任务书基础部分的接口要求 |
| 自动环境测温提高部分 | **一只课程DHT11接A板PG7/D4；A经UDP发布给B** | 两板使用同一个environment_temperature_milli_c及同一测量温度版本；故障保留最后有效值，串口手动设温保留 |

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

两板使用完全相同的 User Label。GPIO 标签采用 `LCD_DISP`、`LCD_BL_CTRL`、`PPS_OUT` 等；业务变量采用 `environment_temperature_milli_c`（计算/传输）、`environment_temperature_c`（显示）、`relative_humidity_pct`、`temperature_valid`、`temperature_sample_age_ms`、`sound_speed_mm_s`（计算）、`board_role`。外设句柄保持 CubeMX 生成的 `huart1`、`hltdc`、`hi2c3`、`heth`、`htim2`、`hsdram1` 等实际名称；不通过改引脚标签来重命名句柄。

普通 GPIO 的 User Label 通常生成 `<Label>_Pin` / `<Label>_GPIO_Port`；AF 引脚是否生成同样的宏取决于生成器。标签用于定位与团队沟通，不保证生成变量。外围 BSP 自带标签/句柄时应统一使用一套初始化，避免 CubeMX 与 BSP 重复改同一外设。

参数组：`L0`=屏幕低位GPIO恒零；`L1`=RGB565 LTDC有效线；`L2`=LCD控制；`T1`=触控；`S1`=SDRAM；`E1`=ETH；`U1`=USART；`P1`=PPS；`X1`=DHT11温湿度接口。具体数值见第5节。“占用”同时记录板上硬连线和本方案分配；未启用某外设不等于其焊接的引脚成为空闲。

### 3.1 配置前必须确认的团队约定

| 资源 | 本项目唯一选择 | 其它模块的访问方式 |
| --- | --- | --- |
| 显示 | **全部RGB565**：LTDC的RGB565引脚模式、图层/帧缓冲、DMA2D输入/输出与业务16位颜色一致；有效引脚按板级原理图，时序按本次用户约定1/43/8、10/12/4，像素时钟9.6 MHz | 第5.2节是唯一目标；不混用另一组RGB888 AF和官方BSP41/13/32时序；GUI历史/现状分开记录 |
| 同步时间 | **ETH IEEE1588 MAC硬件时钟是唯一时间源**：A主时钟、B从时钟；时间服务输出64位PTP纳秒时间 | 网络与声音到达时间戳都在PTP时间域；不从TIM2拼接测量时间 |
| TIM2 / PA15 PPS | 同一时间服务唯一管理；TIM2计数1 MHz，仅执行PTP每秒触发的100 μs脉冲 | 其它模块不得初始化、改分频或读CNT作到达时刻；原自由运行ARR999999方案废止 |
| 环境温度/算法 | 一只DHT11在A板；UDP共享整数毫摄氏度和temperature_version；**math_version=1统一整数公式** | 同次测量两板使用同一温度、t_A_ns/t_B_ns和公式，禁止一侧浮点另一侧整数 |
| A/B角色 | **同一固件按96位MCU UID允许名单固定分配角色**：BOARD_A_UID与BOARD_B_UID映射表两板完全相同 | UID未知/配置重复则UNCONFIGURED，不测量；不根据DHT是否失效临时换角色 |
| LCD/音频时钟 | PLLSAI N192/R5/DivR4及共享PLLM25锁定，LCD9.6 MHz；SAI2走PLLI2S | 音频只配置PLLI2S；PTP通过自身时间/增量校正，不改变主PLL或LCD支路 |
| 音频预留 | PI4～PI7、PG10、PD6；PH15额外保留，PH7/PH8共享I2C3 | PH15 BSP宏与原理图用途不同，按第4.7节核查后才决定IRQ配置 |

这里固定架构与配置目标，当前SBCG尚未实现PTP、角色识别、温度UDP或音频。第5.5～5.9节规定后续实现边界；不得把本文的拟定接口当作现有代码。

## 4. 完整引脚清单

### 4.1 屏幕统一RGB565：20个LTDC AF信号与8个低位GPIO

板上实际连线来自[S1]图29、图33、表11与[S2]表12。RGB565有效颜色线为R3～R7、G2～G7、B3～B7，加CLK/HSYNC/VSYNC/DE，共20个LTDC AF信号；G3必须是板上PJ10，不照搬2026-10-13 GUI历史快照误选的PH14。其它8根已焊接的低位颜色线用对应MCU GPIO推挽输出0（L0），避免屏幕输入浮空；不增加电路。**28是屏幕信号总数，不是28根RGB颜色数据线；其中24颜色+4时序。** 不调用默认BSP_Msp把全部28脚重新设成RGB888 AF。

| 原始 MCU Pin（球位） | 板级物理接口/网络 | 复用功能 | 推荐 User Label | 参数 | 占用与待验证项 |
| --- | --- | --- | --- | --- | --- |
| PI15（G4） | U19-5 / LCD_R0 | GPIO_Output，推挽恒低 | LCD_R0_ZERO | L0 | 已硬连屏；不启用LTDC AF，固定输出0 |
| PJ0（R6） | U19-6 / LCD_R1 | GPIO_Output，推挽恒低 | LCD_R1_ZERO | L0 | 已硬连屏；不启用LTDC AF，固定输出0 |
| PJ1（R7） | U19-7 / LCD_R2 | GPIO_Output，推挽恒低 | LCD_R2_ZERO | L0 | 已硬连屏；不启用LTDC AF，固定输出0 |
| PJ2（P7） | U19-8 / LCD_R3 | LTDC_R3 / AF14 | LCD_R3 | L1 | 已硬连屏，需保留 |
| PJ3（N8） | U19-9 / LCD_R4 | LTDC_R4 / AF14 | LCD_R4 | L1 | 已硬连屏，需保留 |
| PJ4（M9） | U19-10 / LCD_R5 | LTDC_R5 / AF14 | LCD_R5 | L1 | 已硬连屏，需保留 |
| PJ5（M14） | U19-11 / LCD_R6 | LTDC_R6 / AF14 | LCD_R6 | L1 | 已硬连屏，需保留 |
| PJ6（K12） | U19-12 / LCD_R7 | LTDC_R7 / AF14 | LCD_R7 | L1 | 已硬连屏，需保留 |
| PJ7（J12） | U19-13 / LCD_G0 | GPIO_Output，推挽恒低 | LCD_G0_ZERO | L0 | 已硬连屏；不启用LTDC AF，固定输出0 |
| PJ8（H12） | U19-14 / LCD_G1 | GPIO_Output，推挽恒低 | LCD_G1_ZERO | L0 | 已硬连屏；不启用LTDC AF，固定输出0 |
| PJ9（J13） | U19-15 / LCD_G2 | LTDC_G2 / AF14 | LCD_G2 | L1 | 已硬连屏，需保留 |
| PJ10（H13） | U19-16 / LCD_G3 | LTDC_G3 / AF14 | LCD_G3 | L1 | 已硬连屏，需保留 |
| PJ11（G12） | U19-17 / LCD_G4 | LTDC_G4 / AF14 | LCD_G4 | L1 | 已硬连屏，需保留 |
| PK0（G13） | U19-18 / LCD_G5 | LTDC_G5 / AF14 | LCD_G5 | L1 | 已硬连屏，需保留 |
| PK1（F12） | U19-19 / LCD_G6 | LTDC_G6 / AF14 | LCD_G6 | L1 | 已硬连屏，需保留 |
| PK2（F13） | U19-20 / LCD_G7 | LTDC_G7 / AF14 | LCD_G7 | L1 | 已硬连屏，需保留 |
| PE4（A1） | U19-21 / LCD_B0 | GPIO_Output，推挽恒低 | LCD_B0_ZERO | L0 | 已硬连屏；不启用LTDC AF，固定输出0 |
| PJ13（B9） | U19-22 / LCD_B1 | GPIO_Output，推挽恒低 | LCD_B1_ZERO | L0 | 已硬连屏；不启用LTDC AF，固定输出0 |
| PJ14（C9） | U19-23 / LCD_B2 | GPIO_Output，推挽恒低 | LCD_B2_ZERO | L0 | 已硬连屏；不启用LTDC AF，固定输出0 |
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

### 4.6 DHT11温湿度接口：单只接A板PG7 / Arduino D4，B板接收UDP温度

**DHT11为外接课程温湿度传感器，不是DISCO板载器件。** 未发现板载独立环境温度传感器；MCU内部温度传感器测的是芯片温度，不代替DHT11的环境测温。[S1、S2] 本轮按审查意见采用一只DHT11接A板PG7，由A板向B板发布温度；DHT11的单线协议不是I2C，也不需要ADC。[S8]

| 原始 MCU Pin（球位） | 板级物理接口 | 复用功能 | 推荐 User Label/变量别名 | 初始化参数 | 占用与待验证项 |
| --- | --- | --- | --- | --- | --- |
| **PG7（J14）** | **A板CN4-5 / D4 → DHT11 DATA** | **GPIO_Output，Open Drain** | **DHT11_DATA** | X1：初始High、No Pull、Low Speed | 仅A板接线/读取；B板同脚可在共享基线中保留，但不接第二只DHT11、不独立采样 |
| 无MCU信号引脚 | **CN6-4 / 3V3 → DHT11 VCC** | 电源 | 无GPIO别名 | 3.3 V供电，短线 | 核对实际模块支持3.3 V；不默认接5 V |
| 无MCU信号引脚 | **CN6-6或CN6-7 / GND → DHT11 GND** | 地 | 无GPIO别名 | 共地 | 只接地针，不把VIN/5V当GND |
| 无MCU信号引脚 | 裸DHT11的3号脚 | NC | 无 | 不连接 | 仅适用于4脚裸器件；三针小板按其丝印接，不能套用裸件脚序 |
| 无外部引脚 | A板串口或触控设置，UDP共享到B | 软件手动设温 | environment_temperature_milli_c、temperature_source、temperature_version | 基础功能保留，UI标记MANUAL/DHT11/HOLD_LAST | 不把失效或超龄读数标成新有效采样；故障保持最后有效值 |
| 无外部引脚 | MCU内部 | ADC1_IN18 / Temperature Sensor | mcu_die_temperature_c | 仅诊断，不默认启用 | 不作为环境温度；与VBAT共用内部通道 |

PG7在[S1]表11和图26中只接Arduino D4；与第4.1～4.5节的LTDC、FMC、RMII、VCP和PA15/PPS没有引脚重叠。此前给I2C温度模块预留的PB8/PB9、模拟温度预留的PA0不再为DHT11占用；PH7/PH8继续服务触控/音频I2C3，TIM2仅留给统一时间服务的PPS输出。

**接线前核对上拉。** 奥松典型电路使用约4.7 kΩ上拉；若课程三针DHT11小板已带电阻且上拉到其3.3 V电源，直接使用该小板，不额外加电路。若是没有上拉的裸件，PG7开漏方案尚不满足通信条件，不能用MCU内部弱上拉冒充外部上拉，也不能自行补非课程提供电阻。此时可进一步核查课程提供的上拉资源，或评估已硬连板上2.7 kΩ上拉的PB9/CN7-9：该脚与CN2、可能的A4焊桥共网，必须确认总线无其他设备、DHT11低电平驱动和波形合格后，再通过CubeMX迁移；**PB9不是本方案第二个同时使用的DATA脚**。

裸器件正视栅格面的脚序以[S8]图1为准：1=VDD、2=DATA、3=NC、4=GND。三针小板的VCC/DATA/GND顺序由其自身丝印或资料确定，目前没有照片，不能写死第几针为电源。旧版/不同厂家DHT11的电压和数据格式也须与实际发放模块匹配。

### 4.7 音频相关引脚预留与PD6/PH15差异

本节为配SAI前的预留，不计入前面85个显示/网络/温度/PPS默认分配。已直接核对本机CubeF7 V1.17.4的 `stm32746g_discovery_audio.h/.c`，并对照上传UM1907 Rev4的B-04图24、图29和表11。[S1、S9]

| 原始 MCU Pin（球位） | 板级物理接口/网络 | 复用功能 | 推荐 User Label/别名 | 参数 | 占用与待验证项 |
| --- | --- | --- | --- | --- | --- |
| PI4（C3） | WM8994 MCLK1 / SAI2_MCLKA | SAI2_MCLK_A / AF10 | AUDIO_MCLK | SAI2，PLLI2S | 音频保留；采样率/时序后续确认 |
| PI5（D3） | WM8994 BCLK1 / SAI2_SCKA | SAI2_SCK_A / AF10 | AUDIO_SCK | SAI2，PLLI2S | 音频保留 |
| PI6（D6） | WM8994 DACDAT1 / SAI2_SDA | SAI2_SD_A / AF10 | AUDIO_SD_A | SAI2，PLLI2S | 音频保留 |
| PI7（D4） | WM8994 LRCLK1 / SAI2_FSA | SAI2_FS_A / AF10 | AUDIO_FS | SAI2，PLLI2S | 音频保留 |
| PG10（C8） | WM8994 ADCDAT1 / SAI2_SDB | SAI2_SD_B / AF10 | AUDIO_SD_B | SAI2，PLLI2S | 音频输入保留 |
| **PD6（B11）** | **WM8994 ADCLRCLK1/GPIO1 / Audio_INT** | GPIO输入；具体功能由codec配置决定 | CODEC_AUDIO_INT（待确认） | 未默认启用IRQ | B-04确实硬连；不能因BSP宏不同删掉该连线 |
| **PH15（D13）** | **TP3 / TP_PH15测试点** | BSP宏AUDIO_IN_INT；当前保留输入，不作为输出 | AUDIO_IN_INT（软件名，待确认） | 保留；实际IRQ用途待验证 | V1.17.4宏指定GPIOH/PIN15、EXTI15_10；原理图未显示其与Audio_INT相连 |

**PD6与PH15不是同一板级信号。** V1.17.4头文件的 `AUDIO_IN_INT_GPIO_PORT=GPIOH`、`AUDIO_IN_INT_GPIO_PIN=GPIO_PIN_15`、`AUDIO_IN_INT_IRQ=EXTI15_10_IRQn` 与B-04原理图的codec连线用途不同。该BSP输入MSP把PH15配置成GPIO_INPUT/No Pull，同时启用上述NVIC IRQ；这也不能证明PH15已获得外部中断路由或声音到达事件。不得直接照搬为采集到达时刻的触发源。

PH15先不给其它GPIO输出、PPS或课程模块使用；PD6同样保留。启用采集前核对实际板修订、codec配置和所用BSP示例的事件路径；需要时用课程允许的测量工具验证实际电平。SAI DMA的缓冲半满/满回调与codec/PH15 GPIO事件应分别处理。若以后启用PH15/EXTI15，它与触控PI13/EXTI13共用EXTI15_10 IRQ处理入口，须按线路分发，不能覆盖触控回调。

### 4.8 音频DMA预留：尚未启用

以下映射已核查本机CubeF7 V1.17.4 `stm32746g_discovery_audio.h`；团队先预留，后续经CubeMX配置并核对生成结果。当前只做时钟，不新增这些DMA或IRQ。[S9]

| 功能/外设 | 方向 | DMA映射 | IRQ入口 | 当前状态 |
| --- | --- | --- | --- | --- |
| 录音 / SAI2_Block_B | 外设到内存 | **DMA2 Stream7 Channel 0** | DMA2_Stream7_IRQHandler | 音频输入预留，未启用 |
| 放音 / SAI2_Block_A | 内存到外设 | **DMA2 Stream4 Channel 3** | DMA2_Stream4_IRQHandler | 音频输出预留，未启用；只录音时不启用此DMA |

同一个Stream不能因Channel不同就同时分给两个外设。后续采集里程碑再确定采样率、数据宽度、缓冲地址/长度、循环模式、半满/满回调、Cache策略与IRQ优先级，不直接照抄BSP默认优先级或让CubeMX自动分配后未经审核合入。

**ETH有专用DMA，不占用DMA2 Stream4/7。** 音频应防止与其他通用DMA2外设发生Stream重复分配；与网口/LTDC的并发风险主要是总线/内存带宽、Cache一致性和中断负载，不能称为网口抢同一DMA2 Stream。[S2，第40页第2.31节]；[ST架构说明](https://www.st.com/en/microcontrollers-microprocessors/stm32f7-series.html)

## 5. 两板统一的初始化参数

### 5.1 时钟基线（建议值，不是唯一可行方案）

为使 SDRAM 初始化和刷新数与官方 BSP 的100 MHz配置一致，建议两板先采用 **SYSCLK/HCLK = 200 MHz，SDCLK = HCLK/2 = 100 MHz**；APB1 = 50 MHz，APB2 = 100 MHz，采用常规定时器倍频设置，使 TIM2 时钟为100 MHz。参数必须以 CubeMX Clock Configuration 显示值及生成代码为准。

| 设置 | 建议 |
| --- | --- |
| RCC HSE | Bypass Clock Source，输入25 MHz；B-04原理图X2为有源振荡器，经R30接PH0，并给PHY供25 MHz参考 |
| 主PLL | HSE，PLLM=25、PLLN=400、PLLP=2；其它输出只按实际使用外设配置 |
| 总线 | AHB /1，APB1 /4，APB2 /2；TIMPRE采用常规倍频路径并核对TIM2时钟 |
| LTDC | 锁定PLLSAIN=192、PLLSAIR=5、PLLSAI DivR=4：9.6 MHz；所有图层与有效引脚模式RGB565；音频不得改这一支路 |
| SAI2预留 | 选择RCC_SAI2CLKSOURCE_PLLI2S；PLLI2SN/Q/DivQ按采样率单独确定，不使用PLLSAI |
| 电源/Flash | 在CubeMX按实际3.3 V和200 MHz配置电压档位、必要的OverDrive及Flash延时；不凭本表跳过时钟校验 |

ST部分示例代码使用 `HSE_ON`；这里依据上传B-04原理图选择有源时钟的 Bypass。实物若为其他修订，先确认其时钟电路。200 MHz主PLL不能自然提供常见的48 MHz USB时钟；ST-LINK VCP由独立ST-LINK芯片处理，**不需要启用主MCU USB_OTG_FS/HS**。后续若启用USB/SDMMC，重新核查48 MHz域。

不要直接用216 MHz HCLK、FMC /2 得到108 MHz SDCLK。上传[S2，第186页]规定3.0～3.6 V、指定负载下SDCLK最高100 MHz；若采用216 MHz系统时钟，应选合适分频并重新计算SDRAM时序/刷新计数，而不是照抄下面100 MHz数值。100 MHz也仍需确认板负载、实物SDRAM与电压条件。

### 5.2 L1/L2：LCD-LTDC与控制GPIO

| 项目 | CubeMX/初始化建议 |
| --- | --- |
| LTDC引脚模式 | **RGB565**，第4.1节20个有效信号AF推挽、No Pull、高/很高速度；PG12/AF9；G3/PJ10；8个低位GPIO按L0推挽/低电平/低速 |
| 面板有效区 | 480×272 |
| 水平参数（实际长度） | **HSYNC=1，HBP=43，HFP=8**，团队选定值，源自2026-10-13 GUI历史快照 |
| 垂直参数（实际长度） | **VSYNC=10，VBP=12，VFP=4** |
| HAL寄存器字段核对 | HorizontalSync=0；VerticalSync=9；AccumulatedHBP=43；AccumulatedVBP=21；AccumulatedActiveW=523；AccumulatedActiveH=293；TotalWidth=531；TotalHeigh=297 |
| 极性 | HS/VS/DE：Active Low；Pixel Clock：IPC，按BSP选择输入像素时钟极性（通常界面对应正常/上升沿），核对生成的 `LTDC_PCPOLARITY_IPC` |
| 图层固定约定 | 一层RGB565、480×272，起址0xC0000000，Alpha=255、Alpha0=0、背景黑，Blend1/2均CA；HAL字段LTDC_PIXEL_FORMAT_RGB565 |
| DMA2D | 输出DMA2D_OUTPUT_RGB565；使用输入图像时InputColorMode为RGB565；图像/业务颜色用uint16_t，无32位帧缓冲 |
| 缓冲大小 | RGB565每帧480×272×2=261120字节；双缓冲522240字节；可将第二缓冲放0xC0040000，两块按256 KiB间距预留 |
| GPIO控制 | PI12=LCD_DISP，PK3=LCD_BL_CTRL；推挽、No Pull、低速，初始低；SDRAM和LTDC图层准备好后再拉高 |

CubeMX有的界面填写实际长度、有的显示累计字段，按表核对，避免重复减1。所选时序源自2026-10-13读取的GUI历史快照[S10]；官方BSP的41/13/32与10/2/2不是本项目第二套可选配置。用户曾报告GUI已点亮，本次未复测；SBCG目标为9.6 MHz、G3/PJ10和外部SDRAM，必须独立验证显示，不能继承另一版本GUI的实测结论。

**业务与配置都统一RGB565。** 默认 `BSP_LCD_Init()/BSP_LCD_MspInit()` 和ARGB8888图层初始化不得覆盖CubeMX的20个AF+8个恒低GPIO方案。绘图/图像内存均为uint16_t；SDK某个控制参数若要求内部其它编码，只在单一BSP包装内转换，不改变业务数据、帧缓冲或DMA2D的565配置。

| 项目 | 2026-10-13 GUI历史快照 | SBCG唯一目标 |
| --- | --- | --- |
| 引脚模式/图层 | 两者都是RGB565 | 同样全部RGB565；有效G3从PH14纠正为板上PJ10，低8位GPIO输出0 |
| 水平时序 | 1 / 43 / 8 | 1 / 43 / 8 |
| 垂直时序 | 10 / 12 / 4 | 10 / 12 / 4 |
| 像素时钟 | PLLSAIN200/R5/DivR4 → 10 MHz | PLLSAIN192/R5/DivR4 → 9.6 MHz，保持团队时钟约定 |
| 帧缓冲 | 0x20010400，内部SRAM | 0xC0000000，完成FMC/SDRAM与MPU配置后使用 |
| SDRAM CKE连线 | GUI.ioc选PH2 | 板上实际CKE为PC3；不照搬PH2 |

**2026-10-16重新核对GUI现状**：GUI.ioc已采用H41/13/32、V10/2/2，PLLSAI192/R5/div4为9.6 MHz，G3/PJ10、CKE/PC3、帧缓冲0xC0000000；生成main.c也采用对应累计时序。它与上表历史快照不同，与本次用户指定的SBCG目标也不是同一组时序。本轮保留SBCG的1/43/8、10/12/4约定，后续显示里程碑再共同审核配置版本；本次对GUI全程只读，不宣称新GUI已实测通过。

配置完成后验证黑/白/红/绿/蓝、灰阶和渐变、整屏/边缘/刷新，以及200 MHz系统、音频采样同时运行。不能仅以“背光亮”证明颜色脚、时序、SDRAM和帧缓冲全正确。

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
- **统一时间源已经选定：ETH IEEE1588 MAC硬件时钟。** A为固定PTP主时钟，B为从时钟；采用PTPv2/UDP IPv4双向时间交换（事件端口319、一般消息320），TX/RX时刻取MAC硬件时间戳，不取LwIP任务/ETH中断到达时刻。HAL V1.17.4有HAL_ETH_USE_PTP保护的配置/取时/TX/RX时间戳接口。[S11]
- 时间服务统一返回 `SyncTime_GetNs()`：64位PTP纳秒时间与LOCK状态；需要微秒展示时仅做整数ns/1000。采用十进制纳秒rollover，读取秒/纳秒须处理跨秒一致性。A的共同纪元不要求UTC；测量只比较同一同步纪元内的时间。
- B通过PTP时间/增量（addend）校正跟随A；不修改200 MHz主PLL、LCD PLLSAI或音频PLLI2S来校正时间。声音到达时间由采样锚点及样本索引映射到PTP时间域，不把DMA回调时间直接当声音到达时刻；任何模块都不使用TIM2作为第二套测量时间。
- TIM2仅把PTP秒事件送到PA15，按第5.6节执行。失锁不自动切换到TIM2软件时钟并继续声称同步。普通交换机排队/路径不对称仍进入误差预算；硬件支持1588不等于已达到±10 μs，必须实测30秒收敛和连续10次PPS边沿差。

### 5.6 U1/P1：串口与由PTP驱动的TIM2 PPS

USART1仍为PA9/PB7、115200 8N1、无流控、Oversampling16，经CN14 ST-LINK VCP。默认R64接通/R63断开/R58接通；不占用PA9为USB_FS_VBUS。[S1，第48页]

**TIM2只有一个主人，但不再是测量时间基准。** 统一时间服务拥有ETH/PTP和TIM2输出驱动；其它模块只调用PTP时间接口，不另初始化TIM2。原先ARR999999自由运行1 Hz并拼接TIM2时间的方案废止，避免与1588时间域并存。

| 参数 | 唯一目标配置 |
| --- | --- |
| 输出 | PA15 / CN7-2 / D9，TIM2_CH1，AF1，标签PPS_OUT |
| TIM2输入/计数 | 目标HCLK200 MHz、APB1定时器100 MHz；PSC=99，计数1 MHz；其它模块禁止改分频 |
| 触发源 | ETH MAC每PTP秒的内部PPS事件 → TIM2 ITR1，HAL映射常量TIM_TIM2_ETH_PTP；不使用板上PB5作外部跳线 |
| TIM2工作方式 | 内部时钟，Slave Reset Mode由该PTP事件清零计数；CH1 PWM1正脉冲，CCR1=100（100 μs） |
| ARR | **0xFFFFFFFF**，避免定时器自行每秒溢出产生另一组脉冲；1秒周期来自PTP触发，不来自ARR999999 |
| 启动/失锁 | 有效同步锁定后才开启验收PPS；初始化时确保输出低、CNT不落在脉冲区；失锁停止有效PPS并显示状态 |
| 到达时刻 | SyncTime_GetNs() / 采样锚点转换返回PTP时间；禁止读TIM2 CNT当绝对或同步时间 |

V1.17.4 TIM扩展头文件包含TIM_TIM2_ETH_PTP，ST资料说明PTP PPS可连TIM2；具体CubeMX内部映射、触发极性、重置与PWM边沿必须在该版本及实物验证。[S11] 参数由统一负责人通过CubeMX配置；如果界面不能表示该映射，暂停PPS路径，由团队重新评审保持ETH/PTP唯一时间源的方案，不手改.ioc或偷偷改生成初始化，也不退回TIM2自由计满1秒。

**先过SBCG单板验证关卡，再同步到另一工程。** PTP时间初始化/锁定后，观察PA15是否在每个PTP整秒仅输出一次100 μs正脉冲；记录生成配置、触发来源、PTP状态与示波器结果，确认相位由PTP驱动。只看到1 Hz波形不算证明。此关卡与后续双板经交换机连续10次±10 μs验收分开，当前均未执行。

PPS验收时每个上升沿表示PTP共同整秒，而不是复位后的本板自由运行整秒。须验证没有启动伪脉冲、PTP校时重复脉冲、提前ARR溢出脉冲，且100 μs脉宽与连续10次±10 μs边沿差均合格。

示波器CH1尖端接A板CN7-2、CH2接B板CN7-2，地夹各接本板CN6-6/7 GND；确认供电允许共地，高阻/10×探头观察0～3.3 V。PPS信号线不互接，示波器不是同步源。

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
| 变量 | environment_temperature_milli_c、relative_humidity_pct、temperature_valid、temperature_sample_age_ms、temperature_source；environment_temperature_c仅显示 |
| 数据时效 | 返回的是上一次测量；长时间停读后按正常>2秒间隔预读/再读，不用无间隔的两连读绕过采样限制 |

默认保持GPIO开漏输出模式即可释放总线并读取引脚输入，不需要反复把生成的GPIO初始化改成输入/推挽。若后续选EXTI或定时器捕获，先明确新的CubeMX配置和驱动所有权，不能在现有生成代码里偷偷追加AF或IRQ初始化。

微秒计时不能用 `HAL_Delay()`；若使用DWT，其周期计数换算依据实际 `SystemCoreClock`，目标200 MHz和当前216 MHz不可混用。**读取DHT11时不全局关闭中断数毫秒**，以免破坏±10 μs同步、ETH服务或音频DMA。带中断的短轮询在高负载下仍可能漏掉DHT11位边沿；按响应、位宽、校验和超时检测失败，延后重试，不为温度读取阻塞时间同步。正式联调应同时刷新LCD、收发网络并采集音频；若失败率不可接受，再经CubeMX设计独立捕获方案。

**两块板必须用同一个温度。** 一只DHT11接A板，A温度服务是唯一自动温度来源；B通过UDP接收，不独立读取另一只传感器。两板使用相同 `environment_temperature_milli_c` 整数值和第5.9节固定声速算法，避免两板声速不同影响同次测量两屏距离差≤1 mm的目标；时间戳、算法、传感器精度及最终测距指标仍需实测。传感器放通风处，避开MCU、背光和稳压器热源。

- A发布经校验的温度值、temperature_version、来源和采样时效。UDP有丢包/乱序，B不采用旧版本覆盖新值；关键更新需确认接收并在双方约定的后续测量生效。
- **同一测量锁定同一温度版本/快照**：不能A已用新温度、B仍用旧温度，也不能在同一测量计算中途更新声速。温度版本和measurement_id共同用于比对；发送/接收算法在后续协议增量实现。
- DHT11失效、校验失败或超时，**两板均保留上次已共享的有效值**，标记HOLD_LAST/超龄；不写成0℃。UDP暂时中断也不独立产生另一温度值。首次启动尚无已共享并确认的有效温度时，temperature_ready=false，显示“温度未就绪”，**不生成距离，不设默认25°C**，也不能伪造一次0℃采样。已有共同有效值后HOLD_LAST不等于首次未就绪；有效手动设温经A确认、共享后也可使temperature_ready=true。
- 串口手动设温必须保留。A接收手动设温后作为唯一温度发布源同步给B；若命令从B输入，由B转交A确认后再共同生效，不各自直接改变量。手动/自动模式切换同样版本化并显式显示。
- 温度传输固定使用int32_t整数毫摄氏度 `environment_temperature_milli_c`，不因某些DHT11每1℃更新就把所有模块强制取整。实际读到0℃若校验有效仍是有效温度，失效状态不能用数值0表示。

奥松V1.3手册给出约±2℃温度精度、秒级响应，测试条件包含25℃和5 V；3.3 V使用时仍需实测。因此自动获得读数不等于课程的测距温补指标已经通过。温度解码按实际器件版本确定，新版本可能含小数及符号；不要一概把第4字节忽略。[S8]

内部温度只可作为诊断：ADC1_IN18与VBAT共用内部通道，不能同时启用两者并把VBAT当温度；如启用，等待至少10 μs、采样至少10 μs，并按VDDA及出厂校准值换算。[S2，第43、151页]

### 5.8 A/B角色：同源同固件，以MCU UID固定识别

角色机制固定为**96位MCU UID允许名单**，不用传感器是否响应、启动顺序、MAC大小或手动临时猜测角色。

1. 配置前读取两块板的HAL_GetUIDw0/w1/w2，记录三个32位字；由团队指定接DHT11的实体为A，另一块为B。
2. 在后续用户自有板级配置中放入同一张BOARD_A_UID、BOARD_B_UID映射表，两板编译/烧录同一份程序和相同映射；上电按三个字精确比较决定角色。实际UID尚未提供，本文不编造值。
3. UID未知、两条配置相同或检测到同角色冲突时标记UNCONFIGURED/ROLE_CONFLICT；不读取DHT11、不启用验收测量，并经串口/屏幕说明。传感器故障不改变A/B角色。

| 功能 | A板 | B板 |
| --- | --- | --- |
| PTP | 固定主时钟，建立共同纪元 | 固定从时钟，收敛到A |
| DHT11 | 仅A的PG7接线并读取；手动设温也由A统一发布 | PG7预留，不读第二只DHT11；只接收已确认温度 |
| IP / MAC | 192.168.10.1 / 02:00:00:00:00:01 | 192.168.10.2 / 02:00:00:00:00:02 |
| 测距/显示 | 使用同一测量ID、两个PTP到达时刻及温度版本 | 使用完全相同字段和math_version=1公式 |
| 串口角色输出 | role=A及本机/对端UID | role=B及本机/对端UID |

上述地址用于课程独立测试网络，掩码255.255.255.0。角色表是软件板级配置，.ioc硬件基线相同；使用HAL读UID不增加GPIO。UID比较顺序固定w0/w1/w2，传输字段采用网络字节序。角色识别及UDP/PTP实现尚未写入固件。

### 5.9 声速与距离：math_version=1全整数约定

两板调用未来同一公共测量模块，禁止复制两套浮点/整数实现。环境温度的计算/传输规范值是 `int32_t environment_temperature_milli_c`（单位0.001℃）；名称 `environment_temperature_c` 仅作为UI的摄氏度显示，不进入测距计算。显示也可用整数商/余数格式化。

采用线性近似 c=331.3+0.606T（m/s）作为本项目统一模型，不声称其包含湿度/气压全部影响；标定/提高部分另行统一修订模型版本。[S12]

```text
round_div_signed(n, d) = n>=0 ? (n+d/2)/d : -((-n+d/2)/d)
    // d>0；整数除法向零截断；四舍五入，正负恰半均远离零

sound_speed_mm_s = 331300
                 + round_div_signed(606 * environment_temperature_milli_c, 1000)

delta_ns = t_B_ns - t_A_ns
distance_mm = (sound_speed_mm_s * abs(delta_ns) + 500000000) / 1000000000
```

中间乘法用64位整数，delta_ns为int64_t。若PTP绝对时刻为uint64_t，先比较两时刻并求非负差值，检查不超过100000000 ns，再按先后转换为有符号差；不能直接无符号相减后依赖溢出或对INT64_MIN取abs。仅对同一测量ID、同一PTP纪元、已锁定且同一temperature_version的完整数据计算。统一温度模型输入范围-20000～60000毫摄氏度（实际模块测量范围另校验）；温度乘法与距离乘法均在64位范围内。此为两端声到达时间差，**不除以2**。按任务书声源位于两板基线延长线、距近板0.5～1 m布置时，c×时差对应麦克风间距；声源在其它位置时不能把声程差当板间距。[S3，第1、4页]

方向：delta_ns>0为A侧、<0为B侧，=0为未定。默认结果整数mm；屏幕cm/m文本由这个共同distance_mm格式化，不另算浮点距离。DHT失效仍使用最后共同有效温度版本；新温度不在一笔测量中途生效。

| 共同输入 | 两板共同结果 |
| --- | --- |
| T=20.000℃，20000毫摄氏度 | sound_speed_mm_s=343420 |
| T=25.000℃，25000毫摄氏度 | sound_speed_mm_s=346450 |
| T=-10.000℃，-10000毫摄氏度 | sound_speed_mm_s=325240 |
| T=20.000℃，delta_ns=1000000 | distance_mm=343，A侧 |
| T=20.000℃，delta_ns=-1000000 | distance_mm=343，B侧 |

公共模型、取整规则及版本相同可消除两边算法表示差异，不能代替时间同步、声音检测、标定和≤1 mm两屏差的实测验证。

## 6. 分步CubeMX配置指南

1. **记录两板实物。** MCU型号、MB1191修订、屏幕型号、ST-LINK连接与焊接改动；记录CubeMX/CubeIDE版本和STM32CubeF7包版本。若实物与B-04不同，先用对应ST原理图核对差异；核对课程DHT11小板的引脚丝印、电压、上拉和数量。
2. **建立一份共享硬件基线。** 优先Board Selector选STM32F746G-DISCO；若选MCU Selector，必须选STM32F746NGH6/TFBGA216。SBCG现有工程已选NG型号、链接Flash为1024K，保持这一基线；不新建替代项目，也不直接改 `.ioc` 或链接脚本。已有项目通过图形界面编辑。板默认初始化可作参考，但逐项检查默认启用的外设；不能只依赖Board Selector自动结果。
3. **SYS/RCC/Clock Configuration。** Debug选Serial Wire，保留PA13/PA14；配置HSE25 MHz及第5.1节基线。确认SYSCLK/HCLK、APB时钟、TIM2、SDCLK和LTDC时钟实际显示值，无时钟错误。暂不启用主MCU USB。
4. **固定GPIO和标签。** 按第4节逐脚选择功能和User Label；对自动分配到错误焊接位置的引脚，在图形界面重新选择、必要时锁定。重点检查PG12/AF9、SDCKE0/PC3、SDNE0/PH3、USART1_RX/PB7、ETH_TXD0/PG13和ETH_TXD1/PG14。
5. **FMC。** 启用SDRAM Bank1，16位、8列、12行、4内部Bank和第5.3节参数；确认全部38个FMC引脚。生成后的应用还需SDRAM命令序列和刷新设置，不能只打开FMC就使用外部内存。
6. **LTDC。** 选择RGB565引脚模式，按第4.1节配置20个有效AF及8个低位GPIO恒零；G3选PJ10。输入第5.2节唯一时序1/43/8和10/12/4，图层/帧缓冲/DMA2D均565。完成SDRAM/MPU再验证整屏颜色。
7. **I2C3/触控。** 选择PH7/PH8，100 kHz、7位寻址；PI13先输入轮询。接入与当前软件包相符的FT5336/BSP驱动；屏幕触控方向实测后再启用EXTI。复位不增加GPIO。
8. **USART1。** Asynchronous，手动核对PA9 TX和PB7 RX，填入U1参数；选择需要的RX中断。两块板分别经CN14接电脑，测试发送和设温命令。不要选择PC6/PC7来代替VCP。
9. **ETH/LwIP。** RMII、LAN8742、E1参数，核对全部固定引脚和PG2占用；设置不同MAC/IP，选择适合应用的LwIP运行方式及NVIC。按所用CubeF7/HAL版本处理描述符、链接脚本和Cache，保留同步模块的时间戳设计位置。
10. **PTP/TIM2 PPS。** 先落实ETH硬件PTP唯一时钟，再将其内部秒事件映射到TIM2 ITR1；PA15计数1 MHz、CCR100、ARR0xFFFFFFFF按第5.6节输出脉冲。声音时间戳取PTP，TIM2仅执行输出，禁止另一模块另改分频或初始化。
11. **DHT11。** 一只DHT11仅接A板PG7，由A温度服务经UDP发布到B；共享基线中PG7可在B保留但不采样。Pinout选择PG7为GPIO_Output；在System Core → GPIO配置Output Open Drain、Output Level High、No Pull、Low Speed、User Label=`DHT11_DATA`，保存并Generate Code。预期main.h生成DHT11_DATA_Pin/DHT11_DATA_GPIO_Port，GPIO初始化开启GPIOG并设置PG7开漏/初始释放；独立GPIO文件模式时对应gpio.c/gpio.h。不用打开I2C1/ADC3，不更改TIM2/NVIC/DMA。基础手动设温保留。
12. **保存与生成。** 仅点击CubeMX保存`.ioc`、Generate Code；配置保留USER CODE区。检查生成的GPIO AF、外设初始化与时钟值，业务逻辑放USER CODE区或独立文件。SDRAM命令、触控驱动、PHY管理、时间同步并非全部由CubeMX自动完成。
13. **队友共用。** 两板使用同一.ioc基线、固件及BOARD_A_UID/BOARD_B_UID角色表；上电UID查表确定角色。两端共用第5.9节math_version=1整数模块和测量快照，运行验收前核对第8节清单。

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
| 时间域与TIM2 | ETH/PTP为唯一测量时间；TIM2 1 MHz只执行PTP秒脉冲，取消ARR999999自由秒时钟；所有者唯一 |
| PB5：ETH_PPS_OUT候选与USB_HS_ULPI_D7 | 板上PB5连USB PHY，且不是已核实的Arduino输出口；不默认用PB5替代本方案PPS接口 |
| PI1：Arduino D13与板载LD1 | D13带LED负载；不是优先选择的验收PPS接口 |
| Arduino A4/A5与D14/D15 | CN5-5/6通过SB4/SB5、SB1/SB3在PF7/PF6与PB9/PB8之间选择；检查[S1]图26和桥位实物，不能同时视为独立信号 |
| 上传引脚图“CN8电源排针” | 官方电源排针为**CN6**；CN8是SWD相关连接器。按官方针号和实物丝印接地/取电 |
| 上传引脚图“数字接口0～5 V” | STM32逻辑输出为3.3 V；部分FT引脚输入耐压有条件，不能概括为全部5 V可用；模拟输入仅0～3.3 V。本文接线统一3.3 V逻辑 |
| 216 MHz系统时钟与SDRAM /2 | SDCLK108 MHz不能照抄100 MHzBSP配置；本文件建议200 MHz基线，其他时钟需重新校核 |
| LTDC与SAI音频时钟 | PLLSAI N192/R5/DivR4和共享PLLM25锁定，LCD9.6 MHz；SAI2使用PLLI2S，音频不得改PLLSAI |
| CubeMX与BSP各自初始化同一外设 | 统一所有权、句柄和时序；触控/音频共用I2C3时不能互相重新初始化或混用两个锁 |
| Cache、ETH DMA、音频DMA、LTDC同时使用 | 明确DMA可访问内存、MPU属性、对齐及Cache维护；仅关掉报错并不能证明数据一致 |
| 两板温度更新与UDP延迟 | 一只DHT11、A发布/B接收；同一测量绑定相同温度版本，丢包/失效保留最后共同有效值，手动设温也统一发布 |
| PD6与PH15的音频IRQ命名 | B-04的PD6硬连Audio_INT，PH15为TP3；V1.17.4 BSP宏AUDIO_IN_INT指PH15。两脚保留并核实，不当作同一信号 |
| 两板有相同硬件配置 | 相同配置不自动实现同步；MAC/IP唯一，共同时间服务、协议及同一测量温度快照一致 |

**为完整课程任务提前保留音频资源**：[S1]图24、表11中PI4/PI5/PI6/PI7/PG10为SAI2相关、PD6为原理图Audio_INT，PH15须按第4.7节为BSP音频相关信号保留，PH7/PH8用于音频控制；板载U20/U21麦克风的MIC_DATA/MIC_CLK经WM8994编解码器路径处理，不是可随意指定为ADC输入的麦克风接口。第4.8节预留BSP对应的输入/输出DMA映射；采样率、缓冲与具体采集配置仍待音频里程碑确定，尚未启用。

## 8. 两板联调与待验证清单

### 8.1 配置/接线检查

- [ ] A/B板PCB修订与B-04图一致，或已对照对应ST原理图更新差异。
- [ ] 已落实SBCG/SBCJ的共用工程、生成工具与版本；跨工程共享仍未完成时不标为已统一。
- [ ] 对照实物确认CN7-2是D9/PA15，CN6-6/7是GND；不是根据PDF图的观看方向猜针序。
- [ ] 确认PA9 VCP路由电阻状态、两块板各自COM号。
- [ ] 固定外设合计：LCD/触控33个普通I/O、SDRAM38个、ETH10个、VCP2个、PPS1个、DHT11 1个；共85个**不同**I/O分配（其中B板PG7仅预留），不含NRST、SWD、RCC及第4.7节额外音频预留。按生成代码核对没有重复分配。
- [ ] 记录实际HSE、HCLK、SDCLK、LTDC像素时钟及TIM2计数时钟。
- [ ] SDRAM上电命令、刷新、内存区域和Cache方案已统一；LCD图层像素格式与绘图程序一致。
- [ ] 触控型号、7位地址及方向已确认；共享I2C3访问不存在重复初始化。
- [ ] PHY地址/标识、50 MHz参考时钟、自动协商和拔插网线恢复已确认。
- [ ] 已指定统一时间服务负责人；声音/网络都在ETH/PTP时间域，TIM2仅执行内部PTP秒事件，未另建TIM2测量时钟。
- [ ] 已读两板UID、固定A/B角色表；A单只DHT11、B只接收共享温度，角色/IP/MAC重启后不互换。
- [ ] 两端都调用math_version=1整数公式，同一测量使用相同毫摄氏度、t_A_ns/t_B_ns和取整规则。
- [ ] 已保留PH15和PD6，核对V1.17.4 BSP与实物原理图的差异，SAI/DMA事件路径与GPIO事件分开。
- [ ] 音频时钟源为PLLI2S，PLLSAI N192/R5/DivR4及LCD9.6 MHz保持锁定。
- [ ] 音频输入DMA2 Stream7 Channel 0、输出Stream4 Channel 3预留已审核；未与其它通用DMA2外设重复分配，ETH专用DMA另核对。

### 8.2 验收相关测量

- [ ] 单板PPS高电平脉宽≥1 μs，建议100 μs；信号0～3.3 V，无启动异常脉冲误判。
- [ ] SBCG单板PTP整秒到TIM2/PA15路径已证明，随后再同步到队友工程；未用TIM2自由1秒假充PTP整秒。
- [ ] 9.6 MHz、H1/43/8、V10/12/4已重新点亮，纯色/渐变/边缘/刷新通过；未沿用10 MHz测试结论。
- [ ] 双板通过交换机，在LCD刷新、串口输出、音频采集同时运行时，上电30秒内收敛。
- [ ] 收敛后连续记录10个上升沿差 `Δt=tB−tA`，保存符号及最大绝对值，要求每次 `abs(Δt)≤10 μs`；不只取平均值。
- [ ] 显示/串口能区分未同步、正在收敛、已收敛，示波器结果与软件偏差记录一致。
- [ ] A板串口手动设温经UDP共同生效；两板每次测量绑定相同temperature_version和environment_temperature_milli_c，显示温度/声速一致。
- [ ] 模拟DHT11失效及UDP丢包/乱序，两板保留最后共同有效值并显示状态；不把故障写成0℃。
- [ ] 首次温度未就绪不生成距离，不设默认25°C；共同有效手动温度和HOLD_LAST路径行为一致。
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
| 课程DHT11小板 | 已确定单只接A、UDP共享到B；形态、上拉、厂家版本未现场确认 | 核对丝印和上拉；不使用两只独立温度参与同一测量 |
| CubeMX系统时钟与显示配置 | 已读到6.18.1 / CubeF7 V1.17.4；NG/1024K已一致，当前仍216 MHz/HSE_ON且无LTDC | 按第一步指南核对系统时钟；显示图层固定RGB565，完成FMC后再启用帧缓冲读取 |
| SBCG与SBCJ共用工程 | 本机确认SBCG/CMake/6.18.1；另一侧6.15/CubeIDE来自用户审查，工程尚未取得 | 先落实主工程、工具与包版本，未迁移前不能声称共用同一.ioc |
| 音频采样/时钟/DMA与IRQ | PLLI2S及第4.8节BSP DMA预留已定；PH15/PD6用途存在BSP/原理图差异 | 确认采样/缓冲/IRQ与实际生成DMA；禁止更改PLLSAI，不把BSP宏当接线证据 |
| PTP时间服务/PPS内部映射 | 唯一时间源ETH1588、TIM2仅输出已定；协议/服务/映射尚未实现 | 验证硬件TX/RX时戳、跨秒读一致性、PTP→TIM2硬件PPS与指定交换机±10 μs |
| 两板实际UID | UID角色机制已定，BOARD_A_UID/BOARD_B_UID值尚未读取提供 | 配置前记录真实UID，不填假值；同一固件未知UID停用测量 |
| DHT11驱动与UDP温度版本 | 单DHT11共享、手动设温及保留最后值已定；协议尚未实现 | 实现版本确认/同次测量快照/失效状态，验证两屏差≤1 mm与温补误差 |
| 允许的供电和示波器连接线 | 待现场器材确认 | 只使用课程提供器材，不增加未允许的电路 |

## 9. 资料来源与核查定位

本次直接读取了下列四份用户提供文件；页码按PDF页面及印刷页码一致编号。用户先前提供的AGENTS.md已应用；本轮另外完整读取SBCG项目根目录的AGENTS.md，并只读核查当前工程。原对话中的文字建议不作为板级接线证据。

| 编号 | 文件/官方来源 | 本文用到的位置 |
| --- | --- | --- |
| S1 | 用户提供 `en.DM00190424.pdf`，UM1907 / DocID027942 **Rev 4，June 2017，51页** | 第19页ETH/SDRAM；第21～23页连接器；图22第30页ST-LINK；图24第32页音频；图26第34页Arduino；图27第35页SDRAM；图29第37页MCU连接；图31第39页PHY；图33第41页LCD；表11第42～48页球位/网络及VCP路由 |
| S2 | 用户提供 `DS10916.pdf`，DocID027590 **Rev 4，227页** | 第40页ETH专用DMA/PTP；第43页内部温度；第4章Pinouts/表12第75～88页AF映射（PA15在第77页、PG12在第84页）；第151页温度采样参数；第186页SDRAM时钟条件 |
| S3 | 用户提供《嵌入式系统课程设计-分布式声波测距系统-设计任务书（V1.0）》5页 | 第2页交换机/1PPS/同步验收；第3页手动与自动温度补偿；第4页器材限制与麦克风距离；第5页电脑限制 |
| S4 | 用户提供 `STM32F746G引脚对应图.pdf`，1页 | Arduino信号与MCU端口交叉对照；CN8及5 V说明存在上述需纠正项 |
| S5 | [ST官方32F746GDISCOVERY BSP](https://github.com/STMicroelectronics/32f746gdiscovery-bsp) | [LCD驱动](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_lcd.c)、[SDRAM驱动](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_sdram.c)、[SDRAM参数](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_sdram.h)、[板级引脚/地址](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery.h)、[FT5336触控驱动](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_ts.c) |
| S6 | [ST官方CubeF7 Ethernet示例](https://github.com/STMicroelectronics/STM32CubeF7/blob/master/Projects/STM32746G-Discovery/Applications/LwIP/LwIP_HTTP_Server_Netconn_RTOS/Src/ethernetif.c) | HAL_ETH_MspInit引脚含PG2；LAN8742管理与协商；DMA/Cache结构参考，需匹配团队实际软件包版本 |
| S7 | [ST官方RK043FN48H组件参数](https://github.com/STMicroelectronics/stm32-rk043fn48h/blob/main/rk043fn48h.h) | 480×272及官方BSP时序资料对照；本项目唯一时序按S10，不混用 |
| S8 | [奥松DHT11官方产品手册V1.3_20170331](https://www.aosong.com/userfiles/files/media/DHT11-V1_3%E8%AF%B4%E6%98%8E%E4%B9%A6%EF%BC%88%E8%AF%A6%E7%BB%86%E7%89%88%EF%BC%89.pdf) | PDF第2～6页：脚序、电压、典型4.7 kΩ上拉、>2秒采样、18～30 ms起始、40位帧及版本数据格式；实际模块仍需确认 |
| S9 | 本机 `C:/Users/25747/STM32Cube/Repository/STM32Cube_FW_F7_V1.17.4/Drivers/BSP/STM32746G-Discovery/` 的 `stm32746g_discovery_audio.h/.c`；[ST公开音频头文件](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_audio.h)、[ST公开音频实现](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_audio.c) | 本机.h第118～132行PH15/EXTI宏；.c第650～686行SAI2使用PLLI2S，第1136～1181行PH15输入/NVIC与DMA；公开分支可变，结论优先按本机V1.17.4及实物图核查 |
| S10 | 本机D:/electronic_design/GUI/GUI.ioc、Core/Src/main.c、stm32f7xx_hal_msp.c（两次核查均只读） | 2026-10-13历史快照：RGB565、H1/43/8与V10/12/4、PLLSAI200/R5/div4、G3=PH14、CKE=PH2、FB0x20010400；2026-10-16现状：H41/13/32与V10/2/2、PLLSAI192/R5/div4、PJ10/PC3、FB0xC0000000。以S1核对连线，不声称复测GUI |
| S11 | [ST STM32F7 ETH官方培训](https://www.st.com/resource/en/product_training/STM32F7_Peripheral_ETH.pdf)第6页及本机CubeF7 V1.17.4 HAL ETH/TIM扩展头/实现 | MAC硬件PTP时间、PTP PPS到TIM2；HAL_ETH_USE_PTP/GetTime/TX/RX时戳、TIM_TIM2_ETH_PTP内部映射；本机HAL_GetUIDw0/w1/w2提供96位UID |
| S12 | [TI超声测距设计资料](https://www.ti.com/mx/lit/pdf/tiduai8) | c=331.3+0.606T干空气线性近似；本项目自行规定整数单位/取整及双接收TDOA，不套用回波除以2 |

公开资料入口：[ST产品及对应PCB原理图](https://www.st.com/en/evaluation-tools/32f746gdiscovery.html)、[UM1907公开PDF](https://www.st.com/resource/en/user_manual/dm00190424-discovery-kit-for-stm32f7-series-with-stm32f746ng-mcu-stmicroelectronics.pdf)、[DS10916公开PDF](https://www.st.com/resource/en/datasheet/stm32f746ng.pdf)。在线PDF修订可能不同于上述用户文件；不能将在线新版页码套用到上传Rev 4。BSP链接指向可更新分支，团队实施时应固定所用CubeF7版本。

本次交付为**资料核查与配置指南**，未连接实物、未运行CubeMX生成工程、未修改`.ioc`，也未声称硬件或验收已经通过。所有需实测的事项已列入第8节。
