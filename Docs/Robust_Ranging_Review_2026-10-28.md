# 双板稳健测距实施审查报告

<!-- doc-status: review-findings-open; reviewed: 2026-10-28; reviewed-head: 9b6af2197e9b38ec262063219f8c3eab01dfbd42 -->

**VERDICT：FAIL。** 已有模块实现和部分应用集成，但存在结果生命周期、ID/revision、确认链和证据归槽方面的确定缺陷。不能认定 A2b 软件闭环通过，也不能直接认定 A3 或声学精度通过。

本报告记录已完成的只读审查。将报告归档不代表修复了其中问题；后续提交必须逐项提供关闭证据。本文不是烧录、硬件操作或后续阶段的授权。

## 1. 审查基线、范围与方法

| 项目 | 内容 |
| --- | --- |
| 仓库 | `D:/electronic_design/SBCG` |
| 审查日期 | 2026-10-28，Asia/Shanghai |
| 计划起点 | `64f0837b952d9230234864fc45572eac9a6d7850` |
| 被审查 HEAD | `9b6af2197e9b38ec262063219f8c3eab01dfbd42` |
| 依据 | 根目录 AGENTS.md，以及 Robust Ranging 计划包七份文件 |
| 当前本地 Release ELF SHA256 | `97d726fc0365b7382e935cb648080ac658e40063d6de9f302f03d305a697aed1` |
| 本地 ELF 的证据等级 | 本地产物匹配，不是 A/B Flash 回读或新镜像实机通过 |

计划入口：[task_plan.md](Plans/2026-10-27_Robust_Ranging/task_plan.md)。交叉核对了 findings、A/B 路线、C 验收手册、D 调度要求和 progress。

审查包含提交差异、应用调用链、纯模块、网络收发、UI、主机分析、链接布局、G0 和 ITCM 历史证据。并行复核分别覆盖跟踪器、协议/UI、基线/ITCM/分析器。代码位置均针对上述被审查 HEAD；后续修改后应重新定位。

审查期间未修改源码、文档或 Git 暂存区，未连接硬件、烧录、复位或播放声音。按用户列出的测试命令运行主机检查，测试产生了 Logs 等派生产物。本次单独归档报告只新增本文档。

## 2. 计划执行总结

| 增量 | 实际状态 | 可以证明的范围与限制 |
| --- | --- | --- |
| G0 | 基线证据已建立，正式观测合同未全部闭合 | 哈希、补丁、历史报告镜像可追溯；几何仍含用户报告，正式计时口径未确定 |
| O1 | 部分软件实现 | 已增加诊断、pair 元数据和槽账分析；分析关联存在缺陷，计划中的双板采集工具未完成 |
| A1 | 纯模块实现，现有测试通过 | 普通确认/HOLD/重获路径有软件证据；候选跨度和恢复去重等仍有缺陷 |
| A2a | 纯 codec 实现，现有测试通过 | 固定字节布局和基本字段检查有证据；无值状态 ACK 合同不完整 |
| A2b | 软件接线已提交，软件正确性未通过，硬件 NOT_TESTED | 缺真实端到端测试，存在本报告阻断问题 |
| ITCM 子增量 | 有限探针通过 | A 起始 256B、B 起始 1024B；生产状态区未覆盖，不能外推全区或任意板 |
| O2 / A3 / A4 / B / F | 未发现启动这些阶段的提交证据 | 不因规划文本或主机测试通过认定其实施或验收完成 |

G0/O1/A1/A2a/A2b 的提交顺序基本符合依赖顺序，ITCM 探针是中间资源验证子增量；顺序正确不等于各阶段的退出条件已经满足。

## 3. BLOCKING findings

### R01 — P1：源时间过期与硬失效没有进入展示生命周期

**位置：** `App/Measure/measurement_result.c:441`；`App/UI/status_snapshot.c:64`；`App/UI/status_view.c:27,59`；`App/app_main.c:423`。

