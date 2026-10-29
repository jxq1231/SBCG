# M4.2.4-B 原声音候选链64点批量迁移：静默对比记录

<!-- doc-status: stage-b-complete-c-not-started; reviewed: 2026-10-26 -->

## 结论

M4.2.4-B 只把 `SoundDetectorService_Stage` 的逐帧 `SoundDetector_Push` 迁移为 `SoundDetector_PushBatch`（每次最多64点，未满足快速资格自动逐帧回退），保持 Begin/End、工作副本、边界复位、候选暂存/队列、两套阈值和日志策略不变。最终 Debug 镜像已获用户授权烧录 A 板：

- **`STAGE_MS` 从 M4.2.4-A 的 21ms 降到 7ms（−14ms，约 −67%）**；两次独立静默采集均为7ms。
- `GAP_MS` 从 60–61ms 降到 **49ms**；`SCAN_MS` 仍3ms、`SCORE_MS` 5ms；`DONE_MAX_MS` 494ms（≤1000ms），`DROP_MAX_MS=0`。
- 业务无回归：两次采集 SOUND REJECTED/DROPPED/ERRORS=0（候选8/11全发布）、CHIRP BAD/SKIP/ERR/DROP=0（候选10/20全完成）、AUDIO OK=401/889 且 DROP/ERR/HAL/DMA=0、LOG_FULL/WRITE/BAD=0、UI ERRORS=0；单BOOT、A登记UID。
- UI `UPDATE_MS` 本轮为26ms（A轮为19–20ms）。B未改UI代码，这是运行间波动；但说明当前UI更新已大于Stage，是后续条件增量C1的主要候选。
- **主动标准音频 MATCH、双板/声学精度仍未验收**（环境不能发声）。

## 实现与语义

| 文件 | 本轮职责 |
| --- | --- |
| `App/Detector/sound_detector_service.c` | Stage改用64点批量：`int16_t batch[64]`局部缓冲从交错PCM抽取；`available=min(64,剩余)`；按`consumed`推进；`consumed==0/>available`或INVALID/DISCONTINUITY记录Stage时间并返回false；EVENT暂存逻辑不变 |
| `App/Detector/sound_detector_service.h` | Stage说明改为批量优先/逐帧回退；`max_stage_ms`口径改为“批量/逐帧回退暂算循环” |
| `Tests/SoundDetectorService/test_batch_service.c` | 新增真实服务 vs 独立逐帧参考的逐字段对照；`-finstrument-functions`证明窗口64整块Stage中逐帧Push为0、PushBatch>0 |
| `Tests/SoundDetectorService/run_tests.py` | 输出目录改为M4.2.4-B；新增批量对照构建/运行，原service测试保留 |

语义合同：

- 仅替换Stage内部扫描方式；`working=current`、boundary复位、staged候选、End成功才提交、失败清历史、输出队列4条、250ms/1000ms日志节流、两套阈值均未改。
- 非64窗口配置（如现有主机测试的window=4）由`SoundDetector_PushBatch`自动按1点回退，行为与旧逐帧一致。
- 块内剩余不足64帧时以`available<64`调用，接口只消费1点并继续；不存在死循环。
- 因RAM不能新增16KB整块单声道缓冲，使用128字节局部缓冲；栈增量已由布局/构建验证不含异常（RAM静态不变）。
- 计时字段`STAGE_MS`现在覆盖“批量/回退暂算循环”，仍不含入口校验/`working=current`拷贝；`PROC_MS/UPDATE_MS`口径同A。

**No CubeMX change required。** 无新GPIO/TIM/IRQ/DMA/SAI/LTDC/ETH/Cache配置，无DMA所有权变化。

## 主机验证

| 项目 | 结果 |
| --- | --- |
| SoundDetectorService | 原7组全部PASS；新增批量对照PASS：3候选逐字段一致（SESSION/FIRST/CONFIRMED/E_AC/PEAK/CLIP），计数/丢弃/错误/valid/rejected/pending/armed一致 |
| 快速路径证明 | 窗口64的8192帧Stage中`SoundDetector_Push`调用0次、`PushBatch`>0；新会话/不连续边界也通过 |
| 其他回归 | SoundDetector、ChirpService、ChirpMatcher、Audio、App、SoundEventTime、TouchGFXService全部exit0 |
| 构建 | Debug Flash 276080、Release 185156字节；RAM 317920/327680；仅原RWX警告 |
| 布局 | 音频/以太网两配置四项通过 |

