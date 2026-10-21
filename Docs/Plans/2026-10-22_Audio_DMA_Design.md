# M3.1 单板 SAI2/DMA 连续音频采集方案

日期：2026-10-22。**方案已确认；用户随后指定手动操作CubeMX，当前等待用户配置并生成。
CubeMX硬件配置尚未变更，采集代码尚未实现。**
基线提交 `1f95af728de0d0adbca48cae5b1738318de8e4dd`，板端已验收
SBCG-M3.0-CODEC，WM8994 ID=8994；该结果不表示麦克风已可采集。

## 1. 唯一目标

当前单板使用板载 WM8994 数字麦克风路径，连续接收标称48kHz、16位、双通道PCM。
核对 DMA 半满/全满序列、缓冲完整性、实际帧速率及安静/发声时幅度变化，
同时复验现有自动温度、串口、UDP和本地PTP业务。只做采集，后续声音检测、
采样时间到PTP映射、双板同步及测距分别立项。

本增量必须先完成 CubeMX 配置并复查生成代码，才能实现依赖这些资源的应用层。
用户最新选择为自行通过CubeMX配置并生成，助手负责生成后的复查和应用层实现；
此前自动操作授权已被该选择替代，不由助手执行CubeMX配置/保存/生成。
不允许直接编辑`.ioc`文本。
新固件构建复核后另行确认烧录，当前M3.0仍留在板上运行。

## 2. 依据与选择

本机官方 STM32Cube_FW_F7_V1.17.4：

- `Drivers/BSP/STM32746G-Discovery/stm32746g_discovery_audio.c`：
  `BSP_AUDIO_IN_Init()`选择`INPUT_DEVICE_DIGITAL_MICROPHONE_2`；
  `BSP_AUDIO_IN_InitEx()`及`SAIx_In_Init()`使用A主接收产生时钟、B同步从接收，
  64位帧、32位有效FS、4个16位时隙，数字麦克风2激活时隙1和3。
- 同目录`stm32746g_discovery_audio.h`：PI4/5/6/7、PG10及
  DMA2 Stream7 Channel0 / 半字宽循环输入。
- `Drivers/BSP/Components/wm8994/wm8994.c`：数字麦克风2的ADC2/DMIC2路由、
  AIF1从模式、MCLK1时钟和48kHz名义配置；这与普通双时隙默认参数不同。
- `Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_sai.c`：
  MCKDIV计算、HAL_SAI_Receive_DMA的Size单位/回调与错误路径。
- CubeMX本机数据库中STM32F746NGHx的SAI2对应`sai1_v1_1_Cube`。

采用官方录音时序，通过项目自有BSP接入CubeMX句柄；不整体导入会重新配置GPIO、
PLL、DMA和NVIC的板级BSP。控制事务及寄存器配置只属于外部WM8994，
不替代CubeMX对MCU外设的配置。

名义采样率选48kHz，便于后续声波时间分辨率分析；本步仍不声明到达时刻精度。
16kHz占用更低，但单样本时间约62.5us；48kHz约20.83us，现阶段优先保留该分辨率。

## 3. CubeMX逐项配置

打开现有 `D:/electronic_design/SBCG/SBCG.ioc`，不新建工程。

### 3.1 SAI2模式、引脚

入口：**Pinout & Configuration → Multimedia → SAI2**。
选择能手工设置Frame/Slot的Free Protocol模式；界面若出现Basic/Predefined I2S，
不要让其固定为双时隙。以生成的HAL字段为最终核对依据。

| 项目 | Block A | Block B |
| --- | --- | --- |
| Audio Mode | Master Receive / SAI_MODEMASTER_RX | Slave Receive / SAI_MODESLAVE_RX |
| Synchronization | Asynchronous | Synchronous with Block A / SAI_SYNCHRONOUS |
| Protocol | Free Protocol | Free Protocol |
| Data Size / First Bit | 16 bits / MSB first | 同A |
| Clock Strobing | Rising Edge | Rising Edge |
| Output Drive | Enabled | Disabled |
| Master Divider | Enabled | 沿用官方Enabled；B使用内部同步时钟 |
| Audio Frequency | 48 kHz | 若可设置则48 kHz |
| FIFO Threshold | 1/4 | 1/4 |
| Mono/Stereo | Stereo | Stereo |
| Companding | None | None |
| Frame Length | 64 bits | 64 bits |
| Active Frame Length | 32 bits | 32 bits |
| FS Definition | Channel Identification | 同A |
| FS Polarity / Offset | Active Low / Before First Bit | 同A |
| First Bit Offset | 0 | 0 |
| Slot Size / Number | 16 bits（或Data Size=16）/ 4 | 同A |
| Active Slots | 只启用1、3，掩码0x0000000A | 同A |

