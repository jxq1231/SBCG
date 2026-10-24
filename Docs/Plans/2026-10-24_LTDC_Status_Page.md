# U2：CubeMX LTDC RGB565配置与状态页直接验收计划

<!-- doc-status: current-plan-awaiting-cubemx-ltdc; reviewed: 2026-10-24 -->

**目标：** 在当前STM32F746G-DISCO上直接显示480×272真实状态页，并保持原音频采集和前台业务运行。用户要求跳过独立色条实验；不创建色条固件、色条任务或相应验收关口。

**架构：** CubeMX拥有引脚/PLLSAI/LTDC初始化；BSP延后调用生成初始化；前台从业务快照取数，CPU分片绘制RGB565双缓冲并在垂直消隐切换。显示构建不运行会覆盖整片SDRAM的U1破坏性自检。

**技术栈：** 现有GCC/CMake/Ninja、HAL、NO_SYS前台调度、已完成U0纯C绘图层、U1 SDRAM。当前仅A板在场；不需要网线、示波器或主动发声。TouchGFX、触控、DMA2D、波形页和测距算法后续独立处理。

本页是配置及后续接入计划，尚无LTDC生成/烧录/屏幕通过结论。板上仍为已验收的SBCG-U1-SDRAM-R1。遵循根AGENTS.md第4节：先由用户在当前SBCG.ioc通过CubeMX配置并Generate Code，之后再改应用；不手改.ioc或生成初始化。

## 1. 保留的资源与新增范围

保留HSE Bypass25MHz、PLLM25/N400/P2、HCLK200MHz，SAI2音频PLLI2S344/Q7/DivQ1、DMA2 Stream7 Channel0及IRQ4/0，USART1与IRQ5/0，ETH/RMII、I2C3 PH7/PH8、DHT11 PG7、PPS PA15/TIM2、FMC 38脚/PC3/CAS3/HCLK3/时序2/8/5/7/3/2/2和刷新1021。

MPU Region0、Region1（C0000000/8MB普通非缓存XN）、Region2（A0000000/4KB共享Device）保持原样；I/D Cache保持关闭。LTDC寄存器属于现有内部外设地址区，不新增MPU区域。

新增：LTDC RGB565的20根AF、8根未用颜色低位GPIO、2根显示控制GPIO，以及PLLSAI的LCD分支。与当前音频/温度/PPS/FMC引脚无交叉；CubeMX若自动选中表外的替代引脚，应按板上固定连线改回，不移动现有业务引脚。

## 2. CubeMX：LTDC模式和引脚

打开当前SBCG.ioc，在Pinout & Configuration的Multimedia → LTDC选择RGB565。按下表逐项核对，避免照搬RGB888的24根颜色AF。

### 2.1 二十根LTDC有效信号

| MCU引脚 | 功能 | AF |
| --- | --- | --- |
| PJ2 | LTDC_R3 | AF14 |
| PJ3 | LTDC_R4 | AF14 |
| PJ4 | LTDC_R5 | AF14 |
| PJ5 | LTDC_R6 | AF14 |
| PJ6 | LTDC_R7 | AF14 |
| PJ9 | LTDC_G2 | AF14 |
| PJ10 | LTDC_G3 | AF14 |
| PJ11 | LTDC_G4 | AF14 |
| PK0 | LTDC_G5 | AF14 |
| PK1 | LTDC_G6 | AF14 |
| PK2 | LTDC_G7 | AF14 |
| PJ15 | LTDC_B3 | AF14 |
| PG12 | LTDC_B4 | **AF9** |
| PK4 | LTDC_B5 | AF14 |
| PK5 | LTDC_B6 | AF14 |
| PK6 | LTDC_B7 | AF14 |
| PI10 | LTDC_HSYNC | AF14 |
| PI9 | LTDC_VSYNC | AF14 |
| PI14 | LTDC_CLK | AF14 |
| PK7 | LTDC_DE | AF14 |

以上统一Alternate Function Push Pull、No Pull、Very High。PG12的AF9是特例，不改为AF14。

### 2.2 颜色低位固定为0

下列引脚选GPIO_Output，Output Push Pull、No Pull、Low Speed，初始Output Level为Low：

| 引脚 | 板上低位信号 |
| --- | --- |
| PI15 | LCD_R0 |
| PJ0 | LCD_R1 |
| PJ1 | LCD_R2 |
| PJ7 | LCD_G0 |
| PJ8 | LCD_G1 |
| PE4 | LCD_B0 |
| PJ13 | LCD_B1 |
| PJ14 | LCD_B2 |

### 2.3 显示与背光

| 引脚 | User Label | 模式 | 初始输出 |
| --- | --- | --- | --- |
| PI12 | LCD_DISP | GPIO_Output、Push Pull、No Pull、Low Speed | Low |
| PK3 | LCD_BL_CTRL | GPIO_Output、Push Pull、No Pull、Low Speed | Low |

