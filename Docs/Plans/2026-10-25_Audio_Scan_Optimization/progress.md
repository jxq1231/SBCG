# 本轮规划记录

2026-10-25：用户请求针对性能问题规划优化方案。本轮仅编写方案，未修改固件、未构建或烧录。

- 已基于595aa4c及DIAG记录复核问题，保留工作树既有PPS/生成配置改动。
- 并行评估批量扫描语义与调度/日志预算。
- 本地受限启动器多次报CreateProcessSecurityEnvironment 0x80070020，Node读取亦失败；只读命令经授权审查通道成功，无文件清理或硬件操作。
- 当前规划文件按仓库规则存于Docs/Plans，未在项目根目录创建长期分析文件。
- 两路方案复核已完成：推荐先做批量核心的软件等价性，再只迁移扫频链；日志策略及统计共享由复测决定。
- 已写明候选帧/状态等价合同、文件范围、完整命令、性能工程目标、空候选/正样本/压力验收和未测声学边界。
- 当前仅修改规划和文档索引；仍未修改固件、构建、烧录或主动播放声音。
- 合同复核已补齐：首次/连续输入资格、异常元数据在PCM读取前处理、完整逻辑状态等价、DISCONT消费语义及uint64推进边界。
- 延迟目标包含缺尾等待和所有已接纳任务，未完成/丢弃不能从统计中剔除；正常负载与压力负载分别报告。
- 文档编码、代码围栏、相对链接和差异检查通过；计划已自查，准备只提交7份规划/索引文档。
- 2026-10-26：完成合同复核及交付检查；保留目录首次规划日期2026-10-25，现行入口更新到完成日期。

## M4.2.3-A第一阶段实施

2026-10-26：用户授权完成第一阶段。范围仅为sound_detector纯C批量入口、对照测试与文档；不接入Chirp扫描，不烧录。

- 已保存58个既有未提交用户文件的散列快照，后续只提交本阶段文件。
- 已保存改动前Debug/Release ELF及Flash内容散列，用于区分编译产物与板上原DIAG版本；原Debug前缀d3e084b1d83e。
- 测试先行：验证完整64点路径不调用原逐帧Push，回退消费1点，逐字段比较所有有效状态，避免仅以输出事件相同掩盖状态差异。

2026-10-26复跑与收尾（功能提交`8862ffd`）：

- 复跑原生：原门控O0/O2各9组/6553114断言通过；批量O0/O2各11项通过（checks=405111537、calls=10724761、fast=126105、fallback=10598652、events=517）；Fraction参考175用例/2240340样本/711事件通过。
- 复跑SoundDetectorService、ChirpService、ChirpMatcher、Audio、App、SoundEventTime六组回归全部退出0；Debug/Release构建通过，仅保留原RWX LOAD警告；音频/以太网两配置四项布局检查通过。
- 构建ELF SHA256与阶段内既有build-after记录一致；`PushBatch`段在链接map的“Discarded input sections”中，未进入ELF。相对DIAG的Flash +32/+64字节已定位为`CompleteWindow`不再内联进`Push`，原逐帧源码未改。
- 独立只读复核PASS_WITH_RESERVATIONS（代码/测试/合同PASS，无blocker/major；仅文档收尾minor与run_tests.py探测方式info）。复核自建差分模糊测试1677163条断言、UBSan通过并独立复现RED；报告`Logs/Detector/M4_2_3_A/review.json`。
- 功能提交`8862ffd`（5个文件，作者/提交者jxq，未推送）；范围外用户改动未动。按复核意见更新任务计划、状态/入口文档及本记录后随文档提交收尾；M4.2.3-B仍未开始，不烧录。

## M4.2.3-B实施、烧录与静默验收

2026-10-26：用户授权完成B并入烧录。功能提交`743e027`（8文件）。

- 实现：Scan改用SoundDetector_PushBatch，按consumed推进；异常计数/清历史/退出；新增完成与丢弃时延DONE_MAX_MS/DROP_MAX_MS；固件标识SBCG-M4.2-BATCH。
- 主机：ChirpService O0/O2全PASS；新增test_scan_batch证明每8192帧块128次批量、0次逐帧；真实20秒WAV 10/10（64与5 calls/171ms）；时延用例完成32ms/过期1500ms/Complete历史10ms/Reset 7ms；6组回归、两配置构建（Debug 275400/Release 184924字节）及四项布局检查通过。
- 独立复核PASS_WITH_RESERVATIONS（code PASS，无blocker/major）。唯一minor（完成时延用Process入口而非事件时刻）已在提交前修正：完成/丢弃分支实时读tick，并补两条时延回归；复核自建差分显示HEAD逐帧与批量实现282行轨迹、真实WAV 7761/791行完全一致。报告`Logs/Detector/M4_2_3_B/review.json`。
- 烧录：最终Debug ELF SHA256 `26a6496eb5ed183b646e289cbfe4a1e96bcc4185273819198ef75225fbd1e63f`，OpenOCD Verify OK、复位成功；A板现运行SBCG-M4.2-BATCH。
- 静默采集：85.0秒，BOOT后69.567秒；SCAN最大3ms、SCORE最大4ms、GAP最大106ms、DONE_MAX_MS=831ms、DROP_MAX_MS=0；CAND32/DONE32/PENDING0，TIMEOUT/HISTORY/FULL/RANGE/RESET=0；HEARTBEAT TX/LOG错误0、HWM24；AUDIO OK407零丢块/错误；UI错误0；单BOOT、A UID。判定PASS_SILENT_SCAN。
- 主动标准音频未验收：用户说明当前环境不能发声；一次无BOOT同步的PC扬声器试探回放未产生MATCH且未确认可闻，不纳入结论。证据`Logs/Chirp/quiet-batch-02-*`、`active-batch-01-*`（后者仅试探）。
- 范围外用户改动保持未动；烧录镜像基于含既有PPS/CubeMX改动的工作树，功能提交不含这些改动。文档收尾随docs提交。
