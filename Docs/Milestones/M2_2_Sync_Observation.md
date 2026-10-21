# M2.2.3 真实四时间戳估算链

日期：2026-10-21。状态：已获用户实施授权，真实样本回放、新增测试、原回归、两配置构建/布局和独立规格/质量复核通过。本增量只在电脑端运行，没有新烧录、硬件访问或校时。

## 一个已验证目标

将M2.2.2已验收的30组四时间戳及完整键交给真实SyncEstimator_Calculate，取得30个唯一对应的OK结果；保留PC软件和板端硬件观察点的来源、原始时间与整数分子，异常输入明确拒绝。

当前板端仍为SBCG-M2.2-IO / LOCAL/SYNC=0，已验收硬件功能见[M2.2.2记录](M2_2_Time_Exchange.md)。本次对归档数据的计算通过，不能自动推导同步精度或测距精度通过。

## 文件及调用链

- Analysis/sync_exchange_analysis.py：证据/完整窗口验证、主机编译与原生程序调用、结果保存。
- Analysis/sync_estimator_cli.c：有界文本解析及原C状态/整数结果适配，不复制估算公式。
- Analysis/tests/test_sync_exchange_analysis.py：日志、证据、原生边界及真实CLI集成测试。
- Analysis/tests/test_sync_estimator_cli.py：独立的原生CLI协议和数学状态测试。
- Docs/Plans/2026-10-21_Sync_Observation_Design.md、2026-10-21_Sync_Observation_Implementation.md：批准范围与执行清单。
- Docs/Milestones/M2_2_Sync_Observation.md：本报告。
- Docs/Project_State.md、Docs/Project_Roadmap.md、Docs/README.md、Docs/Protocol.md、README.md：准确状态和阅读入口。

```text
main → analyze_capture → validate_capture
                      → build_cli（仅电脑原生程序）
                      → run_estimator → sync_estimator_cli → SyncEstimator_Calculate
                      → observations.jsonl / report.json
```

No CubeMX change required。无新IRQ、DMA或缓存关系；不更改GPIO、TIM、ETH、NVIC、其他外设、MPU、App或生成文件。原C内核保持原字节，仅复用于电脑原生程序；原固件仍未接入运行调用。

## 数据、拒绝和输出规则

A域为电脑monotonic软件时钟，B域为当前板MAC硬件本地时钟；t1/t4来自PC_SW，t2/t3来自BOARD_HW。默认max_exchange_ns=800000000，与采集工具0.8秒等待一致；不以跨域偏差绝对值限制窗口。

输入需有联合验收PASS证据和匹配SHA256，BOOT/UID与采集会话一致；request、ACK、REPORT、four_stamp_sample按完整键一一对应。仅同域比较时间；下一请求RX还必须晚于上一ACK TX，防止相邻交换的板时钟重叠。未知样本字段、来源错误、错配、缺项、重复、倒序、越界、超窗或异常summary均使整个窗口REVIEW/非零退出，并在调用C前停止。

原生CLI每行6个无符号十进制整数：index、t1、t2、t3、t4、window，最多159字节含换行；严格检查符号、字符及uint64溢出。输出逐条回显索引和原C状态；成功结果为有符号十进制字符串，失败不带结果字段。Python核对输出数量、索引、状态及字段，不把原样本夹带的估算值或LOCK字段导出。

所有输出目录必须新建；相对路径按调用者工作目录解析。已有目录和输入不覆盖。目录被文件阻塞或不可写时仍输出结构化REVIEW并返回非零，即使无法保存报告。构建失败、原生运行错误或协议异常也不能确认样本。

逐样本结果和汇总的hardware_verified恒false；input_hardware_evidence只引用此前联合验收，不表示本程序重新验证了物理硬件。两个结果是两倍纳秒整数分子；偶数样本的中位数保留两个中间整数，不静默取整。

## 主机验证

新增42项测试全部通过：Python读取/集成25项，原生CLI17项。原177项回归全部通过，合计219项；其中原估算器13组包含1044次真实函数调用与1000个物理模型，不将调用次数重复计为测试数量。

