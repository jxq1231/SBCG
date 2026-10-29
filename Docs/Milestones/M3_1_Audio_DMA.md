# M3.1 单板连续音频采集

<!-- doc-status: completed-evidence; reviewed: 2026-10-23 -->

> **已归档的增量验收记录。** 仅证明本页所列固件、窗口和测试范围；不是当前待办。后续进展以现行状态页为准，旧版本的未测项和失败不改写为已通过。 当前配置/任务见[项目状态](../Project_State.md)，阅读范围见[文档导航](../README.md)。

日期：2026-10-22。**M3.1单板连续采集、联合业务和受控声音幅度响应验收通过。**
当前固件SBCG-M3.1-AUDIO，Debug SHA前缀2548fb8a71fc；本报告与本增量源码/生成快照
一并提交。此前发声对照因环境限制延后，用户本轮已确认开始发声并完成补验。

## 唯一目标与资源

板载WM8994数字麦克风2 → SAI2 B同步接收 → 循环DMA → PCM16双通道统计。
不新增接线，保留DHT11的3.3V/PG7、网络、USART1及本地PTP；不做声音检测、
采样到PTP时间映射、双板同步、定位或声波距离验收。

用户手动完成并再生成的CubeMX配置：A主接收产生时钟，B内部同步从接收，
64位帧、32位有效FS、4个16位时隙、仅时隙1/3（A/B均0x0A）、上升沿。
PI4/5/6/7 AF10 Very High，PG10 AF10 High；PLLI2SN344/Q7/DivQ1，
主时钟200MHz不变。名义48000Hz，SAI源49142857Hz、MCKDIV=2，
分频推算47991.071Hz；此数不是实测值。

DMA2 Stream7 Channel0，外设→内存、半字、内存递增、循环、High、FIFO关闭。
32768个半字=64KiB；每半区16384元素=8192双通道帧，约170.7ms。
缓冲32字节对齐，独立NOLOAD SRAM1区域0x20010000..0x20020000。
BSS仍在低地址DTCM；自有链接扩展将_end/end设为0x20020000，原_sbrk堆可增长至
_sstack=0x20047000，4KiB主栈到0x20048000。LwIP/ETH区域不动。
当前I/D Cache关闭，不新增缓存启用或维护；若日后启用必须重新审查DMA一致性。

## 文件清单与所有权

自有固件：

- App/BSP/bsp_audio_codec.c、App/BSP/bsp_audio_codec.h：保留ID读取；按本机官方
  CubeF7 V1.17.4配置MIC2/48k/PCM16输入，24次受检I2C写及50ms VMID等待。
- App/BSP/bsp_audio.c、App/BSP/bsp_audio.h：SAI句柄映射、启停、半区领取/归还、
  HAL回调及所有权；不重新配置MCU硬件。
- App/Audio/audio_statistics.c、App/Audio/audio_statistics.h：signed PCM16双通道
  min/max/peak/RMS，64位能量和整数平方根。
- App/Audio/audio_capture.c、App/Audio/audio_capture.h：前台统计、每秒状态、
  回调速率窗口及无效幅度省略。
- App/app_main.c：初始化/调度、固件ID及DHT当前低电平避让。
- CMakeLists.txt、cmake/audio-layout.cmake、cmake/linker/stm32f746_audio.ld：
  自有源文件/宏及独立DMA内存，不改生成链接脚本。

自有检查：

- Tests/Audio/run_tests.py、Tests/Audio/stubs/main.h、Tests/Audio/test_bsp_audio_codec.c。
- Tests/Audio/test_codec_setup.c、Tests/Audio/codec_stubs/main.h。
- Tests/Audio/test_audio_statistics.c、Tests/Audio/test_bsp_audio.c、
  Tests/Audio/dma_stubs/main.h、Tests/Audio/test_audio_capture.c。
- Tests/App/test_app_main.c、Tests/App/run_tests.py：原业务次序、重新初始化、DHT避让。
- Tools/Monitor/audio_response_capture.py：有时限串口记录与操作者阶段标记；本次只保留安静数据，阶段标记不能替代真实发声。
- Tools/Monitor/check_audio_layout.py、Tools/Monitor/tests/test_audio_layout.py：实际ELF和
  9项负例/边界检查，覆盖音频及运行时堆与其它静态节的交叠。
