# M4.3.16 c24b5ba review修复

<!-- doc-status: m4.3.16-review-p1-fixes-software-complete; reviewed: 2026-10-27 -->

## 结论

针对c24b5ba只读review的两个P1和两个P2做最小修复：

### P1#1：B重启/域重建后A恢复重发

- A跟踪B的DOMAIN_ACK序号；
- B重启后序号重置/回退 → 清`acked_result_id`和`last_result_id`，恢复当前结果重发；
- A域invalid→valid恢复时也清ack状态。

### P1#2：放宽短暂域抖动

- `TIME_DOMAIN_PEER_TIMEOUT_MS`从1500ms改为3000ms；
- A仍每500ms发REQ；
- 超过3s失域仍撤销测量，不把旧距离当正式结果显示。
- 注意：这不是无限容忍；长时间失域仍按原设计清结果。

### P2修复

- MRES32 Decode/Encode拒绝`|calibration_offset_mm|>1000`；
- 配对窗口从100ms放宽到250ms，降低两板事件发布墙钟错位导致的漏配对。

### 未修P2

- `boot_nonce`仍为token副本、domain state恒VALID：已记录，不影响当前门控；
- `Calibration_Apply`未在measurement_result使用、Apply无饱和：仍保留，距离发布另有范围检查；
- 测试仍为lwip stub/纯逻辑，真实双板/丢包需硬件。

## 验证

- BoardSync/Measure/Udp/Sdram/UI/ChirpService/SoundEventTime测试PASS；
- Debug/Release构建PASS；
- 未烧录。

## 边界

本次仍不改变“未真实同步则不产测量”的保守行为；PPS/示波器、标准声、双板仍待硬件验收。
