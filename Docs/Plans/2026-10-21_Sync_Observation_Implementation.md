# M2.2.3 Sync Observation Implementation Plan

> 执行superpowers:subagent-driven-development；沿用用户当前功能分支，按批准设计实施一个主机回放增量。每步先失败测试后实现，最终统一验证提交。

**Goal:** 30组已验收样本各调用一次原C估算器，输出来源和完整键可追溯的偏移/模型时延。

**Architecture:** Python验证JSONL和独立证据，调用原生C CLI；CLI链接App/Sync/sync_estimator.c，按索引返回原状态和整数结果。原文件及固件不变。

**Tech Stack:** Python标准库、主机GCC/C11、现有STM32 CMake验证工作流。

批准依据：用户在阅读2026-10-21_Sync_Observation_Design.md并了解作用后明确要求“实现”。基线aa504b8，初始工作区干净。

## 文件与接口

- Analysis/sync_exchange_analysis.py：完整窗口/证据检查、原生构建/调用、有界协议检查、结果输出。
- Analysis/sync_estimator_cli.c：输入适配，不包含第二份公式。
- Analysis/tests/test_sync_exchange_analysis.py：日志与真实CLI接入测试。
- Analysis/tests/test_sync_estimator_cli.py：独立CLI测试，与Python读取层分工，防止共享文件编辑。
- Docs/Milestones/M2_2_Sync_Observation.md：真实回放结果、限制、复验命令。
- 本计划、设计、Docs/Project_State.md、Docs/Project_Roadmap.md、Docs/README.md、Docs/Protocol.md、README.md：执行与准确进度。

CLI每行6个无符号十进制整数：index t1 t2 t3 t4 max_exchange_ns。index为1..UINT32_MAX；其余先按uint64解析，语义边界交给真实C函数。每行最多159字节，最后短行可无换行。输出示例：

```json
{"index":1,"status":"SYNC_ESTIMATOR_OK","offset_x2_ns":"100000","mean_path_delay_x2_ns":"200000"}
```

数学拒绝只返回index/status，进程仍正常完成批次；非法文本输出CLI_BAD_INPUT、index=null并退出2。正常输出必须索引唯一/数量完全匹配；未知状态、非整数结果、进程错误/超时不得确认估算。

Python接口：validate_capture(input_path,evidence_path,uid,max_exchange_ns)返回验证后的记录/元数据；build_cli(output_dir)构建原生程序及源码/程序哈希；run_estimator(executable,samples,max_exchange_ns)返回按索引核对的状态；main负责独占输出目录和JSONL。异常转REVIEW/非零退出，不能覆盖输入。

No CubeMX change required。无STM32资源、IRQ、DMA、缓存或硬件访问；不改App、Core、Drivers、LWIP、CMake或.ioc。

## 1. 原生CLI（独立分工）

- [x] 新建CLI空实现及test_sync_estimator_cli.py，真实编译链接现有C内核；先观察以下合法交换没有预期结果的运行失败：

```text
1 1000000000 1000150000 1000350000 1000400000 800000000
```

- [x] 实现边界解析与原状态透传；测试正负/半纳秒、大纪元、上限、窗口/倒序/负时延、失败无旧输出、非法符号/字符/溢出/长行、CRLF与EOF、批次索引回显。
- [x] 运行python -B -m unittest discover -s Analysis/tests -p test_sync_estimator_cli.py -v，保存RED/GREEN日志。

## 2. 输入与证据（主任务）

- [x] 先建读取空实现和合法最小窗口测试，验证运行断言失败。
- [x] 实现SHA256、唯一ready/summary、固定事件集合、SESSION/UID/SEQ/tick完整配对和事件先后、来源、整数范围及同域连续性。
- [x] 异常测试：缺项/重复/混身份/时间倒退、错来源/浮点时间、证据不匹配、窗口参数不一致、异常summary和重复JSON字段。完整性失败时不调用C。

## 3. 真C调用与保存

- [x] 先写真实CLI集成测试，以正负/半纳秒设计例核对字段映射；测试native输出错索引/少行/重复/未知状态及失败结果污染。
- [x] 构建仅主机CLI；每次输出新建独占目录，保存编译命令/诊断、源码和程序哈希、输入及证据哈希、窗口及域标签。
- [x] 成功逐条输出原C结果；数学拒绝不带结果字段并令整个窗口REVIEW。所有纳秒/分子为字符串，偶数中位数保留两个中间整数。
- [x] 运行python -B -m unittest discover -s Analysis/tests -v，通过后保存日志。

## 4. 真实回放与交付

- [x] 以UID001A002D:30365105:36363631，原m222时间JSONL及runtime-acceptance证据回放，窗口800000000ns；要求实际30个OK，原始数据和summary不变。
- [x] 原177项回归与新增测试通过；Debug/Release原工作流构建/布局通过；ELF和加载内容与基线一致。
- [x] 独立规格及质量复核，完成文档；明确仅已验收数据回放、hardware_verified=false，不校时或声明同步精度。
- [x] 核对diff、只暂存本增量文件，以jxq提交并按现有协作授权推送；停止本增量，不烧录。

## 实际验证

新增42项（Python25、CLI17）及原177项全部通过，共219项；最终baseline-02回放30个OK、无拒绝，hardware_verified=false。规格与质量复核提出的时序、字段污染和目录错误处理问题均经RED/GREEN修复并复验通过。两份ELF及加载内容与任务起点完全一致，414个既有非文档文件、6份原始证据保持原字节。未烧录或访问硬件。完整结果及哈希见M2_2_Sync_Observation.md；提交/推送以实际Git历史与远端核对为准。
