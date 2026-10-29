# 调度、文件所有权与可复制agent任务书

<!-- doc-status: dispatch-instructions-for-future-authorized-work; reviewed: 2026-10-28 -->

## 1. 总指挥执行规程

项目根目录`D:/electronic_design/SBCG`。执行者先读根AGENTS、当前Git状态、本包task_plan/findings和自己领取的增量。任何交接文档中的建议不自动覆盖用户最新指令或授权。

每个任务按以下顺序推进：

```text
复核HEAD/用户改动 → 领取唯一增量 → 前置报告准确文件与资源
 → 写失败测试/保留失败证据 → 最小实现 → 相关软件测试
 → Debug/Release与布局 → 独立差异/接口复核
 → 硬件需要时提供精确镜像并取得授权 → 实物验收
 → git status/diff → 定向stage → jxq提交 → 核对提交元数据
 → 更新执行进度并停止在下一闸门
```

同一增量可分多个软件小提交，但不得把“软件通过”写成实机完成。纯模块和codec未接线时无硬件结论；集成增量有硬件要求但不能观察时，等待用户实屏或实测，不提交“功能完成”。

## 2. 文件与角色所有权

| 角色 | 可写范围 | 禁止事项 |
| --- | --- | --- |
| 负责人/集成者 | 已领取的接口、app_main、协议、CMake及集成测试 | 同时推进两个里程碑、未经复核改合同 |
| 纯算法agent | A1 tracker或B1/B2纯解码模块及对应测试 | 直接改app_main/UDP/UI/生成配置 |
| 证据工具agent | O1主机工具、分析器及其测试 | 打开硬件/播放/改固件，除非另有具体授权 |
| 只读复核者 | 无源码写权限；标准输出报告问题 | 帮实现者顺手修代码、删失败测试 |
| 实验执行者 | 指定Logs新目录，当前镜像已授权硬件流程 | 自动换镜像、改声/音量/设备/阈值等多因素 |

共享头文件和协议由负责人先冻结。允许独立纯模块与只读审查并行；不允许两个agent同时改`App/app_main.c`、`measurement_result.*`、同一测试runner、`cmake/audio-layout.cmake`或音频选择清单。

复用当前checkout并保存用户基线；不要为获取干净树而stash、reset、clean或restore。若另开隔离工作树，必须明确如何带入用户未提交功能；仅检出HEAD会漏掉当前实际镜像源码，不能据此测试或烧录。

## 3. 已存在用户修改的处理

规划基准HEAD6748b27；原有8个功能文件与2个无关项见findings。后续HEAD会前进，因此先刷新而非强行回到该提交。

- 每次记录工作树binary patch、cached binary patch、未跟踪文件及hash。任务改到用户同一文件时按小段适配，不整文件替换；最终diff分清既存与本次。
- 本轮授权只编写计划；后续用户启动某增量后，只实施该增量。已接受的总体方向不是任意新固件烧录许可。
- 原有稳定器等修改若必须随某功能提交，先判断既有用户指令是否已授权归属；若不能区分，保留原件并向用户说明具体hunk归属，不自行把整个用户快照stage。
- `Drivers/BSP/Components/lan8742/LICENSE.txt`删除、`nul`、`Docs/AI_Handoff_2026-10-27.md`不因本计划被自动提交。
- 仅使用仓库local身份jxq和既有email；禁止AI署名、push、amend、rebase、hard reset、clean。
- 定向`git add <file>`仍可能把用户原hunk一起提交，不能把它等同隔离。独立hunk用交互/补丁暂存并审查staged diff；依赖或重叠旧hunk先解决基线归属及提交许可。必须验证提交内容可从其父提交复现，不能只在含未暂存依赖的工作树跑绿测试。

## 4. 领取模板

以下提示词供负责人复制使用。各执行者只领取一个编号，不把整套路线当一次“全部实施”授权。

### G0/O1：基线与证据工具

```text
仓库D:/electronic_design/SBCG。先读根AGENTS.md、
Docs/Plans/2026-10-27_Robust_Ranging/task_plan.md、findings.md、
A_Existing_Chain.md的G0/O1、C_Acceptance.md第2/3节。

当前唯一目标：先固化G0，再按单独增量实现可追踪的结果观测或双板采集工具，
二者不要在一次未验证改动里混做。保留全部用户修改；不得改WAV、检测门限、
同步伺服、CubeMX或生成文件。默认不连接硬件、不播放、不复位、不烧录。

为领取的工具先写失败测试：固定10槽、slot0缺失、背景/额外事件、延迟尾窗、
elapsed_s/t兼容、raw/JSON不符、跨板同session/id、坏包、重复冲突、端口异常关闭。
输出manifest/ledger/integrity，未知字段和缺证据明确报错。
如果领取固件观测，严格证明原业务返回值、状态和发送次数未改变。

交付准确文件、失败前/通过后命令、保留用户改动证据、工具是否接触硬件。
完成软件增量后按AGENTS提交jxq，停止；不自动开始tracker。
```

