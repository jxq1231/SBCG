# CubeMX第一步：系统时钟基线与RGB565显示约定

修订日期：2026-10-18。工程：`D:/electronic_design/SBCG/SBCG.ioc`。里程碑：M0.1——当前单板调试复位启动通过，继续补齐两板共用基线，再按独立目标配置后续外设。

## 1. 当前工程已经确认的状态

| 项目 | 已读取的实际状态 |
| --- | --- |
| 器件/封装 | STM32F746NGH6，TFBGA216，型号已修正 |
| Flash | STM32F746xx_FLASH.ld为1024K，与开发板一致 |
| 软件 | CubeMX 6.18.1、CubeF7 V1.17.4、GCC/CMake |
| 当前系统时钟 | HSE25 MHz，HSE Bypass；PLLM25、PLLN400、PLLP2；HCLK200 MHz；PCLK1 50 MHz、PCLK2 100 MHz |
| 当前调试 | 按老师推荐保留Trace Asynchronous Sw（SWD+SWO），PA13/PA14/PB3；时基SysTick，PA15未分配 |
| PLLQ与电源/Flash | 实际PLLQ=2，Q输出200 MHz；Scale1、OverDrive、FLASH_LATENCY_6；USB/RNG/SDMMC未启用 |
| 验证状态 | 用户此前烧录校验成功；2026-10-18只读确认Flash加载段与Debug ELF一致，当前单板调试复位启动及HAL时基通过；第二板和冷启动待验收 |
| 当前外设 | 尚未启用FMC、LTDC、USART1、ETH、I2C3、TIM2；暂无DHT11/PPS标签 |
| 显示格式要求 | 用户指定RGB565；下文作为两板统一约定 |

型号已经正确，直接打开现有SBCG.ioc，不重新创建工程。用户已通过CubeMX保存并生成本步时钟配置，快照已提交并合并；下文保留操作及复核流程。2026-10-18的单板实物结果见[启动报告](Milestones/M0_1_Startup.md)，本次未修改.ioc或生成初始化。

## 1.1 配置前四条已经固定的架构

- **全部RGB565**：LTDC引脚模式和图层/帧缓冲/DMA2D均565。有效16颜色+4时序采用板级连线，G3/PJ10；未用8低位颜色GPIO输出0。SBCG按本次用户约定固定H1/43/8、V10/12/4，PLLSAI192/R5/div4为9.6 MHz。2026-10-13读取的GUI历史快照中的PH14 G3、PH2 CKE和10 MHz不直接照搬；GUI当前状态见第3.1.1节。
- **时间从ETH1588来**：MAC硬件PTP为唯一时间源，A主/B从，统一时间服务通过SyncTime_GetNs()返回纳秒时间及锁定状态。TIM2只把内部PTP秒事件变成PA15的100 μs脉冲，不再拼接TIM2时间；1 MHz、PSC99、ARR0xFFFFFFFF、CCR100由该服务唯一管理。
- **测距全整数**：两板共用math_version=1及同一测量的temperature_version，温度用int32_t毫摄氏度；sound_speed_mm_s=331300+round_div_signed(606*environment_temperature_milli_c,1000)，单位mm/s。distance_mm=(sound_speed_mm_s*abs(delta_ns)+500000000)/1000000000；delta_ns为有符号t_B_ns-t_A_ns。取整、范围与安全求差按资源文档第5.9节，使用64位中间值，不除以2。
- **A/B按UID固定**：两板同固件、同BOARD_A_UID/BOARD_B_UID表，上电96位UID查表；A读一只DHT11并发布温度，B只接收。实际UID待配置前读取，不编造；传感器故障不换角色。

显示/音频继续分别使用PLLSAI/PLLI2S；PH15和PD6按已核实的软件/原理图差异共同预留。当前SBCG尚未实现上述服务，先按《Hardware_Resources.md》第4.1、5.2、5.5～5.9节共同审核，再配CubeMX。

## 1.2 这一步的范围和团队前置条件

**M0.1范围：Trace Asynchronous Sw、HSE Bypass 25 MHz、HCLK 200 MHz及相应总线分频；当前单板调试复位启动已通过，第二块板与共用基线仍待确认。** 第3节为后续显示约定；本轮不配置FMC、LTDC、低位颜色GPIO、DMA2D、ETH、TIM2、SAI或DHT11，不沿着资源文档一次启用全部外设。