- Analysis/audio_capture_analysis.py、Analysis/tests/test_audio_capture_analysis.py：
  回放初始化/连续计数/丢块/错误/速率/幅度证据，不据此自动声明受控声学响应。
- Docs/Plans/2026-10-22_Audio_DMA_Design.md、Docs/Project_State.md、
  Docs/Hardware_Resources.md、Docs/Protocol.md及本报告：配置、协议和验收边界。

用户CubeMX生成快照：.mxproject、SBCG.ioc、Core/Inc/stm32f7xx_hal_conf.h、
Core/Inc/stm32f7xx_it.h、Core/Src/main.c、Core/Src/stm32f7xx_hal_msp.c、
Core/Src/stm32f7xx_it.c、cmake/stm32cubemx/CMakeLists.txt，以及
Drivers/STM32F7xx_HAL_Driver/Inc/stm32f7xx_hal_sai.h、stm32f7xx_hal_sai_ex.h、
Src/stm32f7xx_hal_sai.c、stm32f7xx_hal_sai_ex.c。
助手没有手改这些生成文件，许可证删除已按原字节恢复。

## 调用、IRQ与所有权

main → CubeMX外设初始化 → App_Init → TemperatureService_Init →
AudioCapture_Init → BSP_Audio_Stop/Start → 开启已配置A时钟 →
WM8994输入配置 → HAL_SAI_Receive_DMA(B)。

main循环 → App_Process → 原网络/PTP与温度/命令服务 → AudioCapture_Process →
BeginBlock → AudioStatistics_Compute → EndBlock → 既有串口日志服务。
DMA2_Stream7_IRQHandler → HAL_DMA_IRQHandler → HAL_SAI_RxHalfCpltCallback /
HAL_SAI_RxCpltCallback；SAI2_IRQHandler → 两块HAL_SAI_IRQHandler → ErrorCallback。
DMA及SAI IRQ4/0，USART1保持5/0；回调只记半区/计数/tick/错误，无I2C、日志或算法。

Begin/End双重校验回调生成数、NDTR和待处理中断标志，防止覆盖中的数据发布。
积压/覆盖记DROP；错误或回调超过1秒不更新显式ERROR/STALLED；幅度过期时VALID=0并省略字段。
DHT启动拉低期间跳过统计和输出，DMA继续运行。回调tick只用于速率/连续性观察，
不是声音到达时间。完整文本定义见[Protocol](../Protocol.md)。

停止时除检查HAL返回值，还检查B的SAIEN、DMA EN、DMA READY；HAL abort超时
即便把SAI改为READY也不释放所有权、不清零重用缓冲。重新Start前同样确认DMA停止。
此错误路径由独立审查发现，已通过先失败后通过的真实HAL行为模拟测试，复核确认关闭。
布局审查提出的未来静态节占入运行时堆问题也已补负例和检查。

## 已执行的软件验证

- 使用原工作流：`python -B Tools/Monitor/autodebug.py build --preset Debug`及Release，均通过、无新增编译/链接警告。
- 10组原生模块回归（Audio/App/Console/Network/Udp/UdpProbe/Ptp/Sync/TimeExchange/Temperature）。
- Tools/Monitor/tests共83项、Analysis/tests共47项全部通过。
- Debug/Release各执行check_eth_layout.py及check_audio_layout.py，均通过。
- 共18组最终命令，证据Logs/Audio/M3_1/final-checks.json及对应输出。
- 独立只读审查完成；重要停止错误路径已修复并复核，布局建议已补齐。
- 用户再次Generate Code后，471个非文档文件逐个哈希一致；生成器删除的LAN8742
  许可证从同一已核对提交恢复。证据Logs/Audio/M3_1/regeneration-check/。
  此后仅在已有用户拥有的App/Tests/Tools/Analysis中补审查修复，没有新增生成区注入点。
- Debug的AudioCapture_Process自身栈640字节；新统计136字节、平方根64字节。
  4KiB保留覆盖前台/库函数/嵌套IRQ的余量仍须实机负载观察，不单靠单函数值声明栈安全。

