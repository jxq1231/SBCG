# M4.2.3 能量扫描性能优化方案

<!-- doc-status: stage-a-complete-stage-b-not-started; reviewed: 2026-10-26 -->

方案始于2026-10-25，2026-10-26完成复核；目录日期保留首次规划日期。

**目标：** 在保持候选帧、门限和失效行为一致的前提下，降低能量扫描对前台调度的占用，使完整扫频搜索和诊断日志能够及时处理。

**当前阶段：** 第一阶段M4.2.3-A已完成软件等价验证并提交`8862ffd`；M4.2.3-B尚未开始，不改板端扫描或烧录。

**Architecture：** 先增加可对照的纯C批量门控，再只迁移扫频扫描，原候选链保持不变；复测后才决定是否调整日志或共享统计。

**Tech Stack：** 现有C11、ARM GCC/CMake、原生GCC O0/O2测试、COM3诊断采集；不新增RTOS、DSP库或构建系统。

> 执行者按本文件逐任务实施和复核；可采用executing-plans。规划文档不是新固件烧录授权。

## 规划进度

- [x] 确认DIAG实测瓶颈及既有未提交改动。
- [x] 复核批量扫描与日志调度的备选方案。
- [x] 写清第一增量边界、等价性测试和实物验收关口。
- [x] 自查方案，完成文档检查与定向提交准备。

## 执行边界

当前只实施M4.2.3-A；板端接入、日志策略及统计共享保持后续增量。No CubeMX change required，不改GPIO、时钟、SAI、DMA、IRQ、LTDC或Cache。当前A仍运行SBCG-M4.2-DIAG。

后续功能实现须先完成软件测试、现有CMake构建、布局及复核；新镜像烧录另行取得授权。既有PPS及CubeMX生成文件差异保留，不混入本计划提交。

## 1. 为什么先优化扫描

DIAG在61.355118秒完整行窗口内观测到SCAN最大59ms、Search最大5ms、相邻Process最大172ms；34次搜索丢弃由21次超时和13次历史覆盖组成。日志错误208次全部为队满，未见UART写失败。当前主要证据指向扫描占用前台、评分与日志得不到足够处理机会，不能据此将问题单独归咎于显示。

每个8192帧块约170.7ms。两条门控都逐帧调用通用接口；扫频链每块8192次，另有原候选链独立扫描。每候选257窗、每轮8窗，至少需要33次评分调用。1500ms/33约45.5ms只是未计排队/等待时的平均机会预算；若等下一半块约171ms，余量约40.3ms/轮。172ms是最大间隔，不是平均值，不应乘33冒充实际完成时长。

已收到约2KB/s日志，低于115200/8N1约11.52KB/s的理论字节容量；丢弃是应用队列满，仍需计入同步发送耗时。优先增加有效前台处理机会，而非先改波特率。所有历史最大值都不是已证明的最坏执行时间。

## 2. 方案比较与选择

| 方案 | 作用和代价 | 决策 |
| --- | --- | --- |
| 64帧完整窗批量门控 | 合并连续性校验、输出清零和状态写回，仍统计每个PCM样本 | 首先实施 |
| 两条链共享窗口统计 | 可再减少重复平方和，但要求窗口原点/部分窗/事务边界一致 | 批量路径实测后再评估 |
| 直接复用原SOUND候选 | 原阈值256/128，扫频128/64，会改变弱信号资格 | 不采用 |
| FFT或粗搜/精搜 | 改搜索算法、数值路径及可能的内存需求；当前Search最大仅5ms | 评分成为主要瓶颈后再规划 |
| 扩大队列或延长期限 | 暂时隐藏积压，增加迟到和过期输入风险 | 不作为性能修复 |

## 3. 首个软件增量：M4.2.3-A批量核心（已完成）

**独立目标：** 新批量入口在同一输入流上与原逐帧门控输出完全一致。它尚不替换实机扫描路径，软件范围验证后单独提交。

文件：

- 修改`App/Detector/sound_detector.h`：明确批量接口、消费数量和输入所有权。
- 修改`App/Detector/sound_detector.c`：增加完整窗快速路径，复用原CompleteWindow判断，保留SoundDetector_Push作参考与回退路径。
- 新增`Tests/SoundDetector/test_batch.c`：逐字段对照逐帧/批量事件和逻辑状态。
- 修改`Tests/SoundDetector/run_tests.py`：执行新增用例，保留原参考检查。
- 更新本计划与`Docs/Project_State.md`中的软件/实物状态；中文函数说明与真实签名保持一致。

已实现接口（2026-10-26，功能提交`8862ffd`）：

```c
SoundDetectorResult SoundDetector_PushBatch(
    SoundDetector *detector,
    uint32_t session,
    uint64_t first_frame,
    const int16_t *samples,
    uint16_t max_frames,
    uint16_t *consumed,
    SoundCandidate *out);
```

