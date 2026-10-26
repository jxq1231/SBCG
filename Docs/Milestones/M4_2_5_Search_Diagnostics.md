# M4.2.5 失败搜索原始峰诊断

<!-- doc-status: hardware-diagnosed-recognition-pending; reviewed: 2026-10-26 -->

## 目标和当前边界

在真实播放10次但匹配0次之后，为每个完整搜索输出门限前最高相关度、峰帧和窗口分类，保持识别规则不变。本增量是诊断，不是识别修复。当前软件测试、构建及合同复核通过，独立质量复核/新固件实机验收另记；旧失败及原始数据见[真实播放记录](M4_2_Active_Chirp_Trial_2026_10_26.md)。

## 文件与调用链

| 文件 | 实际改动 |
| --- | --- |
| `App/Detector/chirp_window.c/.h` | 可选独立诊断，原Score包装共享单次统计/点积；false仍清空原匹配输出 |
| `App/Detector/chirp_service.c/.h` | raw峰与有效匹配best分离，N/V/LR/Z/I统计、一个待发标量报告、RPT/RLOST计数 |
| `App/app_main.c` | 仅CHIRP构建标识为SBCG-M4.2-SEARCHDIAG，原PPS改动不属于本提交 |
| `Tests/ChirpService/test_window_diagnostics.c` | 新增窗口分类、零相关/低RMS、边界及原输出兼容性测试 |
| `Tests/ChirpService/test_search_diagnostics.c` | 新增服务raw峰、独立参考、优先级/失效、预算和长度验证 |
| `Tests/ChirpService/run_diagnostics_tests.py` | 新测试O0/O2加入原诊断运行器 |
| `Docs/Protocol.md` | 只追加M4.2.5字段合同；既有未提交PPS文档不混入提交 |

`App_Process → AudioCapture_Process → ChirpService_Process → Search → ChirpWindow_ScoreWithDiagnostic → StoreReport/EmitReport → 原SendLine队列`。

没有新IRQ关系；相关计算和报告都在前台。没有新DMA关系；沿用SAI2 RX、DMA2 Stream7 Channel0循环半字采集，唯一Begin/End验证后才使用原CPU复制缓冲。Stage/Complete不评分或打印，不增加PCM缓冲。CubeMX、引脚、RCC、DMA、NVIC及Cache未改，当前Cache配置和内存一致性规则沿用原工程。

## 接口与行为

- `ChirpWindow_ScoreWithDiagnostic`与原接口共用一趟整数统计和点积。正方差的低RMS窗可输出raw相关，但不会产生匹配；diag=NULL时旧低RMS路径继续在开方前返回。
- 诊断区分INVALID、ZERO_VARIANCE、LOW_RMS、LOW_CORR、MATCH；原始相关恰为0仍有效，同分保留最早窗口。raw峰按double比较，原匹配best继续按原float比较。
- 范围仍hint−192..hint+64，2400点模板、0.80相关门限、RMS64、门控128/64、每轮8窗及1500ms期限不变。不重算点积或扩大搜索。
- 每任务最多一条SEARCH，成功的原CHIRP立即优先，报告后续调用发送；待发诊断不会暂停评分，每次Process仍至多一行。流失效丢弃旧报告计RLOST，不把它记作任务丢弃。
- SEARCH最长247B，追加RPT/RLOST的DIAG最长252B，均含CRLF、小于256B。RPT是交给日志回调，不保证PC已收到。
- HINT/LO/HI/BEST是PCM窗口起点；WHY为raw峰状态、MATCH为整个任务判定，不能混用。V=0时峰字段为NA。完整定义见[协议](../Protocol.md)的M4.2.5节。

## 软件验证

- RED：`Logs/Detector/M4_2_5_Search_Diagnostics/window_red/`；原评分配临时shim编译后，诊断状态断言失败。`service_red/missing-search-O0.txt`及O2对冻结旧服务实际因没有SEARCH失败，不止验证DIAG版本号。
- GREEN：窗口与服务O0/O2通过，覆盖低RMS高相关、相关0、零方差、非法参数、反相、截幅、极限帧、raw/有效匹配峰独立、边界/NA、报告优先与跨流清理、每轮8窗和一行/调用。
- 冻结旧评分每优化4096例逐字段一致；开方探针确认diag=NULL低RMS仍为0次，新诊断路径为2次开方。证据`window_green/`。
- 完整`python -B Tests/ChirpService/run_tests.py`退出0：O0/O2、WAV每171ms调度64/5次均10/10及118次有效End；四种原压力通过/超载/溢出期望保持。证据`Logs/Detector/M4_2_3_B/native-i5aofuwh/`及新诊断目录`diagnostics-jampi_51/`。
- 主代理独立运行`run_diagnostics_tests.py`，O0/O2共10个原诊断/窗口/搜索/App程序退出0，输出`diagnostics-j66dyrp2/`。
- SoundDetector、SoundDetectorService、ChirpMatcher、Audio、App、SoundEventTime、Temperature、Console、TouchGFXService九组回归退出0，证据本轮`regressions.json`。

数字WAV10/10不是新实物识别结果。新增低RMS开方、诊断汇总/格式化及UART排空均有实际成本，只能在新镜像实物窗口复验。

## 构建与内存

现有`cmake --preset Debug/Release`及`cmake --build --preset Debug/Release --parallel`退出0；音频/ETH两配置四项布局通过。仅保留原RWX LOAD链接警告，无新C警告。

| 项 | Debug | Release |
| --- | --- | --- |
| ELF SHA256 | c219af9876639c74191b3197b8553326f00db7d4e4f4d0893da728d6540b3441 | ac836a0888d22c30819da08922a595a1885f7c47da244e07b784f52aa16a566a |
| FLASH已用 | 279000B | 187196B |
| BSS相对基线 | +320B | +320B |
| ChirpService_Process静态栈 | 608B | 608B |

新待发报告80B，四任务标量区从288→512B；原PCM缓冲22528B/5312B均未变。静态栈数字不包含整个调用链或中断峰栈，不能据此宣布峰栈已实测。

## 审查和后续实物关口

独立合同复核PASS：数值、接口、任务生命周期、测试及协议均符合计划，58个既有文件的原有内容/hunk保持。独立代码质量复核PASS，无必须修复项；新增240例高直流/低方差窗口在O0/O2及-ftrapv下，与独立long-double中心化计算对照均退出0，输出`quality-numeric-fyirqjii/`。实物结果见[M4.2.5真实播放复验](M4_2_5_Active_Search_Diagnostics_2026_10_26.md)：24个完整任务全部LOW_CORR、MATCH0，最高raw相关0.773821、RMS≈2300、N=257/V=257、LR/Z/I=0、RPT24/RLOST0，资源错误全0；诊断链已实机工作，但识别仍未通过。该结果符合“诊断而非识别修复”的范围，不直接降低门限或扩范围。

新固件烧录需按根规则20节取得本版明确授权。准备沿原ST-LINK配置只烧录A板并验证复位，采集原串口，先至少60秒静默再播放同一20秒/10次文件，继续收尾；核对每任务SEARCH的峰、WHY和分类恒等式、RPT/RLOST、原业务错误及耗时。不需要改接线或启用第二板，不修改选项字节。

本轮只实施诊断；识别修复、PTP时间接口、双板测距仍未实施，旧0/10失败保留，不因诊断输出完整而宣布声学识别完成。
