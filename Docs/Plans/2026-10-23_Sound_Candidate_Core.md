# M4.1.1 单板候选声音检测内核：设计及执行计划

<!-- doc-status: completed-software-plan; reviewed: 2026-10-23 -->

用户2026-10-23明确暂不烧录/验收M2.3.3，继续推进下一步。沿用已提出的单机规划第1项：纯C候选帧检测及去重。PPS被冻结待验收，本增量不接入固件、不操作开发板，也不混入其未提交改动。当前只有A板；无新增CubeMX/硬件配置。先前“PPS验收后再做检测”的顺序由本次用户优先级覆盖。

## 目标与方案

独立可测目标：单声道PCM16流→候选事件的session、粗略起始窗帧号、确认帧号和质量字段；静默不触发、持续声音不重复、断流清历史。不是标准音频分类、声音到达精定位或事件PTP查询。

三种候选方案比较：单点绝对幅值实现最少但易受直流/瞬态影响；推荐短窗去直流能量加连续窗、滞回和静默再布防；模板相关适合后续指定音频精定位，但当前未提供标准音频文件及波形参数，不猜测频率。固定门限由调用方显式提供，本增量不根据未知房间数据宣称参数已标定。

## 数学与接口约定

对N=2..256个样本，S=sum(x)、Q=sum(x*x)，E=N*Q-S*S。用64位整数计算，准确比较E与rms_threshold²*N²，避免先除法造成边界漂移。PCM16 x在[-32768,32767]，N²*32768²≤2^46，平方及累加不溢出int64/uint64。输出mean_square_ac为确认窗中最大的floor(E/N²)，单位PCM16幅值平方；峰值绝对值为0..32768，截幅为是否出现INT16_MIN/MAX。

配置SoundDetectorConfig：window_frames 2..256，on_rms_pcm 1..32768，off_rms_pcm 0..on-1，on_windows和quiet_windows为1..65535，refractory_frames为uint32帧数，0允许。没有暗含采样率；所有时长按帧数。

公开API：SoundDetector_Init(state,config)，SoundDetector_Reset(state)，SoundDetector_Push(state,session,frame_index,sample,out)。配置复制到调用方拥有的state；out不可NULL，非事件时清零；每输入一帧最多输出一个SoundCandidate，不保留PCM指针，无动态内存、HAL或日志依赖。调用方逐帧选择一个声道；立体声两个int16元素算一帧，不能将元素偏移当帧号。

返回NONE/EVENT/DISCONTINUITY/INVALID。会话非0；首帧可从任意uint64开始；正常要求同session帧号严格+1。缺口、倒序、重复或session变化清窗/待确认/去重历史并要求重新安静，当前样本作为新流首样本消费，返回DISCONTINUITY。session=0是非法输入，清历史；同session在UINT64_MAX之后不允许回绕，返回INVALID直到显式Reset或新session。NULL out不消费状态。

## 状态及边界

初始化先等quiet_windows个连续低能量窗（E≤off²*N²），才布防；开机已有持续声音不作为新到达事件。布防后须on_windows个连续高能量窗（E≥on²*N²）；中间能量窗也打断高能量连续性。事件onset_frame为第一高窗起点，confirmed_frame为最终确认窗的最后一帧，质量汇总这段确认窗。

报告一次后撤防，必须再次连续安静且距离上次confirmed_frame≥refractory_frames才重新布防。持续强音或仅处于两阈值之间不会续报。所有条件只在完整窗边界推进，未满窗跨调用保留；onset相对首个合格高能量窗末帧回溯N-1帧；不是物理声起点，实际声学偏差未标定，确认延迟和窗量化必须显式保留。N64/48kHz约1.33ms只是示例，不是当前板精度保证。

## 文件及实施步骤

1. 新建App/Detector/sound_detector.h声明上述类型及所有权；Tests/SoundDetector/test_sound_detector.c写真实行为断言，runner按现有原生测试方式编译。先用编译成功但返回失败的缺失实现确认RED（本轮已观察），不将链接错误当行为失败。
2. 新建App/Detector/sound_detector.c实现64位能量和静默/布防/确认/再布防状态；运行C测试确认GREEN。测试覆盖：配置与空指针、60秒静默/直流/有界噪声、阈值等号、跨8192帧边界、持续音去重、单窗瞬态、滞回区、静默再布防、冷却边界、缺口/重复/会话变化、截幅与uint64末端。
3. 新建Tests/SoundDetector/reference_check.py及测试桥接源，用Python Fraction整窗参考核对C事件/质量；按-O0与-O2运行真实C测试，固定种子随机窗流，保存可重现的命令与结果，不使用硬件日志冒充输入。
4. 运行python -B Tests/SoundDetector/run_tests.py，随后现有Debug/Release构建；检查当前ELF与冻结PPS哈希相同，7个生成文件保持，说明算法尚未被固件调用。无新IRQ/DMA，不改Cache。
5. 独立代码审查；修正真实问题并仅复跑受影响检查。新增Docs/Milestones/M4_1_1_Sound_Candidate_Core.md记录软件范围及下一独立接入目标。仅提交本次新增Detector、测试、计划和报告，已有PPS及用户生成变化不混入提交；Git作者/提交者为jxq。

## 验收与后续

本增量是纯算法，主机可完成其定义范围，无需Flash。不能报告安静教室60秒无误报、标准音频分类、±100us到达精度或测距通过；合成60秒静默只验证给定数字输入。下一独立目标才是AudioCapture唯一Begin/End内的暂存检测及End成功后发布，失败回滚检测状态/候选；不让Detector第二次认领DMA。之后再实现事件帧到本地PTP查询，当前AudioTimeMapMonitor不提供该接口。