接口合同：

1. samples仅本次调用借用，max_frames为1..64个单声道PCM16点；不保存指针，不访问HAL、DMA或日志。consumed/out由调用方独占且不与输入/状态重叠。
2. 完整64点可用、used=0、配置窗64且末帧不溢出时，首次输入可建立新窗；已有输入则须同session、last_frame不为UINT64_MAX且first_frame=last_frame+1。完整统计一次和/平方和/峰值/截幅，再推进一次窗口状态。正常8192帧块约128次窗口级推进，不能将其宣传为64倍执行加速。
3. 部分窗、不足64点、其他窗口配置、会话变化/缺口/重复/倒序或uint64邻界时回退原逐帧逻辑，一次最多消费一个点。首个DISCONTINUITY/INVALID或EVENT立即交还调用方，不额外吞掉后续输入。
4. 每次最多返回一个事件。成功消费时consumed为1或64；输入非法或原逻辑拒绝样本时为0。新指针/长度非法时先拒绝且不改状态；对合法批量描述符，session=0等已有清历史语义仍保留。未配置、session0、同会话到达UINT64_MAX等必须先判定，再读取PCM，不能先计算samples[0]后交给原Push拒绝。NULL消费/输出指针明确拒绝，不允许隐藏部分推进。
5. 使用32位计算PCM16乘积，再扩展到64位累计；保留整数能量比较、阈值等号、冷却及确认语义。除候选onset/confirmed和质量字段外，session/have_input、window_first/last_frame及完成后的used/sum/sum_squares/peak/clipped清理均须与逐帧结果一致。
6. DISCONTINUITY表示已消费1点；调用方按consumed推进，consumed=0必须停止处理。不得把已消费点重喂，或让uint64坐标推进回绕。

执行顺序：

- [x] 先写对照测试并观察缺少批量行为的预期失败。（RED日志`native-3yi7yu5p/RED-batch-O0.log`；独立复核另行复现）
- [x] 最小实现快速路径/回退，不同时重写原逐帧参考。（HEAD对比确认原`Push`/`CompleteWindow`未改）
- [x] O0/O2分别比较事件序列及每个有效状态字段；不比较结构体填充字节。（O0/O2共405111537条检查通过）
- [x] 执行原门控及相关服务回归、两种ARM构建，复核差异后提交软件范围；未接入新路径不宣称实机加速。（6组回归、两配置构建及四项布局检查通过；独立复核PASS，功能提交`8862ffd`）

测试矩阵：两组256/128、128/64门限；静默布防、连续确认、中间能量取消、阈值等号、持续声不重复、冷却后再布防；PCM直流、正负极值、截幅、低增益扫频及噪声；1..64任意及随机分块、非64对齐绝对起点、used=63再输入64点、跨8192边界、窗口配置2..256；会话变化、缺口、倒序/重复、session0、NULL、无效元数据+恶意占位PCM指针、first=UINT64_MAX-63、末帧邻界和同会话UINT64_MAX之后的拒绝。

实施、复跑及二进制差异证据见[M4.2.3-A软件等价验证记录](../../Milestones/M4_2_3_A_Batch_Detector_Core.md)。

## 4. 首个实机增量：M4.2.3-B只接入扫频扫描（尚未开始）

**独立目标：** 在同一所有权和检测语义下显著降低SCAN耗时，并使完整搜索能够完成。

文件：

- 修改`App/Detector/chirp_service.c`：Scan使用批量入口，按consumed推进绝对帧和last_scanned；事件仍交原Queue。消费为0或出现异常时按原错误/清理机制处理，不能死循环。
- 修改`Tests/ChirpService/test_service.c`、`test_capture.c`、`test_diagnostics.c`：保持DMA归还验证、跨块、失效与诊断覆盖，并复跑真实20秒WAV。
- 修改`App/app_main.c`：本增量固件标识；不改队列容量/排空策略。
- 若增加任务终结延迟观测，仅在`chirp_service.h/.c`增加分类计数及饱和最大毫秒值，沿用现有DIAG频率；起点为任务已有completed_ms，即关联音频块的完成tick，包含缺尾等待、缓存、调度及处理。完成与丢弃分别报告，不能只挑成功样本计时，也不能用电脑收包时间代替。主机保留完整分布，板端低频汇总，避免逐任务日志洪泛。
- 修改`Docs/Protocol.md`与本计划，必要时在`Analysis/`新增对应验收脚本及有失败用例的测试；不修改现有数据来适配工具。

原`SoundDetectorService_Stage`先保持不变，因此原能量候选、SOUND_TIME及Begin/End暂算/提交关系不迁移。原SAI2/DMA配置及CPU副本位置不变，未通过End的数据仍不发布。保留8窗/轮、257窗/候选、1500ms期限、4任务、32行日志队列及两套阈值。沿用现有Debug优化配置，不叠加新的时钟/缓存调整。