这两根线在首帧准备好且LTDC启动后由用户BSP运行时置高。当前不配置PI13触控中断、不更改I2C3，也不调用会重配硬件的第三方LCD整体初始化。

## 3. CubeMX：时钟

Clock Configuration中保持主PLL和PLLI2S不变，仅设置LTDC使用的PLLSAI分支：

| 参数 | 值 |
| --- | --- |
| PLL输入 | HSE25MHz / PLLM25 = 1MHz |
| PLLSAI N | 192 |
| PLLSAI R | 5 |
| PLLSAI DivR | 4 |
| LTDC/LCD pixel clock | **9.6MHz** |
| SYSCLK / HCLK | 200MHz，保持 |
| SAI2 clock source | PLLI2S，保持 |

PLLSAI P/Q的未用分支保持现有选择。不要把SAI2切到PLLSAI，不为显示把系统时钟改到216MHz。

## 4. CubeMX：面板时序和图层

### 4.1 LTDC General Settings

| 界面参数 | 填写值 |
| --- | --- |
| Horizontal Synchronization Width / HSYNC | 41 |
| Horizontal Back Porch | 13 |
| Active Width | 480 |
| Horizontal Front Porch | 32 |
| Vertical Synchronization Height / VSYNC | 10 |
| Vertical Back Porch | 2 |
| Active Height | 272 |
| Vertical Front Porch | 2 |
| HSYNC Polarity | Active Low |
| VSYNC Polarity | Active Low |
| Data Enable Polarity | Active Low |
| Pixel Clock Polarity | IPC / Input Pixel Clock |
| Background R/G/B | 0 / 0 / 0 |

UI填写的是宽度/porch；生成HAL会累计并减1，不能把下面生成值再填回宽度栏。生成后核对：HorizontalSync=40、VerticalSync=9、AccumulatedHBP=53、AccumulatedVBP=11、AccumulatedActiveW=533、AccumulatedActiveH=283、TotalWidth=565、TotalHeigh=285。对应总566×286像素时钟，约59.3Hz扫描；状态数值计划每秒更新一次。

### 4.2 仅启用Layer0

| 参数 | 值 |
| --- | --- |
| Layer0 | Enabled；Layer1保持Disabled |
| Pixel Format | RGB565 |
| Window X0 / X1 | 0 / 480 |
| Window Y0 / Y1 | 0 / 272 |
| Image Width / Height | 480 / 272 |
| Frame Buffer Start Address | **0xC0000000** |
| Constant Alpha / Default Alpha（Alpha0） | 255 / 0 |
| Blending Factor1 / Factor2 | CA / CA（第二项编码对应1-CA） |
| Layer Background R/G/B | 0 / 0 / 0 |

一帧480×272×2=261120字节，行距960字节。应用计划在同一个Layer0切换0xC0000000和0xC0040000；两个地址均32字节对齐、互不重叠，总范围止于0xC007FC00，位于现有8MB Region1内。CubeMX只设置首地址，不启用第二图层。帧缓冲由BSP映射，不能用内部SRAM上的大数组或启动时自动清零的外部BSS替代。

## 5. CubeMX：延后初始化和中断

进入Project Manager → Advanced Settings → Generated Function Calls，找到MX_LTDC_Init：

| 列 | 设置 |
| --- | --- |
| Generate Code | 保持启用，保留初始化函数和MSP |
| **Do Not Generate Function Call** | **勾选** |
| **Visibility (Static)** | **取消勾选** |
| Driver | HAL |

只调整MX_LTDC_Init这一行；MX_FMC_Init及所有现有初始化调用保留。取消static使用户BSP可以在页面准备好后调用CubeMX生成的全局函数；不手写另一套LTDC初始化。若函数按独立文件生成，同样保留声明并延后调用，不切换整个项目的文件生成模式。

NVIC Settings：LTDC global interrupt和LTDC global error interrupt（若单独显示）均不启用。原DMA2 Stream7/SAI2/USART1优先级保持。本轮不启用DMA2D、额外DMA/IRQ、CRC、RTOS、TouchGFX或触控。

**原因：** 当前main先完成所有MX初始化，再进入App_Init；SDRAM芯片上电命令在App_Init的BSP中。HAL_LTDC_ConfigLayer会立即提交图层，因而不能让CubeMX自动在App前开始读取C0000000。延后生成调用是CubeMX支持的扩展方式。

## 6. 保存并Generate Code后的检查关口

由用户保存和Generate Code，之后再接应用；本计划不授权手改.ioc/生成区。