**证据：** 应用没有调用 `RangingTracker_Tick`。`GetPresentation` 直接复制缓存；UI 将 `presentation.has_value` 作为数字有效性。页面每次刷新都更新页面捕获时间，格式化时检查的是页面年龄，而不是声音源年龄。B 接收 RST2 时检查域，但已缓存 presentation 没有持续资格检查；`ServiceDomain` 仅为 A 调用展示撤销。

**影响：** 最后一声之后超过 6.5 秒，A/B 旧数字和原 fresh 标志仍可能保留；B 失去共同域后，也可能同时显示失同步状态和残留距离。应用传给 tracker 的 `now_common_ns` 还是 pair 的源时刻，不能用它证明当前真实年龄。

**关闭要求：** 由前台以实际共同时间驱动过期；在域无效、读时失败、健康流失效、温度资格失效时撤销相应资格。重复 UDP、UI 刷新和合法温度更新不得延长声音源期限。主机覆盖最后一声后 2.5/6.5 秒边界及硬失效；之后实机检查两屏撤值。

### R02 — P1：临时与确认路径的两套计数器造成 ID 重用和 revision 倒退

**位置：** `App/Measure/measurement_result.c:392,410,431,328`。

**证据：** 临时显示使用外层计数器，确认显示复制 tracker 的计数器。初始化后，三个普通独立声音在 0/2000/4000ms 产生的 `(measurement_id, state_revision)` 为 `(1,1) → (2,2) → (1,4)`。接着调用 `InvalidatePresentation`，外层生成 FAULT revision=3；首次恢复候选还可能复用 revision=4，但内容已经改变。

**影响：** 独立声音重用 ID；B 持有 revision=4 时会拒绝 revision=3 的故障撤值；同 revision 的内容也不再唯一。

**关闭要求：** 在唯一权威层分配 ID/revision。覆盖临时→确认→拒绝→HOLD→FAULT→恢复，断言不回退、不重用、同 revision 内容不变，以及计数耗尽时停止发布。

### R03 — P1：24 字节分派冲突使 RAK2 接收分支不可达

**位置：** `App/Network/udp_sound_event.c:117,151`；`App/Network/domain_wire.h:14`；`App/Network/ranging_status_wire.h:15`。

**证据：** DOMAIN 与 RAK2 都是 24 字节。接收函数先按长度进入 DOMAIN 分支，解码或报错后直接返回；合法 RAK2 因 DOMAIN magic 不符被拒绝，无法到达新 ACK 分支。

**影响：** A 无法处理任何实际 RAK2，持续重传，不能声称确认闭环成立。

**关闭要求：** 使用 magic、版本和长度共同分派。测试必须经过真实 UDP 接收回调，同时覆盖 DOMAIN 与 RAK2，而不是只调用纯 codec。

### R04 — P1：接收缓存缺少启动上下文、payload 一致性和截止时刻保护

**位置：** `App/Measure/measurement_result.c:446`。

**证据：** 只按 domain/revision 比较，未核对 authority epoch；同 revision 直接返回成功，不比较 payload；新 revision 直接覆盖，未防止同 measurement ID 的 deadline 延长。应用上下文还由 pair 直接构造，epoch 固定为 1，而非独立健康协调者确认。

**影响：** 内容冲突可被成功确认，同一源证据可能被续租；同 TOKEN 重启后可能拒绝新状态或混淆启动上下文。

**关闭要求：** 明确上下文建立/恢复和 fail-closed 规则。测试同 revision 篡改、同 ID 延长期限、旧包过期后重放、A/B 单独重启、TOKEN 复用及 DOMAIN=0 后恢复。不能将 A/B 本地 epoch 数字相等作为同步证据。

### R05 — P1：旧门控仍挡在 tracker 之前，旧结果协议继续并行运行

**位置：** `App/app_main.c:576,595,1230`；`App/Measure/measurement_gate.c:32,80`。

