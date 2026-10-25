# M4.2.2 有界扫频接入：初版、R1和R2静默验收记录

<!-- doc-status: diagnostic-observation-verified-performance-open; reviewed: 2026-10-25 -->

## 当前结论

**M4.2-DIAG观测功能已实测，性能问题仍开放。** 当前A板为`SBCG-M4.2-DIAG`，Debug前缀d3e084b1d83e。初版/R1/R2三次静默性能验收失败保留；本次定位采集证明扫描段最大59ms、评分段最大5ms、34次搜索丢弃由21次单任务超时及13次历史覆盖组成，208次串口聚合错误全部为日志队列满。音频累计零丢块/硬件错误，无额外BOOT或CHIRP误报。下一步优先优化逐样本能量扫描及其对日志排空的影响，没有新的待烧录修复镜像。诊断完成不表示搜索负载、标准音频识别或到达精度已通过。

## 当前增量：M4.2-DIAG观测功能已实机验证

用户在R2失败后要求继续，当前只有一个可测目标：区分搜索/日志积压的实际来源。此前三次失败证明原电脑快调度假设不能替代实物预算，本增量不再盲目改变优化选项、搜索算法、门限、队列容量或1500ms期限。

- `App/Detector/chirp_service.c/.h`：五类互斥DROP、完整完成DONE、扫描/评分最大HAL毫秒差及Process最大调用间隔。原8窗/257窗搜索、独立输入缓存、End所有权不变。
- `App/app_main.c`：固件标识SBCG-M4.2-DIAG，心跳增加LOG_FULL/WRITE/BAD/Q/HWM。队列和每轮排空一行策略不变；保留既有PPS改动，不混入当前提交。
- `Tests/ChirpService/test_service.c`：测试专用日志副本和可控测量时钟。
- `Tests/ChirpService/test_diagnostics.c`、`test_app_diagnostics.c`、`run_diagnostics_tests.py`：诊断原因、计时回绕、节流及真实App日志出口测试。
- `Tests/ChirpService/run_tests.py`：完整服务回放后执行诊断测试，不访问硬件。
- 对应实施计划、项目状态、入口、音频边界与协议文档同步更新。

调用链仍为App_Process→AudioCapture_Process→ChirpService_Process→Scan/Search，诊断只读取前台状态；日志仍SendLine→原队列→FlushOneLine→BSP_Console_Write。无新IRQ、DMA、缓存或CubeMX变更。当前I/D Cache仍关闭，原SAI2接收/循环DMA缓冲及网络内存布局不变。

新增CHIRP_DIAG与CHIRP_STATE共享原1000ms节流，交替输出，总体约1Hz、每次最多一行。SCAN_MS包含Scan调用，SCORE_MS包含Search调用（含任务缓存/清理等前台开销），不是纯点积计时；GAP_MS是相邻有效进入Process之间的最大HAL毫秒差，不是DMA中断间隔。新的计时调用及较长心跳带来小量观测开销，不能据此承诺积压已改善。

TIMEOUT只统计Search中单任务期限拒绝；HISTORY是所需主副本输入已丢失；FULL为队列满拒绝；RANGE为帧范围/缓存长度非法；RESET为失败End、会话/流等整体失效时清队列。按实际分支优先级只选一个主原因；计数均uint32饱和，未饱和时五类之和等于DROP。DONE包括完整匹配及完整未匹配；缓存或排队不等于完成。整体副本过期造成的清理属于RESET，不能把所有时间相关清理都读成TIMEOUT。

LOG_FULL、LOG_WRITE、LOG_BAD分别为日志队列满、BSP发送失败、非法长度次数，与原TX_ERRORS同路增加；均采用原有uint32自然回绕语义。LOG_Q是格式化心跳时的待发行数，尚不包含该心跳；LOG_HWM为本次成功初始化以来最高队列深度。新心跳仅CHIRP构建启用，旧版本格式保留；诊断构建使用256字节局部格式缓冲，满宽心跳163字节、CHIRP_DIAG174字节，均小于255字节上限。

