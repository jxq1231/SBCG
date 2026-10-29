# M4.3.6 真实共同时间域门控（软件完成）

<!-- doc-status: m4.3.6-time-domain-gate-software-complete-hardware-pending; reviewed: 2026-10-27 -->

## 结论

已用现有`TimeSync`状态建立可撤销的共同事件时间域门控，替代App中的固定`time_domain=1`：

- A参考：`TimeSync`已配置且未FAULT；
- B跟随：还要求`offset_valid`与`internal_locked`；
- 域ID为协议常量，不比较A/B原始PTP epoch；
- 域无效时：
  - 不发送事件UDP；
  - 撤销`MeasurementResult`，UI不显示正式距离；
- 域有效时才允许事件携带`time_domain`进入配对。

实现文件：[time_domain.c](../../App/Sync/time_domain.c)、[time_domain.h](../../App/Sync/time_domain.h)。

## 边界

- 这是**内部同步状态门控**，不是PPS示波器/外部±10µs验收；`SYNC`仍为0；
- PPS资格失败通过`TimeSync` fault/清域路径间接反映；当前未直接读取PPS QUALIFIED状态（未改用户PPS文件）；
- 域有效只表示可进入配对，不证明事件身份、物理时间或距离精度；
- 未烧录/未两板实测；A/B同时未同步/断链场景仍需硬件验证；
- 事件类型/质量仍是诊断；标准声身份MATCH0未解决。

## 软件验证

- `Tests/BoardSync/run_tests.py` 新增time domain用例：A配置、A fault、B缺锁/缺offset、B锁+offset、非法role、null；
- App/UI/TouchGFX/Audio/Measure/SoundEventTime/SoundDetectorService/ChirpService/Sdram回归；
- Debug/Release构建、audio layout通过；未烧录。

## 下一步

1. 波形/质量分类：标准chirp vs 单次击掌，不能只靠周期；
2. 两屏权威结果交换：同一测量ID/同一距离≤1mm；
3. 静默/瞬态/温度补偿/0.5m标定；
4. 明天两板交换机/PPS/标准声实测。
