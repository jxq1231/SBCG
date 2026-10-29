# M3.2.3 帧位置到本地PTP时间估算与回放

<!-- doc-status: completed-software-evidence; reviewed: 2026-10-23 -->

状态（2026-10-23）：**纯C内核、真实C/Fraction对照和已有实测数据留出回放通过**。这是软件增量，无新烧录或硬件验收。板端最近验收固件仍为SBCG-M3.2-ANCHOR。

## 目标与实际行为

实现AudioTimeMap_Estimate：在同一BOOT/板及SESSION范围内，用两个有效锚点估算被完整包围的NDTR连续帧坐标对应的LOCAL PTP时间。只插值，不外推；非法锚点、会话不同、倒序、过期窗口、异常率、越界及无包围均拒绝并清零输出。

每端frame_lo/frame_hi转为闭包[lo,hi+1]，保留完整一帧量化。输出estimate_ns、model_lower_ns、model_upper_ns均uint64纳秒；valid仅表示模型输入有效。最少100ms/最多10s、跨度≤500000帧、WIDTH≤4；两端平均率完整区间须落在47511.160～48470.982Hz健全性带。所有乘法只用已校验差值，最大5e17，无浮点/堆内存/HAL/128位扩展。

线性模型和公式见[已实施方案](../Plans/2026-10-23_Audio_Time_Map_Implementation.md)。目标是连续NDTR坐标N，不直接等同零基PCM样本N的物理时刻；完整消费样本N对应计数N+1，实际codec/SAI/FIFO/DMA链路仍待校准。

## 文件变更

| 文件 | 作用 |
| --- | --- |
| App/Audio/audio_time_map.h、audio_time_map.c | 无状态纯C接口、模型区间和显式失败 |
| Tests/Audio/test_audio_time_map.c、run_tests.py | 新原生程序，9程序回归；秒边界、会话、超大编号、NULL、窗口/率/量化/清零 |
| Analysis/audio_time_map_cli.c | host-only真实C桥，15个严格无符号十进制输入、6字段输出；坏字节/负号/溢出/超长行拒绝 |
| Analysis/audio_time_map_analysis.py | 既有证据SHA绑定、唯一BOOT/UID、原锚点检查、Fraction参考、固定留出、残差和不覆盖写出 |
| Analysis/tests/test_audio_time_map_analysis.py | 17项新测试，含4000例对照、75例潜在真实线性相位与率带边界、JSON重复字段和200us异常负例 |
| Docs/Plans/2026-10-23_Audio_Time_Map_Implementation.md、Plans/M3_2_3/task_plan.md、findings.md、progress.md | 用户批准范围的细化与执行记录 |
| Docs/Milestones/M3_2_3_Audio_Time_Map.md | 本轮软件验收、来源和限制 |
| README.md、Docs/README.md、Project_State.md、Project_Roadmap.md、Protocol.md、Document_Status.md、Development_Workflow.md | 更新唯一现行入口和软件/实物分层状态 |

前置文档整理已单独以 **cff9758** 提交：原有自有Markdown57份已检查；20旧方案、23实验报告标记生命周期，307本地目标存在；旧报告正文逐字保持，失败/延期/原哈希不改写。第三方依赖文档和Logs旧快照未改，不属于当前进度资料。

## 调用链、IRQ、DMA与CubeMX

主机replay → 编译的audio_time_map_cli → AudioTimeMap_Estimate。

**main/App_Process没有新增调用；新C模块未登记到固件源列表。** 尚无板端运行或新性能结论。

No new interrupt relationship。No new DMA relationship。No CubeMX change required。
原SAI2-B/DMA2 Stream7 Channel0半字循环、64KiB缓冲、IRQ4/0、ETH专用DMA及USART1 IRQ5/0均不变；缓存仍关闭，无新增维护。未改.ioc、生成代码、CMake、链接脚本、引脚、时钟或中断配置。

## 软件验证

先观察新C模块缺失导致Audio测试失败、主机分析模块缺失导致新测试失败，再完成实现。重复JSON字段未被拒绝的测试也先失败，再增加重复键检测。错误状态明确区分INVALID_ANCHOR与RANGE，参考及测试按已写接口契约校正。

