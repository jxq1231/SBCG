# M4.2.4-A4 对照基线与音频统计优化：测量记录

<!-- doc-status: stage-a4-complete-c2-pending; reviewed: 2026-10-26 -->

## 结论

A4 分两步完成：

1. **关闭 A3 两行诊断的对照采集**：控制镜像（A3代码、无`$PROFILE/$AUDIO_PROFILE`输出、心跳以`FLUSH_MS`替代`LOG_Q`）两次采集为 `LOOP_MS=43`、`AUDIO_MS=29`、`OTHER_MS=13`、`FLUSH_MS=13`；A3诊断镜像为 `LOOP=40`、`AUDIO=27`、`FLUSH=12`。控制值不低于A3，**未发现A3诊断抬高最大值**，A3基线与FLUSH≈12–13ms可保留。
2. **优化`AudioStatistics_Compute`**：从“逐帧内层双声道循环+结构体索引”改为一次循环读两声道、局部标量min/max/peak与uint64能量和；峰值/RMS/min/max及无效输入语义不变（主机逐字段对拍通过）。静默实测：
   - `STAT_MS` **15→8–9ms（约−45%）**；
   - `AUDIO_MS` **27→17–19ms**；
   - 最大循环 `LOOP_MS` **40→34ms**；
   - 此时同轮 `OTHER_MS/FLUSH_MS=15–16/15ms` 成为最大单项，`UI_MS=1–2ms`；
   - SOUND/CHIRP/AUDIO/LOG/UI错误全0，候选零丢弃，AUDIO VALID=1。

**下一步对象已由“音频统计”转为“FlushOneLine/日志策略（C2）”；C1 UI继续暂缓。**

## 对照构建（未提交）

| 项 | 内容 |
| --- | --- |
| 临时镜像 | Debug SHA256 `066bfbb1ff1f91ee2b188ae625acf9c652462880c25fb1286897980eda3bb4d0` |
| 改动 | 关闭两行A3诊断输出；心跳去掉`LOG_Q`、追加`FLUSH_MS`；其余A3代码不变 |
| 采集1 | LOOP43/AUDIO29/UI1/OTHER13/FLUSH13；LOG_HWM=20，业务错误0 |
| 采集2 | LOOP43/AUDIO29/UI1/OTHER13/FLUSH13；UI最大27ms在另一轮；业务错误0 |
| 证据 | `Logs/Chirp/control-m424a4-01/02-*` |

对照结论：A3诊断行没有抬高`LOOP/FLUSH`（控制值更高）；A3单次40ms/12ms属正常波动范围，FLUSH基线≈13ms。控制构建只用于测量，工作树已恢复为无`SBCG_PROFILE_CONTROL`的A3代码。

## 优化实现

| 文件 | 改动 |
| --- | --- |
| `App/Audio/audio_statistics.c` | 双声道展开为局部标量循环；保留每声道min/max/peak、uint64 square和、floor sqrt语义；无新分配、无外设/volatile顺序变化 |

未改门限、队列、日志频率、A3/A2诊断字段、BSP/DMA/IRQ/CubeMX。优化不改变`$AUDIO`字段定义。

## 静默实测

| 指标 | A3优化前 | A4优化后（两次） |
| --- | --- | --- |
| `STAT_MS` | 15 | **8 / 9** |
| `SD_STAGE_MS` | 6 | 6 / 6 |
| `CH_PROC_MS` | 6 | 3 / 6（波动） |
| `AUDIO_MS` | 27 | **18 / 18**（首轮19） |
| `LOOP_MS` | 40 | **34 / 34**（心跳末值35） |
| `FLUSH_MS` | 12 | 15 / 15 |
| `OTHER_MS` | 13 | 15 / 15 |
| `UI_MS` | 0 | 1 / 1–2 |
| `GAP_MS` | 46/56 | 40 / 40 |
| 错误 | 全0 | 全0 |

优化镜像Debug SHA256 `de2120055c2c681abeb7935070bb1de8a175012113bfbc6160032a252bde2b9f`，Flash 277616、RAM 317920。证据：`Logs/Chirp/statopt-m424a4-01/02-*`。

主机验证：Audio测试（含RMS/peak逐字段、无效输入、边界）通过；SoundDetector/SoundDetectorService/ChirpService/ChirpMatcher/App/SoundEventTime/TouchGFXService全部exit0；Debug/Release构建和四项布局通过。

## 限制与下一步

- `STAT_MS`为1ms分辨率墙钟差，含SysTick抢占与A3插桩；8–9ms是观测值，不是纯CPU或最坏时间保证。
- 优化只改计算路径，不改变识别率/声学精度；主动标准音频MATCH仍因环境不能发声未验收。
- 两次短静默（约60–75秒）不足以证明长时最优；最大循环也可能受不同状态页/网络事件影响。
- **C2候选**：`FlushOneLine`同步UART约15ms/行（115200下138字节≈12ms）现为最大单项；需先评估“减少纯状态行/合并状态/降低非关键频率”对日志证据和实时性的影响，保留BOOT/RESET、命令回复、声音事件、AUDIO_POS/锚点等关键日志。不得仅凭单次最大值直接改策略；C2实施和烧录需另行授权。
- 主动标准音频恢复后优先于C2验收。

## 独立复核

复核结论 **PASS**（无blocker/major）：
- 对HEAD原实现与优化实现做599个差分输入、6522次逐字段检查，在`-O0/-O2/-O3/-Os`及`-ftrapv`/UBSan下全部一致；覆盖frame_count=1/2/…/65535、INT16_MIN/MAX/0边界、随机/奇偶/双声道不等/无效参数/页保护不越读；独立Newton整数平方根参考实现也逐字段一致。
- Audio/SoundDetectorService/SoundDetector/ChirpService四套主机测试exit0；A4范围仅`audio_statistics.c`，临时控制宏已从工作树删除，`app_main.c`仅余范围外用户PPS hunk，A3诊断代码未被改动。
- 低风险保留项：结构体复合字面量按位置初始化，字段顺序变化会静默错配（建议后续改指定初始化）；Debug -O0下栈帧/代码量增大，Release -Os两者均减小；不影响本次验收。
- 报告：`Logs/Detector/M4_2_4_A4/review.json`。
