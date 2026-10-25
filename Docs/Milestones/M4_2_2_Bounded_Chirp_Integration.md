# M4.2.2 有界扫频接入：初版、R1和R2静默验收记录

<!-- doc-status: hardware-failed-next-software-revision-required; reviewed: 2026-10-25 -->

## 当前结论

**初版、R1、R2均已烧录，三次静默验收未通过；当前A板为R2。** 固件 `SBCG-M4.2-CHIRP-R2`，Debug前缀 `d089855a3ea9`。R2已收到的音频累计丢块/错误为0，但搜索丢弃56、串口发送/队列错误503、状态行缺失，不能判为识别服务正常。软件回放通过不等于实物实时性通过。当前没有准备新的待烧录镜像；先降低相关计算负担并完善日志调度，再另行构建/复验。标准音频识别、手机识别率和到达精度均未验收，功能完成提交尚未创建。

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

下一软件增量应先拆分扫描/评分耗时及丢弃原因，降低每候选相关计算量，再处理诊断日志生产/排空的调度预算；不能仅扩大队列或延长超时来掩盖积压。新方案及实现另行推进，本次止于记录失败证据；没有新硬件配置或新IRQ/DMA/Cache关系。

## 文件变更

- `App/Detector/chirp_window.h/.c`：预计算模板统计与单窗口去直流归一化相关。
- `App/Detector/chirp_service.h/.c`：CPU副本、独立队首输入缓存、候选门控、4任务搜索、8192帧/8窗预算及诊断。
- `App/Audio/audio_capture.c`：唯一Begin内复制，真实End结果提交，归还后分批工作；保留原电平、能量候选与时间服务。
- `cmake/audio-layout.cmake`：用户自有、可再生成保留的源码/宏注册，不修改生成CMake。
- `App/app_main.c`：新增优先固件标识，仅本轮对应两行；原有未提交PPS部分保留、不得混入本轮提交。
- `Tests/ChirpService/test_service.c`：窗口评分和服务所有权/预算/边界测试。
- `Tests/ChirpService/test_capture.c`：真实AudioCapture回放，End时擦模拟DMA，验证10个实际WAV事件。
- `Tests/ChirpService/run_tests.py`：原生O0/O2测试、逐次独立输出目录，避免并发测试争用可执行文件。
- `Docs/Protocol.md`：仅追加M4.2.2串口定义；此前未提交PPS协议改动保留且不能混入本轮提交。
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

1. 本轮三次烧录/静默均已完成，不重复使用旧授权；R2仍失败，不作功能完成提交。
2. 下一软件修复仍限M4.2.2：独立耗时及丢弃原因证据、相关计算预算和日志调度，保护唯一Begin/End和既有用户改动。
3. 修复版完成测试、构建及复核后，新镜像另行取得烧录授权；先复验至少60秒静默下的原业务、搜索与日志完整性。
4. 允许发声后另做手机20秒/10次文件，核对识别、重复/漏检、完整257窗及共存负载。静默不能替代这一验收。
5. ±100μs、codec声学延迟、多径、其他播放设备和双板测距仍须独立验证。

手动只读采集示例（不烧录、不复位）：

```powershell
python -B Tools/Monitor/serial_monitor.py --port COM3 --duration 70 --log-prefix Logs/Chirp/quiet-followup-01
```
