# M4.2.5 失败搜索诊断实施计划

> 执行：使用测试先行与独立合同/代码复核；本次用户“实施”承接真实播放报告的诊断方案。沿用当前工作区，保护既有PPS/CubeMX改动，不创建缺失生成配置的替代工程。

**Goal:** 每个完整搜索任务有界输出最高原始相关度、峰帧、门控提示及搜索边界，保持匹配结果、门限、搜索范围和次数预算不变。

**Architecture:** 原Score包装同一单窗计算实现；新增可选诊断输出，独立于只在匹配成功时有效的ChirpMatch。任务单独聚合raw峰与窗口统计，原匹配best保留原float比较逻辑。前台每次最多一行，成功CHIRP优先，诊断延后输出。

**Tech Stack:** 用户自有C11、现有原生GCC测试、CMake Debug/Release，现有串口回调。No CubeMX change required，无新GPIO/TIM/DMA/NVIC/Cache关系。

## 合同

新增`ChirpWindow_ScoreWithDiagnostic`，参数在原Score后增加可NULL的`ChirpWindowDiagnostic *diag`；原Score仅调用同一实现并传NULL。诊断状态INVALID/ZERO_VARIANCE/LOW_RMS/LOW_CORR/MATCH；输出double correlation、correlation_valid、uint16 RMS、inverted/clipped。有效正方差窗口即使低RMS也可有raw相关度，但不得因此填充匹配out；false时原out仍清零。diag/out不得与输入或彼此重叠；diag=NULL不增加旧低RMS路径的开方。

保留同一趟整数sum/squares/dot，不再次点积；raw最高相关度按double比较，原有效匹配仍按旧float比较和先到优先规则。每次最多8窗，完整范围仍hint−192..hint+64（帧0自然截断），不更改2400点模板、128/64门控、0.80相关度、64 RMS、0.5秒冷却或1500ms期限。

每任务保存hint、原搜索首尾、ID、raw峰和计数。N为实际评分次数，V为正方差可评分窗，LR为V中低RMS窗，Z零方差、I非法；N=V+Z+I，LR<=V。raw峰与有效匹配best独立，WHY为raw峰状态，不冒充整个任务唯一失败原因；MATCH为任务是否有有效匹配。无raw峰时BEST/CORR_PPM/RMS/CLIP/INV/EDGE为NA，WHY=NO_VALID。

新串口行：`$CHIRP_SEARCH,ID=...,SESSION=...,HINT=...,LO=...,HI=...,BEST=...,CORR_PPM=...,RMS=...,CLIP=...,INV=...,EDGE=...,MATCH=...,WHY=...,N=...,V=...,LR=...,Z=...,I=...`。时间位置均为本会话绝对PCM帧的窗口起点，不是声学时刻；EDGE为1左/2右/0内部。ID为候选累计计数，饱和后不得单独用作唯一身份。含CRLF最坏247字节，小于256。

一个完成诊断槽足够：最短搜索65窗，大于单轮8窗预算，下一任务不可能在紧邻轮次完成。成功时先发原CHIRP，下轮继续工作并发待发诊断；失败完整完成可当轮发诊断。流失效清除旧待发报告，累计RLOST；防御性覆盖同样计RLOST，不影响测量结果。CHIRP_DIAG追加RPT/RLOST（交回调次数/诊断失效丢弃），最坏252字节；不改原状态周期。Stage/Complete/ISR不打印，DHT门控仍由原调度保证。

## 范围及执行步骤

1. `Tests/ChirpService/test_window_diagnostics.c`、`test_search_diagnostics.c`（新增）及`run_diagnostics_tests.py`：先写失败测试，覆盖低RMS/低相关/零方差/无效/反相/截幅，原Score false清零；真实服务成功/失败任务、边界、高帧、延迟报告、失效、预算和长度。旧实现必须实际变红。
2. `App/Detector/chirp_window.c/.h`：实现上述可选诊断接口，保持原统计/匹配数学；中文注释描述单位、空指针、所有权和失败行为。
3. `App/Detector/chirp_service.c/.h`：实现独立raw峰、完成报告槽、RPT/RLOST及一行调度，不改匹配序号/结果和借用归还事务；最坏行长有测试/静态约束。
4. `App/app_main.c`仅CHIRP分支FW改为`SBCG-M4.2-SEARCHDIAG`；既有用户PPS改动保持，提交时只暂存版本差异。
5. 原生O0/O2新增测试、数字WAV10/10、原压力及Detector/Audio/App/时间/UI回归；Debug/Release构建和音频/ETH四项布局。独立合同复核后再做代码质量复核。
6. 文档更新：当前入口、状态及本轮报告；`Docs/Protocol.md`仅追加本接口，既有未提交PPS文字不混入提交。新固件准备完成后按根规则20节另行确认烧录/真实播放验收；不把构建当实测，不自动修复匹配。

## 验证与停止点

`python -B Tests/ChirpService/run_tests.py`；相关SoundDetector/SoundDetectorService/ChirpMatcher/Audio/App/SoundEventTime/TouchGFXService运行器；`cmake --build --preset Debug --parallel`及Release；`Tools/Monitor/check_audio_layout.py`与`check_eth_layout.py`两配置。

最终实机仅A、原接线、同一20秒文件，先静默再播放，核对每个完整任务诊断、原音频/搜索/日志业务和评分耗时。允许诊断阶段继续0匹配，只要数据有效且明确失败原因边界；不是识别修复完成。发生行为偏差、内存/日志预算问题时先修诊断，不能调整识别参数掩盖。

- [x] 授权范围与接口合同、硬件/生成边界核对。
- [x] RED→GREEN、新原生及数字WAV回归。
- [x] 两配置构建/布局、独立合同与质量复核。
- [x] 具体镜像准备后取得烧录授权并验收：`c219af98…` 已烧录A板并完成自动标准WAV复验；诊断链实机工作，24任务全部LOW_CORR、MATCH0、最高raw相关0.773821<0.80，资源错误0，识别仍未通过。详见[M4.2.5真实播放复验](../../Milestones/M4_2_5_Active_Search_Diagnostics_2026_10_26.md)。
- [x] 仅提交本增量，保留既有用户改动，不推送；提交 `53c2555`。