SAI2_Block_A仅负责产生时钟，不启动A数据DMA，也不消费其接收数据；
这与官方录音路径一致。Block B才是业务音频接收句柄。

| MCU引脚 | SAI信号 | 建议逻辑标签 | 本次作用 |
| --- | --- | --- | --- |
| PI4 / AF10 | SAI2_MCLK_A | AUDIO_MCLK | WM8994 MCLK1 |
| PI5 / AF10 | SAI2_SCK_A | AUDIO_SCK | WM8994 BCLK1 |
| PI6 / AF10 | SAI2_SD_A | AUDIO_SD_A | 保留官方A数据脚，无A数据DMA |
| PI7 / AF10 | SAI2_FS_A | AUDIO_FS | WM8994 LRCLK1 |
| PG10 / AF10 | SAI2_SD_B | AUDIO_SD_B | WM8994 ADCDAT1输入 |

这些引脚当前未分配，本次新增用途不替换现有外设。
PH7/PH8仍为I2C3，PG7仍为DHT11；PD6和PH15继续保留，不启用其GPIO/EXTI中断。
本次板载麦克风不需要在蓝色扩展板接新模块。

### 3.2 SAI2时钟

入口：**Clock Configuration** 中SAI2时钟选择及PLLI2S分支。

| 设置 | 值 |
| --- | --- |
| SAI2 Clock Source | PLLI2S Q输出经DivQ |
| PLLI2SN / PLLI2SQ / PLLI2SDivQ | 344 / 7 / 1 |
| HSE / 共享PLLM | 现有25MHz / 25 |
| 主PLL / HCLK / PCLK1 / PCLK2 | 保持400÷2 / 200MHz / 50MHz / 100MHz |

保持PLLSAI原设置。新增音频不使用PLLSAI，也不改变ETH/PTP时间基准。
该PLLI2S组合来自官方48kHz组：SAI2内核约49.142857MHz；
主块自动计算MCKDIV预期为2，MCLK约12.285714MHz，帧率约47991.071Hz，
相对48000Hz约-186ppm。**这是按分频关系推算，不是实测。**
不得将字面量48000当成已经验证的精确采样率，后续输出名义值、分频推算值及窗口观测值。

### 3.3 DMA

入口：**SAI2 → DMA Settings → Add → SAI2_B（接收）**。

| 设置 | 值 |
| --- | --- |
| Controller / Stream / Channel | DMA2 / Stream7 / Channel0 |
| Direction | Peripheral to Memory |
| Mode / Priority | Circular / High |
| Peripheral Increment / Memory Increment | Disabled / Enabled |
| Peripheral / Memory Data Width | Half Word / Half Word |
| FIFO | Disabled |
| Burst（若可见） | Single / Single |

不要增加SAI2_A DMA或占用DMA2 Stream4。ETH使用专用DMA，不占此通用DMA流。
保存后以生成的DMA映射和`__HAL_LINKDMA(...hdmarx...)`复核，不默认接受其它Stream。

### 3.4 NVIC

入口：**SAI2 → NVIC Settings**，必要时到 **System Core → NVIC**。

| IRQ | Enabled | Preemption / Subpriority |
| --- | --- | --- |
| DMA2 Stream7 global interrupt | 是 | 4 / 0 |
| SAI2 global interrupt | 是 | 4 / 0 |

DMA中断报告半满、全满及DMA错误；SAI2中断处理B块采集错误。
保留USART1的5/0、SysTick现有配置及全局分组；不启用EXTI15_10或PD6相关IRQ。

### 3.5 Generate Code及复核

保留当前GCC/CMake工具链、目录结构和USER CODE。保存并执行Generate Code。
预计变更：`.ioc`（仅CubeMX写入）、`.mxproject`、`Core/Src/main.c`、
`Core/Src/stm32f7xx_hal_msp.c`、`Core/Src/stm32f7xx_it.c`、`Core/Inc/stm32f7xx_it.h`、
`Core/Inc/stm32f7xx_hal_conf.h`、`cmake/stm32cubemx/CMakeLists.txt`，以及按需复制的SAI HAL文件。
若当前生成选项把外设分文件，可能另有`Core/Inc/sai.h`、`Core/Src/sai.c`及dma文件；
不为此自行切换工程结构。生成后必须检查：

- DMA初始化在SAI初始化前，初始化完成后才调用App_Init。
- 两个SAI句柄、B的hdmarx、HAL模块与IRQ入口齐全，参数符合上表。
- 原HCLK、I2C3、DHT11、USART1、ETH/LwIP/PTP及USER CODE未被改变。
- 用现有Debug/Release构建检查生成快照；构建成功尚不证明采集通过。

## 4. 应用层方案（配置确认后实施）

### 缓冲与调度

