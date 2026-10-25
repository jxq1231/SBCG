# M4.2.4-C2 日志策略评估

<!-- doc-status: c2-evaluation-complete-implementation-pending; reviewed: 2026-10-26 -->

## 结论摘要

A4优化后，`FlushOneLine`同步UART约15ms/行已成为最大前台单项；C2先做现状量化而非直接改日志：

- 70秒静默样本（A4优化镜像）共1661条协议行、173529字节；按115200/8N1估算，**UART占用约15.06秒/70秒 = 21.5%前台时间**，约23.7行/秒、2.48KB/s。
- 最大单行：`$UI` 174字节≈15.1ms；`$SYNC_CTRL`153B≈13.3ms；`$AUDIO`149B≈12.9ms；`$AUDIO_MAP`149B≈12.9ms；`$HEARTBEAT`140B≈12.2ms；`$SOUND_STATE`132B≈11.5ms；`$AUDIO_PROFILE`131B≈11.4ms；`$AUDIO_ANCHOR`130B≈11.3ms。
- **只降低频率不会明显降低最大`FLUSH_MS`**：只要某轮仍发送一条15ms长行，该轮仍可能成为最大循环；要降低最大值必须缩短/替换长行，或改异步UART（需要CubeMX/DMA，本轮不做）。
- 日志不是单一模块问题：约30种前缀，其中多数是1Hz纯状态行，含部分用户PPS/网络诊断；直接改各模块会触碰范围外用户改动，**应优先在`SendLine`集中做日志策略**。

## 现状量化（70秒静默，A4优化镜像）

总字节173529，估算UART 15063ms，占空比21.5%。按总耗时排序的前缀：

| 前缀 | 频率/s | 平均字节 | 单行最大UART ms | 70秒UART ms | 分类 |
| --- | --- | --- | --- | --- | --- |
| `$UI` | 1.0 | 173.4 | 15.1 | 1054 | 纯状态（含ERRORS） |
| `$SYNC_CTRL` | 1.0 | 153.0 | 13.3 | 916 | 同步状态（用户PPS范围） |
| `$AUDIO` | 1.0 | 144.1 | 12.9 | 876 | 纯状态（含ERR/HAL/DMA） |
| `$HEARTBEAT` | 1.0 | 139.4 | 12.2 | 847 | 关键错误计数（可压缩） |
| `$SOUND_STATE` | 1.0 | 132.0 | 11.5 | 802 | 检测状态（含ERRORS） |
| `$AUDIO_ANCHOR` | 1.0 | 129.1 | 11.3 | 773 | 纯状态 |
| `$AUDIO_POS` | 1.0 | 124.7 | 10.9 | 758 | 纯状态 |
| `$PTP_*`/`$TS_*`/`$OBS_RX`/`$SYNC_*` | 1.0 | 60–114 | 5–10 | 合计约4500 | 用户PPS/网络诊断 |
| `$CHIRP_STATE`/`$CHIRP_DIAG` | 0.5+0.5 | 117–121 | ~10.5 | 722 | 检测状态/诊断 |
| `$TEMP`+`$TEMP_STATS` | 0.99 | 50/102 | 4.3/8.9 | 910 | 纯状态 |
| `$AUDIO_MAP`+`$AUDIO_MAP_CHECK` | 0.34 | 147/62 | 12.9/5.6 | 436 | 纯状态 |
| `$PROFILE`+`$AUDIO_PROFILE` | 0.2+0.2 | 63/131 | 5.5/11.4 | 236 | A3临时诊断 |
| `$SOUND_CAND`+`$SOUND_TIME` | 事件 | 108/136 | 9.5/11.8 | 63 | **关键事件，必须保留** |

完整数据：`Logs/Detector/M4_2_4_C2/log-analysis.json`；原始采集：`Logs/Chirp/statopt-m424a4-02-serial.*`。

## 关键日志与可压制项