| 项目 | 当前证据 | 配置前处理 |
| --- | --- | --- |
| 本机工程 | SBCG；.ioc记录CubeMX 6.18.1、CubeF7 V1.17.4、CMake | 本指南以这个现有工程为基线；Git提交范围仍只在SBCG |
| 队友另一工程 | 用户审查反馈为SBCJ、CubeMX 6.15、CubeIDE；本机未取得该工程 | “共用一份.ioc”仍是目标，尚未完成；先约定主工程、CubeMX/固件包版本和生成工具，不将另一工程生成文件直接覆盖过来 |
| 两板身份 | 当前板UIDw0/w1/w2已读：001A002D / 30365105 / 36363631；第二板与A/B尚未指定 | 分别记录真实96位UID，指定接DHT11的实体为A；未知UID不测量，不根据传感器故障换角色 |

UID按w0/w1/w2顺序记录，每字8位十六进制。可在现有调试流程查看 `HAL_GetUIDw0/w1/w2()` 的返回值；若这些未使用函数被链接器裁掉，按当前CMSIS头文件的UID_BASE只读查看三个32位字：**0x1FF0F420、0x1FF0F424、0x1FF0F428**。ST-LINK的USB序列号不能代替目标MCU UID。当前板实际读数已录入上表；不能据此猜测第二板或擅自指定A/B。

后续关卡：PTP到TIM2的路径由SBCG负责人先做单板验证，确认PTP时间已初始化/锁定后，PA15在每个PTP整秒产生100 μs脉冲，再同步经过验证的配置到队友工程。CubeMX不能表示内部映射时暂停该路径并重新评审，不手改.ioc、不恢复TIM2自由计满1秒。9.6 MHz显示需重新点亮并验证，不能沿用GUI项目10 MHz的稳定性结论。

## 2. 第一步现在执行：SYS、RCC与系统时钟

### 2.1 打开和确认工程

1. 用STM32CubeMX打开现有 `SBCG.ioc`。
2. 确认器件仍为STM32F746NGH6，Package为TFBGA216。
3. Project Manager中保持Project Name为SBCG、Project Location为现有目录、Toolchain为CMake；保持保留USER CODE区的选项。

### 2.2 SYS调试配置

在 **Pinout & Configuration → System Core → SYS**：

- Debug按老师推荐选择 **Trace Asynchronous Sw**，保留PA13/SWDIO、PA14/SWCLK和PB3/SWO。
- Timebase保持 **SysTick**。
- PB3保留给SWO，不分配其它功能；该模式不占用PA15，PA15留给后续PPS，不选择完整JTAG。此前释放PB3的Serial Wire建议已由此约定替代。
- 选中此模式不自动产生SWO日志；后续需单独配置程序输出和调试器接收。当前VS Code调试配置未启用SWO接收，不作为本步启动验收条件。

### 2.3 RCC外部时钟

在 **System Core → RCC**，将HSE选择 **Bypass Clock Source**，输入频率按板载时钟填写 **25 MHz**。

依据用户上传UM1907 Rev4图29，MB1191 B-04的X2是有源25 MHz振荡器，经R30接PH0，同时给Ethernet PHY供参考时钟。因此本方案采用Bypass。实物若是其他修订，先与对应原理图核对。当前生成代码为RCC_HSE_BYPASS，当前单板调试复位启动与RCC寄存器核对已通过；此前HSE_ON属于修改前的历史状态，PCB修订及另一板仍须独立确认。

### 2.4 Clock Configuration

采用与《Hardware_Resources.md》一致的 **200 MHz HCLK基线**，便于下一阶段得到100 MHz SDRAM时钟。填写并核对以下值：

| GUI项目 | 设置/目标值 |
| --- | --- |
| HSE输入 | 25 MHz |
| PLL Source | HSE |
| PLLM | 25 |
| PLLN | 400 |
| PLLP | /2 |
| SYSCLK Source | PLLCLK |
| SYSCLK | 200 MHz |
| AHB Prescaler / HCLK | /1 → 200 MHz |
| APB1 Prescaler / PCLK1 | /4 → 50 MHz |
| APB2 Prescaler / PCLK2 | /2 → 100 MHz |
| 常规定时器倍频路径 | APB1定时器100 MHz，APB2定时器200 MHz；以GUI显示为准 |
| PLLQ | 当前实际/2，Q输出200 MHz；USB/RNG/SDMMC未启用，本增量不依赖该输出 |

