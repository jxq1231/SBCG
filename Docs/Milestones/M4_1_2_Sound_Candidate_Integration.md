# M4.1.2 PCM候选检测接入：单板管线与未通过项

<!-- doc-status: completed-integration-evidence-acoustic-pending; reviewed: 2026-10-23 -->

2026-10-23仅A板，UID `001A002D:30365105:36363631`、COM3。用户授权当前已连接板测试及电脑扬声器播放；无B板、网线、示波器或课程标准音频。本增量完成实验能量候选的安全接入和管线运行观测；**严格声音实验 FAIL_OR_INCOMPLETE**，不能宣布M4声音识别、到达精度、双板±10us或测距通过。

## 实现与文件

| 文件 | 本增量职责 |
| --- | --- |
| App/Detector/sound_detector_service.c / .h | 工作副本暂算、End成功才提交、两级4条队列、过期/断流清除、限速本地帧日志 |
| App/Audio/audio_capture.c / .h | 在原唯一PCM借用中检测，归还后发布；三处快照/时间顺序修正 |
| App/app_main.c | 检测功能固件标识优先为SBCG-M4.1-CAND-R1；原PPS改动保留并单独区分 |
| cmake/audio-layout.cmake | 用户自有、可再生成保留的源列表/编译宏登记 |
| Tests/SoundDetectorService/run_tests.py / test_service.c | 服务生命周期、跨块、失效、队列/限速、整数边界 |
| Tests/Audio/run_tests.py / test_sound_capture.c | 真实AudioCapture路径、归还后PCM不可访问、无借用期日志、DHT门控及三处IRQ时间竞态 |
| Tools/Monitor/sound_candidate_trial.py | 有时限采集、确定性PC短音、启动UID/固件检查、既有证据防覆盖 |
| Tools/Monitor/tests/test_sound_candidate_trial.py | 旧输出拒绝覆盖、空前缀无副作用 |
| Analysis/sound_candidate_trial.py | 严格实验分析；周期覆盖、最终状态时效、候选会话/序号/坐标，不放行失败 |
| Analysis/tests/test_sound_candidate_trial.py | 正例及背景/重复/跨会话/过时状态/重启/DMA错误等反例 |
| Docs/Plans/2026-10-23_Sound_Candidate_Integration.md / 本报告 | 设计、实测边界、补验列表 |
| Docs/Protocol.md、Project_State.md、README.md、Document_Status.md、Project_Roadmap.md、Hardware_Resources.md及根README.md | 现行接口和阅读入口更新；旧PPS工作区变化保留 |
| Docs/Milestones/M4_1_1_Sound_Candidate_Core.md | 将“未接入”明确标为历史范围 |

默认实验配置：声道0、64帧去直流能量窗、开启RMS256/关闭128、连续2高窗确认、8低窗再布防、冷却24000帧。未按课程标准音频标定。本轮所有试验均保持相同阈值，没有用调参掩盖背景候选。

`main → App_Process → AudioCapture_Process → 原Begin → 统计/Stage → 真实End → Complete → Process限速日志`。Stage不保留PCM指针、不打印；工作副本隔离未验证样本。End失败或元数据/流健康失效清确认历史与队列。有效块8192帧，两级队列各4条，过载/超龄显式计数；每250ms至多1候选、每1000ms至多1状态，DHT低态延后前台处理。

## IRQ、DMA、CubeMX、Cache

无新IRQ或DMA关系。沿用SAI2 B接收、DMA2 Stream7 Channel0、P→M halfword circular、32768个halfword/64KiB双声道缓冲；半满/满各8192帧，经原回调记录元数据再由前台唯一Begin/End消费。DMA2_Stream7/SAI2 IRQ优先级4/0保持。候选算法和日志均在前台。

CubeMX change: **No CubeMX change required**。本次7份生成配置/文件SHA均保持；没有改引脚、RCC、DMA、NVIC、ETH、I2C、SAI或LTDC初始化。已有TIM2/PPS来自此前用户生成，不作为本增量新硬件配置。音频0x20010000..0x20020000及ETH布局不变；I/D Cache仍关闭，不添加cache维护。检测工作副本与队列在DTCM，不与DMA共享所有权。

## 软件与固件验证

- 实际执行 `python -B Tests/SoundDetector/run_tests.py`：O0/O2各9组/6,553,114检查；175组Fraction参考、2,240,340样本、711事件逐值一致。
- `python -B Tests/SoundDetectorService/run_tests.py`：5组通过；`python -B Tests/Audio/run_tests.py`：11个组合通过。App、PpsIntegration/PpsBsp/PpsService、Ptp、ClockEpoch关联运行器通过。
- `python -B -m unittest discover -s Tools/Monitor/tests`：115项通过；`python -B -m unittest discover -s Analysis/tests`：110项通过。新增反例均先观察RED，再修复GREEN。
- 沿用CMake Debug/Release预设构建成功、无新增未解释警告；Debug Flash144784B，Release83648B；RAM布局预留317920B，两配置ETH/音频布局均通过。独立复核已完成。
- 冻结 `build/Firmware/SBCG-M4.1-CAND-R1/manifest.json`：Debug SHA256 `4d8ae40acd5155b906617a037205d40b7dc99fb8c62fbf2b674a0316c0351066`；Release `b161eaf915563ad052986a8c141c28270016dd8114fef9b590cd2fa25dd3226f`。108份固件输入哈希与最终源码一致。
- 使用原 `.vscode/openocd-stm32f746.cfg` 烧录上述Debug，原命令 `program {build/Firmware/SBCG-M4.1-CAND-R1/SBCG-M4.1-CAND-R1-Debug.elf} verify reset exit`，Observed **Verified OK**。随后为获得明确BOOT边界额外执行一次原配置 `reset halt / read_memory UID / reset run`，未再烧录；未改选项字节。
- 冻结镜像包含此前未提交的PPS生成/应用快照。当前检测增量提交不吞入这些受保护改动，单凭本提交不等于该工作区完整快照；精确复现依赖manifest的108输入和保留的PPS增量。

