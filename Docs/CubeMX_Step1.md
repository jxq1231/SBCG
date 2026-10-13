# CubeMX第一步：系统时钟基线与RGB565显示约定

日期：2026-10-13。工程：`D:/electronic_design/SBCG/SBCG.ioc`。里程碑：M0.1——先建立两块板共用的系统配置，再按固定参数配置显示外设。

## 1. 当前工程已经确认的状态

| 项目 | 已读取的实际状态 |
| --- | --- |
| 器件/封装 | STM32F746NGH6，TFBGA216，型号已修正 |
| Flash | STM32F746xx_FLASH.ld为1024K，与开发板一致 |
| 软件 | CubeMX 6.18.1、CubeF7 V1.17.4、GCC/CMake |
| 当前系统时钟 | HSE25 MHz，HSE_ON；PLLM25、PLLN432、PLLP2；HCLK216 MHz |
| 当前调试 | SWD+SWO，PA13/PA14/PB3 |
| 当前外设 | 尚未启用FMC、LTDC、USART1、ETH、I2C3、TIM2；暂无DHT11/PPS标签 |
| 显示格式要求 | 用户指定RGB565；下文作为两板统一约定 |

型号已经正确，直接打开现有SBCG.ioc，不重新创建工程。以下设置由用户在CubeMX完成，本次没有手改.ioc或生成的硬件初始化代码。

## 1.1 配置前给队友确认的固定边界

- **TIM2只能有一个主人**：统一时间服务维护1 MHz计数、1秒周期，目标200 MHz HCLK时PSC=99、ARR=999999；其它模块取同步时间戳，不改分频、不各自初始化TIM2。服务统一处理1秒回绕。
- **两板必须用同一个温度**：一只DHT11接A板PG7，A通过UDP发布；同次测量锁定相同temperature_version/environment_temperature_c。失效保留上次共同有效值，串口手动设温也统一发布。
- **音频不要改PLLSAI**：LCD固定PLLSAIN192、R5、DivR4，在HSE25 MHz/PLLM25下9.6 MHz；SAI2时钟源选PLLI2S。采样率相关PLLI2S参数后续通过CubeMX确定。
- **PH15先预留**：本机V1.17.4 BSP把AUDIO_IN_INT定义为PH15；上传B-04图中PH15是TP3、PD6硬连WM8994 Audio_INT。两脚都保留，配SAI前核实真实IRQ事件路径；不把BSP宏直接当原理图连线。

这些是团队目标配置约定，当前工程仍未启用TIM2/SAI/LTDC。详细证据和接口边界见《Hardware_Resources.md》第3.1、4.7、5.6、5.7节。

## 2. 第一步现在执行：SYS、RCC与系统时钟

### 2.1 打开和确认工程

1. 用STM32CubeMX打开现有 `SBCG.ioc`。
2. 确认器件仍为STM32F746NGH6，Package为TFBGA216。
3. Project Manager中保持Project Name为SBCG、Project Location为现有目录、Toolchain为CMake；保持保留USER CODE区的选项。

### 2.2 SYS调试配置

在 **Pinout & Configuration → System Core → SYS**：

- Debug选择 **Serial Wire**，保留PA13/SWDIO和PA14/SWCLK。
- Timebase保持 **SysTick**。
- 当前SWO不是后续功能必需；改为Serial Wire后PB3会释放。PA15留给后续PPS，不选择完整JTAG。

### 2.3 RCC外部时钟

在 **System Core → RCC**，将HSE选择 **Bypass Clock Source**，输入频率按板载时钟填写 **25 MHz**。

依据用户上传UM1907 Rev4图29，MB1191 B-04的X2是有源25 MHz振荡器，经R30接PH0，同时给Ethernet PHY供参考时钟。因此本方案采用Bypass。实物若是其他修订，先与对应原理图核对。当前生成代码的HSE_ON是读取到的现状，并不是本指南已经修改好的结果。

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
| PLLQ | 可用/8，输出50 MHz；当前不使用需要48 MHz的USB/RNG/SDMMC域 |