**必须保留/优先：**
- `$BOOT`/`$BOARD`/`$RESET`/`$AUDIO_INIT`；
- `$SOUND_CAND`、`$SOUND_TIME`、`$CHIRP`匹配/事件、命令回复；
- `$HEARTBEAT`中的`TX_ERRORS/LOG_FULL/LOG_WRITE/LOG_BAD`（可压缩、不可丢失）；
- 各状态行中的错误/丢弃/超时字段：`SOUND_STATE.REJECTED/DROPPED/ERRORS`、`CHIRP_STATE.BAD/SKIP/ERR/DROP`、`AUDIO.ERR/DROP/HAL/DMA`、`UI.ERRORS`。

**可降频/合并的纯状态：** `$UI`、`$AUDIO`、`$AUDIO_POS`、`$AUDIO_ANCHOR`、`$SOUND_STATE`、`$SOUND_TIME_STATE`、`$CHIRP_STATE`/`$CHIRP_DIAG`、`$TEMP`/`$TEMP_STATS`、`$AUDIO_MAP*`、`$CODEC`、`$ETH`、`$UDP*`、`$PTP*`、`$TS_*`、`$OBS_RX`、`$SYNC*`、`$PPS`。其中部分（PPS/网络）属于用户未提交改动，**不直接修改其模块**。

## 可选方案与量化预期

| 方案 | 内容 | 平均UART占用（估算） | 最大FLUSH | 风险/代价 |
| --- | --- | --- | --- | --- |
| A. 仅关A3诊断 | 默认不输出`$PROFILE/$AUDIO_PROFILE`（测量专用） | 21.5%→约21.1% | 略降（11.4ms行减少） | 最小；只损失A3粒度 |
| B. 纯状态降频 | 所有1Hz纯状态改5s，保留关键/事件行 | 约21.5%→5–7% | 仍可能15ms（长行偶发） | 分析脚本依赖1Hz行；不解决最大单行 |
| C. **集中quiet/full过滤**（推荐） | 在`SendLine`按前缀/频率策略：默认quiet只保留关键/事件行+压缩心跳，full模式恢复现网全部；不修改用户模块 | 可<3% | 取决于保留行；若保留心跳仍12ms | 需命令或编译模式；必须避免隐藏非零错误 |
| D. 压缩长行 | `$UI`/`$SYNC_CTRL`/`$AUDIO`/`$AUDIO_MAP`/`$HEARTBEAT`缩短字段 | 21.5%→约12–15% | 15→约8–9ms | 改动多模块，部分属用户PPS；分析字段丢失 |
| E. 合并为`$STATUS`摘要 | 一条≤100B/秒集中错误/候选/状态，替换大量纯状态行 | 可<3% | 约<9ms | 改动最大、需协议/分析/主机测试同步 |

> 估算依据：115200/8N1约86.8µs/字节；60–70秒静默样本；只用于方向判断，不代表最坏时间。

## 建议

**推荐C：集中quiet/full日志策略，分两步实施。**

1. **C2a（低风险）**：默认quiet关闭A3两行测量诊断，并压制纯周期状态（保留关键/事件/错误摘要）；full模式（编译宏或控制台命令）恢复当前完整日志供分析。目标：UART占用<3%、不丢失关键事件/错误、日志队满/写错误仍为0。
2. **C2b（按测量结果）**：若quiet下最大`FLUSH_MS`仍>10ms（例如保留的心跳/摘要行过长），再压缩该行或改异步UART；异步DMA属CubeMX变更，不在本轮直接实施。
3. **验证**：主机测试覆盖过滤分类/命令切换/队列；板端静默对比A4基线（UART占用、FLUSH、LOOP、GAP、错误/候选/事件行数）；full模式需能复现原日志字段。
4. **保留策略**：quiet默认不发送AUDIO_POS/ANCHOR等分析行，但full模式必须完整；或提供“事件/错误立即发、状态5s发”的折中，不删除字段。

## 待决策

- 采用 quiet默认+full命令，还是纯编译期两版本？
- 纯状态降频目标（5s/10s）与是否保留1Hz错误摘要？
- 是否需要保留A3 A3粒度（默认关，full开）？
- 是否接受修改`app_main.c`集中过滤（不触碰用户PPS模块）并另行烧录授权。

本评估不改代码/不烧录；实施前需用户选择方案并授权新镜像。