## 三轮记录，分别保留

所有原始数据位于git忽略的 `Logs/Detector/M4_1_2/`，不以清洗结果覆盖raw；时刻为主机接收关联窗口，不是物理检测延迟。

| 记录 | 实际结果 | 结论 |
| --- | --- | --- |
| trial-01，SBCG-M4.1-CAND | 90.097秒；背景0候选；5次短音均有响应，但共16候选，最后一音附近及恢复阶段多出候选；19条音频STALLED | 保留FAIL。STALLED经定位为摘要使用旧tick与新IRQ快照的下溢，非DMA停止；R1只修正时间读取顺序，未改阈值 |
| trial-02-R1 | 等待45秒未捕获匹配BOOT，未播放短音，保存FAILED和关闭串口状态 | 不能计作验收；后续只复位建立明确启动边界。未据“烧录成功”推定已采集到BOOT |
| trial-03-R1，SBCG-M4.1-CAND-R1 | 90.096秒；一个匹配BOOT，5次短音每个2.5秒响应窗恰好1候选；背景42.285/47.111秒另2候选，总计7；最后恢复段0候选 | 管线观测通过；严格实验仍FAIL_OR_INCOMPLETE，背景零候选/全程仅5候选两项未通过，来源未确认 |

刺激为电脑默认扬声器的1kHz/0.5秒短音，PCM16单声道48kHz、数字峰值0.12、5ms淡入淡出，播放时刻60.097/63.055/66.069/69.084/72.001秒。WAV SHA256 `b41925bb9c0cc83c3e172d9642be54e7abbf9d62432bf3e4482763ebf15e6d05`。声压、系统音量、距离和环境声未校准；它不替代教师标准WAV。**不能将背景候选直接断言为算法误报或某个外部声源，也不能证明标准识别抗干扰。**

R1窗口管线证据：88条HEARTBEAT序号连续、TX_ERRORS0；88条AUDIO均RUN/VALID1、DROP/ERR/HAL/DMA0；88条AUDIO_POS连续、无缺口/索引故障；末SOUND_STATE有效块527，拒绝/重置/丢弃/错误均0，7个候选SEQ1..7、SESSION1、帧坐标严格推进、均CLIP0，最终ARMED1/PENDING0。43次codec读取ID8994/ERRORS0。原始二进制与JSON逐行一致，BOOT后1792完整行严格ASCII；复位前343行不计入实验，采集截止的15字节尾片段另以hex保留，不当完整行解释。

PPS末状态QUALIFIED、EMITTED89/MISSED0/ERRORS0，仅证明本地排程/寄存器观测运行，**没有波形、脉宽、两板边沿偏差证据**。温度从测试前即UNREADY/TIMEOUT，本次不改线、不宣称温度复验通过。无网线，ETH DOWN和UDP LINK_DOWN符合现场条件，但原网络业务未实测。所有声音事件仍LOCAL1/SYNC0，无事件帧→PTP接口或测距结果。

## 必须补验及下一独立目标

| 补验 | 所需条件/操作 | 判据与当前边界 |
| --- | --- | --- |
| 背景与标准音频 | 可控安静房间、教师标准音频/干扰录音、固定播放器位置音量；记录背景并重复 | 严格零背景/去重实验本轮未通过，先查声源和标注，不任意调阈值 |
| 物理到达时刻 | 事件帧→PTP独立接口及codec/SAI/FIFO/声学路径校准、可参考激励 | 当前FIRST/CONFIRMED仅本地采样帧；不能报告±100us |
| A PPS电气波形 | 示波器探头接D9/PA15/CN7-2、地接本板GND | 量测正脉冲、≥1us宽度（目标100us）、无异常首脉冲；当前计数不是通过证据 |
| 双板同步课程验收 | A/B/PC接交换机、两板更新、双通道示波器 | 冷启动≤30秒收敛，收敛后连续10次边沿差最大绝对值≤10us；本轮NOT_RUN |
| 网络/温度联合回归 | 恢复网线及原D4/PG7、3.3V DHT11接线 | 重验三路UDP/PTP、温度恢复、检测并行无丢块；本轮NOT_RUN |

下一独立开发建议：先实现本地候选帧到现有音频/PTP模型的有界查询，明确失效/区间/延迟限制；它无需B板，但仍不能绕过声学校准或冒称精度。需用户选择后再启动，本轮停止。