Debug SHA256：2548fb8a71fcd5c4133dfd45c3c8e6f88c7b22191d99c3079e5cb5b51f212bd0。
Release SHA256：6c2b7cca42fd93e399e83e3720aaede94f01ef7ce62186263ee5d2663c8e0f2e。

## 联合实机结果与保留边界

已取得本版本明确授权，只烧录一次，原ST-LINK配置program/verify/reset通过。
首次约90秒采集发现DHT11 TIMEOUT，用户确认模块未连接或松动；接好后只复位当前固件，
没有重复编程。首轮未计为联合通过，原始记录保留于Logs/Audio/M3_1/first-run-environment.json。

复位后完整约90秒窗口，原始前缀`Logs/UDP/m31-audio-reset-1792620479889`：

| 项目 | 观察结果 |
| --- | --- |
| 音频初始化 | OK，24次WM8994写入成功，DERIVED_MILLI_HZ=47991071 |
| 音频连续采集 | 89条完整AUDIO均VALID=1、RUN；最后完整记录H=261/F=261、4276224帧、522块处理成功 |
| 音频错误/速率 | DROP/ERR/HAL/DMA均0，窗口观测47953～48018Hz；不是精密频率或声音到达时间精度验收 |
| 芯片控制接口 | 45次ID8994，错误0，读取0～1ms |
| 温度 | 17个有效版本，25.0～25.8℃，所有温度错误0，PRIME=1，ATT=18 |
| 原UDP心跳 | 70包SEQ6～75，缺口/重复/乱序/非法/外来均0，板tick相邻992～1008ms |
| 原请求/RX/TX | 30组成功且唯一关联，错误0 |
| 新时间交换 | 30组ACK/REPORT/四时刻及板端观测，与真实C参考逐条匹配 |
| UART/PTP | 心跳90条、PTP90条连续，TX_ERRORS=0，PTP仍LOCAL/SYNC=0、错误0 |
| 记录完整性 | BOOT唯一，无首部丢弃；二进制与JSONL逐字节对应；末尾AUDIO90半行单独保留并排除，不凑成第90条有效音频 |

### 心跳旧警戒线未通过：如实保留

相邻串口心跳1000～1042ms，均值1002.034ms；4→5为1042ms、14→15为1026ms、
15→16为1021ms，超过复制自M3.0的1020ms经验警戒。因此原严格分析仍为FAIL，
证据`Logs/Audio/M3_1/strict-joint-analysis.json`和.txt，不改成通过。

既有App心跳按前台now跳期、UART同步发送的实现未改；新增负载与延后相容，但没有各服务
耗时剖析，不能断言全部由音频统计、IRQ或串口中的某一项造成。
[T1.1报告](T1_1_Temperature.md)已将1020ms明确为当轮观察门槛，不是长期课程指标；
本次已确认音频方案要求心跳连续及错误0，没有20ms周期约束。独立复核确认不据此阻断本步
连续采集范围。另行生成`m31-requirements-acceptance.json`核对本步要求，同时带上述警戒失败
注记，不以放宽旧脚本获得全绿，也不声明精确心跳周期或两板同步达标。

### 首次受控声学对照暂缓（历史，已由后续sound-02补验）

`Tools/Monitor/audio_response_capture.py`已额外保存安静环境数据到
`Logs/Audio/M3_1/sound-01-*`，端口已关闭。
用户先选择开始，随后说明无法发声；因此该阶段标记不是声源事件证据，不计算安静/发声
增益，不把这段当作受控发声测试。状态`DEFERRED_USER_QUIET_ENVIRONMENT`，见
`Logs/Audio/M3_1/sound-test-deferred.json`。

**该阶段尚缺安静与受控发声两段响应对照（历史）。** 当前已经观察到两通道非恒零且有变化的PCM幅度，
仅证明安静环境的数据流与统计正常；不据此宣称声学灵敏度、信噪比、声音检测或测距通过。
该阶段完整功能验收尚未补齐，当时保留工作区、不作功能完成提交；下方sound-02现已补齐。
后续条件允许时可沿用已烧录的相同固件直接补采，无需再次Generate Code或重烧。
本次停止，不进入M4。