TDD证据位于Logs/M2_2_Sync_Observation：native-red.log、reader-red.log；独立复核发现的板时间重叠与字段污染分别先以order-red.log、fields-red.log复现再修复；跨目录路径和输出目录错误路径由path-red.log、artifact-red.log先复现。最终analysis-green.log为42项通过。独立规格/质量复核分别复跑并确认修复，不存在未关闭的阻断问题。

Debug/Release沿用autodebug.py build与check_eth_layout.py，两配置构建无诊断、布局通过。各119个C编译单元保持统一HAL_ETH_USE_PTP，Analysis未进入固件编译数据库。

| 配置 | ELF SHA256（与基线完全相同） | PT_LOAD文件内容字节数 |
| --- | --- | --- |
| Debug | 807cbc9f708b3c1f2ca84f0f350cbb5efc4ab7de496178d0784f6fbd5006e165 | 67696 |
| Release | 3e3773515b4a7ab83b06b16ca7084e1fdae195a9562dcbd83f6ca532a29d2116 | 43084 |

ELF逐字节不变，因此其Flash加载段也不变；加载段拼接哈希及独立段记录见Logs/M2_2_Sync_Observation/build-verification.json。Debug构建报告为Logs/AutoDebug/20261021T061559-33430e26/report.json，Release为20261021T061559-156ce4a0/report.json。相对任务起点414个既有非文档文件和6份原始实物证据均保持原字节。无需重新Generate Code或烧录。

## 30组归档数据回放结果

输入：Logs/UDP/m222-io-1792561420906-time.jsonl。来源证据：Logs/M2_2_Time_Exchange/runtime-acceptance.json。本次最终结果：Logs/M2_2_Sync_Observation/baseline-02/report.json及observations.jsonl，status=PASS、valid=30、rejected=0、algorithm_replay_verified=true、hardware_verified=false。

| 量 | 最小值 | 中间两个值 | 最大值 |
| --- | ---: | --- | ---: |
| offset_x2_ns | -1786267208240 | -1786266914920 / -1786266897960 | -1786266465540 |
| mean_path_delay_x2_ns | 155800 | 311960 / 316220 | 754420 |

上述表格均为两倍纳秒整数分子。模型平均路径时延范围为77.900～377.210μs，中间两个值为155.980 / 158.110μs。观测偏移约为−893.133秒，主要反映电脑与板端计时参考起点不同；不以接近0为通过条件，不将变化范围当作时钟准确度或同步精度。

| 追溯项 | SHA256 |
| --- | --- |
| 输入JSONL | 3ffda33be646e91327913e68fdcf2191c983c69ac7c531c6762738bbad9ae53b |
| 原联合证据 | 4e84b8d3090696887c435bec0328c9e79f258ffeaef12ef953ff122ce912f647 |
| 本次原生程序 | 4c30003d5008364a24bd0e8c9bea201c7a7626eba3b52296b0cce44400c73feb |

C源码和头文件、CLI源码、编译命令、原生输入/输出与诊断均保存在本次独占目录。原文件及原summary没有被改写，也未为本增量重新采集数据。原始数据和程序输出按项目规则不提交Git；队友可运行已提交的合成测试，实测回放则需要对应的原始日志及联合证据。

## 复验

需要电脑原生GCC和Python标准库；编译器用于主机CLI，与ARM固件编译器用途不同。

```powershell
python -B -m unittest discover -s Analysis/tests -v
python -B Analysis/sync_exchange_analysis.py --input Logs/UDP/m222-io-1792561420906-time.jsonl --evidence Logs/M2_2_Time_Exchange/runtime-acceptance.json --uid 001A002D:30365105:36363631
```

第二条默认自动创建新的Logs/M2_2_Sync_Observation目录。正常退出0；任何输入、证据、原生协议或计算拒绝返回非零/REVIEW。现有GitHub Host checks仍只覆盖Tools/Monitor/tests，本轮新增Analysis测试由本地完整验证执行，不能把原CI标记当作新增测试已在云端运行。

本增量以主机回放验证范围提交并按已有协作授权推送；停止于M2.2.3。后续板端同步观测/控制、PPS及双板精度均须各自设计和验证。