### A1：纯跟踪器

```text
仓库D:/electronic_design/SBCG，先读AGENTS与本包A_Existing_Chain.md A1。
唯一目标：实现ranging_tracker纯C实例模块及其O0/O2行为测试，不启用实际发布。
按计划确定的RangingObservation/API与参数实现，不另造第二套中值器。
特别保证真实2秒节奏下拒绝一次后下一声不因3秒超时直接清窗，
三个互不一致离群不能强制换簇，三个独立一致新位置样本才自动接管。

允许写新tracker、Tests/Measure及用户CMake注册；sound_pair元数据应已由O1补传；
不得改app_main、UDP协议、UI、现有WAV、PTP、CubeMX或硬件。
先补失败测试并运行，再实现。共同域/session/单侧身份复用/源时刻/方向/温度/
回绕/溢出边界按计划逐项覆盖。初始一致错误簇不能被称为物理真值。

输出软件测试、Debug/Release/布局、完整diff与接口说明；提交仅此增量，
明确software-only/not wired，停止在A2a前。
```

### A2a：纯状态协议

```text
仓库D:/electronic_design/SBCG，读AGENTS与A_Existing_Chain.md A2a。
唯一目标：新增RST2 88字节/RAK2 24字节纯编解码和golden vector，先不启用发包。
以字节表为唯一布局，显式大端/有符号/截止时间/状态组合校验；
旧MRES32不改成新协议的静默降级。PROTO版本和unknown flags必须拒绝。
不新增UDP PCB、不改CubeMX/硬件、不接app_main或UI。

覆盖全部状态、空值、非法deadline、代理质量冒充身份、负delta和整数极限。
旧revision/同revision不同内容的状态性拒绝由A2b缓存/exchange测试负责，
不得往纯codec里藏接收历史。ACK确认精确revision，不续租。
提交纯codec及测试后停止；报告仍未启用/未实机。
```

### A2b：唯一集成者

```text
仓库D:/electronic_design/SBCG。读AGENTS、A_Existing_Chain.md A2b和C验收手册。
检查A1/A2a提交与接口已通过，不以旧私有中值器继续运行。
唯一目标：将tracker、显式源deadline、RST2/RAK2和两屏状态串成一条权威链。
只有本agent可写app_main/共享协议/结果缓存/UI协调/CMake集成。

软静默/单点拒绝不直接清历史；域/流硬故障立即撤值。
重复UDP/ACK/UI刷新不刷新源年龄；同revision payload不可变，旧包不能复活。
TEMP合法变化不伪造新声音。保留代理/临时/正式身份区别。
测试必须走真实app_main→tracker→codec→远程缓存路径，不能全用桩绕过。

软件、构建、布局、独立差异复核后，提供精确A/B镜像SHA与测试，
需要烧录时仅在取得当前镜像授权后执行H0/H1/H2/H3。
两屏目视/视频必须由用户或可实际观察的工具确认。
硬件未过则列未闭合，不提交feature complete。完成当前增量后停止。
```

### A4：当前WAV检测离线资格

```text
仓库D:/electronic_design/SBCG。读AGENTS、A路线A4、findings及C统计口径。
唯一目标：基于真实分板PCM，比较现过阈TOA与匹配/首径候选方法，先不改固件/WAV。
把245/254/385对应原事件与完整PCM对上；没有PCM的案例标LOG_ONLY。
保留全部失败/负样本，不事后只挑312～316簇。
训练与留出按原始录音隔离；新评分不得冒用旧0.80含义。

交付每事件响应/TOA/头或身份/质量/拒绝及原始真值误差、成本预算。
若算法有资格，再提出单一板端接入增量和镜像授权，不自动烧录。
给出是否达到B路线触发条件的证据，不凭主观“效果不好”换声。
```

### O2：为检测资格补连续真实PCM

```text
仓库D:/electronic_design/SBCG。读AGENTS、C_Acceptance.md O2，确认O1分板接收已通过。
唯一目标：诊断编译模式下由真实End通过的CPU副本，连续分片发送单声道PCM。
不是主机工具单独任务；允许C文件列出的audio_capture/pcm_stream/udp_chirp_capture/
用户CMake及最小App模式接线，复用原PCB，不改DMA/IRQ/CubeMX或正式音频。
两个CPU银行有界，4片/2ms预算，队满/缺块/End拒绝必须可追踪。
先失败用例、软件和布局，提供诊断镜像hash；当前镜像获授权后才烧录采集。
65秒连续负窗和正样本没有完整PCM覆盖就报告BLOCKED_PCM_COVERAGE，
不得把缺失段补零或只抓成功候选。正常模式恢复也需核对真实板卡镜像。
```

