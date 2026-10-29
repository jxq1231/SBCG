# M4.3.15 0.5m单点标定与温补结果版本

<!-- doc-status: m4.3.15-calibration-temperature-software-complete-calibration-trigger-pending; reviewed: 2026-10-27 -->

## 结论

新增 `App/Calibration/calibration.*`：

- 参考距离500mm；
- `offset_mm = 500 - raw_mm`；
- `Apply(raw)=raw+offset`；
- 标定版本递增；
- 偏移限制±1000mm，原始距离0..10000mm；
- 失败不改已有标定。

测量结果：

- `MeasurementResult_SetCalibration(offset,version)`；
- A板发布前应用标定偏移；
- 距离仍由当前温度声速计算，再应用标定；
- MRES从28字节扩为32字节，增加：
  - `calibration_version`（u16）；
  - `calibration_offset_mm`（i16）；
- B板保存A的标定后距离/版本，不重算；
- 温度版本仍随MRES传递。

## App接入

```text
Calibration_Init()
→ A发布测量时 Calibration_Get()
→ MeasurementResult_SetCalibration()
→ MeasurementResult_PublishPair()
→ 结果含标定版本/偏移
→ MRES32
→ B显示A权威距离
```

## 边界

- **标定触发尚未接板端按键/界面**；当前API可由后续板端操作调用，不能用PC命令；
- 未做0.5m实际测量，±5mm未验收；
- 未做温度变化下±2mm验收；
- MRES尺寸从28→32字节，旧28字节结果包将被视为未知长度；两板必须同镜像；
- 未烧录/未两板实测。

## 软件验证

- `Tests/Calibration/run_tests.py` O0/O2：offset/version/apply/bounds/reset；
- `Tests/Measure`：标定后距离/版本、MRES32字段往返；
- App/Sdram/UI/ChirpService/SoundEventTime回归；
- Debug/Release构建PASS。

## 下一步

1. 接板端标定触发/确认（按键或界面），不使用PC命令；
2. 0.5m实际标定/±5mm、温度变化±2mm；
3. 真实标准声/击掌分类；
4. PPS/两板/双屏硬件验收。
