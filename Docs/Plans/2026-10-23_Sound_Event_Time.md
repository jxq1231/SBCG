# M4.1.3 候选帧关联本地PTP模型：实施计划

<!-- doc-status: implemented-quiet-scope; reviewed: 2026-10-24 -->

Goal：为已验证且已发布的候选输出同一采集/时间域内的本地PTP模型估计，或明确拒绝原因。用户已批准上一轮规划并要求实现。沿用当前工作区，不混入已有PPS改动。

Architecture：FIRST是从0开始的PCM帧号，POS=FIRST+1是该双声道组传输完成的整数进度坐标。只查询该坐标的线性模型，绝不声称采样/物理声到达时刻。前台单次TakeObservation，再把同一副本交给原三点自检和新时间服务；不增加第二个单槽消费者。候选在原End验证且SOUND_CAND出队后复制提交，无PCM指针。保留4锚点/4候选，等右锚点；2500ms超时，250ms至多一结果。失败输出无时间，LOCAL=1/SYNC=0。

Tech Stack：现有C11/GCC/CMake、用户App模块及主机Python/Fraction；No CubeMX change required。无新GPIO/TIM/IRQ/DMA/Cache关系。SAI2循环接收和原ETH/PTP只读观测沿用，不配置硬件。

- [ ] 创建App/Detector/sound_event_time.h，定义Init/Submit/Observe/Process/Invalidate/Snapshot；先写Tests/SoundEventTime/test_event_time.c及运行器。RED要覆盖FIRST+1、等待右锚、无左锚/超时、失效不泄漏时间、固定队列、会话与序号、回绕、uint64边界。
- [ ] 新增sound_event_time.c，复用AudioTimeMap_Estimate，不复制插值算法；队列/元数据有界，事件序号不复用。每条成功行包含SEQ/SESSION/FIRST、L/R源锚序号、EST_NS/LO_NS/HI_NS；失败只带身份/原因/CORE，不携带时间。状态累计排队/成功/拒绝/丢弃数。
- [ ] audio_time_map_monitor.c/.h抽取Observe入口，保留原Process兼容包装。app_main.c在SBCG_USE_SOUND_TIME分支单次取锚、双路分发，复制最新音频快照后刷新tick再处理事件队列。初始化位于音频/锚点服务之后。
- [ ] sound_detector_service.c只在归还后出队交接；确认历史失效或块缺口时同步清事件模型。time_sync.c的既有InvalidateConsumers在硬件校时/调频前通知新服务。用户cmake/audio-layout.cmake在已有LwIP音频组合登记宏和源；FW=SBCG-M4.1-TIME。
- [ ] 新增真实C桥接和Python独立Fraction参考，至少30组逐值比较；补原Audio/App/时钟代际测试，验证旧自检继续接收相同锚点，DHT低态不输出，无IRQ/Begin/End变化。
- [ ] 原预设Debug/Release构建，原ETH/音频布局检查，7份生成文件哈希保持，独立复核；冻结固件与输入哈希。硬件观测时按当前授权核对A UID，保存BOOT边界/采集数据；若不能实测则如实标待验，不做功能完成提交。
- [ ] 更新Docs/Protocol.md、Project_State.md、README.md、Document_Status.md及本轮Milestones报告；保留M4.1.2背景额外候选/失败。只提交已验证增量，作者和提交者jxq，不推送、不混入旧PPS生成/源码变化。

验证命令：python -B Tests/SoundEventTime/run_tests.py；python -B Tests/Audio/run_tests.py；python -B Tests/App/run_tests.py；python -B Tests/ClockEpoch/run_tests.py；python -B Tools/Monitor/autodebug.py build --preset Debug --json；Release同上。主机映射通过不等于物理声学延迟、标准音频识别、双板±10us或测距通过。本轮不调检测阈值，不接入LCD/TouchGFX。


执行结果见[本轮报告](../Milestones/M4_1_3_Sound_Event_Time.md)：64组真实C/Fraction通过；原TIME含5次归还失败，保留FAIL。TIME-R1统一32行队列、每轮一行，BOOT后67.933秒静默范围通过，9模型精确一致、1条NO_LEFT明确拒绝。用户取消进一步主动发声，未宣称R1受控刺激或声学精度通过。本计划不再作为重复烧录指令。
