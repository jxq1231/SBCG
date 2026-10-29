# 实施记录

2026-10-26：用户明确“先保存候选附近经End验证的PCM（有界、不影响搜索/匹配），离线做宽带相关，区分声学失真/多径与门限/模板；同时受控距离/音量/方向重复观察raw相关分布；若确认只是门限过严，用负样本证明放宽后误报可接受，不能直接改0.80”。已读根规则、M4.2.5实机证据与评分/服务/网络边界；确认运输通道为UDP 5008→6008、范围±2048（8544帧定界）。

- 固件：`udp_chirp_capture.c/.h`（56字节大端头+≤512×int16分片、懒绑定、计数、$CHIRP_CAPTURE）；`chirp_service.c/.h` 捕获缓存/右尾补丁/零拷贝sink/计数；app_main 注册sink；CMake LwIP分支注册。
- 链接：定界缓冲非BSS，改置 `.ChirpCaptureSection`（0x20020000，NOLOAD，17088B）；`stm32f746_audio.ld` 新增 CAPTURE 区并更新 `_end` 与断言；Debug/Release RAM 97%上限内的 BSS 仍限 DTCM（0x20010000 前），check_audio/check_eth 布局全 GREEN。
- 测试：`test_pcm_capture.c` O0/O2 全过（无sink只计miss、单块裁剪、补尾标记1234、两任务连续交接、左裁剪、End失败后重缓存）；原 ChirpService/诊断/压力/WAV回归全过；App/Audio/SoundDetector(Service)/ChirpMatcher/SoundEventTime/TouchGFXService/Udp/UdpProbe/Network/BoardSync/Ptp 12组回归退出0。
- 主机：`udp_chirp_capture.py` 5用例如 乱序/重复/缺片/本地UDP回环通过；`chirp_capture_analysis.py` 4用例如 精确对齐1.0、带外噪声宽带下降/带限恢复、负样本FA曲线、错位峰追踪通过；真实模板WAV SMOKE 通过。
- 已知修正：捕获跨度上限由4KB的DTCM头寸推导改为固定段放置；CopyCaptureRange 补尾写法初始重复覆盖头段（已修）；DIAG=2→3 后 INIT 字段变多，测试改为匹配 `,DIAG=3,`。

下一步按计划：两配置构建已 GREEN，待转换后与用户确认烧录/受控实验的步骤与本机ip（192.168.10.100）可达性。