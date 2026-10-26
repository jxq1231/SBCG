# M4.3.11 配对P0与结果边界P1修复

<!-- doc-status: m4.3.11-pairing-p0-fixed-domain-evidence-still-open; reviewed: 2026-10-27 -->

## 结论

Review findings中的结构性P0已修复：

- `UDPSoundEvent_Send()`现在先`SoundPair_Submit()`本端事件，再发送UDP；
- `Receive()`仍只接收对端事件；
- 配对器因此同时拥有A/B两侧，真实双板才可能产生`$SOUND_PAIR`；
- 新增主机UDP集成测试：本端发送提交 + 对端Receive → 恰好一个valid结果，距离343mm、方向A、A/B序号正确。

同时修复可局部收紧的P1：

1. A板`ServiceAuthoritativeResult`重发前检查共同域及结果域ID，域失效立即撤销结果；
2. B板`PublishRemoteMeasurement`域不匹配时撤销旧结果，而非仅丢弃新包；
3. `MeasurementResult_PublishRemote`拒绝比当前新鲜的旧ID，相同ID刷新年龄；仅在当前结果超龄后允许低ID（A重启场景）；
4. MRES不接收方向UNKNOWN，Encode/Decode都要求A/B方向；
5. 现有评分/结果边界测试补齐上述用例。

## P1 #2：域ID共享证据（部分修复）

- A板用自身UID+启动时刻派生域令牌，随事件携带；
- B板只有收到A侧事件后采用该令牌，之后B事件/MRES均使用同一令牌；
- `time_domain`不再固定常量1；A/B配对/结果仍校验令牌相等；
- 但这只是**A派生启动令牌+单向前置采用**，不是双向握手、不是PTP epoch证据；仍不能当外部同步验收。

## 仍未解决（不掩盖）

- 无ACK/结果序列号；A周期刷新仍可能短暂丢包。
- 真实两板、PPS示波器±10µs、标准声MATCH=0、静默/标定/温补未验证。
- `event_classifier.high_ratio`仍只作诊断，未参与分类。
- RATE_REQUALIFY已从共同域门控排除（仅严格QUALIFIED放行），但仍需真实PPS样本验证。

## 验证

- `Tests/Udp/run_udp_sound_event_tests.py` PASS；
- `Tests/Measure`（结果ID/旧ID/方向门控）O0/O2 PASS；
- BoardSync/PpsService/Sdram/UI/App回归；
- Debug/Release构建PASS；
- 未烧录。

## 硬件验收影响

P0修复是双板测距的前置条件；但PPS/共同域证据和真实硬件仍未验收，不能报基础分。