算式：25 MHz / 25 × 400 / 2 = 200 MHz。电源电压按实际3.3 V；让CubeMX按200 MHz确定电压档位、必要的OverDrive及Flash等待周期，并检查时钟页面的错误提示。

ST-LINK VCP来自独立ST-LINK芯片，不需要主MCU的USB时钟。此前建议的PLLQ=/8仅得到50 MHz，并非当前实际设置，也不是可用的USB FS 48 MHz时钟。将来启用USB/RNG/SDMMC前须按各自要求重新规划该时钟域；当前200 MHz Q输出不能直接用于这些外设。修改须通过CubeMX完成。

后续FMC计划使用当前HCLK200 MHz的/2得到100 MHz；FMC尚未启用，当前没有已运行的SDRAM时钟。若团队以后改回216 MHz，需要重新确定FMC分频和时序，不能照搬100 MHz的SDRAM表格。

### 2.5 保存、生成与检查

1. 点击Save，再点击Generate Code，保持工程路径和软件包版本一致。
2. 确认生成的 `SystemClock_Config()` 表达HSE Bypass、PLLM=25、PLLN=400、PLLP=/2和上述总线分频；只读取检查，不直接改该函数。
3. 使用已有VS Code任务 **STM32: Build**，或在SBCG根目录执行：

```text
cmake --build --preset Debug
```

4. 2026-10-16已对当前200 MHz生成代码执行上述构建，结果通过且无编译警告。2026-10-18复验Debug配置/构建通过，并确认当前板Flash加载段与该ELF一致；实物观察范围见第2.6节。

这一增量的预期变化是SBCG.ioc及系统时钟/调试相关生成文件；不新增业务调用链、IRQ或DMA。若工具自动调整相关生成元数据，应一并检查。

### 2.6 实物启动验收：当前单板调试复位启动通过

在用户执行或明确授权烧录/调试后逐板核对。当前板已完成烧录，2026-10-18使用共享OpenOCD配置只读比对Flash、临时硬件断点、复位/暂停和寄存器读取完成下列观察，没有重复下载固件。F5 launch会下载ELF，保留不写入固件的授权范围时不要用它代替只读连接。

1. 在SystemClock_Config()之后的MX_GPIO_Init()调用处设断点，确认时钟初始化正常返回。
2. 观察SystemCoreClock=200000000，并结合RCC寄存器核对HSE Bypass、PLL和总线分频；SystemCoreClock是软件计算值，不等于独立测频。
3. 继续运行后暂停，确认进入主循环；在两段运行之间检查uwTick持续增加。
4. 确认未进入Error_Handler()或异常处理函数；记录板卡、烧录结果和观察值。
5. 分别读取两板真实UID并固定A/B。另一板未验证时，不把本机结果推广为两板均通过。

当前板的实测结果：在MX_GPIO_Init入口0x08001064命中断点，随后进入0x08000F84主循环；SystemCoreClock=200000000，运行约1、2、5秒的分段tick为0→1002→3005→8006；再次复位后仍进入主循环。CFSR/HFSR均为0，RCC/电源/Flash等待周期与基线一致。具体ELF哈希、寄存器和限制见[单板启动报告](Milestones/M0_1_Startup.md)。第二板、冷启动和独立测频未执行。

当前调用链为main() → MPU_Config() → HAL_Init() → SystemClock_Config() → MX_GPIO_Init() → 空主循环。SysTick_Handler()仅维护HAL tick；没有新增外设IRQ、DMA关系或Cache策略变化。本轮只提交与当前固件对应的验收记录，不重写或重复提交生成代码，不将单板结果扩展为两板通过。

## 3. RGB565固定约定：下一阶段配置FMC/LTDC时采用

**RGB565表示帧缓冲颜色格式：R占5位、G占6位、B占5位，每像素16位、2字节，可表示65536种颜色。** 这是本项目选定格式，不再以ARGB8888作为默认图层。

**CubeMX也选择RGB565引脚模式**，图层Pixel Format、帧缓冲和DMA2D输入/输出全部565。使用R3～R7、G2～G7、B3～B7及4个时序信号，共20个AF；G3按板级连线选择PJ10。屏另8个已焊接低位颜色线由原MCU脚推挽恒低，不浮空、不新增电路。硬件连线仍是24颜色+4时序，但不配置28个RGB888 AF。

### 3.1 LTDC图层设置

在下一阶段启用LTDC后，于 **LTDC → Parameter Settings → Layer Settings**（按版本界面命名）设置：

