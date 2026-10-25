# M4.2.3-A 批量门控核心：软件等价验证记录

<!-- doc-status: stage-a-complete-stage-b-not-started; reviewed: 2026-10-26 -->

## 结论

纯C批量门控入口 `SoundDetector_PushBatch` 及逐字段等价测试已在电脑端完成并通过，功能提交 `8862ffd`。在同一输入流上，完整64点快速路径与回退路径的候选事件、质量字段和全部有效逻辑状态均与原逐帧 `SoundDetector_Push` 一致。

**未接入任何 App 运行路径，未改固件标识，未烧录或连接开发板。** 当前A板仍为 `SBCG-M4.2-DIAG`（Debug前缀d3e084b1d83e）；本阶段不提供板端加速倍数、SCAN≤10ms、任务完成延迟或声音到达精度结论。M4.2.3-B（Chirp扫描接入）尚未开始。

## 本轮目标与范围

只实现[优化方案](../Plans/2026-10-25_Audio_Scan_Optimization/task_plan.md)的第一阶段：在纯C能量门控中增加最多64点的批量输入，验证其与逐样本实现完全等价。不迁移Chirp扫描或原声音候选服务，不改`App/app_main.c`的固件标识，不连接或烧录开发板。

## 文件与职责

| 文件 | 本轮职责 |
| --- | --- |
| `App/Detector/sound_detector.h` | 批量上限宏、`SoundDetector_PushBatch`接口合同及输入借用/消费/拒绝语义 |
| `App/Detector/sound_detector.c` | 完整64点快速统计与全部逻辑状态写回；其他输入回退原逐帧`SoundDetector_Push` |
| `Tests/SoundDetector/test_batch.c` | 逐字段对照事件与状态；`-finstrument-functions`钩子区分快速/回退路径；WAV及噪声回放 |
| `Tests/SoundDetector/run_tests.py` | 原生O0/O2严格编译、批量测试编排及独立临时输出目录 |
| `Tests/SoundDetector/reference_check.py` | 仅增加可选`--output-dir`；原Fraction公式与175组用例未改 |
| `Docs/...` | 本报告、计划/进度状态及入口/状态文档同步 |

## 接口与语义摘要

```c
SoundDetectorResult SoundDetector_PushBatch(
    SoundDetector *detector, uint32_t session, uint64_t first_frame,
    const int16_t *samples, uint16_t max_frames,
    uint16_t *consumed, SoundCandidate *out);
```

- **快速路径资格**：`max_frames==64`、`used==0`、配置窗64、窗口连续（无输入或同session且`last_frame!=UINT64_MAX`且`first_frame==last_frame+1`）且`first_frame<=UINT64_MAX-63`。一次完整窗统计后只调用一次`CompleteWindow`。
- **回退**：部分窗、不足64点、其他窗口配置、会话变化/缺口/重复/倒序、uint64邻界时，恰好调用一次原`SoundDetector_Push`并只消费1点；不吞掉后续输入。
- **拒绝顺序**：`session==0`、未配置、NULL、长度0或大于64、同session末帧已为`UINT64_MAX`等，均在读取PCM前拒绝；`session==0`保留原清历史语义。
- **消费语义**：成功消费为1或64；INVALID为0。入口清零`consumed`与`out`；调用方按`consumed`推进绝对帧。
- **数值路径**：int32样本平方、uint64平方和、int64状态和；复用原`CompleteWindow`的整数能量比较、阈值等号、冷却与确认语义。
- **无依赖**：不访问HAL时钟、DMA、IRQ、日志或动态内存；`samples`仅在本调用借用，不保存指针。

原`SoundDetector_Push`/`CompleteWindow`源码未改（`git diff HEAD`仅文件头说明与尾部新增函数，共+60行）；新增第二个调用者使编译器不再把`CompleteWindow`内联进`Push`，这是两种构建Flash体积变化的原因，见下节。

## 调用链与硬件边界

测试调用链仅存在于主机：`run_tests.py → gcc → test_batch.c → SoundDetector_PushBatch（快速统计/回退）→ 对照路径逐点调用SoundDetector_Push`。

`App/`运行路径中没有任何`PushBatch`调用者，Chirp扫描和原候选服务仍走逐帧接口。**No CubeMX change required**；无新GPIO、TIM、IRQ、DMA、SAI、LTDC、ETH或Cache配置，无DMA缓冲所有权变化。

## 测试先行证据

先写对照测试，缺失实现使用仅消费1点的临时链接替身；严格编译成功后，完整64点消费断言行为性失败：`Logs/Detector/M4_2_3_A/native-3yi7yu5p/RED-batch-O0.log`。独立复核用`git show HEAD`原始源码加替身另行复现同一红灯（`FAIL test_batch.c:145`），证明不是链接失败。原门控O0/O2测试在红灯环境下仍各通过6553114断言。

## 已执行验证

2026-10-26复跑命令（工作树与功能提交`8862ffd`相同）：

