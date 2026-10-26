# M4.3.14 静默/瞬态测量门控

<!-- doc-status: m4.3.14-quiet-transient-gate-software-complete-hardware-pending; reviewed: 2026-10-27 -->

## 结论

新增纯C `measurement_gate` 状态机，接在A板发布测量之前：

```text
BOOT_WARMUP（1s）
  → WAIT_STANDARD
  → SEQUENCE_CAND（检测到标准序列）
  → MEASURED（成功发布）
  → SILENCE_TIMEOUT（3s无新标准序列）
  → FAULT（失域/断流）
```

规则：

- 只有分类为CHIRP的事件才进入标准序列/测量门控；
- 单次击掌/瞬态不会打开测量门；
- 启动/流会话变化/域恢复后重新预热；
- 3s没有新标准序列 → 撤销MeasurementResult；
- 共同域失效、音频会话变化 → FAULT并立即撤销；
- 无标准序列时`CanPublish=false`；
- 只有A板本地测量使用此门控；B板只显示A权威结果，不因本地静默撤销远端结果（仍受2s年龄约束）。

## App接入

- `MeasurementGate_Init`在App初始化；
- 每个CHIRP事件：`OnChirp(now,session)`；
- 标准序列形成：`OnSequence`；
- `PublishMeasurement`：要求`CanPublish`，成功发布后`OnMeasurement`；
- `ServiceDomain`：`Tick`、失域/恢复转换、取走撤销请求；
- 音频会话变化：`OnStreamInvalid` + `MeasurementResult_Invalidate`；
- 不改变TOA/配对/标准序列/分类器算法。

## 边界

- 不是60s静默硬件验收；当前状态机用3s“无新标准序列”撤销，实际60s零输出需硬件计时确认；
- 启动预热1s、静默3s是工程初值，未用真实声学标定；
- 单次击掌依赖`event_classifier`分类，分类阈值仍未真实标定；
- 断流仅以BSP音频session变化触发，未覆盖所有网络/传感器异常；
- 未烧录/未两板实测。

## 软件验证

- `Tests/Measure/run_tests.py` 新增measurement gate：预热、序列、静默超时、单CHIRP拒绝、失域FAULT、恢复预热、session变化、流失效；
- App/Sdram/UI/ChirpService/SoundEventTime/Measure回归；
- Debug/Release构建PASS。

## 下一步

1. 真实60s静默/起始瞬态/断流硬件测试；
2. 0.5m标定/温补；
3. 真实标准声/击掌阈值；
4. PPS/两板示波器验收。