- 10组原生模块回归通过，Audio包含9个程序；Tools83项、Analysis78项（原61+新增17）通过。
- 4000组固定种子合成案例由真实C执行，与独立Fraction上下界/中点/失败字段逐项一致；包含不同端点宽度、超大基数、异常路径。另75组潜在线性相位实值处于C区间、4个率带邻界状态符合预期。
- CLI验证非法token、负号、uint32/64越界、NUL、过长输入、输出错序/少行/状态/清零，以及编译/运行错误；输出目录存在时拒绝覆盖。
- 留出中点时间人为增加200us的合成负例仍可通过原±1%率健全性带，但新回放明确FAIL，不对异常重新拟合或放宽门槛。
- 沿原autodebug工作流执行Debug/Release构建及两种ETH/audio布局，均通过。两份ELF逐字节SHA保持不变；这不代表新内核已在ARM固件中运行。

独立只读C/数学阶段审查已证明端点偏导符号及整数乘积界、并另编译原生C测试，无阻断项；后续完整主机独立审查未完成（审查进程中断），主任务完成源代码/实际输出复核与全部测试。不将其记录为完整独立审查通过。

证据：Logs/Audio/M3_2_3下c-tests-red.txt、host-tests-red.txt、host-additional-red.txt、regression-01、replay-criteria.json和replay-01。原始M3.2.2数据只读，未访问开发板。

## 固定留出回放（已有M3.2.2实物日志）

只使用既有capture-serial.jsonl及其acceptance.json，SHA精确绑定；保留BOOT前5条记录，仅检查器视图从唯一新版BOOT开始。90锚点先全部经原正常窗口检查，再固定分30个互不重叠三元组(1,2,3)…(88,89,90)：两端建模，中间留出。目标坐标由中间相位区间取最近整数中点，恰半向下；不使用中间PTP时间拟合、筛选或调参。

| 检查 | 结果 |
| --- | --- |
| 工具结论 | PASS_M323_SOFTWARE_REPLAY，hardware_verified=false |
| 输入锚点/留出 | 90个有效锚点；60端点拟合、30点留出 |
| 真实C/Fraction一致 | 4000合成+30留出，共4030组 |
| 留出残差定义 | 已观察中间PTP时刻−目标坐标估计时刻，单位ns |
| 有符号残差范围 | −11327～13004ns |
| 最大绝对残差 | 13004ns，即13.004us |
| RMS残差 | 7283.876ns，即7.284us |
| 模型区间宽度 | 31223～41676ns；只含线性模型量化/取整 |
| 模型区间覆盖 | 30/30；区间外最大距离0ns |
| 预先固定经验检查 | 最大绝对残差≤100000ns，通过；非课程精度判据 |

来源SHA256：

- capture-serial.jsonl：48756b1a01b8894677fa3337e00d7825d9b0df95999d26858a2291b780aa5902
- acceptance.json：5dd6d418bd468da40baa1187a5606a2124b60f3a540826160874b681d3b32305
- Debug ELF：add1e06a892bf2a9027c28662c7a269eed5cfe2a97be2ede9862ba976d61c634
- Release ELF：bcee73517bb4085ff3ae4adeaa28947232c7dc7fcf635ed59a15117763679f1c

完整输出保留summary.json、holdout.json、真实C的holdout-input/output、synthetic-input/output、native-build.json及实际可执行文件哈希；输出目录不得覆盖。复现（本机原始Logs存在时，换成新输出目录）：

```powershell
python -B Analysis/audio_time_map_analysis.py --serial Logs/Audio/M3_2_2/runtime-01/capture-serial.jsonl --evidence Logs/Audio/M3_2_2/runtime-01/acceptance.json --output-dir Logs/Audio/M3_2_3/replay-new
python -B Tests/Audio/run_tests.py
python -B -m unittest discover -s Analysis/tests -v
```

## 完成范围与限制

这证明同一板、同一启动、当前日志窗口下的NDTR相位线性估算与实现一致。中间锚点是相位区间观测，目标整数坐标和当时真实相位仍有量化差异；留出残差不是物理声音到达误差。30/30覆盖不能证明任意未来窗口、时钟校正、故障、瞬时率变化或不同板仍受同一区间约束。

SESSION不跨BOOT唯一，caller必须先隔离BOOT/UID。无声学延迟标定、同步控制、双板、检测或距离结论；仍LOCAL/SYNC=0。本轮没有板端接入、Flash/复位或新实物测试。完成该独立软件增量后停止，下一步板端接入另行规划。