测试先在旧实现实际断言失败：`diagnostics-utg1wnj5/service-O0.txt`缺少CHIRP_DIAG，App缺少LOG_FULL等字段；随后实现。O0/O2的服务、统一排队、直接发送六组诊断全通过，覆盖五种原因、DONE、3ms分段/6ms整体、计时与调用间隔回绕、16/32行队列、DHT门控、深度/峰值、发送失败和重初始化清零。完整ChirpService快慢调度10/10、App、Audio、SoundEventTime回归通过；两配置构建及音频/ETH布局全过。证据 `Logs/Detector/M4_2_2/DIAG/`，独立复核另跑 `diagnostics-bb07muyy/`，无阻塞项。

| DIAG构建 | Debug | Release |
| --- | --- | --- |
| Flash字节 | 274568 | 184188 |
| BSS结束 / 最小堆栈段结束 | 0x2000E9CC / 0x2000EFD0 | 0x2000E9CC / 0x2000EFD0 |
| 到音频DMA起点静态间隔 | 4144字节 | 4144字节 |

原音频0x20010000..0x20020000、newlib堆起点0x20020000和MSP0x20047000..0x20048000不变；静态间隔不是运行栈水位。仅保留原RWX LOAD链接警告。

- DIAG Debug SHA256：`d3e084b1d83e75ec0c23b4c3a6e5917cbf52f6b8adfdd9635d94315ff7806676`。
- DIAG Release SHA256：`aade0fd3734963a13b968375d1dfd5c36ff5720899b4918b73a8c5f4cc70b3bf`。
- 冻结Debug：`Logs/Firmware/M4_2_2_DIAG/d3e084b1d83e/Debug/SBCG.elf`；清单 `Logs/Detector/M4_2_2/DIAG/build-manifest.json`。

DIAG已获用户对本镜像的明确授权，并仅A沿原配置完成烧录、校验与复位；本版不改选项字节、不主动播放声音。原R2批准未被复用，新授权及结果按本次记录。冻结镜像基于包含原用户PPS/生成文件改动的工作树，功能提交不混入这些既有改动，不能将纯提交检出构建冒充本次相同镜像。

## DIAG实物定位结果及证据边界

采集COM3共70.183867秒，120369字节、1205个完整行，BOOT后完整行窗口61.355118秒；精确FW/UID相符，新BOOT一次。OpenOCD退出0、Verified OK、复位成功，耗时8.694232秒；冻结Debug散列与批准值一致。

| 本次观测 | 最后已收到的记录/窗口最大值 |
| --- | --- |
| CHIRP_DIAG | 29条；SCAN_MS59、SCORE_MS5、GAP_MS172 |
| 分类丢弃与完成 | TIMEOUT21、HISTORY13、FULL/RANGE/RESET均0、DONE0 |
| CHIRP_STATE | 29条；末CAND34、DROP34、MATCH0、BAD/SKIP/ERR均0、EVAL3704、PENDING0 |
| HEARTBEAT | 58条；末TX_ERRORS208、LOG_FULL208、LOG_WRITE0、LOG_BAD0、LOG_Q20、LOG_HWM32 |
| AUDIO | 51条；末OK342、DROP/ERR/HAL/DMA均0、VALID1、RATE_HZ48000 |
| UI | 35条；末RUNNING、FRAMES858、ERRORS0、UPDATES58；未重新目视验屏 |
| 目标判定 | **PASS_DIAGNOSTIC_OBSERVATION_SCOPE**；性能仍未修复 |

扫描段是当前明确的主要耗时来源；这些最大值不一定来自同一次调用，不能简单相加当作严格最坏时限。评分段包含Search调度与缓存，而非纯点积。相邻Process最大172ms表明前台可发生较长间隔。21次超时和13次覆盖是诊断所报告的实际分支；日志错误在每条已收到心跳中均满足TX_ERRORS=LOG_FULL+LOG_WRITE+LOG_BAD，本窗均为队列满，没有串口写失败证据。DONE0说明本窗口尚没有完整搜索完成，不以MATCH0宣布分类质量通过。