复验入口：原18组命令见Logs/Audio/M3_1/final-checks.json；最终ELF两种布局均通过。
生成及代码所有权见前文。受控采集工具可用`--help`查看端口、日志前缀及阶段标记参数；
使用新的日志/标记路径以保护原始证据。Flash=PASS，连续采集/联合业务=PASS（保留心跳警戒注记），
当时受控声学响应=DEFERRED、完整M3.1=AWAITING_ACOUSTIC_CHECK；最终状态见下节sound-02。


## 后续安静稳定性窗口（2026-10-22）

用户要求继续无需声音的步骤，已完成[5分钟安静稳定性复验](M3_1_Quiet_Stability.md)：
316条音频有效、300包UDP零缺口、温度新增63次、两组30请求及真实C匹配通过，
没有固件变更、复位或重烧。本轮心跳最大1015ms，前次1042ms记录保持原样。
本窗口只补充持续运行证据，当时受控声音响应仍待补验；最终结果见下方sound-02。


## 温度模式切换并行验收（2026-10-22）

[本轮联动报告](M3_1_Temperature_Coexistence.md)已通过：25℃保持17.256秒、零温/负温、
四种非法拒绝及恢复自动期间，96条音频有效、无丢块，80包UDP及两组30请求/真实C核对通过。
历史温度BUS=1/TIMEOUT=7在开窗前已存在，本轮未增加；当前已恢复AUTO并关闭采集端口。
该联动窗口无固件修改/复位/重烧；当时受控发声对照仍待补验。


## 受控声音响应补验sound-02（2026-10-22，最终通过）

用户明确回复“已开始发声”，沿用相同固件采集约134.636秒，端口已关闭；未改线、重烧或复位。
音源由用户执行，未独立记录具体设备、实际距离或声压，不据此作标定结论。
先记录安静基线，发声前将比较窗口和门槛写入sound-02-criteria.json：
安静取采集第5～20秒，发声取用户确认标记后第3～26秒，各至少10条；
两通道RMS中位数和峰值中位数均须达到安静的2倍以上，且非持续满幅。
该门槛只是本次响应健全性要求，不是任务书声音检测或灵敏度指标。

| 通道（有效时隙顺序） | 安静RMS中位数 | 发声RMS中位数 | RMS比 | 安静峰值中位数 | 发声峰值中位数 | 峰值比 |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 61 | 1857 | 30.44× | 358 | 12165 | 33.98× |
| 1 | 63 | 1811 | 28.75× | 369 | 9167 | 24.84× |

幅度单位均为PCM16计数，RMS/峰值范围0..32768，不预先声明实物左/右位置。
安静15条、发声23条；135条完整音频记录全部RUN/VALID=1，DROP/ERR/HAL/DMA=0，
窗口帧率47953～48018Hz。通道0有3/23条峰值达到32768；通道1无触顶，最大22364。
最大RMS分别10793/3396，不构成持续满幅。**保留短时峰值削顶事实，不宣称全动态范围无失真。**
后续算法/增益评估应避免过近/过响，不能将这一结果用作校准声压或信噪比。

温度保持正常更新，开窗前历史BUS=1、TIMEOUT=7均未增加，UART/PTP错误0。
原始串口169780字节与1848行JSONL重构完全一致；所有5份证据文件哈希一致。
程序、生成配置和Debug/Release ELF均与授权冻结快照一致。

原始证据：Logs/Audio/M3_1/sound-02-serial.bin/.jsonl、sound-02-run.json、
sound-02-marker.json、sound-02-criteria.json；汇总sound-02-acceptance.json。
早先sound-01属于用户无法发声时的未执行声学试验，仍保持DEFERRED，不覆盖历史记录。

本轮重跑18组completion检查（两配置构建、10组原生模块、83项工具/47项分析测试、
两配置ETH/音频布局）全部通过；记录completion-checks.json。独立只读完成审计复算原始
数据/阶段比值，复核DMA停止与内存布局，未发现阻断M3.1的P1/P2问题。

最终：Flash=PASS、单板连续采集=PASS、原业务联合回归=PASS、受控幅度响应=PASS。
此前1042ms心跳及旧经验guard失败、首次DHT未连接、历史温度错误及短时削顶均保留。
M3.1仅完成单板采集与幅度响应；不声明样本到PTP映射、精密采样时间、事件检测、
双板同步、距离/方向或课程整机指标通过。完成本增量提交后停止，不自动启动M3.2/M4。
