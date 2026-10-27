# A路线：保留当前单chirp，修复可信稳定结果发布

<!-- doc-status: reviewed-increments-not-implemented; reviewed: 2026-10-28 -->

本文件每个编号为一个独立增量；同一时刻只执行一个编号。所有C/C++新增/修改遵循根AGENTS第33节中文注释；所有构建/硬件结论均须实际执行，不从计划推断。

## G0：冻结输入与验收合同，不改固件

**唯一目标：**后续每一轮都能确定使用什么源码、镜像、WAV、几何和统计口径。

**文件：**仅创建`Logs/Acceptance/<new-run>/baseline.json`与用户改动快照；必要的人类说明放本计划的执行记录。不要修改或stage现有用户文件。

- [ ] 执行`git status --short`、`git rev-parse HEAD`、`git diff --name-only`、`git diff --cached --name-only`；若发现其他工具正在写同一源码，先让其停止写入。
- [ ] 逐个计算当前8个用户修改文件、WAV、模板、本地Release ELF的SHA256；记录用户报告的已烧镜像SHA256，不能以本机ELF哈希代替Flash回校证据。
- [ ] 以`git diff --binary --output=Logs/Acceptance/<new-run>/user-baseline.patch`保留工作区差异；复制所需未跟踪输入为实验附件，不修改原件。原有LICENSE删除和`nul`不得提交。
- [ ] 另以`git diff --cached --binary --output=Logs/Acceptance/<new-run>/index-baseline.patch`保存原暂存区差异；同文件重叠/依赖旧未提交功能时先确定基线归属及提交许可，不能只验证脏工作树就提交不可复现的部分源码。与用户原hunk独立时按hunk暂存并检查staged diff；不可分时提交动作等待具体归属决定，不整文件stage。
- [ ] 检查`SBCG.ioc`存在；只读确认现有SAI/DMA、USART、ETH、LTDC配置。当前路线无CubeMX变化。
- [ ] 在baseline写入A/B UID、端口、角色/IP、WAV与模板散列、声源/音量/出声孔、环境、麦克风间距、几何坐标、校准版本、温度模式。
- [ ] 明确三个不同合同：一次声音到首次数字、到可信稳定结果、当前结果保持/过期。用户已选“连续确认新距离后自动更新”；3个2秒间隔事件需约4秒确认，不能写为2秒内稳定。若正式验收要求首包2秒内可信结果，A1一致性过滤不承担该保证，必须加入A4/B的单包身份证据。
- [ ] 冻结一轮10个预期槽，来自当前WAV的0/2/…/18s，不以首个检出事件作为slot0。手动手机播放无声学起点参考时，明确`onset_reference=operator/video`及其误差，不能用电脑API时间证明物理TOA精度。

**退出条件：**基线字段完整、哈希一致、时间合同未混用；没有覆盖用户改动。未知硬件镜像保持`reported_not_read_back`，不伪造已校验。

## O1：先补齐可观测性与全槽对账

**唯一目标：**能够重建“原始TOA→接受/拒绝→结果ID/状态→两屏快照”，不改变检测、测量或播放行为。

**准确文件范围：**

- 修改`App/Measure/measurement_result.c/.h`：只读诊断快照，说明raw时间差、温补前后距离、窗口成员、接受/拒绝/清窗原因。
- 修改`App/Measure/sound_pair.c/.h`：仅将输入的a/b_session、a/b_time_ns复制到结果，原公式/配对/门限不变；相关测试证明这是被动元数据补传，不改变旧决定。
- 修改`App/app_main.c`：在现有前台调用点记录决策，不改变既有调用顺序/分支。
- 修改`App/UI/touchgfx_service.c`：日志取实际提交给UI的快照，增加测量ID、状态、距离、方向与年龄；不在绘制/IRQ里加阻塞输出。
- 新增`Analysis/acoustic_acceptance_analysis.py`、`Analysis/tests/test_acoustic_acceptance_analysis.py`：固定槽账、计数增量和证据校验。
- 修改`Tests/Measure/test_sound_pair.c`、`test_measurement_result.c`、`Tests/App/test_app_udp_integration.c`：新诊断不改变旧返回值、状态、时间和发送次数。

