# M4.3.9 权威结果周期刷新与结果计数（软件完成）

<!-- doc-status: m4.3.9-result-refresh-software-complete-hardware-pending; reviewed: 2026-10-27 -->

## 结论

在A→B结果交换基础上增加：

- A板每500ms重发当前有效`MeasurementResult`，测量ID变化时立即发送；
- B板每次收到同一ID结果刷新本地2s有效期，单包丢失不再立即导致旧值/无值；
- 新增结果报文独立计数：
  - `$MEASURE_RESULT_STATE,TX_OK,TX_ERR,RX_OK,RX_BAD`；
  - 1Hz限速输出，带`DIAG=1,LOCAL=1,SYNC=0`；
- 结果包仍走5009端口28字节MRES，B不重算距离/不改变ID。

## 边界

- 这是**周期刷新**，不是ACK/可靠传输；仍可能连续丢包；
- 没有结果序列号/重复检测/乱序检测；
- B板依赖结果包刷新，A无结果时B在2s后超龄；
- 未接真实两板/交换机；两屏≤1mm未硬件验证；
- 结果计数只证明报文收发，不证明距离正确；
- 未烧录。

## 软件验证

- Debug/Release构建PASS；
- App/Sdram/UI/ChirpService/SoundEventTime/Measure回归PASS；
- 结果线协议/远端ID测试继续PASS；
- 无新CubeMX/IRQ/DMA关系。

## 下一步

1. 静默/瞬态/温度补偿/0.5m标定；
2. PPS直接资格门控；
3. 两板实测结果刷新/同一ID/距离一致性；
4. 真实标准声/击掌阈值标定。