- 单个64KiB环形缓冲：32768个16位DMA元素，按有效时隙交错组织，
  共16384个双通道帧；半缓冲16384个元素=8192帧，标称覆盖约170.667ms。
- 使用32字节对齐的独立NOLOAD节，计划位于SRAM1 `0x20010000..0x20020000`。
  用户拥有的链接扩展将普通BSS及newlib堆安排在其后，校验初始化数据、
  DMA、BSS、堆、4KiB主栈和ETH保留区互不重叠；不得在malloc可增长区中留一个未隔离的DMA洞。
- 当前基线`_ebss=_end=0x20004788`、主栈`0x20047000..0x20048000`，
  ETH/LwIP固定区不动。链接后以实际符号/节检查，不能仅靠总内存百分比判断重叠。
- I/D Cache保持当前关闭状态，不增加缓存维护。配置若有变化先重新评估，不能沿用本假设。
- 半满/全满回调只提交半区号、生成计数和观测时间，不打印、不算RMS、不读I2C。
  回调观测时间只用于流量/速率检查，**不是声音到达时刻**。
- 主循环校验半区当前未被DMA覆盖后计算有符号16位双通道的min/max/peak/RMS；
  结束再校验生成计数/当前DMA半区。覆盖或积压必须计入丢块，不发布混合数据。
  平方和用64位，-32768取绝对值不得溢出。DHT11启动低电平期间延后这些处理和日志。
- 本步较长半区窗口用于容纳现有串口/网络主循环负载；它是缓冲延迟，
  不代表后续声音检测或测距精度，也不冻结最终产品的DMA块长。
- 每秒最多一条`$AUDIO`统计：帧累计、半/满回调、有效/丢弃块、HAL/DMA错误，
  两通道幅度和观测帧速率。串口走既有日志服务，不加入原始PCM网络传输。

### 文件职责

| 计划文件 | 用途 |
| --- | --- |
| `App/BSP/bsp_audio_codec.c/.h` | 增加受检的WM8994控制事务/麦克风2配置；保留已验收ID接口 |
| `App/BSP/bsp_audio.c/.h` | 映射CubeMX SAI句柄、启动/停止、HAL回调与DMA所有权 |
| `App/Audio/audio_capture.c/.h` | 主循环调度、统计、连续性和错误输出 |
| `App/Audio/audio_statistics.c/.h` | 独立整数双通道幅度统计 |
| `App/app_main.c` | 初始化、调度与DHT11避让；保持原网络服务顺序 |
| `CMakeLists.txt`、用户链接扩展 | 注册自有源文件和受保护DMA节 |
| `Tests/Audio/`、`Tests/App/test_app_main.c` | 错误/所有权/统计边界及集成回归 |
| `Tools/Monitor/check_audio_layout.py`及其测试 | 最终ELF的DMA可用内存/范围/重叠验证 |
| `Docs/Milestones/M3_1_Audio_DMA.md`、Project_State/Hardware_Resources/Protocol | 生成配置、接口和真实验收记录 |

实际文件清单及接口在生成结果复查后写入实施前报告；不提前编写依赖未确认配置的采集逻辑。

## 5. 验收与提交边界

1. 先写可失败的主机测试：交错通道、全零、直流、INT16_MIN、满幅和RMS；
   半/满交替、迟处理覆盖、错误、错误句柄、tick回绕和DHT11避让。
2. 主机回归、Debug/Release构建、ETH及音频布局、独立复核和实际CubeMX再生成保留性检查。
3. 固定ELF及源码哈希，取得本版本烧录授权；使用原ST-LINK配置校验复位，不改选项字节。
4. 约90秒联合窗口：音频半满/全满持续交替、丢块/SAI/DMA错误0，
   窗口帧速率符合分频推算值的1%初始健全性范围（不是精密频率验收）；
   温度正常更新、UART错误0，原UDP窗口无缺口，两组请求各30成功，
   30组板端估算与真实C参考一致，仍LOCAL/SYNC=0。
5. 安静/受控发声两段幅度对照：采集就绪时通知用户轻拍或播放声音，检查两通道
   非恒零、非恒值且幅度随声音变化。仅连续DMA计数不足以证明接到了麦克风。
6. 实测通过后提交这一功能；没有实测则保留待验收，不创建“功能完成”提交。
   原始PCM留待有必要时另立采集/导出增量；本步不改变UDP协议、不做第二板或声音定位。

## 当前检查点

- [x] 已验收M3.0基线且工作区初始干净。
- [x] 官方录音模式/数字麦克风时隙、DMA、分频和当前内存边界已核对。
- [x] CubeMX命令接口帮助已只读检查，可使用其配置/保存/生成工作流。
- [x] 用户确认本方案，最新选择由用户手动操作CubeMX并生成。
- [ ] CubeMX生成结果复查，随后开展上述单一采集增量。
