# M4.2.4-A2 同循环前台归因：测量记录

<!-- doc-status: stage-a2-complete-c-not-started; reviewed: 2026-10-26 -->

## 结论

A2 只增加低频诊断：在CHIRP构建的`App_Process`记录最大一轮前台循环总时长`LOOP_MS`，以及同一轮内`AUDIO_MS`、`UI_MS`和残差`OTHER_MS`，随原1Hz心跳输出；非CHIRP心跳格式不变。两次静默采集结果一致：

- 最大前台循环 **38ms / 42ms**；同一轮构成 **AUDIO 21ms / 25ms、UI 1ms、OTHER 16ms**。
- UI自身的最大`UPDATE_MS`为20ms（更早B采集为26ms），但该峰值**不在最大循环内**；最大循环的UI仅1ms。
- 因此：**UI 不是最慢前台循环的主要构成**，C1“直接改UI”缺少当前证据；下一步应拆分 `AUDIO_MS` 内部（音频统计计算、音频日志等）与 `OTHER_MS`（FlushOneLine同步UART、LwIP、温度/PTP等）。
- 其他指标：`GAP_MS`44ms、`SCAN_MS`3ms、`SCORE_MS`4ms、`STAGE_MS`6ms、`PROC_MS`1ms；SOUND/CHIRP/AUDIO/LOG/UI错误全0，候选零丢弃。

**重要口径：** `LOOP_MS`与Chirp `GAP_MS`采样点不同（GAP在AudioCapture内相邻ChirpService_Process之间；DHT整轮跳过等会使GAP跨多轮）。`OTHER_MS`是残差桶，包含同步UART日志、LwIP、温度/PTP/PPS、AudioAnchor/SOUND_TIME、SDRAM等，**不是纯CPU或未解释时间**；不能用`LOOP_MS−GAP_MS`推断未归因时间。

## 实现

| 文件 | 本轮职责 |
| --- | --- |
| `App/app_main.c` | CHIRP构建：记录每轮入口、AudioCapture、UI耗时；仅在`loop_ms>max_loop_ms`时保存四项分解；心跳追加`LOOP_MS/AUDIO_MS/UI_MS/OTHER_MS`；非CHIRP心跳不变 |
| `Tests/ChirpService/test_app_diagnostics.c` | 断言新增字段存在且旧字段可核对 |
| `Docs/Protocol.md` | 记录新增字段、口径与限制 |

- 只新增HAL tick读取（每轮固定约6次），不改变业务调用顺序、队列、日志频率或阈值；`APP_STATUS_LINE_CAPACITY=256`最坏行长239+NUL<256。
- 心跳行变长本身约+44B，115200下≈+3.8ms/行，可能计入其报告的最大轮，属诊断自扰动；不能把`OTHER_MS`当纯CPU。
- 最大循环/心跳发送可能同时发生，`OTHER_MS`16ms中很可能含一行同步UART约12ms；需A3拆分确认，不能直接归因于日志策略。

**No CubeMX change required。** 无新GPIO/TIM/IRQ/DMA/SAI/LTDC/ETH/Cache配置。

## 静默采集

| 采集 | 时长 | LOOP_MS | 同轮AUDIO_MS | 同轮UI_MS | 同轮OTHER_MS | GAP_MS | UI最大 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| loop-m424a2-01 | 85s | 38 | 21 | 1 | 16 | 44 | 20 |
| loop-m424a2-02（续采） | 70s | 42 | 25 | 1 | 16 | 44 | 20 |

两次采集业务均为零错误：SOUND STAGE6/PROC1、CAND0/8、DROPPED0；CHIRP SCAN3/SCORE4、DONE3/18、DROP0；AUDIO OK400/1098、DROP/ERR/HAL/DMA=0；LOG_FULL/WRITE/BAD=0、HWM20；UI ERRORS=0。证据：`Logs/Chirp/loop-m424a2-01/02-*`。

最终Debug ELF SHA256：`d19a0005da26454af2e0db6c15a5e9765bea7224fe85aa6d5203ea68197a2a98`；固件标识仍`SBCG-M4.2-BATCH`，以ELF哈希区分。未改选项字节、未发声。

## 独立复核

复核结论**PASS_WITH_RESERVATIONS**（无代码blocker/major）：
- 核对7种宏组合`-Werror`无告警；最大更新分支只在更慢轮覆盖；uint32回绕探针正确；非CHIRP心跳精确串不变；最坏心跳239B+NUL<256；调用序列唯一差异为范围外既有PPS。
- 保留项：`OTHER_MS`残差桶不可当CPU/未解释时间；`LOOP_MS`与`GAP_MS`采样点不同，不能相减推断（已写入代码注释和协议）；正式测试只断言字段存在，tick恒定的测试桩无法进入“更慢轮更新”分支（低风险，临时探针已覆盖）；`Protocol`已同步四字段；`app_main.c`混有范围外PPS改动，提交时只暂存A2 hunk（已执行）。
- 报告：`Logs/Detector/M4_2_4_A2/review.json`。

## 限制与下一步

- 仅两次约70–85秒静默窗口、无主动发声；不能代表标准音频或长时最坏情况。
- `LOOP_MS`是1ms分辨率墙钟差；UI/AUDIO是同一最大轮的子区间；`OTHER`不可细分。
- 没有用本结果宣称UI或日志已被证明为根因；C1暂不启动。
- **建议下一步A3测量**：拆分`AUDIO_MS`内部（`AudioStatistics_Compute`、音频日志格式化等）与`OTHER_MS`内部（`FlushOneLine`同步UART、LwIP/温度/PTP等），再决定优化音频统计还是日志策略。需要新镜像烧录授权。
- 主动标准音频MATCH仍因环境不能发声保持未验收。
