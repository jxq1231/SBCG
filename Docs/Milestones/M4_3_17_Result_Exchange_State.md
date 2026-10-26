# M4.3.17 结果重发状态模块与B重启测试

<!-- doc-status: m4.3.17-result-exchange-state-software-complete; reviewed: 2026-10-27 -->

## 结论

把app_main中A结果重发/Ack状态抽成纯模块：

```text
App/Measure/result_exchange_state.h/.c
```

状态：

```text
last_id / last_tx_ms / acked_id
last_peer_sequence / have_peer_sequence
```

规则：

- ID变化立即发送；
- 同ID最多每500ms刷新；
- MACK后停止；
- B的DOMAIN_ACK序号回退（B重启）→清ACK，当前结果重新发送；
- 域恢复→清ACK，当前结果重新发送；
- 时间无符号差、序号有符号差，支持回绕。

App改为调用该模块，删除app_main里的重复静态状态。

## 测试

`Tests/Measure/test_result_exchange_state.c` O0/O2：

- 首次发送；
- 500ms刷新节流；
- ACK后停止；
- 新ID立即发送；
- B ACK序号正常递增不误清；
- B ACK序号回退→恢复重发；
- 域恢复→恢复重发；
- ID 0拒绝。

`python -B Tests/Measure/run_tests.py` PASS。

## 边界

- 仍为主机状态测试，不覆盖真实UDP/双板；
- B重启识别依赖DOMAIN_ACK序号，若B序号不重置则无法识别；
- 域恢复清ack是显式设计，不保留旧显示；
- 未烧录/未硬件。