**日志合同：**所有字段带单位；每个独立pair至多一条决策；状态变化即时、稳定快照最多1Hz；单行不超过现有发送缓冲容量，日志满须计数。

```text
$RANGE_DECISION,A_SESSION=...,A_SEQ=...,B_SESSION=...,B_SEQ=...,RAW_NS=...,RAW_MM=...,CORRECTED_MM=...,DECISION=...,REASON=...
$RANGE_RESULT,REV=...,ID=...,STATE=...,MM=...,DIR=...,SRC_NS=...,AGE_MS=...,HAS_VALUE=...,FRESH=...,EVIDENCE=...
$UI_RANGE,REV=...,ID=...,STATE=...,MM=...,DIR=...,AGE_MS=...,HAS_VALUE=...,FRESH=...
```

若一行过长拆为同一个`REV/ID`键的两条，不截断字段。已有`SOUND_PAIR.DIST_MM`保持原定义，不换单位或悄悄改成温补结果。

O1使用schema=1；尚不存在的权威REV、源deadline或正式/保持状态输出NA/UNAVAILABLE，不能用接收时刻或一个诊断序号冒充。被动补传后源时间可从pair取得；当前窗口/临时资格取不到时如实缺省。A2接入后schema=2才完整满足新合同，分析器按schema限制可证明的指标。

**分析器拟新增命令（O1实现后才可运行）：**

```powershell
python -B -X utf8 Analysis/acoustic_acceptance_analysis.py --manifest Logs/Acceptance/<run>/trial_manifest.json --a Logs/Acceptance/<run>/A-serial.jsonl --b Logs/Acceptance/<run>/B-serial.jsonl --out Logs/Acceptance/<run>/analysis
python -B -m unittest discover -s Analysis/tests -p test_acoustic_acceptance_analysis.py
```

输出`ledger.csv`固定10行/轮、`summary.json`、`integrity.json`；输出目录必须新建，原件不覆写。接口显式接受`elapsed_s`或`t`，保存原字段/原行号；同一文件混用或时间倒退应报错。丢失raw/残尾/坏行保留并影响证据资格。

- [ ] 先写“背景不占槽、slot0缺失、重复候选、额外候选、跨板同号、尾窗延迟结果、全槽缺失、旧计数不当新增”的失败测试。
- [ ] 增加诊断与分析器；同输入原业务行为必须逐项相同。
- [ ] 跑`Tests/Measure/run_tests.py`、`Tests/App/run_tests.py`、`Tests/App/run_app_udp_integration.py`及上述分析测试；Debug/Release构建。
- [ ] 若需要新镜像采集，先提供镜像SHA/差异/测试并取得该镜像烧录授权。短时旁观日志验证无队列溢出和音频丢块；此时仍不是声音精度通过。
- [ ] 审查差异、定向提交，建议`feat: add traceable ranging decision and display audit`。

## A1：纯有符号TDOA跟踪器，不先改硬件和网络

**唯一目标：**用真实2秒节奏可靠处理稳定值、孤立离群和移动后新簇；拒绝不能触发历史清窗。

**准确文件范围：**

- 新增`App/Measure/ranging_tracker.c/.h`：纯C实例状态机，无HAL、网络、UI、PCM或动态分配。
- 复核O1已在`App/Measure/sound_pair.c/.h`补传的`a_session/b_session`、`a_time_ns/b_time_ns`；A1不重复改原公式、窗口和质量门槛。
- 新增`Tests/Measure/test_ranging_tracker.c`；修改`Tests/Measure/run_tests.py`注册新纯模块。
- 修改`Tests/Measure/test_sound_pair.c`验证新增元数据按输入复制；必要时修测试夹具，不能调整原断言来隐藏回归。
- 修改用户自有`cmake/audio-layout.cmake`注册新模块。不得改`cmake/stm32cubemx/`、生成链接脚本或`.ioc`。

**输入与API合同（新定义，实施者按此实现）：**