**证据：** `PublishTrackedPair` 位于旧 formal/provisional 判断之后。SILENCE 状态不在临时旁路允许列表，单次 OnChirp 不会退出 SILENCE，因此重播前两声可能被挡掉，到序列重新形成后才进入 tracker。随后仍调用旧 `PublishPair`；前台同时执行旧 MRES 发送和新 RST2 发送。

**影响：** 没有实现计划要求的预先收集独立证据；展示、旧中值器、日志、门控和两套网络协议持有不同权威语义。当前 UI 只读新 presentation，不能据此声称 MRES 直接覆盖新 UI。

**关闭要求：** 将合格 pair 的跟踪与正式发布资格分离，明确唯一权威结果及兼容接口。验证静默后重播、漏一声 4 秒恢复、序列 RESET、真实移动及同域断流恢复；新协议启用后旧 MRES 不应继续发布另一套测量状态。

### R06 — P2：新展示路径的温补、标定和 revision 不一致

**位置：** `App/Measure/measurement_result.c:150,323,361,388,396`；`App/Measure/ranging_tracker.c:294`。

**证据：** 确认约 315mm 后，重复同一 pair 并把声速从 343000 改成 350000、温度版本从 9 改成 10，转换可变成约 321mm，但 Push 拒绝重复源，不推进 revision；包装层仍以原 revision 输出新内容。没有新 pair 时 presentation 不重算。另设标定 +20mm 后，临时数字应用偏移，第三声确认却复制未标定 tracker 距离，约 335mm 变回 315mm。标定变化仅重置旧中值器。

**影响：** A/B 可因同 revision 缓存规则显示不同温补结果；临时→确认出现标定跳变。

**关闭要求：** 合法温补/标定变化按同一源证据重算，保留源 ID 和期限，权威内容变化推进 revision；tracked 路径必须验证版本、偏移及拒绝重复源的组合。

### R07 — P2：候选簇跨度和硬恢复后的去重不符合冻结合同

**位置：** `App/Measure/ranging_tracker.c:168,374,102,129,317`。

**证据：** 三点只分别与第一点比较。900000、920000、880000ns 在独立 2 秒节奏下可以确认，实际最大减最小为 40000ns，超过配置的 20000ns。另消费身份 1～20 后硬失效并恢复同上下文，源时间保护被清空，最近 16 项只保留 5～20；原身份 1、2、3 可以重新被消费，没有持久序号高水位。

**影响：** 不一致新簇可能被确认；已消费的旧声音可在恢复后重新组成确认结果。

**关闭要求：** 检查完整候选最大/最小值；同启动上下文保留消费高水位。测试两侧极值、恢复后旧身份重放、同 A/不同 B、同 B/不同 A，以及旧 session 包只拒绝而不重置当前历史。

### R08 — P2：槽账按打印时间关联，延迟和 HOLD 可污染测量统计

**位置：** `Analysis/acoustic_acceptance_analysis.py:175,235`。

**证据：** pair、decision、result、UI 各自按日志时间就近归槽，再选第一条记录，没有通过 board/session/源键/ID/revision 关联。已有尾窗样例中，12.1s 的 pair 属于 slot1，13.8s 的延迟 result 却进入 slot2。同 ID 的 VALID/HOLD 快照可以计入多个槽的 slots_with_result；不相关 B UI ID 也可挂到该槽，未形成关联错误。

**影响：** 工具不能证明逐声出数、首显延迟或两屏一致；现有四项测试通过不能使这些统计可信。

**关闭要求：** 先关联源身份和权威结果，再归属固定 WAV 槽。新测量、临时、保持、缺失分开计数；尾窗结果仍归原声槽，跨板时钟对齐和参考不确定度必须显式处理。

### R09 — P2：ACK 匹配、重同步和失败退避仍未闭合

**位置：** `App/Network/ranging_status_wire.c:124`；`App/app_main.c:670,684,738,751`。