没有主动播放，但存在自然环境能量候选；没有网络或DHT验收。AUDIO和其他日志仍有缺口，累计硬件计数零不等于日志连续性通过。诊断与状态行交替约1Hz按主机已验证策略运行，缺行风险仍在，不能声称捕获了所有周期状态。

**采集结束边界明确保留：** 70秒截止时最后一条SOUND_TIME只收到111字节，原raw没有末尾换行，严格完整文件验证首先拒绝。采集器将该残片按`incomplete_tail_hex`完整保存在JSONL；逐字节核对相等后，仅从分析输入排除该尾片，原始文件不改写。其余1205个完整行raw/JSON一致、严格ASCII，新BOOT后覆盖61.355118秒；本次只使用完整行定位，不以此宣称原文件尾部完整。

- 原始完整raw SHA256：`9cea579f4d7127b15b303026fc72e8b27acc1ce23903e49dcf571d0c8457ffe4`。
- 排除111字节尾片后的完整行前缀SHA256：`f024c05bf33a7e1a19a803bde759c2e80b5ee97e38986bc0677c72b580b9a002`。
- 完整行规范化SHA256：`f60e534f1e47984298745639ade1014d447cd92e44eeba35711536a62a04dd97`。
- 文件：`Logs/Chirp/quiet-diag-01-serial.bin`、`-serial.jsonl`、`-run.json`、`-flash.txt/.json`、`-summary.json`；摘要保留尾片hex、排除字节数及所有判定，不提交原始日志。

下一软件目标：降低能量扫描的逐样本通用接口开销，再测日志队列是否仍供过于求。不能直接复用原SOUND候选替代扫频候选：原服务开/关阈值256/128，扫频门控128/64，直接替换会改变弱信号资格。应评估按64帧窗批量推进/共享统计且分别保留状态和阈值的方式，以原逐帧纯C实现作对照，保持完整搜索、End验证、失效清理和候选坐标。本轮不继续实施该下一增量。

## 初版实物失败证据与R1根因修复

用户授权的初版Debug前缀ef79f6e76870已沿用现有OpenOCD配置，仅选择A板ST-LINK `066EFF3832394D3457253156` 完成烧录、校验和复位；未改选项字节、接线或播放声音。COM3采集从烧录前开始，总长70.193435秒，BOOT后61.367295秒。raw/JSON逐行一致，仅排除5行旧输出；新BOOT只有一次，UID为001A002D:30365105:36363631。

- 心跳、音频、CHIRP_STATE各59条。末AUDIO为RUN/VALID=1，OK=353、DROP=0、ERR=0、HAL=0、DMA=0。
- 末CHIRP_STATE：OK=353、BAD=0、CAND=26、MATCH=0、DROP=26、SKIP=305、ERR=0、EVAL=392；COPY_MS=4、WORK_MS=30、S_MAX=512、E_MAX=8。
- 没有CHIRP匹配行；末UI诊断RUNNING/FRAMES=1754/ERRORS=0/UPDATES=60，本次未重新目视验屏。无网线及DHT，NET=DOWN/TEMP_VALID=0符合环境。
- 判定 **FAIL_CHIRP_BACKLOG**，不是功能验收通过。没有额外BOOT不表示历史未知重启原因已解决。

原始数据及完整性判定：`Logs/Chirp/quiet-01-serial.bin`、`quiet-01-serial.jsonl`、`quiet-01-summary.json`，同前缀保留烧录和运行记录。raw SHA256为 `1d5f3c19abdf5cb90c30ad4f8ae79aae003e1f747abc6b2837b2d4ec77cf1a9d`，BOOT后规范化SHA256为 `14fa9dd363c18cd41665e594ddaf6e3b02af304e317c3461095ba0a6e840d404`。

根因：512帧/轮至少需要16次调用才能扫完8192帧块，约要求94次调用/秒；实际混合业务不能满足。原电脑回放假定每块64次调用，未覆盖慢调度。未完成搜索依赖会被新块替换的主副本，且Debug未优化的8窗评分最大30ms，进一步增加负担。没有证据将问题单独归因于TouchGFX阻塞。