```c
/* 所有标量均复制；不持有调用者指针。 */
typedef struct {
    uint32_t time_domain, a_session, a_sequence, b_session, b_sequence;
    uint64_t a_time_ns, b_time_ns;
    int64_t delta_ns;             /* 必须等于 b_time_ns-a_time_ns */
    uint8_t proxy_quality;        /* 旧分数，仅代理质量 */
    uint8_t evidence_kind;        /* PROXY=0, MATCHED=1, CODED=2 */
    bool geometry_assumed_valid;
} RangingObservation;

void RangingTracker_Init(RangingTracker *self,
                         const RangingTrackerConfig *config);
void RangingTracker_SetContext(RangingTracker *self,
                         const RangingTrackerContext *context,
                         uint32_t now_ms, uint64_t now_common_ns);
RangingDecision RangingTracker_Push(RangingTracker *self,
                         const RangingObservation *observation,
                         uint32_t now_ms, uint64_t now_common_ns);
void RangingTracker_Tick(RangingTracker *self,
                         uint32_t now_ms, uint64_t now_common_ns);
void RangingTracker_Invalidate(RangingTracker *self,
                         RangingInvalidReason reason, uint32_t now_ms);
bool RangingTracker_Get(const RangingTracker *self,
                         RangingTrackerSnapshot *out);
```

`RangingTrackerConfig/Context/State/Decision/Snapshot`均定义在新头文件。Context包含已确认domain、两端audio_session、A本地时钟代际、域/流/温度健康标志；只由前台健康协调者SetContext，不由任意包Push改变。上下文含5个已接受样本、3个候选新簇样本、最近16个已消费A/B源身份、状态与源截止时刻。`Init`后为ACQUIRING；空指针/零域/未知上下文/越范围/差值不符/时刻在未来2ms以上均拒绝，不能更新源时间。

`SetContext`在确认硬边界时清样本/候选；同一上下文FAULT恢复须由健康条件显式调用，重新进入ACQUIRING。单条未知session/旧domain包仅拒绝，不能让其重置当前有效结果。外层measurement_id/state_revision在tracker重新获取时继续递增，不随tracker的Init/恢复归零；去重高水位在同boot上下文恢复时保留。

**起始参数，冻结后验证，不自动调优：**

| 参数 | 起始值 | 语义 |
| --- | ---: | --- |
| accepted_window | 5 | 最近5个独立已接受有符号时间差 |
| confirm_count | 3 | 建立初值或更换簇需要3个连续独立一致事件 |
| inlier_gate_ns | 45000 | 相对已确认中值约15.5mm；这是拒绝门，不是精度声明 |
| candidate_span_ns | 20000 | 3个候选最大减最小不超过约6.9mm |
| candidate_max_gap_ms | 2500 | 连续候选的最大接收间隔；缺声重建候选，不清已确认簇 |
| source_interval_min/max_ms | 1500 / 2500 | 三点确认的相邻源时刻间隔；不同序号但同一短声的突发不算三声 |
| fresh_age_ms | 2500 | 从最后真正接受的声事件源时刻计，不从UDP接收计 |
| display_max_age_ms | 6500 | 保持旧值的硬截止；HOLD数值必须带历史状态 |
| display_delta_hysteresis_ns | 5800 | 约2mm的显示时间差滞回；不阻止温补应有变化 |
| legacy_quality_min | 50 | 只沿用旧代理准入，不称身份已验证 |

源时刻取`max(a_time_ns,b_time_ns)`；纯模块判时使用common_ns，局部now_ms仅用于前台接收间隔与回绕测试。前台处理延迟可能消耗FRESH预算，G0/O1须测量；超过预算时先修处理/映射，不无限增大有效期。

**状态合同：**

| 状态 | 数字/新测量资格 | 转移 |
| --- | --- | --- |
| ACQUIRING | 尚无确认数字；按G0保留既有临时首显时可有PROVISIONAL数字，不能称正式/稳定 | 连续3个一致独立事件→TRACKING |
| TRACKING | 有确认估计；是否正式还取决于身份/序列/域资格 | 在簇内接收并更新5点中值；首个离群→HOLD |
| HOLD | 展示旧确认值，`fresh=false`，不新增ID | 接回旧簇→TRACKING；第二个一致新簇点→REACQUIRE |
| REACQUIRE | 旧值标待更新；不可把候选直接显示为新结果 | 第3个一致独立新簇点→整体换窗并TRACKING |
| STALE | 不显示旧数值，不新增ID；候选仍可收集 | 新簇确认且资格有效→TRACKING |
| FAULT | 立即撤数字与全部历史 | 域/健康流重新确认后显式回ACQUIRING |