| 图层参数 | 统一值 |
| --- | --- |
| 启用图层 | 只启用一层；第二图层暂不启用 |
| Pixel Format | **RGB565** |
| Frame Buffer Start Address | **0xC0000000**，来自SDRAM Bank1 |
| Window X0 / X1 | 0 / 480 |
| Window Y0 / Y1 | 0 / 272 |
| Image Width / Height | 480 / 272 |
| Constant Alpha | 255 |
| Default Alpha / Alpha0 | 0 |
| 背景R/G/B | 0 / 0 / 0 |
| Blending Factor 1/2 | CA/CA，常量Alpha255；两板同一设置 |

生成代码中的关键核对字段：

```c
pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
pLayerCfg.FBStartAdress = 0xC0000000;
pLayerCfg.ImageWidth = 480;
pLayerCfg.ImageHeight = 272;
```

变量名可能不同；检查含义和值，不把示例粘到生成函数里。ST BSP的默认 `BSP_LCD_LayerDefaultInit()` 为ARGB8888；公开版本还提供 `BSP_LCD_LayerRgb565Init()`。如果采用BSP，必须确认团队V1.17.4版本中的API和初始化所有权，避免重新覆盖CubeMX图层配置。[ST官方LCD BSP](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_lcd.c)

### 3.1.1 同一套引脚与时序

有效RGB565脚和8个恒低GPIO按《Hardware_Resources.md》第4.1节。2026-10-13读取的GUI历史快照采用H1/43/8、V10/12/4，SBCG按本次用户约定保留这组时序；像素时钟9.6 MHz，迁移后须复测。CubeMX实际长度与生成累计字段统一如下：

| 参数 | GUI设置/生成字段 |
| --- | --- |
| HSYNC/HBP/HFP | 1 / 43 / 8 |
| VSYNC/VBP/VFP | 10 / 12 / 4 |
| HorizontalSync / VerticalSync | 0 / 9 |
| AccumulatedHBP / VBP | 43 / 21 |
| AccumulatedActiveW / H | 523 / 293 |
| TotalWidth / TotalHeigh | 531 / 297 |
| DMA2D | 输出DMA2D_OUTPUT_RGB565；输入图像层选择RGB565 |

**2026-10-16重新核对GUI的现状已不同**：H41/13/32、V10/2/2，PLLSAI192/R5/div4为9.6 MHz，G3/PJ10、CKE/PC3、帧缓冲0xC0000000。不能把这份最新GUI说成与SBCG目标时序相同；也不能仅因文件已改就宣称实物复测通过。本轮不改显示目标，后续显示里程碑再共同审核版本并验证。

低位GPIO为PI15/R0、PJ0/R1、PJ1/R2、PJ7/G0、PJ8/G1、PE4/B0、PJ13/B1、PJ14/B2，推挽/初始Low/No Pull/Low Speed。避免BSP默认初始化把它们重新改成RGB888 AF。SDK内部控制参数的编码由单一BSP包装处理，业务图像和颜色始终16位565。

### 3.2 帧缓冲布局

| 项目 | RGB565值 |
| --- | --- |
| 每像素 | 2字节，原始内存像素使用uint16_t |
| 每行 | 480×2 = 960字节，无额外填充 |
| 每帧 | 480×272×2 = **261120字节** |
| 第一缓冲 | 0xC0000000起；有效像素到0xC003FBFF |
| 可选第二缓冲 | 0xC0040000起；有效像素到0xC007FBFF |
| 双缓冲有效像素总量 | 522240字节；按每帧256 KiB间距预留时总预留512 KiB |

**前置条件：FMC/SDRAM配置、上电命令序列、刷新计数、读写测试及内存/Cache策略完成后，才让LTDC访问0xC0000000。** 第一阶段系统时钟生成后不能直接往该地址写色值；当前工程尚未初始化SDRAM。后续再打开LCD_DISP和LCD_BL_CTRL，测试纯色画面。

当前 `MPU_Config()` 的4 GiB No Access默认区域、子区域掩码0x87仍覆盖0xC0000000所在区间；后续还要通过CubeMX配置允许CPU访问SDRAM的MPU覆盖区域，并确定LTDC/DMA共享内存的Cache策略。不能只完成FMC时序就认为CPU可以安全写帧缓冲。

### 3.3 原始RGB565颜色