先新增每块5次调用的回放，旧实现以matches==10U断言失败，见 `Logs/Detector/M4_2_2/native-xkppogz1/capture-O0-5.txt`。R1改为一次扫描当前8192帧；评分仍至多8窗；队首首次评分前完整缓存2656点，跨后续块继续评分；任务/副本时限1500ms。仅chirp_window.c在Debug启用O2、无fast-math，PCM16乘法先32位后64位累计。以下数据生命周期说明适用于R1/R2；历史构建表与R2表分别列出，失败数据不覆盖。

## R1实物失败与R2修复

R1镜像94a2be871a98已获明确授权，仅A板烧录、校验和复位成功。静默采集70.188213秒，BOOT后61.159862秒；新BOOT一次，无主动播放。raw/JSON一致，排除8行旧输出。当前这次验收判定 **FAIL_AUDIO_DROPS_AND_LOG_OVERFLOW**。

- 38条音频行，末条SEQ48、OK284、DROP40、ERR/HAL/DMA均0；存在VALID=0及日志序号缺口，不能通过连续性检查。
- 收到49条心跳，末条SEQ52/TX_ERRORS870，表示应用串口发送/队列错误，不是UDP网络丢包计数。
- 只有2条CHIRP_STATE：末条OK10/BAD1/SKIP0/EVAL0/WORK_MS79；该行不代表整个窗口计数。没有CHIRP匹配，无法用稀疏日志宣布全程零积压。
- UI末FRAMES329/ERRORS0/UPDATES53，仍运行；未重新目视验屏。

证据：`Logs/Chirp/quiet-02-*`，摘要 `quiet-02-summary.json`。raw SHA256 `819a1184958a20aa66d894dd7ab5fd7b0a9873ea44b57e6e36ec583c193c4459`；规范化SHA256 `63433d15392a7a46547c1a786309bf78bb2c798a2f21247c4d7a1665b0b1b3c5`。

EVAL=0时WORK_MS已有79，说明仅优化相关评分未覆盖能量门控/扫描热路径。R2不改算法，只在用户CMake将Debug O2扩大到 `chirp_service.c` 和共用 `sound_detector.c`，与 `chirp_window.c` 一起优化。实际编译顺序为-O0 -O2，无fast-math；Release仍-Os，其余Debug模块仍-O0。共用门控影响旧能量候选，已重跑原模块及服务回归；优化函数单步调试的变量可见性可能降低。

R2测试与构建报告：`Logs/Detector/M4_2_2/R2/`。ChirpService O0/O2快慢调度10/10、SoundDetector及服务、Audio、App均退出0；两配置构建及四次音频/ETH布局检查通过。独立复核无阻塞项；该结论不代替实物耗时与丢块复验。

| R2构建项目 | Debug | Release |
| --- | --- | --- |
| Flash字节 | 274048 | 183524 |
| 主副本范围，末端不含 | 0x20008060..0x2000D860 | 0x20008060..0x2000D860 |
| 队首缓存范围，末端不含 | 0x20006BA0..0x20008060 | 0x20006BA0..0x20008060 |
| BSS结束/最小堆栈段结束 | 0x2000E98C / 0x2000EF90 | 0x2000E98C / 0x2000EF90 |
| 到音频区静态间隔 | 4208字节 | 4208字节 |

R2 Debug SHA256：`d089855a3ea98c486988cef430994edbcc696c44aea2baef303154994e21d386`；Release：`7e76b415ef7bf59e3717c9e433cd950f6c9614415c7c9f0db205ba5bfd19f0c9`。本次冻结烧录Debug为 `Logs/Firmware/M4_2_2_R2/d089855a3ea9/Debug/SBCG.elf`，散列/实际编译命令/地址见 `Logs/Detector/M4_2_2/R2/build-manifest.json`。原R1及初版镜像、失败日志保留，不覆盖。

## R2本次实物结论：仍失败

已取得本镜像授权，沿用 `.vscode/openocd-stm32f746.cfg`，选择原A板ST-LINK，仅烧录冻结R2 Debug文件；SHA256与批准值一致。OpenOCD退出0、Verified OK、复位成功，耗时8.948391秒。不改选项字节、不主动播放声音、没有新增接线。