规则补充：

- ACQUIRING采用滚动候选3点；不一致就保留能够组成最新一致簇的后缀，不能“第3个无条件通过”。初始错误簇也可能一致，必须保留`evidence_kind=PROXY`，不能声称仅靠一致性找到了真值。
- 确认用源时间必须严格递增且相邻1500～2500ms；接收间隔仅辅助保护。三条不同序号的突发候选、同一声音多个峰不能完成确认；序列允许4秒漏声与三点“连续新簇”规则是不同职责。
- 相同A身份或相同B身份不得重复消费；旧包重配、重复pair和重复UI帧不累计confirm_count。高水位+最近16身份共同去重；序号回退需要明确新session/新域，不能因窗口过期就接受旧包。
- 输出质量取实际支持窗口的保守值，不能拿被拒绝新点的高分替换历史质量。源时刻属于最新独立支持证据，5点中值及其成员ID另列，二者不能混写。
- 方向由确认后的有符号delta计算；相反符号单独成为新簇，不能把正负平均成零。未确定方向不发布正式距离。
- 一个离群被拒绝时，不改变已确认样本、源时间、measurement_id或截止时刻；只改变状态/候选和状态revision。新候选不一致就重建候选，旧确认簇不被3秒计时清掉。
- `abs(delta_ns)`上再乘声速必须检查64位乘加范围。先滤时间差，再按当前温度和标定求距离；温度版本变化不能把旧毫米误标为新温度结果。
- 温度无效/域失效/流断裂/会话变化为硬边界；标定变化重建数值合同。温度合法更新保留已确认时间差，但不得刷新该声音的年龄或计作新发声。
- 稳定器承诺“移动后停在新位置并连续确认”，不承诺任意速度边运动边高精度定位。持续反射错簇与真实新距离可能有相同标量序列，A4/B提供额外声学证据，不能用盲目平滑消除此歧义。

**最小失败用例（测试放在新模块，O0/O2均运行）：**

```c
/* 配置与上表一致；测试helper delta_from_mm按固定测试声速转换，不用现场真值做运行门限。 */
push_mm(315, 0); push_mm(314, 2000); push_mm(316, 4000);
assert_state(TRACKING); assert_distance_near(315, 1);
push_mm(245, 6000);
assert_state(HOLD); assert_last_source_time(4000); assert_no_new_measurement();
push_mm(245, 8000);
assert_state(REACQUIRE); assert_distance_near(315, 1); /* 不能清窗直接245 */
push_mm(245, 10000);
assert_state(TRACKING); assert_distance_near(245, 1); /* 真变化确认合同，不证明245物理正确 */
```

测试helper必须真实调用`RangingTracker_Push/Tick/Get`并设置common_ns和独立源序号，不模拟另一个滤波算法。

- [ ] 补齐至少：单离群后恢复；245/385/254三个互不一致离群；固定2秒/±200ms接收抖动；漏一声4秒后恢复；候选间隔超限；同一短声内三个不同序号；初始错簇；同A或同B身份重复；源时间倒退；相反方向；温度变化；换域/换会话；FAULT同域健康恢复；32位毫秒回绕；乘法溢出；新簇确认时旧值即将到期。
- [ ] 已知single-03第一10对完整回放，不删245；比较每点决策和历史状态。raw误差继续原样报告，不把滤波输出当独立原始精度。
- [ ] 先运行测试观察旧行为失败，再实现；新模块不得自行初始化硬件。
- [ ] 执行`python -B Tests/Measure/run_tests.py`；Debug/Release构建与现有布局检查。
- [ ] 纯模块未接线，无需硬件验收；提交需明确`software-only, not wired/hardware accepted`。建议`feat: add evidence-aware ranging tracker`。

## A2a：权威展示状态线协议，不先启用发送