| 颜色 | RGB565数值 |
| --- | --- |
| 黑 | 0x0000 |
| 白 | 0xFFFF |
| 红 | 0xF800 |
| 绿 | 0x07E0 |
| 蓝 | 0x001F |
| 黄 | 0xFFE0 |
| 青 | 0x07FF |
| 品红 | 0xF81F |

原始RGB888到RGB565的转换约定为：

```c
rgb565 = ((r8 & 0xF8u) << 8)
       | ((g8 & 0xFCu) << 3)
       |  (b8 >> 3);
```

r8/g8/b8范围0～255，结果存入uint16_t。按 `y*480+x` 定位像素，地址按每像素2字节前进。不要把0xFFFF0000等32位ARGB颜色直接当RGB565写入内存，也不要用uint32_t像素步长。

BSP/DMA2D函数的颜色参数可能是RGB888/ARGB8888，再由内部转换输出RGB565；直接写像素、填充、文本、位图并不一定采用相同参数约定。后续实现时分别核对所用API，不能因图层为RGB565就把所有BSP接口一律传16位颜色。

### 3.4 下一阶段加入音频时的时钟核对

在Clock Configuration中将SAI2时钟源选择为PLLI2S，按实际采样率配置PLLI2SN/Q/DivQ。保持LCD支路N192/R5/DivR4和共享PLLM25不变；生成后分别核对 `RCC_SAI2CLKSOURCE_PLLI2S` 与LTDC的PLLSAI参数，再实测LCD刷新和音频采样率。使用BSP时还需核对其时钟回调，不能让BSP与CubeMX分别改同一RCC配置。

### 3.5 后续音频DMA预留与温度就绪条件

已核查本机CubeF7 V1.17.4 BSP：录音SAI2_Block_B采用 **DMA2 Stream7 Channel 0**，放音SAI2_Block_A采用 **DMA2 Stream4 Channel 3**。这两条先预留给音频，后续在CubeMX人工核对，不能把自动分配直接当作团队配置；仅录音时不因此启用放音DMA。缓冲、采样率、循环模式、Cache和中断优先级在音频里程碑再落实。

ETH使用自己的专用DMA，不占用上述DMA2 Stream；音频需要避免与其他通用DMA2外设复用同一Stream，并核对与ETH/LTDC共享的总线、存储器带宽、Cache和中断负载。详见资源文档第4.8节及DS10916第2.31节。

首次没有已共享并确认的有效温度时，`temperature_ready=false`，显示“温度未就绪”，不生成距离；**不设默认25°C，也不把失败当0°C**。之后传感器失效可保留最后共同有效值并标记HOLD_LAST。有效的串口手动设温经A确认、共享后，同样可使温度就绪。

## 4. 第一步完成检查

- [ ] 已确认实际操作工程及版本；两板真实UID、A/B指定已有记录；未把SBCJ迁移或共享.ioc标为已完成。
- [x] MCU保持NGH6/TFBGA216，Flash保持1024K。
- [x] SYS为Trace Asynchronous Sw，PA13/PA14/PB3保留，SysTick时基；PA15未分配，留给后续PPS。
- [ ] HSE25 MHz Bypass与实物时钟电路一致。
- [x] .ioc与生成代码表达HSE25 MHz Bypass、HCLK200 MHz、PCLK1 50 MHz、PCLK2 100 MHz；GUI时钟截图对应总线频率一致。
- [x] 工程从CubeMX保存/生成，现有Debug构建通过且无编译警告；实际PLLQ=/2已记录，不用于USB/RNG/SDMMC。
- [x] 本增量只涉及SYS/RCC时钟与相应生成文件；未启用屏幕、网口、TIM2或音频DMA。
- [x] 当前连接板已烧录并完成第2.6节调试复位启动检查，SystemCoreClock、uwTick及异常状态已归档；不代表冷启动或第二板通过。
- [ ] 两板共用同一配置基线及软件包；记录实物启动检查结果。
- [ ] 队友已确认ETH/PTP唯一时间、TIM2只做PPS、同一整数测距公式及UID角色表；单DHT11 UDP温度快照、PLLSAI锁定不变，PH15/PD6预留。
- [ ] 团队已统一LTDC模式/图层/帧缓冲/DMA2D全部RGB565、20有效AF+8恒低GPIO和同一组时序；2字节像素、261120字节帧缓冲。

本指南完成的是第一步系统配置操作和后续RGB565约定。屏幕显示、温度、PPS、网络和声音采集仍按《Hardware_Resources.md》的顺序分增量配置和验证，不以编译通过代替硬件验收。
