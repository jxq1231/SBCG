# M4.2.6 候选附近PCM捕获（里程碑实施记录，实物待验）

<!-- doc-status: implemented-awaiting-hardware; reviewed: 2026-10-26 -->

## 目标

候选附近经End验证的PCM有界快照经UDP下传，支持离线宽带相关分析；不改变门限/搜索/匹配，不改0.80。

## 已实现（软件侧验证完成）

| 组成 | 文件 | 说明 |
| --- | --- | --- |
| 捕获 | `App/Detector/chirp_service.c/.h` | 缓存时刻复制 `hint−2048..hint+6496` 到8544帧定界缓冲；完成时按新块补右尾；零拷贝sink；捕获计数 |
| 下传 | `App/Network/udp_chirp_capture.c/.h` | UDP 5008→6008 懒绑定分片（56B头+≤512×int16）；`$CHIRP_CAPTURE` 行；错误/跳过计数 |
| 接线 | `App/app_main.c`、`CMakeLists.txt`、`cmake/linker/stm32f746_audio.ld` | sink注册；LwIP分支源文件注册；CAPTURE固定区（0x20020000）与断言 |
| 测试 | `Tests/ChirpService/test_pcm_capture.c`、`run_pcm_capture_tests.py` | O0/O2 全过：无sink仅miss、裁剪、补尾、连续交接、End失败重缓存 |
| 主机 | `Tools/Monitor/udp_chirp_capture.py`（+测试） | 6008收包重组、`--tag`受控条件、完整性计数 |
| 分析 | `Analysis/chirp_capture_analysis.py`（+测试） | 离线全对齐宽带相关、2–6kHz带限变体、静音负样本FA曲线、lag_shift |

## 软件验证结果

- 原生 ChirpService 全套 + 12 组回归退出 0；Debug/Release 构建 GREEN（RAM 97%、FLASH 26.9%/18.0%，CAPTURE 17088/17408B）。
- check_audio_layout / check_eth_layout 两配置全部 PASS。
- 主机重组 5 用例、离线分析 4 用例全过；真实模板 WAV SMOKE：宽带峰在预期lag、带限恢复。

## 实物验收（待烧录授权，未执行）

1. 烧录 Debug `SBCG-M4.2-PCMCAP` 至 A 板（openocd program+verify，受控reset）。
2. PC 固定 192.168.10.100，先启动 `udp_chirp_capture.py --out Logs/Chirp/PCM/<trial> --tag dist=..;vol=..;dir=..`。
3. 原接线、同一 `standard_chirp_2-6khz_10x_20s.wav` 20秒/一次，记录串口 `$CHIRP_SEARCH`/`$CHIRP_CAPTURE`。
4. 核对：每完成任务恰好一包PCM、`$CHIRP_CAPTURE` 与 `$CHIRP_SEARCH` 按 (SESSION,ID) 对账、PKTS==TOTAL、资源/跳过错误计数 0。
5. 离线：`chirp_capture_analysis.py --captures <trial> --template chirp_2-6khz_50ms_template.wav --out out.json`，对比宽带峰/带限峰/0.80、`lag_shift`，读取负样本FA曲线。
6. 受控重复（距离/音量/方向各≥5次）后聚合 CORR_PPM 分布与负样本，确定能否把“门限过严”与“声学失真/多径”分开；只有负样本证明放宽后误报可接受才讨论门限，不在本里程碑改0.80。

## 已知限制

- 捕获内容为模板幅度级（±2048窗外静音）；UDP 发送不重试，RESULT 只表示栈接受。
- 左/右裁剪在 CLAMP/左/右标志与 TAIL 中如实上报；离线分析自动以实际跨度为本。
- 本里程碑不证明 PTP 时间、物理到达精度、双板配对或测距通过。