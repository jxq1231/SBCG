# M4.3.3 诊断型两板事件UDP路径（软件完成，硬件待验）

<!-- doc-status: m4.3.3-diagnostic-udp-software-complete-hardware-pending; reviewed: 2026-10-27 -->

## 结论

已把M4.3.1成功TOA事件接到M4.3.2事件线协议/配对核心，并接入诊断型UDP板间路径：

- `SoundEventTime`新增固定4条成功事件队列；旧`$SOUND_TIME`不变；
- App从该队列取事件，构造`SoundEventRecord`并调用`UDPSoundEvent_Send`；
- `udp_sound_event`使用5009端口，按本板A/B身份选择对端，接收36字节事件、来源IP/端口/侧别校验后交给`sound_pair`；
- 配对成功输出`$SOUND_PAIR`（距离/方向/ΔT/L/R质量），失败输出明确拒绝状态；1Hz `$SOUND_EVENT_STATE`。

## 重要边界

- 当前`time_domain=1`只是**诊断常量**，不是PTP同步证明；`$SOUND_PAIR`固定`SYNC=0`且带`DIAG=1`，不能作为任务书测距验收。
- 当前事件类型固定为`CHIRP`、quality=100，是诊断事件；标准声MATCH仍0，不能据此声称已识别。
- 尚未接真实交换机/两板实测；未烧录；未验证UDP丢包、乱序、延迟、配对超时边界。
- 事件配对数学/线协议已由主机测试验证；真实A/B时间域、PTP收敛、方向/距离必须明天实测。
- 不改TOA估算器、门控、0.80、CubeMX、时钟/DMA/GPIO/NVIC/缓存。

## 软件验证

- `Tests/Measure/run_tests.py`：O0/O2配对/距离/线协议PASS；
- `Tests/SoundEventTime/run_tests.py`：Q16、事件队列、旧日志、App fanout/时钟失效PASS；
- `Tests/SoundDetectorService/run_tests.py --phase GREEN`、`Tests/Audio`、`Tests/App`、`Tests/Sdram`、`Tests/UI`、`Tests/ChirpService`：PASS；
- Debug/Release构建：PASS（既有RWX链接警告）；
- audio layout：PASS。

## 明天硬件验收要点

1. 两板同一镜像、BOOT/UID/角色/IP/镜像SHA；
2. 交换机冷启动≤30s、PPS示波器连续10次≤±10µs、断线恢复；
3. 观察`$SOUND_EVENT_STATE` TX/RX/foreign/bad、`$SOUND_PAIR`状态；
4. 并排零声程差验证固定偏置，再A/B两侧方向，再卷尺0.20–2.00m重复；
5. 若同步尚未通过，只能把事件/距离标为诊断，不能计任务书测距分数；
6. 标准声识别/质量、温度补偿、双屏统一结果仍为后续独立增量。
