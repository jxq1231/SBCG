# M4.1.1 候选声音检测纯C内核：软件验证记录

<!-- doc-status: completed-software-evidence; reviewed: 2026-10-23 -->

> 历史范围：以下“未接入/未烧录”仅指M4.1.1交付时。后续已在[M4.1.2](M4_1_2_Sound_Candidate_Integration.md)接入并开展受限单板验收，勿重复实施旧下一步。

用户已明确“先不烧录验收，继续推动下一步”。本轮沿用单机规划，完成纯算法部分；不以PPS暂未验收阻止独立算法开发。M2.3.3 PPS保持冻结未烧录，本轮未连接、复位、读取或写入开发板。当前仅A板在场。

## 本轮可验证目标及结果

新增逐帧PCM16候选声音帧定位及去重：去直流窗口能量、连续窗确认、静默布防/再布防及帧数冷却；输出session、首合格窗起点onset_frame、末确认窗末帧confirmed_frame、最大去直流均方能量、原始峰值及截幅标记。仅称能量候选；没有标准音频分类、声学精定位或PTP时间查询。

调用链是`主机测试 → SoundDetector_Init → 逐帧SoundDetector_Push → CompleteWindow → SoundCandidate`。当前App/AudioCapture没有调用它，顶层CMake未登记新源文件，因此没有新固件标识或板端事件串口输出。

## 文件与边界

本次独立提交仅包含8个新增文件：

| 文件 | 职责 |
| --- | --- |
| App/Detector/sound_detector.h | 配置、状态、候选和逐帧接口契约，中文Doxygen及单位/所有权 |
| App/Detector/sound_detector.c | 精确整数方差、流连续性、静默/布防/确认/去重及冷却 |
| Tests/SoundDetector/test_sound_detector.c | 真实内核公开行为及整数/计数/帧边界测试 |
| Tests/SoundDetector/run_tests.py | 现有风格主机GCC测试，-O0/-O2后运行独立参考 |
| Tests/SoundDetector/reference_bridge.c | 独立CLI进程执行真实内核并传出事件字段 |
| Tests/SoundDetector/reference_check.py | 精确Fraction整窗参考、固定随机种子输入及逐事件比较 |
| Docs/Plans/2026-10-23_Sound_Candidate_Core.md | 本增量设计、契约、执行步骤及范围 |
| Docs/Milestones/M4_1_1_Sound_Candidate_Core.md | 本报告 |

现行Project_State/README/文档索引/路线和PPS待验收说明同步更新为用户新优先级；这些文件原已有PPS改动，留在原工作区，未夹带进本次算法提交。PPS相关App/Core/HAL/.ioc/CMake改动全部保留，不自动提交或丢弃。

**CubeMX：无需修改，本轮未改。IRQ：无新增。DMA：无新增。Cache：无修改。** 算法无HAL/中断/网络/动态分配依赖，不保存PCM指针。状态由调用方独占并可整体复制，适合下一步在Begin/End之间暂算、成功后提交。

## 算法契约

窗口N为2..256，精确E=N*sum(x²)-sum(x)²，与on_rms_pcm²*N²及off_rms_pcm²*N²比较。中间量不超过2^46，避免先取整把0<方差<1误判为静默。输入PCM16幅值范围-32768..32767；mean_square_ac是连续确认窗中最大的floor(E/N²)，单位PCM16幅值平方，peak_abs可为32768，截幅仅标记，不自动拒绝候选。

初始化须先连续quiet_windows个低能量窗才布防；开机已有声音不作为新到达事件。连续on_windows个高窗报告一次；滞回中间窗打断确认。之后必须再安静且经过refractory_frames才重新布防，持续声音不会重复报告。所有参数由调用方显式给出，当前没有经教室数据标定的默认阈值。

session或帧坐标不连续会清历史并消费当前样本为新窗首帧；session0非法并清历史。UINT64_MAX末端不允许同session回绕，新会话或显式Reset才能恢复。无事件/非法输入时输出清零，out为NULL不消费状态；Reset只接受内容已定义的对象，不能传未初始化自动变量。

onset_frame是第一合格高能量窗起点，相对该窗末帧回溯N-1帧；这是算法坐标，不能当作已标定的真实声音到达时刻。N64/48kHz的约1.33ms窗长仅是解释示例，当前没有固定板端配置；标准音频模板和精定位需后续明确。

## 新鲜软件验证

1. TDD：缺失实现编译成功后，在SoundDetector_Init真实行为断言处观察RED；保存Logs/Detector/M4_1_1/RED-O0.log。实现后运行`python -B Tests/SoundDetector/run_tests.py`。
2. -O0和-O2各**9组、6,553,114次检查通过**，包含60秒数字静默/直流/有界噪声、阈值等号、持续音去重、单窗瞬态、滞回区、冷却边界、跨8192帧位置、gap/repeat/reverse/session、PCM极值和截幅、uint64终端、非整方差、断流新原点、实际65535个安静/确认窗。
3. Python Fraction对真实C逐事件核对：**175例、2,240,340个样本、711个事件完全一致**，含奇数窗、门槛边界、部分末窗、直流、随机幅值和截幅。随机种子20261023，摘要fraction-reference.json。该参考逐整窗计算，不复制逐帧累加状态实现。
4. 最初直接加载测试DLL时遇到主机GCC为32位PE(0x14c)、Python为64位，无法跨位宽加载；改为独立CLI。首帧/事件uint64分成高低uint32传输，避免MinGW的I64扫描格式差异；未通过降低编译警告标准掩盖问题。最终-Wall/-Wextra/-Werror/-pedantic通过。
5. 按现有工作流运行`python -B Tools/Monitor/autodebug.py build --preset Debug`及Release，两者成功且ninja无新工作；报告Logs/AutoDebug/20261023T122021-2d259590及20261023T122021-8e2836de。**构建成功不表示新算法已接入固件。**
6. 复核当前Debug/Release ELF仍分别为b595ce776873…/6965b96c3cad…，与冻结PPS完全相同；其104个源输入及7个CubeMX生成快照SHA保持，见preservation-check.json。算法/测试无原模块依赖，未无依据重复整套原业务硬件验收。
7. 独立只读代码审查未发现阻止提交的严重/重要问题；建议的3项边界覆盖已全部补齐并通过，已明确Reset对象初始化前提。没有硬件验收证据。

完整原始软件证据位于Logs/Detector/M4_1_1，构建及日志不入Git。纯C公开契约的验收可在主机完成，适用范围为软件算法，因此本次可单独提交；不将未验收PPS或未来硬件功能标为完成。

## 下一独立目标：M4.1.2安全接入已有PCM

将检测器接入AudioCapture唯一的Begin/End认领，在End之前用工作副本计算有界候选，End成功且index_valid/连续性有效后才提交检测状态并发布。End失败必须丢弃该块产生的候选，并清除不能继续信任的跨块确认历史；不得把“尚未提交”误当作可以保留旧待确认声音。需要明确候选容量及溢出计数，不可在8192帧半块中无界打印；Detector不能二次领取DMA或保留samples指针。

下一版可先输出候选帧/会话/质量及LOCAL/SYNC=0，不伪造事件PTP时间。需要板端运行验收时再通知用户并取得该新版本授权；当前不实施、不烧录。标准音频识别/精定位、事件帧→PTP查询和声学延迟校核继续分增量处理，LTDC/TouchGFX及两板测距不在本轮范围。
