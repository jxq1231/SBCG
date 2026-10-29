# M4.2.6 候选附近PCM捕获诊断实施计划

> 执行：承接M4.2.5实机证据（24任务全部LOW_CORR、最高0.773821<0.80、数字/模板一致1.0）的下一步：保存候选附近经End验证的PCM供离线宽带相关，区分“声学失真/多径”与“门限/模板/对齐”。沿用当前工作区，保护既有PPS/CubeMX改动；**不改0.80门限、不调整识别参数**。

**Goal:** 每个完整搜索任务在完成时，把候选附近 End 验证 PCM 的定界快照经 UDP 分片下传；提供离线宽带相关分析与受控重复条件分布工具，输出用于区分声学路径失真与门限/模板/对齐问题的证据，并统计静音负样本以论证“如放宽门限其误报是否可接受”。

**Architecture:** `ChirpService` 在任务缓存时刻从已验证 CPU 副本复制 `hint−2048..hint+6496` 到定界缓冲（8544 帧，NOLOAD 固定段，位于音频 DMA 之后 SRAM），完成时按新到验证块补右尾；以函数指针 sink 零拷贝交接切片，Detector 不依赖 Network。新增 `UDPChirpCapture`（lwIP RAW UDP 懒绑定板端 5008→PC 6008）分片发送 + `$CHIRP_CAPTURE` 元数据行。主机侧接收重组与离线分析是独立脚本。

**Tech Stack:** 用户自有C11、现有原生GCC测试、CMake Debug/Release、lwIP RAW UDP、Python3+numpy（scipy/matplotlib可选）。**No CubeMX change required**，无新GPIO/TIM/DMA/NVIC/Cache关系。

## 合同

- 切片跨度为名义 6496 帧（左2048+模板2400+右2048），缓冲 8544 帧满足候选早于 2048 帧时左端贴流起点的最大跨度 8543；两端按已验证范围裁剪并置 CLAMP/左/右标志，右端数据未到在完成时按新块补丁（TAIL=1 且清右裁剪），会话断开不拼接。
- 切片交接发生在任务完成分支（与是否匹配无关），每任务一次，sink 仅在调用期间借用；sink 为 NULL 时不复制、不产生任何工作。
- `UDPChirpCapture_Send` 前台同步分片发送（每片 56 字节大端头+≤512×int16），失败即停计错误，不重试；链路未就绪/无日志回调时跳过并计数；`$CHIRP_CAPTURE` 行长度预算 <192B。
- 现有计数/报告/日志契约不变；`ChirpServiceSnapshot` 新增 capture_ok/clamped/tail/miss；CHIRP_INIT 的 DIAG=2→3，追加 `CAPT=8544,L=2048,R=2048`。
- 捕获缓冲经 `.ChirpCaptureSection` 置于 0x20020000 固定段，BSS 仍限 DTCM（0x20010000 前）；新增链接断言与 check_audio/eth 布局保持 GREEN。
- 主机工具：`udp_chirp_capture.py`（6008 收包重组、`--tag` 受控条件、完整性计数）；`chirp_capture_analysis.py`（离线全对齐宽带相关、2–6 kHz 带限变体、静音负样本 FA 曲线、lag_shift）。分析脚本不得修改固件门限。

## 范围及执行步骤

1. `App/Network/udp_config.h/.c`：常量 + `udp_chirp_capture.h/.c` 分片发送（先写协议，主机测试字节向量对齐）。
2. `App/Detector/chirp_service.c/.h`：捕获缓存/补尾/交接/计数 + `ChirpService_SetCaptureSink`；`app_main.c` 接线，CMake 注册。
3. 链接：`cmake/linker/stm32f746_audio.ld` 新增 CAPTURE 区与断言；布局检查应对齐。
4. `Tests/ChirpService/test_pcm_capture.c` + `run_pcm_capture_tests.py`（O0/O2），并更新 DIAG 检查；原服务/诊断/压力/WAV 回归。
5. `Tools/Monitor/udp_chirp_capture.py` + 测试；`Analysis/chirp_capture_analysis.py` + 测试（宽/带限相关、负样本分布、SMOKE 用真实模板）。
6. 文档：`Docs/Plans/2026-10-26_Chirp_PCM_Capture/`、`Docs/Milestones/M4_2_6_Candidate_PCM_Capture.md`、`Docs/Protocol.md` 追加 M4.2.6 节。

## 验证与停止点

`python -B Tests/ChirpService/run_tests.py`；`Tests/Monitor/tests/test_udp_chirp_capture.py`；`Analysis/tests/test_chirp_capture_analysis.py`；`Tools/Monitor/check_audio_layout.py` 与 `check_eth_layout.py` 两配置；Debug/Release 构建。

实机验收（另行烧录授权）：A板 + 原接线 + 同一 20 秒 WAV；PC 运行 `udp_chirp_capture.py --out Logs/Chirp/PCM/<trial> --tag dist=..;vol=..;dir=..`，串口仍记录 SEARCH/DIAG；核对每个任务一包 PCM、`$CHIRP_CAPTURE`/`$CHIRP_SEARCH` 同 ID 对账、跳过/错误计数 0，然后离线跑分析。允许诊断阶段继续 0 匹配，只要数据有效并给出失败原因边界；不把构建/仅编译当作实物验收，不自动调参。

- [ ] 授权范围与接口合同、硬件/生成边界核对。
- [ ] 固件+链接+原生测试 GREEN（O0/O2）。
- [ ] 主机重组/离线分析脚本与测试 GREEN。
- [ ] 两配置构建、布局、回归与对账。
- [ ] 烧录授权后实机验收并写入里程碑文档。
- [ ] 仅提交本增量，保留既有用户改动，不推送。