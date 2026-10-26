# M4.3.8 两屏权威测量结果交换（软件完成）

<!-- doc-status: m4.3.8-authoritative-result-exchange-software-complete-hardware-pending; reviewed: 2026-10-27 -->

## 结论

实现A板权威结果→B板显示：

- A板在本地配对/标准序列/共同时间域/温度门控通过后发布`MeasurementResult`，分配唯一ID；
- A板通过5009端口发送28字节`MRES`结果报文：
  - 结果ID、距离mm、方向、质量、声速、温度版本、共同时间域；
- B板收到后：
  - 校验来源IP/端口、本地B角色、报文长度/魔数/版本/字段；
  - 校验共同时间域ID与本地一致；
  - 按A板原ID/距离直接发布`MeasurementResult`，不重算、不改ID；
- B板本地配对结果不再发布权威测量，只保留诊断日志；
- UI两板读取各自`MeasurementResult`，目标是同一ID/同一距离。

## 边界

- 当前只在收到A结果包时更新；**没有重传/ACK**，单包丢失时B可能保持旧结果至2s超龄；
- A/B两屏≤1mm需真实两板网络/同步验收，当前未烧录；
- 结果是A计算的权威值，不是两板独立测量一致性统计；
- 质量/时间域仍受分类器/TimeSync内部状态限制；标准声MATCH0未解决；
- 未接PPS QUALIFIED直接门控；内部锁定不等于外部精度；
- 结果包不含原始TOA/PTP时间，不能用接收时刻反推距离。

## 软件验证

- `Tests/Measure/run_tests.py` 新增28字节结果线协议与`MeasurementResult_PublishRemote`用例PASS；
- App/Sdram/UI/ChirpService/SoundEventTime/Measure/TouchGFX回归PASS；
- Debug/Release构建、audio layout PASS；未烧录。

## 下一步

1. 增加结果重传/ACK或固定周期刷新，处理单包丢失；
2. 两板实测同一ID/距离差≤1mm；
3. 静默/瞬态/温补/0.5m标定；
4. 明天交换机/PPS/标准声验收。
