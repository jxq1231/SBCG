# M4.3.18 c24b5ba review P2修复

<!-- doc-status: m4.3.18-result-p2-fixes-software-complete; reviewed: 2026-10-27 -->

## 结论

针对review的P2做两项可主机验证的修复：

### P2#1 发送失败退避

- `ServiceAuthoritativeResult`在调用`UDPSoundEvent_SendResult`后**无论成功失败**都`ResultExchange_MarkSent`；
- 链路断开时按500ms节拍重试，不再每个主循环尝试；
- 不改变成功/失败计数。

### P2#2 重复ACK不误判B重启

- `ResultExchange_OnPeerAckSequence`只在序号**严格回退**（`<0`）时清ACK；
- 同序号重复（UDP重复）不清；
- 旧序号乱序仍视为回退并恢复重发（当前设计）；
- 增加重复ACK测试。

### 额外

- MRES32增加一致性校验：`calibration_offset_mm!=0`时`calibration_version`不得为0。

## 未修

- app_main+UDP端到端接线测试仍缺；当前只有状态模块/UDP模块分别测试；
- domain `boot_nonce/state`语义、`Calibration_Apply`未使用仍未修；
- 硬件/两板/60s/标定未验证。

## 验证

- Measure/BoardSync/Udp/Sdram/UI/ChirpService/SoundEventTime PASS；
- Debug/Release构建PASS；
- 未烧录。