**唯一目标：**HOLD/STALE/FAULT及源时刻可以无歧义地编解码，重复包不续租。

**准确文件范围：**新增`App/Network/ranging_status_wire.c/.h`、`Tests/Measure/test_ranging_status_wire.c`；修改`Tests/Measure/run_tests.py`与`cmake/audio-layout.cmake`注册。保留旧MRES32/MACK16编解码及其测试，协议启用留A2b。

新`RST2`固定88字节、大端，禁止直接发送C结构体；`RAK2`固定24字节。双方一次部署同版本，不接收旧MRES作为新状态的回退。

| RST2偏移 | 类型/长度 | 含义 |
| ---: | --- | --- |
| 0 | 4字节 | ASCII RST2 |
| 4/5/6/7 | u8各1 | version=2、状态、方向、质量 |
| 8/10 | u16各2 | flags、length=88 |
| 12/16/20/24 | u32各4 | domain、authority_clock_epoch、state_revision、measurement_id |
| 28/32/36 | u32/u32/i32 | 温度版本、声速mm/s、距离mm |
| 40/42/44/46 | u16/i16/u16/u16 | 标定版本、标定偏移mm、原因、保留0 |
| 48/56/64 | u64各8 | source_common_ns、fresh_deadline_ns、display_deadline_ns |
| 72 | i64 | filtered_delta_ns |
| 80/84/86/87 | u32/u16/u8/u8 | source_key_crc32诊断摘要、检测版本、evidence_kind、保留0 |

flags：bit0 HAS_VALUE、bit1 FRESH、bit2 IDENTITY_VERIFIED、bit3 FORMAL；其它必须0。PROXY不得设置IDENTITY_VERIFIED；FRESH必须HAS_VALUE，FORMAL还必须FRESH、身份及域资格。HOLD/REACQUIRE的FRESH和FORMAL均为0。没有值的状态distance=0只是线协议填零，UI不得显示为0m；source_key_crc32只用于诊断摘要，不能作为唯一去重键。

RAK2：magic[0:4]，version=2[4]、status[5]、保留0[6:8]、domain[8:12]、authority_clock_epoch[12:16]、revision[16:20]、measurement_id[20:24]。

**新鲜度与会话合同：**

- A是唯一写者；`measurement_id`仅新接受的独立声事件产生，`state_revision`在状态或权威展示内容变化时递增，耗尽则停止发布而不回绕复用。
- 重传同revision必须逐字段相同，不能每次发送重算deadline；相同revision不同payload报错，旧revision过期后也不可复活。
- B用当前已确认共同域的本地PTP读时比较A源截止时刻，不能用接收时刻刷新源年龄。若本地PTP不可用/本地域无效，则立即撤数字。
- 同一measurement_id/同一源证据下，B本地接收保护期限只能取已有期限与本次期限的更早者；重复包、温补和状态变化不能延长。新的独立源证据产生新measurement_id时，可以建立新的deadline。UDP接收活跃不等于声音新鲜。
- `authority_clock_epoch`是A本地代际，不要求等于B的epoch数字；本地两锚点epoch匹配是另一层合同。
- 现有domain token为UID/tick散列，不是绝对唯一启动ID。新旧域、A单独重启、B单独重启、令牌复用必须测试；身份无法确认时fail closed，旧结果不恢复。若无法仅靠现有握手确定新会话，暂停A2b，单列只用现有5009 PCB的状态会话握手设计，不引入RNG/写Flash或更改PTP伺服来伪造唯一性。
- 域/流硬失效使A立即撤本地值，并尽力在旧上下文发送撤销状态；撤销报文不可授予新测量。网络不可用时B依靠本地资格和硬截止自行过期。

- [ ] 写逐字节golden vector、signed负delta、最大值/溢出、未知flags/state、长度错、无值字段、deadline逆序、代理冒充FORMAL测试。旧包、同revision不同内容和域接收授权属于A2b的状态缓存测试，纯codec不持有隐藏接收状态。
- [ ] 编解码测试通过，协议字节表与实际实现逐项一致；建议提交`feat: define versioned authoritative ranging state wire`，注明尚未启用。