**证据：** 成功接收无值 ACQUIRING/STALE/FAULT 时 ID 应为零，但 ACK codec 拒绝 ACK_OK+ID0。即使修复 R03，ACK handler 仍只验证 domain，未匹配待确认 epoch/revision/measurement ID。确认后 A 停止该 revision 发送，B 重启没有对应状态重同步路径。发送失败又只在成功后才记录时间，会每个主循环重试。

**影响：** 撤销状态不能正常确认；错误 ACK 可干扰待确认状态；B 重启可能收不到当前状态；pbuf/发送失败时会丢失 500ms 退避。

**关闭要求：** 完整元组确认和明确重同步机制；记录失败尝试。测试无值状态 ACK、错 ACK、延迟 ACK、10 秒丢 ACK、B 重启和持续发送失败，证明不误确认、不续租、不高频重试。

## 4. NON-BLOCKING findings

| 编号 | 位置 | 问题及边界 |
| --- | --- | --- |
| N01 | `App/Measure/ranging_tracker.c:38` | 乘法未检查溢出。delta=2^61、声速343000的结构合法极端输入可使乘积回绕并输出0mm；当前上游配对量程降低实际暴露风险，但纯模块仍需边界保护 |
| N02 | `App/Measure/ranging_tracker.c:353` | 已确认+5mm后首个−5mm可直接走inlier接受，不经独立换向确认；此例低于当前正式量程，应补符号边界测试 |
| N03 | `Analysis/acoustic_acceptance_analysis.py:317` | 不可用manifest产生空ledger后访问ledger[0]，抛IndexError，来不及写完整integrity/summary；应输出证据无效而非崩溃 |
| N04 | `Docs/Plans/2026-10-27_Robust_Ranging/task_plan.md:31`及progress | 计划仍保留所有功能未实施的历史状态，应通过有版本的执行记录区分冻结计划与当前进度，不能把旧说明当当前结论 |
| N05 | `Tests/ChirpService/run_tests.py` | 既有capture构建缺少event_classifier.c，链接失败；是已有测试基础设施问题，不能声称全仓回归全绿 |

## 5. 实际执行的检查与结果

以下结果来自本次审查，不是修复后的验证，也不是本次文档归档重新执行的硬件测试。

| 命令 | 结果 | 能证明什么 |
| --- | --- | --- |
| `python -B Tests/Measure/run_tests.py` | PASS；8组在O0/O2下通过 | 现有纯模块用例；不覆盖全部报告反例 |
| `python -B Tests/App/run_tests.py` | PASS | 现有应用回归 |
| `python -B Tests/App/run_app_udp_integration.py` | PASS | 主要为旧MRES/MACK边界，不是新RST2端到端 |
| `python -B Tests/Udp/run_udp_sound_event_tests.py` | PASS | 现有UDP用例，没有覆盖24字节RAK2分派冲突 |
| `python -B Tests/UI/run_tests.py` | PASS | 现有UI用例，不是新状态完整矩阵 |
| `python -B Tests/TouchGFXService/run_tests.py` | PASS | 框架服务和已有显示所有权用例 |
| `python -B Tests/TouchGFXView/run_tests.py` | PASS | 视图绑定等现有用例，不是实屏 |
| `python -B Analysis/tests/test_acoustic_acceptance_analysis.py` | PASS；4项 | 固定槽等已有用例，不代表源身份关联正确 |
| `cmake --build --preset Debug --parallel` | 成功；ninja: no work to do | 已有Debug产物处于增量构建最新状态；未强制全量重编译 |
| `cmake --build --preset Release --parallel` | 成功；ninja: no work to do | 已有Release产物处于增量构建最新状态；未强制全量重编译 |
| `python -B Tools/Monitor/check_audio_layout.py` | AUDIO_LAYOUT_OK | Debug主机布局检查 |
| `python -B Tools/Monitor/check_audio_layout.py --preset Release` | AUDIO_LAYOUT_OK | Release主机布局检查 |
| `python -B Tests/ChirpService/run_tests.py` | FAIL；退出码1 | 最终capture链接缺少EventClassifier_ClassifyStrided，未完成整个runner |
| `git diff --check 64f0837..9b6af21` | PASS | 提交差异空白检查 |
| Release ELF SHA256核对 | 与报告基线完全一致 | 仅本地文件匹配，不是板卡Flash验证 |