COM3采集70.189030秒，BOOT后61.166706秒；879条原始行，排除4行旧输出后875行；raw/JSON逐行一致、严格ASCII且完整换行，精确R2 BOOT和A UID均只有一次。无额外启动。

| R2本窗口观测 | 结果 |
| --- | --- |
| AUDIO | 收到55条，末SEQ57、OK345、DROP0、ERR0、HAL0、DMA0、RATE_HZ48000、VALID1 |
| 音频日志完整性 | 序号有缺口，原连续性分析明确失败；累计DROP0不能替代日志完整 |
| CHIRP_STATE | 58条；末OK347、BAD0、CAND58、MATCH0、DROP56、SKIP0、ERR0、EVAL4713、PENDING1 |
| 已观测最大耗时/操作量 | COPY_MS2、WORK_MS68、S_MAX8192、E_MAX8；WORK包含扫描及评分，未分开计时 |
| HEARTBEAT | 58条，末SEQ58、TX_ERRORS503；这是串口发送/队列错误，不是网络丢包 |
| UI | 收到40条，末RUNNING、FRAMES647、ERRORS0、UPDATES56；本次未重新目视验屏 |
| CHIRP匹配 | 0，没有主动播放条件下的扫频误报 |
| 判定 | **FAIL_SEARCH_DROPS_AND_LOG_OVERFLOW** |

证据：`Logs/Chirp/quiet-03-serial.bin`、`quiet-03-serial.jsonl`、`quiet-03-run.json`、`quiet-03-flash.txt/.json`、`quiet-03-summary.json`。raw SHA256 `67a33d3e4be4ece83bdc91157104eb64f8c31186fb0a537eea2d3c9072e0eb79`；BOOT后规范化SHA256 `8ff455c95a2d43a9edd127270ca762cdc38a9c247fd7231897d43ed5d625c233`。

相比R1，本窗口音频累计丢块消失、串口错误减少，但多数搜索未完成；这些窗口不是完全相同的环境输入，不能将耗时差值当作严格性能对照。DROP包含超时、覆盖、容量及失效清理，现有日志不能证明56次全部由同一原因造成。没有网线、DHT未接，本窗不验网络、同步精度、自动温度或手机识别。

下一软件增量应先拆分扫描/评分耗时及丢弃原因，降低每候选相关计算量，再处理诊断日志生产/排空的调度预算；不能仅扩大队列或延长超时来掩盖积压。后续上文DIAG观测已实测，当前仍没有新的实物性能修复结论；无新硬件配置或IRQ/DMA/Cache关系。

## 文件变更

- `App/Detector/chirp_window.h/.c`：预计算模板统计与单窗口去直流归一化相关。
- `App/Detector/chirp_service.h/.c`：CPU副本、独立队首输入缓存、候选门控、4任务搜索、8192帧/8窗预算及诊断。
- `App/Audio/audio_capture.c`：唯一Begin内复制，真实End结果提交，归还后分批工作；保留原电平、能量候选与时间服务。
- `cmake/audio-layout.cmake`：用户自有、可再生成保留的源码/宏注册，不修改生成CMake。
- `App/app_main.c`：新增优先固件标识及DIAG日志分类/心跳字段；原有未提交PPS部分保留、不得混入本轮提交。
- `Tests/ChirpService/test_service.c`：窗口评分和服务所有权/预算/边界测试。
- `Tests/ChirpService/test_capture.c`：真实AudioCapture回放，End时擦模拟DMA，验证10个实际WAV事件。
- `Tests/ChirpService/run_tests.py`：原生O0/O2测试、逐次独立输出目录，避免并发测试争用可执行文件。
- `Docs/Protocol.md`：仅追加M4.2.2及DIAG串口定义；此前未提交PPS协议改动保留且不能混入本轮提交。
- `Docs/Plans/2026-10-25_Bounded_Chirp_Integration.md`：设计、阶段状态和待验收清单。
- 本报告、`Docs/Project_State.md`、`Docs/README.md`、`Docs/Document_Status.md`、`Docs/Audio_Selection.md`：区分新固件准备与板上实物状态。