## A2b：接入A权威跟踪、状态交换和两屏呈现

**唯一目标：**两板展示同一份有来源、有年龄、有失效状态的确认结果；短时拒绝不乱跳，真实移动连续确认后更新。

**准确文件范围：**

- `App/Measure/measurement_result.c/.h`：替换旧私有中值路径，委托新tracker；保留用户工作差异归属，不能整体覆盖文件。
- `App/Measure/measurement_gate.c/.h`：普通静默与单点拒绝发软状态，不再3秒调用清全部历史；失域/断流仍硬撤销。明确健康流恢复路径，不只依赖domain布尔上升沿。
- `App/Measure/result_exchange_state.c/.h`：ACK键改为当前域/authority_epoch/revision/ID；失败500ms退避保留，重发不续租。
- `App/Network/udp_sound_event.c/.h`：复用5009 PCB分派RST2/RAK2；不增加UDP PCB或CubeMX配置。
- `App/app_main.c`：仅协调，先收独立pair构建候选，第三声可以同时满足序列和三点确认，不等序列形成后才从零收三点。
- `App/UI/status_snapshot.c/.h`、`status_view.c/.h`、`status_screen.c`、`touchgfx_service.c`及`TouchGFX/gui/src/screen1_screen/Screen1View.cpp`、`TouchGFX/gui/include/gui/screen1_screen/Screen1View.hpp`：显示CONFIRMING/VALID/HOLD/REACQUIRE/STALE/FAULT；只改实际启用后端，生成TouchGFX文件不手改。
- `Docs/Protocol.md`及对应`Docs/Milestones/`：按实际启用的版本/证据更新，不把本设计当已实现。
- `Tests/Measure/`对应测试、`Tests/App/test_app_udp_integration.c`、`Tests/App/test_app_main.c`及其它真实编译到的桩、`Tests/UI/`、`Tests/TouchGFXView/`和`Tests/TouchGFXService/`对应测试。

**协调规则：**

```text
音频事件 → TOA模型 → 当前域事件交换/唯一配对
    → 预收集tracker观察 → 单包身份/序列/域资格检查
    → A权威状态/温补 → RST2/RAK2 → B原样状态缓存
    → A/B同一份显示快照 → 实际TouchGFX界面
```

- 单次无效pair是拒绝原因，不默认清历史；temperature失效和真实域/流边界明确硬撤销。
- 序列RESET不应自动把旧确认值变成新鲜结果，也不应仅因一次坏声抹掉历史；保留HOLD标识和截止时刻。只有新的充分证据可以恢复FRESH。
- 在用户改变G0合同前，按现有准入条件保留临时首显，不因接tracker隐含关闭。ACQUIRING+HAS_VALUE表示PROVISIONAL，FRESH至多2500ms、FORMAL=0，UI明确“临时/确认中”，不计正式覆盖/精度。每个临时呈现若来自新的独立事件可分配ID，但统计必须与确认/正式新结果分列；确认的新源事件必须新ID，不能通过延长同一临时ID期限升级。
- HOLD可以看见旧数字，但必须标“上次/保持”和年龄；STALE、FAULT无数字。B接收同revision不能自行变回VALID。
- 保留`MeasurementResult_Get`的当前有效结果语义；新增`MeasurementResult_GetPresentation`返回含has_value/state的展示快照，UI通过后者显示带标签的HOLD，不能通过把旧valid强设true来达到保持效果。
- 单次新温度改变声速时，按确认delta重算、增加状态revision，源事件ID与deadline不变；显示滞回不得吞掉任务要求的温补变化。
- 显示刷新延迟计入实屏结果；1Hz刷新若不能满足确定的状态/显示延迟预算，先测量，再在用户UI前台提高刷新频率，不能把模型时间当屏幕时间。

**软件测试：**

