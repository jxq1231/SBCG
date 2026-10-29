# M4.3.10 PPS资格共同域门控（未验证版）

<!-- doc-status: m4.3.10-pps-domain-unverified-hardware-pending; reviewed: 2026-10-27 -->

## 状态

**UNVERIFIED / 未硬件验证。** 本提交包含：

- 用户PPS工作（PPS BSP/服务、CubeMX相关配置、测试）；
- `PpsService_GetSnapshot()`只读资格/时效/硬件观察；
- `TimeDomain_UpdateWithPps()`：
  - A：`TimeSync`有效 + PPS `QUALIFIED/RATE_REQUALIFY`；
  - B：`offset_valid/internal_locked` + PPS `QUALIFIED/RATE_REQUALIFY`；
  - PPS `FAULT/UNQUALIFIED`或快照不可读 → 撤销共同域；
- 域无效时事件UDP/测量继续全部拒绝；
- App在前台每轮PPS处理后更新域，不在中断/回调做网络或显示。

## 已验证

- `Tests/PpsService` 快照/资格边界PASS；
- `Tests/BoardSync` TimeDomain sync+PPS门控PASS；
- App/Sdram/SoundEventTime回归；
- Debug/Release构建PASS。

## 未验证（必须硬件）

- 两板PPS边沿示波器差≤±10µs、连续10次；
- 冷启动≤30s；
- 断链/恢复、PPS FAULT/UNQUALIFIED时事件/测量全停；
- 真实两板共同域/距离/两屏一致性；
- 烧录、真实网络、标准声。

## 硬件验证步骤摘要

1. 两板同镜像、A/B角色、IP/UID/BOOT记录；
2. 交换机冷启动，串口确认两板`$PPS STATE=QUALIFIED,QUALIFIED=1`；
3. 示波器CH1 A-PPS、CH2 B-PPS，共地，CH1上升沿触发，连续10脉冲测`tB-tA`≤10µs；
4. 探头偏斜先测/交换探头复核；记录每次差值；
5. 断B网线/交换机断电，确认PPS失格、事件UDP/测量停止；恢复后重新QUALIFIED；
6. 同步通过后再播放标准声，观察`$STANDARD_SEQUENCE`/`$SOUND_EVENT_STATE`/`$SOUND_PAIR`/`$MEASURE_RESULT_STATE`；
7. 记录冷启动时间、最大/最小/平均差值、丢脉冲/错误计数。

## 结论边界

PPS `QUALIFIED`只是内部输出资格，不是外部精度验收；只有双通道示波器边沿测量通过后，才能在后续提交中标记验证版。当前提交不得用于申报同步±10µs/基础分。