```powershell
python -B Tests/SoundDetector/run_tests.py
python -B Tests/SoundDetectorService/run_tests.py
python -B Tests/ChirpService/run_tests.py
python -B Tests/ChirpMatcher/run_tests.py
python -B Tests/Audio/run_tests.py
python -B Tests/App/run_tests.py
python -B Tests/SoundEventTime/run_tests.py
cmake --build --preset Debug --parallel
cmake --build --preset Release --parallel
python -B Tools/Monitor/check_audio_layout.py --preset Debug
python -B Tools/Monitor/check_eth_layout.py --preset Debug
python -B Tools/Monitor/check_audio_layout.py --preset Release
python -B Tools/Monitor/check_eth_layout.py --preset Release
```

| 验证项 | 结果 |
| --- | --- |
| 原门控（O0/O2） | 9组全PASS，各6553114断言 |
| 批量对照（O0/O2） | 11项全PASS；checks=405111537、calls=10724761、fast=126105、fallback=10598652、events=517 |
| 精确Fraction参考 | PASS，175用例/2240340样本/711事件 |
| 相关服务/应用回归 | SoundDetectorService、ChirpService、ChirpMatcher、Audio、App、SoundEventTime均退出0 |
| Debug构建 | PASS；Flash 274600字节、RAM 317920/327680、AUDIO_DMA 64KiB；仅保留原RWX LOAD警告 |
| Release构建 | PASS；Flash 184252字节；其余同上 |
| 音频/以太网布局 | 两配置四项检查均`AUDIO_LAYOUT_OK`/`ETH_LAYOUT_OK`（host only） |

本次复跑产生的原生测试输出目录为`Logs/Detector/M4_2_3_A/native-9tj1wp1z`；逐项计数与阶段内既有记录一致，构建ELF SHA256也与既有`build-after`记录一致（Debug `dcb9368a3c54f931ebed9725943840d97523d9849bccea387d3926880a05adf0`，Release `2f669ea0dcbbd6a20c97a883035543b7c288a5f565a8332fb1a2824edc268099`），构建可复现。

### 相对DIAG基线的二进制差异

当前工作树构建与板上DIAG（Debug d3e084b1d83e）只差`sound_detector.c`的新增函数与文件头注释。Flash体积变化已定位：

| 构建 | DIAG Flash | 当前Flash | 差值 | .text差值 |
| --- | --- | --- | --- | --- |
| Debug | 274568 | 274600 | +32字节 | 0x36684→0x366A4（+0x20） |
| Release | 184188 | 184252 | +64字节 | 0x21464→0x214A4（+0x40） |

差异来自`CompleteWindow`原先内联在`Push`中，新增第二个调用者后改为独立函数：Debug下`Push` 580→282字节、`CompleteWindow`新增326字节，Release下`Push` 560→282字节、`CompleteWindow`新增338字节；原逐帧源码未改。`PushBatch`因无运行时调用者被链接器`--gc-sections`丢弃（两配置map的“Discarded input sections”分别记录0x1B0/0x1A2字节，ELF无`PushBatch`符号）。

**这些构建与哈希不是硬件验证**：未烧录、未连接开发板；A板仍运行原DIAG镜像，当前镜像不存在“实物通过”结论。

### 独立只读复核

复核者独立读代码、对比HEAD、运行原生测试、自建差分模糊测试并复现红灯，结论**PASS_WITH_RESERVATIONS**：代码/测试/合同=**PASS**，无blocker/major；保留项仅为文档状态收尾（本提交已处理）及`run_tests.py`的`has_batch`字符串探测属info级健壮性提示。独立证据包括1677163条差分断言（8种子、O0/O2及UBSan三变体）和原测试独立重跑；复核未重跑ARM构建、布局检查或服务回归，本报告上表由本轮另行复跑。完整报告：`Logs/Detector/M4_2_3_A/review.json`。

## 已知限制与未验证边界

- M4.2.3-B未开始：无实机扫描耗时、SCAN≤10ms、任务完成延迟或声音到达精度证据；本阶段也没有实机加速倍数。
- 批量接口尚未有任何运行时调用者，构建仅证明可编译、可链接与内存布局不变；主机测试不能证明200MHz STM32上批量路径的实际收益。
- `run_tests.py`以源码字符串探测批量接口是否存在，若未来签名换行重排可能误判并追加替身；该失败是显式链接错误而非静默错误，可在后续测试基础设施中加固。
- 主机测试为Windows MinGW GCC 6.3.0；未在ARM目标运行测试，未测真实声学数据分类率或到达时刻。

## 下一步

按[方案](../Plans/2026-10-25_Audio_Scan_Optimization/task_plan.md)第4节实施M4.2.3-B：只把Chirp扫描迁移到`SoundDetector_PushBatch`，保持原候选阈值、所有权、Begin/End事务和失效清理；新镜像烧录需另行取得授权。本增量到此为止，不自动开始B。