```powershell
python -B Tests/SoundDetector/run_tests.py
python -B Tests/SoundDetectorService/run_tests.py
python -B Tests/ChirpService/run_tests.py
python -B Tests/ChirpMatcher/run_tests.py
python -B Tests/Audio/run_tests.py
python -B Tests/App/run_tests.py
python -B Tests/SoundEventTime/run_tests.py
cmake --build --preset Debug --parallel
cmake --build --preset Release --parallel
python -B Tools/Monitor/check_audio_layout.py --preset Debug
python -B Tools/Monitor/check_eth_layout.py --preset Debug
python -B Tools/Monitor/check_audio_layout.py --preset Release
python -B Tools/Monitor/check_eth_layout.py --preset Release
```

全部退出0才准备冻结镜像/独立复核，并按AGENTS.md第20节单独取得烧录授权。每一阶段使用新日志前缀，保留现有失败证据。通过本增量实际可验收范围后提交，不自动进入下一增量。

## 5. 性能和正确性验收

下表为后续工程目标，不是现有保证：

| 项目 | 目标/判定 |
| --- | --- |
| 每8192帧扫频扫描 | SCAN最大争取≤10ms，由实机证明 |
| 相关搜索 | 保持257窗完整计算、8窗预算和现有5ms量级；不得跳窗伪装完成 |
| 单任务完成延迟 | 正常受控负载下，所有已接纳任务争取从原completed_ms起≤1000ms；未完成/丢弃判失败并单列，不剔除。原1500ms失效保留 |
| 原业务 | AUDIO累计DROP/ERR/HAL/DMA为0，CHIRP BAD/SKIP/ERR为0 |
| 日志 | LOG_FULL/WRITE/BAD为0，深度无持续增长，BOOT/状态/事件证据可核对 |
| 正确性 | 主机实际WAV仍10/10精确位置，事件字段与逐帧参考相同 |

验收顺序：

1. 主机对照通过后，授权烧录仅A，采集约70秒、BOOT后至少60秒，不主动发声；核对DIAG及原业务。没有候选时DONE=0不代表吞吐失败，也不代表搜索通过。
2. 可发声时播放标准20秒/10次文件，核对已触发任务完整DONE、无资源超时/覆盖、完成延迟和旧业务共存；再核对MATCH的10次成功、漏检和重复。资源处理通过但MATCH不通过时单独定位声学识别，不篡改为整体通过。
3. 主机增加候选频率压力以及“偶发172ms间隔+周期日志突发”的合成调度测试。原冷却允许约每0.5秒一次候选，压力结果需报告可持续处理能力；目前只有间隔最大值，不能称为真实时序轨迹回放。
4. 若扫描≤10ms但仍超时/日志溢出，保留失败原因，进入下一节有证据的后续选项；不延长期限或缩小搜索范围取得表面通过。

任务完成时延不等于声音到达精度；后者来自采样帧及时间模型。批量化不抽点、不降采样，也不将64帧门控窗当作±100μs到达检测。双板配对、物理同步精度、距离/方向和两屏一致性仍单独验收。

## 6. 有条件的后续优化

仅在前一独立增量验收后选择，不同时实施：

- 扫描仍慢：测旧候选服务耗时，再迁移其工作副本为批量处理，保留真实End后才提交的事务。其输入来自volatile DMA借用，需另行设计兼容读取或复制路径，不得强制去掉volatile。之后才评估两套门控共享同窗统计；共享需证明窗原点、声道、流边界和有效期相同，状态/门限仍独立。
- 扫描已快但日志仍满：统计每秒产出/排空行数和字节数、最长阻塞发送；对纯状态考虑降频或只保留最新值，主动合并单独计数。BOOT/RESET、命令回复和声音事件保持可靠；AUDIO_POS/锚点等分析证据不得未经协议/分析器适配直接合并。排空策略若变化必须另测DMA与DHT时序，不能一轮无界发送。
- 主循环仍长：分别测原门控和UI，再决定分片或降低非关键绘制工作；现有172ms不能证明UI是单独根因。
- Search成为主要瓶颈：再规划滚动统计、DSP点积或FFT/粗精搜索，重新验证相关峰和帧精度；本轮暂缓。

## 7. 自查与实施入口

- [x] 各建议都对应已有观测；未把不同最大值相加或承诺加速比例。
- [x] 第一实机改动只迁移扫频链，保留旧候选事务和两套门限。
- [x] 定义精确对照、资源预算、空候选/正样本/压力三类验收。
- [x] 说明未测声学及双板指标，不以本轮优化宣称测距完成。

M4.2.3-A已完成软件等价性并提交`8862ffd`；下一步为M4.2.3-B，本文件不构成其实施或烧录授权。
