# 2026-11-02 七项P2软件修复记录

基线：`5fd8bfa`（`jxq/ui-navigation`）。本轮只做主机软件修复、回归与构建；不烧录、不播放声源、不修改 CubeMX/生成区。物理精度与现场验收另行进行。

## 修复项与证据

| 编号 | 原问题 | 修复 | 主机证据 |
|---|---|---|---|
| P2-1 | 分类器把非上扫声音兜底成CLAP | `App/Detector/event_classifier.c` 增加短活动段、宽带差分能量、起始峰、包络衰减四项独立准入；不满足保留UNKNOWN | `Tests/Detector/run_event_classifier_tests.py` 正反例通过（1kHz纯音、截断标准音为UNKNOWN） |
| P2-2 | 移动A时B波形滑动 | `App/UI/waveform_service.c` 首次A过零锁存共同相位参考，后续按整数周期延续，不随A声学相位重置 | `Tests/Waveform/run_tests.py` moving board：A delta 11494 PCM、B delta 1 PCM |
| P2-3 | 静态单音整数周期切换导致>±2%抖动 | `FindPeriod` 返回分数帧周期（三点抛物线插值），计划使用分数周期 | 1882.35Hz半帧周期、997Hz/2kHz回归通过；fractional phase max error 104 PCM、jitter 90 PCM |
| P2-4 | 音频index故障后仍显示新鲜有效 | `App/app_main.c` 的`stream_ok`与`App/UI/status_snapshot.c`的`audio_valid`纳入`audio.index_faults==0` | `Tests/App/run_app_udp_integration.py` FAULTS注入：A `has=0/audio_valid=0`，清除后恢复健康 |
| P2-5 | 延迟旧CLP1可恢复旧标定系数结果 | `ClapRanging_ObserveCalibration` 与 `OnMessage` 共用有序标定合同高水位；RST2和CLP1都只前进，旧包不降级 | `Tests/Clap/run_tests.py` v2→旧v1拒绝、新合同撤旧值、迟到v2不降合同、v3接受 |
| P2-6 | 温度失效时A撤值、B保留，两板不一致 | A温度无效只停止新发布并冻结已发布结果；两板均按原源期限共同到期 | `Tests/Clap/run_tests.py` 两板350mm保留至14.691s同时到期，A新发布被拒 |
| P2-7 | 低温真实10m被固定343m/s提前拒绝 | `UDP_SoundEvent_Init` 粗配对`max_distance_mm` 10000→11000；最终10m由温补结果判断 | `Tests/Measure/run_tests.py` 31327000ns：旧门拒绝、11m门接受10745mm |

## 验证结果

- Debug构建：PASS，`build/Debug/SBCG.elf` sha256 `0ba191d627198fa678e969e25a6acb10e9d5e4e6ab2524498ff11bfb60a62934`；FLASH 378272B/36.07%，ITCM 14944B/91.21%，WAVEFORM_STATE 84864B/64.75%，RAM 317920B/97.02%。
- Release构建：PASS，`build/Release/SBCG.elf` sha256 `5c584292af3bf135bb092926461ea7a98c38fe7431332c82c8f59c99d2606b93`；FLASH 241372B/23.02%。
- 标准35组`Tests/*/run_tests.py`：31通过；4组既存无关失败（`ChirpMatcher`资产期望、`Sdram`应用stub、`SoundDetector`初始化器、`SoundDetectorService`日志断言），未放宽断言。
- 专项：`Tests/App/run_app_udp_integration.py` 100检查（75 baseline + 25 advanced）通过；`Tests/TouchGFXView` 四组、`Tests/Waveform/check_layout.py`、`Tests/UI/run_snapshots.py`、`Tests/ChirpService/run_diagnostics_tests.py` 均通过。
- 未实机验证：真实纯音/截断标准音/拍掌隔离、移动A/B相位、非整数周期单音、两板标定与温度失效、低温长距离。软件测试不能替代物理精度结论。

## 审查发现但未在本轮修改

- `App/Measure/measurement_result.c:459` 的`(void)formal`使formal恒false；`App/app_main.c:857`因此永不下发FORMAL，跟踪态UI只能显示CONFIRMING。属既有证据等级设计，需单独任务决定是否升级为有效正式结果。
- `App/app_main.c:585`丢弃`MeasurementGate_TakeInvalidation()`的3s撤值事件；gate未接入发布撤值路径，需与显示/超时语义一起评审。
- 标准页无数值质量/剔除标注，仅`CONFIRMING`；满足课程“测量质量指示”仍需显示层改动。
- 标定仅RAM、无掉电保存/多点；DHT11 1°C量化@2m约3.5mm，与±2mm温度补偿指标无余量。
- 以上未改原因：超出用户本轮七项P2范围，且涉及证据等级、显示语义或硬件方案选择，需单独确认。

## 提交

本轮软件检查点定向提交，作者/提交者`jxq`；不push、不改写历史、不包含用户受保护的LICENSE删除、`Analysis/`、旧`Docs/`与`nul`文件。