ChirpService的实际错误：

```text
undefined reference to `EventClassifier_ClassifyStrided'
collect2.exe: error: ld returned 1 exit status
```

## 6. TEST GAPS：主机可测与必须实机

`Tests/App/test_app_udp_integration.c:92-95` 丢弃 RST2/RAK2 消费者注册，两个发送桩恒返回false；这类绿灯不能代替新链路。纯codec的golden向量也不能发现UDP分派、缓存、ACK和UI接线问题。

| 测试 | 主机可测部分 | 必须实机部分 |
| --- | --- | --- |
| A→tracker→RST2→B | 真实模块、实际接收回调、codec和B缓存；仅HAL/LwIP底层边界使用桩 | 实际交换机链路、包字节与计数 |
| ID/revision | 临时/确认/拒绝/FAULT/恢复单调性，同revision内容一致 | 双屏与串口对应 |
| 独立证据 | 同源包三次仅计一次，同A/同B复用、旧session、高水位 | 真实重复包与重启运行 |
| 截止与失效 | 源期限、当前读时失败、DOMAIN0、同ID不延长期限 | 停声、断网、失域后两屏撤值 |
| tracker节奏 | 2s、接收±200ms、漏一声4s、候选超接收间隔、完整跨度、回绕和溢出 | 实际调度与处理延迟 |
| 温补/标定 | 同源重算、版本、期限保持、偏移一致 | 实际温度变化、两屏一致；标定功能不能据单元测试宣称现场通过 |
| ACK/恢复 | 错ACK、10秒丢ACK、重发失败退避、分别重启、TOKEN复用 | 抓包、物理重启及恢复后的双屏 |
| UI状态 | ACQUIRING/TRACKING/HOLD/REACQUIRE/STALE/FAULT文本、无值不显示0m | 实屏可读、同ID/状态/距离/方向，无闪烁撕裂 |
| 槽账 | 固定10槽、slot0缺失、延迟结果、重复HOLD、跨板冲突、不同采集起点 | 有播放参考的真实日志和双屏录像 |
| 声学能力 | 软件回放只能验证算法和数据处理 | 标准声、静默/瞬态、物理TOA、方向、误差和重复性 |

## 7. HARDWARE / NOT_TESTED 与 ITCM 限制

当前A2b镜像没有以下新证据：A/B部署后运行、两屏同ID/状态/距离/方向、RST2/RAK2抓包、HOLD/REACQUIRE/STALE/FAULT实屏、ACK丢失、分别重启、TOKEN复用、静默和瞬态恢复。

已有ITCM历史探针记录：

```text
A：ADDR=00000000, BYTES=256,  ERRORS=0, CRC=35F978AD, PASS
B：ADDR=00000000, BYTES=1024, ERRORS=0, CRC=5E581E48, PASS
```

本地Release map显示 `.ItcmProbe` 为0x00000000起的0x400字节，`.ItcmState` 从0x00000400开始，到0x00000660之前结束。生产状态区不在上述两个已测试区间内。

用户链接扩展把ITCM状态设为NOLOAD，`MeasurementResult_Init`显式初始化tracker/presentation；布局检查未发现与音频DMA、普通RAM堆栈等重叠。ITCM状态是CPU-only，不得用于DMA。这些是静态布局/初始化证据，不能代替实际状态区运行。

A板1KiB、全16KiB和A/B生产状态区验证仍为NOT_TESTED；全16KiB并非自动新增验收任务，后续应按实际使用区间和资源风险确定最小验证范围。已有PPS示波器结论保持原镜像、原测量范围，不安排本轮复测，也不外推为当前A2b镜像已通过外部精度验收。

## 8. COMMIT / SCOPE 与 G0 可复现性

| 提交 | 内容 | 范围判断 |
| --- | --- | --- |
| `91e7fe8` | O1观测与用户稳定器/临时显示基线hunk | 提交正文明确包含基线hunk；并非仅相对原HEAD增加被动观测 |
| `ff5f276` | A1纯tracker | 记录software-only、未接线 |
| `49180e4` | A2a RST2/RAK2 codec | 记录未启用发送 |
| `e85aaa6` | 256B ITCM探针 | 有A板有限证据 |
| `5602d2b` | 1KiB探针 | 有B板有限证据 |
| `9b6af21` | A2b软件接线和ITCM状态迁移 | 提交正文明确硬件未验收；仍有本文软件阻断 |

六个提交的作者、提交者均为jxq，未发现禁止的署名。差异未包含`.ioc`、CubeMX生成初始化、生成链接脚本或生成构建配置；链接变化位于用户扩展。

用户音频生成器、WAV、manifest/selection、LICENSE删除、nul和交接文档没有混入上述提交。审查结束暂存区为空，受保护的工作区状态保持。

G0位于`Logs/Acceptance/robust-ranging-G0-20261028-01/`。二进制用户补丁及空index补丁的SHA256与baseline.json一致；保留的生成器、manifest、WAV、selection和模板哈希仍匹配。历史已烧镜像明确标为NOT_READ_BACK，未把本地ELF冒充Flash回读。几何和2秒口径的未定事项仍需按原证据等级处理。

O1包含用户已授权基线hunk的记录可见提交正文及`A1-A2a-software-evidence.md:14-16`；本次没有独立重建授权对话原文。未发现剩余未提交C源码依赖。普通checkout不会自动包含未提交实验音频选择，完整实验还需冻结补丁和哈希。审查未逐提交建立干净目录重构建，不能声称所有父提交构建已独立复现。

## 9. CLAIMS：允许和禁止的结论

**可以说：** 已有对应模块和部分接线；列出的现有主机测试通过；Debug/Release增量构建命令成功；本地Release哈希匹配；主机布局检查通过；A/B有限ITCM探针有PASS记录。

**不能说：** A2b软件闭环通过、当前A/B镜像或两屏验收通过、旧结果不会续租、重启/移动恢复可靠、标准声身份或物理精度通过，或有限探针证明任意ITCM地址/任意板通过。

当前PROXY分数和三声一致性仍不代表物理身份、直达路径或测距精度。raw、温补、标定、滤波、临时、确认、正式、HOLD和缺失必须分别报告；不能用保持值重复显示凑独立测量标准差。

## 10. RECOMMENDED NEXT INCREMENT

**唯一下一目标：A2b主机闭环修复与验收。**

以真实模块串起A→tracker→RST2→UDP接收→B缓存→UI文本。先写能复现R01～R09的失败用例，再在A2b范围逐项关闭；每个独立已验证修复定向提交，不将未完成的整组工作记为已验收。分析器R08至少在使用它生成正式验收统计前关闭。

软件退出条件：

- [ ] ID/revision单一权威、单调、同revision内容不变。
- [ ] RAK2真实接收可达，无值状态可确认，错/旧ACK不会误确认。
- [ ] 同源重复、温补和状态更新不续租；真实共同时间驱动过期。
- [ ] 失域、断流、读时失败及时撤值，显式恢复后不复活旧包。
- [ ] 静默重播、漏声和移动重新确认不会被旧门控额外阻塞。
- [ ] 新旧协议不存在两套同时发布的测量权威。
- [ ] tracker、温补/标定、缓存和UI矩阵新增测试通过；已有回归失败单独说明。
- [ ] Debug/Release与布局通过，检查定向提交范围和jxq元数据。

软件闭环通过后，才单独提供具体镜像、SHA256、差异和结果，取得匹配授权再进行A2b双板实机验收。本文不启动A3、A4、B或F，不改0.80、不换WAV、不改同步伺服或CubeMX资源。