算式：25 MHz / 25 × 400 / 2 = 200 MHz。电源电压按实际3.3 V；让CubeMX按200 MHz确定电压档位、必要的OverDrive及Flash等待周期，并检查时钟页面的错误提示。

ST-LINK VCP来自独立ST-LINK芯片，不需要主MCU的USB时钟。将来启用主MCU USB或其它48 MHz域时，要重新规划该域，不能把上面的50 MHz当48 MHz。

后续FMC使用HCLK/2得到100 MHz；不要把当前216 MHz直接/2当成100 MHz。若团队以后选择保留216 MHz，需要重新确定FMC分频和时序，不能照搬100 MHz的SDRAM表格。

### 2.5 保存、生成与检查

1. 点击Save，再点击Generate Code，保持工程路径和软件包版本一致。
2. 确认生成的 `SystemClock_Config()` 表达HSE Bypass、PLLM=25、PLLN=400、PLLP=/2和上述总线分频；只读取检查，不直接改该函数。
3. 使用已有VS Code任务 **STM32: Build**，或在SBCG根目录执行：

```text
cmake --build --preset Debug
```

4. 记录编译结果。只有烧录并观察启动正常后，才可认定新时钟在实物上工作；本次只检查了用户现有216 MHz基线的构建，没有测试未来200 MHz配置。

这一增量的预期变化是SBCG.ioc及系统时钟/调试相关生成文件；不新增业务调用链、IRQ或DMA。若工具自动调整相关生成元数据，应一并检查。

## 3. RGB565固定约定：下一阶段配置FMC/LTDC时采用

**RGB565表示帧缓冲颜色格式：R占5位、G占6位、B占5位，每像素16位、2字节，可表示65536种颜色。** 这是本项目选定格式，不再以ARGB8888作为默认图层。

DISCO的屏与MCU之间已焊接24根RGB数据线和4根时序线。RGB565内存格式由LTDC读取并输出到该屏，保留《Hardware_Resources.md》第4.1节的完整引脚分配。CubeMX的RGB输出引脚模式与图层Pixel Format是两个设置：本板保留完整RGB888物理输出，而**图层选RGB565**。

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
| Blending Factor 1/2 | PA×CA，对应BSP的PAxCA配置；RGB565没有像素Alpha，保持常量Alpha为255 |

生成代码中的关键核对字段：

```c
pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
pLayerCfg.FBStartAdress = 0xC0000000;
pLayerCfg.ImageWidth = 480;
pLayerCfg.ImageHeight = 272;
```

变量名可能不同；检查含义和值，不把示例粘到生成函数里。ST BSP的默认 `BSP_LCD_LayerDefaultInit()` 为ARGB8888；公开版本还提供 `BSP_LCD_LayerRgb565Init()`。如果采用BSP，必须确认团队V1.17.4版本中的API和初始化所有权，避免重新覆盖CubeMX图层配置。[ST官方LCD BSP](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_lcd.c)

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

## 4. 第一步完成检查

- [ ] MCU保持NGH6/TFBGA216，Flash保持1024K。
- [ ] SYS为Serial Wire，SysTick时基；PA15留给后续PPS。
- [ ] HSE25 MHz Bypass与实物时钟电路一致。
- [ ] HCLK200 MHz、PCLK1 50 MHz、PCLK2 100 MHz，生成代码与GUI一致。
- [ ] 工程从CubeMX保存/生成，现有Debug构建通过。
- [ ] 两板共用同一配置基线及软件包；记录实物启动检查结果。
- [ ] 队友已确认TIM2单一所有者、单DHT11 UDP温度快照及PLLSAI锁定；PH15和PD6均未分配给其他功能。
- [ ] 团队已统一后续图层RGB565、2字节像素、261120字节帧缓冲；完整LCD接线保留。

本指南完成的是第一步系统配置操作和后续RGB565约定。屏幕显示、温度、PPS、网络和声音采集仍按《Hardware_Resources.md》的顺序分增量配置和验证，不以编译通过代替硬件验收。