- 新增hltdc、全局非static的MX_LTDC_Init、LTDC MSP及原厂HAL LTDC源。
- main的自动初始化调用列表中没有MX_LTDC_Init；函数定义本身仍在。
- 20根AF及10根GPIO与本页一致；PI12/PK3初始低。
- PLLSAI LCD9.6MHz；原主PLL/PLLI2S、SAI、TIM2、ETH、UART、DHT、FMC/MPU均不变。
- 只启用一个RGB565图层，窗口/时序/首地址正确，LTDC NVIC未开启。
- 运行既有MPU预检查；应用适配后再走原Debug/Release预设及布局检查。不把仅生成的旧App直接作为显示固件烧录，因为延后调用和SDRAM所有权交接尚需接入。

## 7. 生成后的应用接入计划

单缓冲直接写屏实现较少，但页面分片刷新会被扫描到；本轮推荐CPU双缓冲加垂直消隐换页。两块261120字节缓冲使用已验证的外部内存，不占音频SRAM，不依赖DMA2D或新IRQ。

### 7.1 SDRAM与显示启动顺序

U1保留为专用内存诊断：当前每次启动约50秒的全8MB破坏性测试不能与显示共用同一区域。显示构建将芯片上电READY与完整自检PASS分开表示；保留U1自检模式及测试，不把“显示模式未做完整测试”输出为本次自检PASS。

显示模式先发送既有上电命令并取得READY，之后该外部区域只归UI。CPU分片绘制第一张真实状态页，整帧完成且有效后，BSP才调用全局MX_LTDC_Init并打开LCD_DISP/背光。显示运行期间禁止重新启动整片破坏性测试；失败时保持背光关闭并报告，原串口/音频业务继续。

计划调用链：

```text
main → 原CubeMX初始化（含FMC，不自动调用LTDC）→ App_Init
  → SDRAM显示模式上电READY → 原业务初始化 → 状态服务初始化
main循环 → App_Process → 原音频归还 → UI快照/分片绘制
  → 首帧完整 → BSP调用生成MX_LTDC_Init → 打开显示/背光
后续 → 绘制后台缓冲 → 请求垂直消隐换页 → 轮询确认 → 归还旧前台缓冲
```

初始化错误不宣称页面成功；HAL生成函数的错误处理保留，不改生成区。BSP封装运行时开关、地址切换和状态轮询，不重设引脚/时序/时钟。

### 7.2 有界绘制与换页

每个主循环最多绘制8行，约1秒启动一轮新页面；DHT起始低电平期间延后UI工作。一次绘制使用同一份快照及固定格式化时刻，避免上下半页属于不同数据。发布前超过2秒则丢弃后台帧重画，不将超龄数值作为当前状态。

首帧用A缓冲，后续只写后台B；完成后通过HAL不重载地址接口及BSP运行时VBR请求在垂直消隐切换。前台轮询SRCR的VBR完成，100ms未完成则报告错误并停止新换页；确认前不改写任何正在扫描或待换入缓冲。HAL_LTDC_Reload会开启RR中断，本轮不用它引入新中断；具体API/位定义须在本工程新生成的HAL版本再次核对。

I/D Cache和MPU属性保持原样，无需新增Clean/Invalidate。CPU完成后台帧后以屏障保证写入完成，再交给LTDC。LTDC有自己的硬件取数主设备，不占用当前音频DMA2 Stream7；SAI回调继续只交接块，不绘图。

### 7.3 状态内容与数据来源

| 页面内容 | 数据来源/显示规则 |
| --- | --- |
| A/B角色 | BSP_BoardIdentity_Get，保留未知UID禁用业务策略 |
| 网络 | 新增EthernetMonitor只读快照，复用现有PHY轮询；不让UI另发MDIO/I2C事务 |
| 同步/偏差 | 新增TimeSync只读快照；A为REFERENCE，无B测量时偏差--；B只在新鲜有效估计时显示偏差 |
| 内部锁定 | INTERNAL LOCK明确属于内部状态，不改成外部±10us已通过 |
| 温度/声速/来源 | TemperatureService_GetSnapshot，区分AUTO/MANUAL、无效/过期显示-- |
| 音频 | BSP_Audio_GetSnapshot，显示RUN/错误/丢块；不新增PCM Begin/End消费者 |
| 候选数 | SoundDetectorService_GetSnapshot，只读累计，不消费候选队列 |
| 距离 | 预留m单位位置；尚无测距，显示--，不显示0或示例数字 |
| 方向 | 预留A SIDE/B SIDE/--；无同次双板声学结果时为--，不将A板角色当作声源方向 |
| 质量 | 先显示--，未实现相关度/质量结果时不填百分比 |

复用U0布局及字库，首版ASCII英文，A SIDE/B SIDE分别表示A侧/B侧。增加方向、质量占位及温度来源，距离单位改为m；中文字体和触控留给后续TouchGFX。当前没有标准声音分类或测距结果，不因显示页接入而更改这些功能状态。

