# M4.2.4-A3 AUDIO/OTHER内部分解：测量记录

<!-- doc-status: stage-a3-complete-next-optimization-pending; reviewed: 2026-10-26 -->

## 结论

A3 在 CHIRP 构建中增加低频率诊断（每5秒两行）：`$PROFILE` 输出最大循环的 `LOOP/AUDIO/UI/OTHER/FLUSH`，`$AUDIO_PROFILE` 输出最大一轮 AudioCapture 内部分解。一次约84秒静默采集（A3镜像Debug `4e8a53f5…`）结果：

- 最大循环 **40ms**：`AUDIO=27ms`、`UI=0ms`、`OTHER=13ms`，其中 **`FLUSH=12ms`**。
- AudioCapture 内部最大一轮 **28ms**：**`STAT_MS=15ms`（音频统计计算）**、`SD_STAGE_MS=6ms`（原候选Stage批量后）、`CH_PROC_MS=6ms`（Chirp扫描/评分）、`CH_STAGE_MS=1ms`、其余（IO/LOG/OTHER）≈0–1ms。
- 业务无回归：SOUND REJECTED/DROPPED/ERRORS=0，CHIRP BAD/SKIP/ERR/DROP=0，AUDIO OK=488、DROP/ERR/HAL/DMA=0，LOG_FULL/WRITE/BAD=0、HWM=21，UI ERRORS=0；单BOOT、A UID。
- UI自身 `UPDATE_MS` 仍为20ms，但最大循环的UI=0；**UI不是最慢循环的主要构成**，C1仍不启动。

结论：当前最有依据的下一优化对象是 **`AudioStatistics_Compute`（15ms）** 与 **`FlushOneLine`同步UART（12ms）**，而不是UI；CHIRP扫描/评分（3/4ms）与批量Stage（6ms）已较小。

## 实现

| 文件 | 本轮职责 |
| --- | --- |
| `App/Audio/audio_capture.h` | 新增 `AudioCaptureProfile`（total/io/stats/sound_stage/sound_process/chirp_stage/chirp_complete/chirp_process/log/other）与 `AudioCapture_GetProfile` |
| `App/Audio/audio_capture.c` | 在现有调用之间分段读HAL tick；仅当本轮total大于已存最大值时保存分解；日志路径包含格式化/发送；非日志提前返回也结算；Init清零 |
| `App/app_main.c` | CHIRP构建测量 `FlushOneLine`；每5秒输出 `$PROFILE` 和 `$AUDIO_PROFILE` 两行；非CHIRP/无AUDIO构建不引用 |
| `Tests/Audio/test_audio_capture.c`、`Tests/App/test_app_main.c` | 增加诊断时钟/GetProfile桩，保持原测试语义 |

**No CubeMX change required。** 无新GPIO/TIM/IRQ/DMA/SAI/LTDC/ETH/Cache配置；不改队列、日志频率策略（仅测量增量增加2行/5秒）。

口径：
- `AUDIO_MS` 是最大循环内整个 `AudioCapture_Process` 的墙钟；`AUDIO_PROFILE` 是其内部同一次最大轮的分解，`total=各段之和+other`。
- `FLUSH_MS` 是最大循环内 `FlushOneLine()` 的耗时；`OTHER_MS` 仍含LwIP/温度/PTP/AudioAnchor等，不能当纯CPU。
- `LOOP_MS` 与 Chirp `GAP_MS` 采样点不同，不得相减推断未归因时间。
- 两行诊断每5秒各一条，115200下合计约195字节/5秒≈0.4KB/s，且每行最多一次UART发送；会轻微抬高自身报告值，不能忽略。

## 静默采集

