# M4.3.12 共同域双向REQ/ACK握手

<!-- doc-status: m4.3.12-domain-handshake-software-complete-hardware-pending; reviewed: 2026-10-27 -->

## 结论

在A派生域令牌基础上增加双向确认，替代“A发事件、B单向采用”：

- `DOMN` 24字节大端报文：
  - `DOMAIN_REQ`：A→B，携带令牌/启动nonce/序号/本地状态；
  - `DOMAIN_ACK`：B→A，回显同一令牌并声明本地严格合格；
- 使用现有5009端口和来源IP/端口校验，不新增CubeMX/端口配置；
- A每500ms发REQ；B只有本地TimeSync/PPS严格合格才回ACK；
- A收到同令牌、B本地有效的ACK后才把域置为valid；
- B收到REQ且本地合格后域valid，可发送事件；
- 对端超时1500ms、PPS FAULT/UNQUALIFIED、TimeSync fault立即撤域；
- 事件/测量仍只在`TimeDomainSnapshot.valid && id!=0`时放行。

## 关键状态

```text
TimeDomain:
  local_valid   TimeSync + PPS严格QUALIFIED
  peer_seen     收到对端REQ/ACK
  handshake     InitWithId启用
  id            A派生令牌；未确认时也保留给A发REQ
  valid         local_valid && peer_seen && token!=0
```

- A未收到ACK：`valid=false`，但`id`保留，用于继续发REQ；
- B未收到REQ：`token=0`，不发事件；
- B本地未合格：不回ACK；
- A收到B ACK但B state非VALID：不接受；
- 超时/本地失格：peer清空，需重新握手。

## 边界

- 不是双向PTP epoch证明；
- 不是外部±10µs/示波器验收；
- 无加密/抗重放，只是协议一致性；
- A令牌=UID+启动时刻；同板重启令牌应变化；
- 未烧录/未两板实测。

## 软件验证

- `Tests/Udp/run_udp_sound_event_tests.py`：DOMN编解码、REQ发送、A本地拒绝ACK、接收ACK sink；
- `Tests/BoardSync/run_tests.py`：双向令牌、错误令牌、超时、本地失格；
- App/Sdram/UI/ChirpService/SoundEventTime/E2E；
- Debug/Release构建PASS。

## 硬件验证

1. 两板同镜像，A/B角色；
2. 同步+严格PPS QUALIFIED；
3. 串口观察`$DOMAIN`/PPS；
4. 示波器PPS±10µs；
5. 断链/FAULT后确认两边域撤销、事件/测量停止；
6. 恢复后重新REQ/ACK；
7. 域CONFIRMED后才播放标准声。