M4.2.1的流式直接相关核心保留作软件基线，板端不逐采样调用它；这里只链接其默认评分配置和模板，主路径使用单窗口评分和候选有限搜索。未更改WAV、模板内容或其源文件SHA256 `c37114332a698910e12ac855b47b566df229a3c7ec01414371e32969da3e084d`。

## 调用链、IRQ、DMA与缓存

```text
App_Process → AudioCapture_Process
  → BSP_Audio_BeginBlock（唯一消费者）
  → 原统计/原候选Stage + ChirpService_Stage（仅复制声道0）
  → BSP_Audio_EndBlock（原始真实归还验证）
  → ChirpService_Complete + 原候选Complete
  → 原候选发布 + ChirpService_Process
      → 当前块至多8192帧门控 → 队首完整缓存 → 至多8窗评分 → 最多一行日志
```

SAI2 B接收、DMA2 Stream7 Channel0、外设到内存、16位循环、32768个int16元素/64KiB原DMA缓冲不变；半满/全满各发布8192帧完成块，IRQ仍只更新原索引和状态。没有新IRQ/NVIC关系，没有在ISR相关/绘图/发送串口。

CPU主副本为11264个int16：3072帧历史＋8192帧当前块，共22528字节；队首另缓存2656点、5312字节。首次评分前完整缓存搜索输入，缺尾等待；缓存后不依赖主副本。未缓存输入被覆盖则DROP；新块覆盖尚未扫描数据则SKIP并清门控历史。换队首重新缓存；失败End、缺口或会话变化清历史。未完成全部搜索范围不发布；不保存DMA或调用方描述符指针。

**No CubeMX change required。** GPIO/RCC/TIM/SAI/DMA/NVIC/LTDC/FMC及生成文件未改。本轮沿用当前未启用I/D Cache的状态；新CPU副本放在DTCM、DMA不访问，不增加随机缓存维护。

## 计算范围与未覆盖情况

候选门控另用64帧窗、开128/关64 PCM、连续2窗、安静8窗、去重24000帧。每候选围绕提示起点−192..+64，共257个连续窗口，各2400点。每轮至多8窗，即19200个点积项，另有平方和等统计；候选整体最多616800个点积项，分轮执行。当前块一次扫描≤8192帧；1500ms任务/副本超时、250ms新块年龄、1000ms流过期以HAL无符号差处理。任务时限不证明课程2秒出结果要求。

这限制的是工作次数，不是测得的执行微秒数。`COPY_MS/WORK_MS`只给观测到的最大粗粒度HAL毫秒差；时限目标仍需实机验证。持续噪声可能使门控不再布防；弱信号导致门控位置偏移超范围、积压导致历史被替换时也可能漏检。反射最高峰不等于直达声，截幅只标记；名义48kHz模板未在本轮做接收采样率重采样。新CHIRP只有帧坐标，没有PTP估计值或测距结果。

## 已执行软件验证

```powershell
python -B Tests/ChirpService/run_tests.py
python -B Tests/Audio/run_tests.py
python -B Tests/App/run_tests.py
python -B Tests/SoundDetector/run_tests.py
python -B Tests/SoundDetectorService/run_tests.py
python -B Tests/SoundEventTime/run_tests.py
python -B Tests/ChirpMatcher/run_tests.py
python -B Analysis/export_chirp_template.py --check
cmake --build --preset Debug --parallel
cmake --build --preset Release --parallel
python -B Tools/Monitor/check_audio_layout.py --preset Debug
python -B Tools/Monitor/check_eth_layout.py --preset Debug
python -B Tools/Monitor/check_audio_layout.py --preset Release
python -B Tools/Monitor/check_eth_layout.py --preset Release
```

全部退出0。原生GCC O0/O2均用严格警告；新服务先以缺失实现替身的Init断言失败，再实现。接入测试也先验证AudioCapture尚未配置服务而失败，再添加调用链；初始红灯日志保存在 `Logs/Detector/M4_2_2/RED-O0.txt` 和 `RED-capture.txt`。