| 项目 | 第1次 | 第2次（续采） |
| --- | --- | --- |
| 采集 | 100秒，BOOT后约83.5秒 | 70秒 |
| `$PROFILE` / `$AUDIO_PROFILE` | 各16条 | 各14条 |
| 最大 `LOOP_MS` | 40 | 40 |
| 同轮 `AUDIO_MS/UI_MS/OTHER_MS/FLUSH_MS` | 27 / 0 / 13 / 12 | 27 / 0 / 13 / 12 |
| AudioCapture最大轮 `TOTAL_MS` | 28 | 28 |
| 内部分解 `STAT/SD_STAGE/SD_PROC/CH_STAGE/CH_COMP/CH_PROC/IO/LOG/OTHER` | 15/6/0/1/0/6/0/1/0 ms | 15/6/0/1/0/6/0/0/0 ms |
| `GAP_MS` / `SCAN_MS` / `SCORE_MS` | 46 / 3 / 4 ms | 56 / 3 / 4 ms |
| 错误 | SOUND/CHIRP/AUDIO/LOG/UI全0 | 同左；SOUND CAND6/DROPPED0，AUDIO OK2686 |

两次分解一致，说明最大轮构成可重复：**STAT15 + SD_STAGE6 + CH_PROC6 + 小段 ≈ AUDIO27；FLUSH12 ≈ OTHER13**。证据：`Logs/Chirp/profile-m424a3-01/02-*`（raw SHA256 `1f018b54…` / `1e0ba98f…`）。`$PROFILE` 最长63字符，`$AUDIO_PROFILE` 最长132字符，均<256。

## 限制与下一步

- 只有两次约70–100秒静默采集；最大值可能随不同调度/页面更新变化（两次一致，但仍非长时统计）。
- **诊断自扰动**：`$PROFILE`/`$AUDIO_PROFILE`两行在`loop_ms`采样后入队，由后续`FlushOneLine`同步发送（132字节约11.5ms）；A3采集内最大值在首次诊断输出后从36→38→40ms递增，最大轮`FLUSH=12`与132字节行吻合。A2无A3输出时LOOP为38/42ms、AUDIO21/25、OTHER16；A3 LOOP40、AUDIO27，未超过A2最慢循环，但**AUDIO增量可能含插桩开销，FLUSH12不能完全排除A3自身发送**。A4前应做关闭诊断（或标记排除其flush）的对照采集。
- `OTHER_MS`仍包含`FLUSH_MS`（`LOOP=AUDIO+UI+OTHER`，不能再用OTHER加FLUSH）；`$PROFILE`最大循环与`$AUDIO_PROFILE`最大轮独立（AUDIO27 vs TOTAL28）。
- `FLUSH_MS=12ms` 与心跳/状态行长相关（约138字节@115200≈12ms）；不能据此直接认定日志策略必须改，需结合真实LOG错误和证据完整性。
- `STAT_MS=15ms` 是当前最大单段；需在A4确认 AudioStatistics 的逐样本实现与等价优化空间（保持峰值/RMS字段完全一致），并扣除插桩开销。
- 主动标准音频MATCH仍因环境不能发声未验收。
- **建议下一步A4**：先做关闭A3诊断的对照采集（确认FLUSH/LOOP基线），再优化/替换 `AudioStatistics_Compute`（15ms），最后评估 `FlushOneLine`/日志策略（C2）；C1 UI继续暂缓。新镜像烧录需另行授权。

## 独立复核

复核结论 **PASS_WITH_RESERVATIONS**（无代码 blocker/major）：
- 复核者独立核对Debug ELF SHA256=`4e8a53f5…`，Audio/App/ChirpService/TouchGFX四套主机回归exit0；临时探针覆盖分段求和、other钳位、uint32回绕、仅更慢轮覆盖、Init清零、getter只读、日志/提前返回路径，全部PASS。
- 原始采集16+16条`$PROFILE/$AUDIO_PROFILE`均满足分段和==TOTAL_MS；最坏行长`$PROFILE`106、`$AUDIO_PROFILE`222<256，SendLine无截断，LOG错误0、HWM21。
- 保留项：诊断行自身flush可能抬高FLUSH/LOOP（medium，需关闭诊断对照）；OTHER仍含FLUSH且两个最大轮独立（medium，已写入协议/报告）；新profile逻辑主机测试覆盖不足（medium，已补Audio profile getter/最大轮更新测试）；`io_ms`含Sound Complete、`log_ms`不含真正UART发送（low，已写入口径）；注释/typedef措辞（low，后续维护处理）。
- 报告：`Logs/Detector/M4_2_4_A3/review.json`；临时探针：`Logs/Detector/M4_2_4_A3/review_probe_profile.c`。
