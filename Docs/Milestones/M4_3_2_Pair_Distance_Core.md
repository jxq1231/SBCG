# M4.3.2 两板事件配对/距离纯计算核心（软件基线）

<!-- doc-status: m4.3.2-pure-core-software-complete-network-pending; reviewed: 2026-10-26 -->

## 结论

在两板硬件联调之前，先完成可主机验证的纯软件核心：`8811e57 feat: add two-board event pairing distance core`。

- [sound_pair.c](../../App/Measure/sound_pair.c)：固定队列配对、乱序/重复/漏检/歧义拒绝、质量/类型/共同时间域门控、`d=c|tB−tA|`、A/B方向；
- [sound_event_wire.c](../../App/Network/sound_event_wire.c)：固定36字节显式大端序事件线协议，编解码与字段/长度/魔数校验；
- O0/O2主机测试覆盖距离/方向、乱序、重复、歧义、超时、质量/类型/时间域/量程、线协议roundtrip和坏包；
- Debug/Release构建通过，未烧录、未接交换机/两板、未产生真实距离验收。

## 输入边界

- 两事件必须已带相同、非零`time_domain`；该标识由后续同步层提供，**不能直接把A/B各自的PTP epoch数值比较**。
- `time_ns`必须是本地PTP模型时间（M4.3.1的`SOUND_TOA_TIME`路径），不能使用`TOA_US`名义采样轴或UDP接收时刻。
- `quality/type`必须达标且相同；声源位于两板基线延长线时才有`d=c|Δt|`；任意二维定位不在本增量。
- 配对窗口/最大距离/声速由调用方配置；当前纯核心不负责温度版本、标定或两屏一致结果。

## 软件验证

```powershell
python -B Tests/Measure/run_tests.py
```

结果（O0/O2均PASS）：

- `PASS sound pair: distance/direction, identity gates, out-of-order, duplicate, ambiguity, timeout`
- `PASS sound event wire: 36-byte big-endian roundtrip, field/length/magic gates`
- `cmake --build --preset Debug` / `Release` PASS（既有RWX链接警告）

## 尚未完成

- 事件发布回调：`SoundEventTime`成功TOA → 填充`SoundEventRecord` → UDP发送；
- 实际UDP服务/端口/收发cbuf、共同`time_domain`的来源、BOOT/session/质量字段；
- 两板交换机冷启动、PTP±10µs、真实同声配对/方向/距离、两屏显示；
- 标准声识别MATCH0、击掌分类、温度补偿/标定；
- 任何物理精度/量程验收。

本记录只证明纯C配对/距离数学和线协议在主机可复现，不证明两板联调或任务书测量指标。
