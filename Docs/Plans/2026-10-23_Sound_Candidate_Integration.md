# M4.1.2 PCM候选检测接入与受限单板验收

<!-- doc-status: implemented-pipeline-acoustic-pending; reviewed: 2026-10-23 -->

用户明确推进M4.1.2，随后确认板已连接并授权在无示波器、无固定音频、无网线情况下自行设计验收；电脑扬声器可用。当前仅A/COM3，ST-LINK序号066EFF3832394D3457253156。先核对A UID，再按现有配置烧录通过软件验证和复核的SBCG-M4.1-CAND，不改选项字节。此授权属于当前工作流，不重复请求同一烧录许可。

> 执行结果：软件及单板管线观测已完成；R1五音各一候选，但背景另有2候选，严格实验仍FAIL_OR_INCOMPLETE。历史失败、固件哈希及补验见[本轮报告](../Milestones/M4_1_2_Sound_Candidate_Integration.md)。本计划不再作为重复烧录指令。

## 一个独立目标

现有AudioCapture唯一Begin/End借用内计算候选，End成功后发布有界候选帧/质量；End失败或索引/连续性失效不得泄漏未验证候选或沿用旧确认窗。不是标准音频分类、PTP事件查询、PPS精度或测距。PPS原软件已在工作区，但未有波形验收，不混称完成。

## 已选方案

立即打印候选会在End失败后泄漏无效结果；另起DMA消费者违反唯一认领。选用检测器工作副本+4条暂存候选+4条已验证FIFO。Stage只在原Begin成功后调用；Complete紧随End返回；Process在认领结束后限速发送。不中断DMA，不保存samples指针。

默认实验配置：声道0，窗64帧，去直流RMS开256/关128，连续2高窗确认、8低窗布防/再布防，冷却24000帧。参数集中、初始化日志完整输出，标EXPERIMENTAL；不称为已标定阈值。每个输入为双声道组的一帧，只取指定声道；以后再根据受控录音确定标准音频阈值/分类。

Stage校验非NULL、8192帧、index_valid、非零session、first+count不溢出及块年龄<250ms。无旧历史时首个非连续标志允许从本块重建；已有历史且!contiguous/gap>0/会话或帧坐标变化则工作副本清历史，成功提交时丢弃旧排队候选并计重置。INVALID/End失败清全局确认历史和待发队列，统计拒绝，不记录该无效块计算出的候选数。按原AudioStatistics结果调用End，不因检测元数据无效阻断原电平业务。

所有8192帧继续推进工作副本；暂存满后只累计丢弃数，End成功才将丢弃统计提交。FIFO满也显式丢弃并计数，保持时间顺序；没有动态分配。Process检查采集运行、session、index_faults及最近回调年龄<1000ms，异常清历史/队列。已验证候选排队年龄>=1000ms丢弃；每250ms至多发送一条候选，每1000ms至多一条状态，不补发时间积压。DHT低态不调用AudioCapture处理，沿用现有日志队列。

串口新增SOUND_INIT/SOUND_CAND/SOUND_STATE，全部有界<256字节、CRLF、SYNC=0；CAND含序号、session、声道、FIRST/CONFIRMED帧、E_AC/PEAK/CLIP及LOCAL=1，不输出虚构PTP时刻。计数饱和，序号不作为跨开机唯一键。用十进制手工转换uint64避免nano printf依赖。

## 文件与执行

- 新增App/Detector/sound_detector_service.c/.h：Stage/Complete/Process、状态及有界FIFO，纯前台。
- 修改App/Audio/audio_capture.c/.h：初始化实验检测服务、唯一认领内调用和归还后发布、当前采集快照健康检查。
- 修改App/app_main.c：SBCG_USE_SOUND_DETECTOR时标识SBCG-M4.1-CAND，保留原PPS等条件编译。
- 修改用户文件cmake/audio-layout.cmake：音频启用时登记内核/服务及宏。CubeMX生成CMake不改。
- 新增Tests/SoundDetectorService/及Tests/Audio/test_sound_capture.c，原Audio运行器登记集成组合。先观察缺失功能RED，再实现GREEN，覆盖先验后发、End失败清历史、跨块、元数据/会话/缺口、FIFO溢出/过期、日志上限、声道和tick回绕；旧音频行为保持。
- 运行检测内核、服务、音频及App/PTP关联回归；Debug/Release构建，ETH/音频布局和生成SHA检查，独立复核后冻结。记录已有PPS改动，不在提交中掩盖未测内容。

## 本次硬件窗口与补验

1. 只读串口确认当前状态；ST-LINK按原配置读UID核对A后，仅烧录本次已构建复核文件，校验复位，保存BOOT及约60秒串口。
2. 无网线应LINK=DOWN、UDP LINK_DOWN计数，不要求电脑UDP或B同步通过；系统心跳/音频帧持续、无自发BOOT/异常复位。
3. 记录背景窗口；生成可复现的PC短音（1kHz、约0.5秒、低幅度、间隔至少1.5秒）并通过默认扬声器播放数次，记录主机播放窗口。观察声音候选数量/帧/质量与前后背景比较，扬声器到麦克风的路径未校准，不能把主机播放时间当板端到达基准。
4. DHT如未连接，TIMEOUT应如实记录，不为通过而修改供电或引脚。保留原始失败及边界，不因声音触发不明显而随意调参重写成通过。
5. 必补：示波器D9的100us脉宽/首脉冲，B恢复后交换机冷启动<=30秒及连续10次±10us；课程标准音频/相邻到达精度；真实教室静默60秒、不同播放器；事件帧→PTP映射及声学延迟；网络与温度完整联合回归。当前可通过的仅单板管线和实验刺激响应范围。