### 7.4 计划修改的用户文件

| 文件 | 原因 |
| --- | --- |
| App/BSP/bsp_lcd.c/.h（新增） | 生成LTDC函数的延后调用、缓冲所有权、运行时换页和显示GPIO抽象 |
| App/UI/status_service.c/.h（新增） | 真实业务快照、分片绘制、1Hz调度与错误日志 |
| App/UI/status_view.c/.h | 温度来源、方向/质量占位、m单位及状态有效性 |
| App/UI/status_screen.c | 在现有页面布局接入新字段；不创建色条模式 |
| App/BSP/bsp_sdram.c/.h | 分离显示READY与U1自检模式，禁止破坏性测试与显示同时拥有区域 |
| App/Network/ethernet_monitor.c/.h | 为UI提供已有轮询的只读缓存 |
| App/Sync/time_sync.c/.h | 只读同步快照，不新增估计/校正动作 |
| App/app_main.c | 初始化和前台调度接入，保持每轮UART节流和DHT门控 |
| cmake/lcd.cmake（新增）/CMakeLists.txt | 用户源登记及生成参数/延后调用的预检查 |
| Tests/UI、Tests/Sdram及新增LCD BSP/服务测试目录 | 状态、资源、缓冲、超时、调度及原业务回归 |
| Tools/Monitor、Analysis（按验收需要） | 有时限采集、日志/快照一致性检查，不代替实屏观察 |
| Docs当前状态/协议/里程碑 | 记录实际版本、结果及限制，保留U1历史证据 |

生成文件由CubeMX负责，预期main.c/main.h、hal_msp.c、HAL配置、生成CMake及LTDC原厂驱动变化。现有PPS和许可删除继续保护，不混入显示提交。

## 8. 直接状态页验收

- [ ] 用户CubeMX完成上述配置和Generate Code，检查生成配置及保留资源。
- [ ] 先用主机失败用例验证：未READY不触碰帧缓冲/不调用LTDC；未画好首帧不点亮；扫描/待切换缓冲不能写；超时不归还错误缓冲；自检不能与UI同时拥有内存。
- [ ] 实现最小BSP/状态服务，验证8行上限、DHT门控、单份快照、未知/过期值、A参考/B未同步和温度手动/自动切换；旧音频/网络/时间单测保持通过。
- [ ] 原Debug/Release预设构建、ETH/Audio布局及独立复核通过后，在当前硬件授权范围烧录A，采集约90秒串口；不播放声音、不要求网线或示波器。
- [ ] 首次点亮就是状态页。自动核对显示READY/刷新计数/无换页错误、原音频连续且DROP/ERR/HAL/DMA=0、串口TX_ERRORS=0。无网线显示DOWN，传感器未接显示AUTO/--，A显示REFERENCE/偏差--，距离/方向/质量--。
- [ ] 通过已有串口命令TEMP SET 25.0、TEMP SET 30.0核对屏幕温度/声速及MANUAL来源随真实服务变化，之后TEMP AUTO恢复；不把手动设温当作传感器实测。
- [ ] 用户目视确认文字可读、位置/颜色合理、状态持续更新且无明显闪烁/撕裂；串口与寄存器不能单独证明面板正确。依根AGENTS.md第19节，取得该观察后才宣称显示功能完成并提交。
- [ ] 更新实际报告、按范围提交为jxq，停止本增量；不自动加入TouchGFX、波形、触控或测距。

若首次显示异常，按状态页数据、帧缓冲、LTDC寄存器、时钟/引脚顺序定位，不预先恢复独立色条流程；用户后续要求时再调整实验范围。

## 9. 已核对的主资料

- [ST RK043FN48H参数](https://github.com/STMicroelectronics/stm32-rk043fn48h/blob/main/rk043fn48h.h)：480×272和水平41/13/32、垂直10/2/2。
- [ST F746G-DISCO LCD BSP](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_lcd.c)：板上LTDC引脚、AF及显示/背光有效电平；不直接调用其整体初始化覆盖CubeMX。
- [STM32CubeMX 6.18.1手册](https://dev.st.com/stm32cube-docs/stm32cubemx/6.18.1/en/docs/markup/CubeMX_UserManual/chapters/04_4_stm32cubemx_user_interface.html)：Advanced Settings中保留函数、取消自动调用及取消static。
- [ST HAL LTDC](https://github.com/STMicroelectronics/stm32f7xx-hal-driver/blob/master/Src/stm32f7xx_hal_ltdc.c)：ConfigLayer立即重载；Reload会开启RR中断，具体以本工程生成版本复核。
- 当前仓库Core/Src/main.c、BSP SDRAM及U0 UI接口为应用接入真值；GUI_FOR_TEST仅作为板型和显示参数对照，不移植216MHz/ADC/TIM2/TouchGFX设置。