最终Debug ELF SHA256：`192c6a33ee0dd808b8c87764f8427d613b05480b9463d3b63b0448ca2117eeac`。固件标识仍为`SBCG-M4.2-BATCH`，以ELF哈希区分本轮镜像；未改`app_main.c`。

### 独立复核

复核者用`git show HEAD`的旧逐帧服务与当前批量服务做黑盒差分：5个种子、110个随机场景（window64/4/32/128/256、ch0/1、连续/跳帧/新会话、非法元数据、End true/false、健康故障、tick回绕/step），stdout逐字节一致；新batch测试对旧逐帧服务变红（hook/字段），反汇编确认PushBatch内部仍call Push、钩子非空转；SoundDetectorService/Chirp/Audio/SoundDetector核心测试均exit0。结论**PASS_WITH_RESERVATIONS**：无代码blocker/major；唯一major是复核时尚未进行的新镜像静默实测（本报告两次采集已补齐该验收缺口）；低风险项为批量测试原只做O2、只测ch0、无window64 rejected-End。提交前已补齐O0/O2、ch1和window64 End失败用例。`Stage`栈帧约264B(Debug)/240B(Release)，较HEAD增加144/152B，4KiB MSP下未见溢出但无运行时高水位；服务级`available<64`分支在8192帧/64点契约下不可达，由detector核心测试覆盖1..63回退。复核报告：`Logs/Detector/M4_2_4_B/review.json`。

## 静默实机对比

同一A板、同一COM3采集方式、不改选项字节、不主动发声。

| 项目 | M4.2.4-A（3cca12ce） | M4.2.4-B（192c6a33） | 变化 |
| --- | --- | --- | --- |
| `STAGE_MS` | 21ms | **7ms**（两次） | −14ms |
| `GAP_MS` | 60–61ms | **49ms**（两次） | −11~12ms |
| `SCAN_MS` | 3ms | 3ms | 不变 |
| `SCORE_MS` | 4ms | 5ms | 波动 |
| `PROC_MS` | 1ms | 1ms | 不变 |
| `UPDATE_MS` | 19–20ms | 26ms | 运行间波动，B未改UI |
| `DONE_MAX_MS` | 157ms（当次） | 494ms（当次） | 负载不同，均<1000ms |
| 错误 | 全0 | 全0 | 无回归 |

B两次采集：

- `quiet-m424b-01`：BOOT后69.28秒；STAGE=7、GAP=49；SOUND CAND=8/DROPPED=0；CHIRP CAND=10/DONE=10/DROP=0；AUDIO OK=401零错误；LOG零错误；UI错误0。raw SHA256 `78336ba55783d90c210bc02e728bce5bb01e31e6678bdc858e854b1c9520ad87`。
- `quiet-m424b-02`（续采，无复位）：STAGE=7、GAP=49；SOUND CAND=11、CHIRP CAND=20，均零丢弃；AUDIO OK=889零错误。
- 证据：`Logs/Chirp/quiet-m424b-01/02-*`（serial.bin/jsonl/summary/flash）。

## 限制与边界

- 静默采集主要验证前台耗时与业务零错误；环境自然候选8–20次，不是标准音频10次受控识别。
- `STAGE_MS/GAP_MS` 是HAL毫秒墙钟最大差，1ms分辨率，含SysTick抢占；不是纯CPU或最坏执行时间。
- 本采集未触发候选密集/日志洪泛压力；主机合成压力结论仍只适用于其模型。
- UI本轮26ms与A的19–20ms差异需更多样本判断；B未修改UI，不能把26ms归因于B。
- 未做主动发声、双板、PPS波形、距离/方向或声学到达精度验收。

## 下一步

- **条件C1（UI）**：B之后Stage已降到7ms，UI 26ms成为更大的已测单项；若后续样本继续显示UI为主要GAP来源，可另立UI分片/降频增量，需显示验收。
- **主动标准音频**：环境允许发声时仍按M4.2.3-B计划补MATCH 10/10、漏检/重复和完成时延。
- 不自动进入C，不延长/缩小搜索或改日志策略；后续烧录另行授权。
