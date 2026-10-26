# M4.3.13 权威结果MACK确认

<!-- doc-status: m4.3.13-result-ack-software-complete-hardware-pending; reviewed: 2026-10-27 -->

## 结论

在A→B MRES权威结果交换上增加16字节`MACK`确认：

- B收到并通过共同域/旧ID校验、成功发布结果后回MACK；
- MACK回显测量ID和共同域令牌；
- A只在收到同令牌、成功状态的MACK后停止该ID刷新；
- ACK丢失时A仍按500ms周期刷新，B重复收到同ID只刷新年龄/再ACK；
- B拒绝旧ID时不回成功ACK，避免A被错误确认。

## 线协议

```text
magic "MACK" 4
version      1
status       1
reserved     2
measurement_id 4
time_domain    4
```

共16字节，显式大端；magic/版本/状态/保留/非零ID/令牌校验。

## App行为

```text
A发布MeasurementResult
→ ServiceAuthoritativeResult每500ms发MRES（或ID变化立即发）
→ B收到并发布
→ B回MACK
→ A HandleMeasurementAck：域令牌一致/status OK
→ acked_result_id=ID
→ ServiceAuthoritativeResult跳过该ID
```

- 无ACK：继续重发，不把“已发送”当“已显示”；
- ACK旧令牌/非B/非OK：忽略；
- 结果ID变化：重新发送；
- 结果失效/域失效：撤销并停止。

## 观测

`$MEASURE_RESULT_STATE`增加：

```text
ACK_TX/ACK_RX/ACK_BAD
```

MACK接收路径计入结果状态日志；不单独打印高频日志。

## 边界

- MACK只证明B接收并接受了报文/结果，不证明距离正确；
- 未实现NACK/重传次数上限/链路质量统计；
- 无序列号压缩/乱序窗口，仅有ID单调约束；
- 真实UDP丢包/乱序/两板显示未验证；
- 未烧录。

## 软件验证

- `Tests/Udp/run_udp_sound_event_tests.py`：MACK编解码、本地角色拒绝、ACK接收路径；
- BoardSync/Measure/MACK相关Ack状态；
- App/Sdram/UI/ChirpService/SoundEventTime回归；
- Debug/Release构建PASS。