### B1：编码候选资产与离线资格

```text
仓库D:/electronic_design/SBCG。只有负责人已记录A路线失败闸门，才领取B1。
读AGENTS、B_Coded_Packet.md第1/2节与C验收统计口径。
唯一目标：生成候选95ms声包与20.4s文件，并实现离线参考/生产候选比较。
保留旧标准文件与选择清单；候选不自动被选为标准。

严格按帧布局、A6D1头、source/slot、CRC8、4FSK窗口合成；
两板slot必须来自解码，不能来自各自检出次数。头通过也不等于直达路径正确。
覆盖时间伸缩、截断、反相、带限、反射、强回声、旧单chirp及连续负样本。
直接穷举只作参考；必须说明可落地的生产预算和失败边界。

交付文件hash、逐样本check、golden头、冻结参数、训练/留出清单和全部失败；
不构建/连接/播放/烧录板卡，不改PTP/CubeMX。只提交候选离线增量后停止。
```

### B2/B3：先服务诊断，再映射，再接测量

```text
仓库D:/electronic_design/SBCG。B1已通过后，从B2、B3a、B3b中只领取一个编号。
读根AGENTS、本包B对应章节与C验收手册。不要把三个编号合并实现。

B2：唯一Begin/Stage/End所有权，CPU副本、互斥大缓冲、有界Process；
先纯C对照和所有分块边界测试，实机只诊断，不驱动测距。
B3a：锚点/事件显式本板epoch与rate revision；只做同代夹读，不名义48k外推；
可选200ms前台锚点需独立验证。PTP/PPS/SAI时钟配置保持不变。
B3b：SEVT v2实际48字节（旧v1是36），身份/slot/共同时间唯一配对；
完整单包身份资格和三次稳定确认分开，单包不再等待三次周期身份。

每个编号先失败测试、参考对照、构建/布局、差异复核，再报告精确镜像。
仅有该镜像明确硬件授权才进行相应诊断/验收，完成即停，不自动选择新标准资产。
```

### 实验执行者：硬件只能测已批准内容

```text
仓库D:/electronic_design/SBCG。只执行负责人指定的C文件H编号。
先核对A/B UID、镜像SHA、协议版本、WAV SHA、几何、校准/温度、计时合同。
无当前镜像授权不得换固件；无当前操作授权不得自行复位/改线/播放。
正式离线测试由手机播放，PC只USB旁观；抓PCM的PC接网轮次另标DIAGNOSTIC。

每次写新的Logs/Acceptance目录，固定10槽分母，保留raw/JSON/残片/视频/错误。
两屏同ID同状态需要实际观察，不能仅看代码或收发计数。
HOLD不能计新样本；输出expected/raw/TOA/pair/new/hold/missing与误差、年龄。
失败停在最早断点，报告，不改门限/音频/程序凑结果，不删除旧轮。
```

## 5. 独立复核者核查清单

- [ ] 只实现领取的唯一目标，没有顺手改资源/门限/测量合同。
- [ ] 用户原有修改被保留且归属明确；所有C修改有实际准确的中文说明。
- [ ] 最少一个测试在旧行为失败；新测试不是复写实现分支，而是检验实际2秒时序和可观察行为。
- [ ] 新增字段在C接口、协议字节、日志、UI和测试中语义一致。
- [ ] RAW、新测量、HOLD、缺失分母清晰，统计没有事后筛选和重复保持值作弊。
- [ ] 相同/旧revision不续租、不复活；域/会话恢复不使用旧模型；本地epoch不当成跨板共同编号。
- [ ] DMA Begin/End、CPU副本、IRQ/Cache/链接区域边界未破坏；无阻塞相关放进IRQ。
- [ ] Debug/Release/布局无新不明warning；性能以实际端到端耗时和丢块计数证明。
- [ ] 硬件证据来自当前镜像和真实观察；历史PPS读数范围、Q16与物理精度没有混淆。
- [ ] 本轮只stage准确文件，commit作者/提交者jxq，无推送/历史改写。

审查输出`PASS / PASS_WITH_LIMITATIONS / FAIL`并逐条引用文件/行号、测试和run。出现阻断则由原执行者只修该点再复核，复核者不要同时写共享文件。

## 6. 任务完成与恢复

每次交接把baseline、当前编号、已改文件、测试、镜像、授权、硬件run和未闭合写进`progress.md`及对应`Docs/Milestones/`。更新本包task_plan的执行状态只基于实际证据。

中断后先读本包三份状态文件和当前Git，不重做已完成硬件实验、不恢复到旧HEAD。未完成进程需先确认是否仍在采集，不能重复占用串口。

失败恢复优先新增小修复提交；需要退回旧镜像必须说明精确hash和功能损失并取得该操作授权。不要用清理用户工作区来回退，不改写已提交历史。
