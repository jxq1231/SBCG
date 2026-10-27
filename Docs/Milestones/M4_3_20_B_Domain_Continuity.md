# M4.3.20 B共同域连续性修复（软件完成，硬件待复验）

<!-- doc-status: m4.3.20-b-domain-continuity-software-complete-hardware-pending; reviewed: 2026-10-27 -->

## 问题

实物日志中B板周期出现：

```text
$SYNC_CTRL,ROLE=B,STATE=TRACKING,INTERNAL_LOCK=0
$PPS,ROLE=B,STATE=RATE_REQUALIFY
```

旧共同域要求B板`offset_valid && internal_locked`且PPS严格`QUALIFIED`，导致B板约每8秒掉域，A板随后因对端超时掉域。

## 修改

`App/Sync/time_domain.c`：

- B板本地资格改为`offset_valid`；`TRACKING`仍有新鲜offset，内部锁在正常调频时会短暂消失，不应拆掉事件时间域；
- PPS接受`QUALIFIED`或`RATE_REQUALIFY`；后者是PPS服务的有界内部重新资格区间，输出/qualified保持；
- `UNQUALIFIED/FAULT`仍撤域；
- 外部±10µs仍需示波器验收。

## 验证

- `Tests/BoardSync/run_tests.py` PASS；
- Debug/Release构建PASS；
- 已烧当前测试镜像到A/B；
- 实物run04暂未建立DOMAIN，原因是**A板事件UDP绑定失败**（`$SOUND_EVENT_STATE INIT_ERR`持续增长，B为0），A未发DOMAIN REQ；尚未验证本修复的真正效果。

## 边界

本修复不解决、也不隐瞒A板UDP绑定失败；该问题需单独排查（A板网络/端口/PCB资源/重新上电）。未完成音频闭合、PPS示波器或距离验收。
