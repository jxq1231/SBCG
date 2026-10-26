# M4.3.5 标准声周期序列监控（软件原型）

<!-- doc-status: m4.3.5-sequence-monitor-pure-software; not-wired; reviewed: 2026-10-27 -->

## 结论

标准声已冻结为10次、约2s间隔的2–6kHz chirp文件。新增纯C `standard_sequence` 监控：

- 输入事件本地PTP模型时间和质量；
- 按`period=2s`、容差20ms、最小3事件、低质量/长静默重置；
- 输出是否形成周期串、事件数、最大周期误差、最小质量；
- 不依赖模板相关，不改0.80，不访问PCM/HAL/网络。

完整软件路径：

```text
标准声契约 → 事件时间 → 周期序列监控候选
```

## 边界

- **周期性不等于标准声身份**：重复击掌在相同间隔下也可能被标为候选；
- App已把每个成功TOA事件喂入监控并在状态变化时输出`$STANDARD_SEQUENCE`（带`DIAG=1,SYNC=0`）；尚未用它门控MeasurementResult/UDP。
- 标准声模板相关仍MATCH0；本模块不宣称标准识别通过；
- 未硬件/两板验证；事件质量当前尚未由真实波形质量指标提供。

## 软件验证

```powershell
python -B Tests/Detector/run_standard_sequence_tests.py
```

O0/O2均PASS：周期、抖动、低质量重置、漏一事件、单次击掌/不规则串拒绝、长静默重置。

## 下一步

1. 把序列监控接入App事件流，仅在明确标准序列/质量达标时给测量结果加门控；
2. 用波形/能量特征而非仅周期，区分标准chirp与重复击掌；
3. 两板实测同步后，再用标准声验证序列与距离；
4. 不把周期候选当课程标准识别/测距验收。