- [ ] A1所有2秒案例经过真实`app_main→tracker→RST2→B`，不是仅测试桩伪造结果。
- [ ] 三次相同源包只算一次；ACK丢失10秒重发不改变source/deadline；旧revision在STALE后仍拒绝；A/B分别重启、令牌复用、域恢复无新声不显示旧值。
- [ ] 同ID的温补/状态变化靠revision一致更新；错序包不能覆盖较新revision；DOMAIN0和健康流恢复明确处理。
- [ ] 测量结束→静默≥7秒→再次播放；不允许SILENCE锁死；首显、临时、确认延迟分别输出。
- [ ] UI快照带ID/revision/state，测试界面字符串不把HOLD写成VALID、不把无值0显示为0m；A/B数值、方向、年龄策略一致。

```powershell
python -B Tests/Measure/run_tests.py
python -B Tests/App/run_tests.py
python -B Tests/App/run_app_udp_integration.py
python -B Tests/Udp/run_udp_sound_event_tests.py
python -B Tests/UI/run_tests.py
python -B Tests/TouchGFXView/run_tests.py
python -B Tests/TouchGFXService/run_tests.py
cmake --build --preset Debug --parallel
cmake --build --preset Release --parallel
```

完成软件、差异和独立复核后报告待烧镜像SHA256；取得授权后A/B同步部署同一协议版本。实机执行C文件H1/H2/H3，用户确认两屏后才能提交硬件完成增量，建议`feat: publish consistent stable ranging state to both displays`。

## A3：保持WAV不变的低成本实机闸门

按C文件执行0.320m两侧各3轮、真实移动/微扰、静默与瞬态。不要先跑全量程。

- raw稳定且仅UI出问题：回A2b，禁止换音频。
- TOA/MISSING/NO_LEFT等先定位窗口或时间映射；不能用延长HOLD冒充捕获恢复。
- raw仍有约200µs错簇且存在PCM证据：进入A4。
- HOLD占比高、确认错误簇、首包身份不能证明：A路线未通过，记录失败；不能靠删槽或放宽质量分进入最终验收。

## A4：当前50ms声的TOA/身份离线资格，再决定接入

**唯一目标：**证明新检测比当前过阈方法更能区分真实起点、强反射和伪标准声，不先改WAV。

**文件：**新增`Analysis/ranging_detector_benchmark.py`、`Analysis/tests/test_ranging_detector_benchmark.py`及纯C回放CLI；先复用`App/Detector/toa_estimator.c`、`chirp_matcher.c`、冻结模板与CPU副本，不修改其现有行为。只有离线资格通过后，另立接入增量，允许新增`App/Detector/chirp_arrival.c/.h`、对应`Tests/Detector/`和`cmake/audio-layout.cmake`。

- [ ] O1的分板PCM工具须先证明接收完整；丢PCM/混板优先修工具。没有原始PCM的245案例标LOG_ONLY，不合成“原始声波”。
- [ ] 比较旧过阈、归一化模板相关、受约束首径候选；加峰/副峰比、有效带内SNR、左右窗/首径可辨性等独立质量，不把代理98当到达精度。
- [ ] 用不同试次划分训练/验证，禁止相同原始PCM的增益变体跨集合；门限在训练集冻结，未见设备/位置为留出集。
- [ ] 合成直达+强回声、反相、通道带限、增益0.25～2、噪声、±1000ppm采样率误差、启动截断、跨8192块、孤立瞬态。两个路径不可分辨时应拒绝，不能强报高可信最高峰。
- [ ] 真实PCM至少每板30个完整标准事件与60秒连续负样本；覆盖正常簇和异常簇。没有异常PCM则先补受控采集，不以合成结果宣称解决实测245。
- [ ] 配对后的时间误差与几何真值、共同域时钟/编解码延迟分开统计；识别/TOA/距离三层资格不混用。
- [ ] 固定当前0.80诊断门限，不因新匹配算法分数量纲不同而复用0.80；新评分单独命名、冻结依据和负样本曲线。

**转入编码声包B的明确条件：**在固定几何和可确认单出声单元条件下，完成上述一次离线对比与一次实机复核后，仍出现多径分支切换导致±10mm或σ2mm失败；或单次50ms身份无法兼顾正样本检出与60秒零新正式误报；或确定的首包时限无法靠当前周期身份实现。最多两轮有根因证据的窄修复，不无限调0.80/中值/超时。B只在负责人确认该闸门失败后开启，保留A全部失败证据。