R1 O0/O2均验证：每块64次及5次调度时，20秒实际WAV的10个起始帧均为48000＋96000×k、无重复/漏检；118块真实AudioCapture调用，End擦DMA仍匹配；DHT门控、失败End拒绝、跨块尾部等待、257窗完整评分、独立缓存跨块继续、未缓存覆盖拒绝、1499/1500ms、流过期、会话/缺口、tick回绕及声道1。补测已缓存任务遇失败End/换会话、两队首换缓存、INT16极值及UINT64末帧。每轮≤8192帧/8窗。

原音频与应用回归、原候选175组Fraction、服务5组、时间关联64组、流式匹配基线O0/O2各3001断言/38参考场景通过。独立复核未发现阻塞问题，复核者在隔离产物目录另行完整重跑新测试通过。并发测试曾共享可执行文件导致文件占用/半写文件运行失败，已改用每次独立 `native-*` 目录，最终完整重跑通过；该失败不是开发板重启或固件运行错误。

R1主执行报告：`Logs/Detector/M4_2_2/native-272tgkps/`，完整补测见 `native-ppz3aq7v/`；独立复核核对全部报告及缓存生命周期，无阻塞项，先前独立完整复跑见 `native-isjixra6/`。原始日志不提交。原SoundDetector 175组Fraction回归在初版准备时通过；R1未改该模块，重跑音频、应用、原候选服务、时间服务和匹配基线均通过。

## R1历史构建、内存与冻结镜像

| 项目 | Debug | Release |
| --- | --- | --- |
| Flash使用字节 | 275712 | 183524 |
| CPU主副本范围（末端不含） | 0x2000697C..0x2000C17C | 0x20008060..0x2000D860 |
| 队首缓存范围（末端不含） | 0x2000C17C..0x2000D63C | 0x20006BA0..0x20008060 |
| BSS结束 | 0x2000E98C | 0x2000E98C |
| 生成最小堆栈段结束 | 0x2000EF90 | 0x2000EF90 |
| 到原DMA起点0x20010000剩余间隔 | 4208字节 | 4208字节 |

音频DMA仍0x20010000..0x20020000，运行时newlib堆起点仍0x20020000，MSP保留0x20047000..0x20048000。链接RAM97.02%包含固定网络保留窗，不能解读为CPU静态BSS占用97%；现有ETH/音频布局和链接断言均通过。保留原有RWX LOAD链接警告，无新增源码警告。

完整ELF SHA256：

- R1 Debug：`94a2be871a987e69f196584ce09241d335eb61609106834d28bb10f72955b5ca`。
- R1 Release：`af694d1556492569f9e84657227d43ef7c979bdf35b82e00d60a8eb317d80538`。

R1冻结Debug：`Logs/Firmware/M4_2_2_R1/94a2be871a98/Debug/SBCG.elf`，地址/散列清单 `Logs/Detector/M4_2_2/R1-build-manifest.json`。初版冻结文件 `Logs/Firmware/M4_2_2/ef79f6e76870/Debug/SBCG.elf` 继续保留。烧录前重新核对散列，不以日后build文件替代；R1已烧录但静默失败，当前板上R2也已完成本次静默并判失败。静态间隔4208字节不是运行栈实测余量。

## 未完成的验收与提交边界

1. 三次性能验证及一次DIAG定位均已完成，不重复使用旧授权；R2性能失败保留，本轮仅提交已验证的定位增量，不作识别/性能完成提交。
2. DIAG已完成分段/分类观测；下一软件修复仍限M4.2.2，优先优化能量扫描，保留不同阈值和唯一Begin/End，再评估日志调度。
3. 修复版完成测试、构建及复核后，新镜像另行取得烧录授权；先复验至少60秒静默下的原业务、搜索与日志完整性。
4. 允许发声后另做手机20秒/10次文件，核对识别、重复/漏检、完整257窗及共存负载。静默不能替代这一验收。
5. ±100μs、codec声学延迟、多径、其他播放设备和双板测距仍须独立验证。

手动只读采集示例（不烧录、不复位）：

```powershell
python -B Tools/Monitor/serial_monitor.py --port COM3 --duration 70 --log-prefix Logs/Chirp/quiet-followup-01
```
