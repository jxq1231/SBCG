# B路线：带身份与槽号的95ms短编码声包

<!-- doc-status: conditional-candidate-design-not-selected-or-validated; reviewed: 2026-10-28 -->

## 0. 进入条件与承诺边界

必须先完成G0/O1和A路线失败分流。若只剩UI/TTL问题，继续修A2，不换声；若PCM缺失/混流，先修采集工具；若PTP掉域或DMA断流，修对应边界。只有A4证明当前声波身份/TOA方法无法满足目标，负责人才能启动本路线。

本路线是“候选→离线资格→纯C→诊断实机→正式接入→正式验收”，不是直接替换当前标准WAV。任何一级失败都保留记录，禁止同时调整音频、阈值、几何和播放设备。

编码解决声音身份和事件槽号；不保证直达路径可辨、不区分播放同一编码的两个手机扬声器、不把任意摆位变成可测基线。两板仍需共同域，仍用A权威结果和A路线的保持/重获机制。

## 1. 固定一个可测试候选，而非泛称“双chirp”

### 1.1 精确PCM结构

采样率48000Hz，单声道有符号PCM16小端，峰值≤0.5FS。一个包4560帧/95ms：

| 包内闭区间帧 | 长度 | 内容 |
| --- | ---: | --- |
| 0～2399 | 2400/50ms | 当前模板逐样本相同的2→6kHz线性chirp；两端各48帧余弦渐变 |
| 2400～2639 | 240/5ms | 零保护间隔 |
| 2640～4559 | 1920/40ms | 16个4FSK符号，每符号120帧/2.5ms，承载32位头 |

4FSK符号`00/01/10/11`对应2400/3200/4000/4800Hz。每符号使用`cos(2πfn/48000)`、n=0..119、相位从0开始、两端各12帧余弦渐变、0.5FS上限；四种频率在120点内分别6/8/10/12周期。窗口长度L的上升端为`0.5*(1-cos(pi*i/(L-1)))`、i=0..L-1，下降端反序；pilot L=48、FSK L=12、唤醒L=480。量化固定为`rint(32767*amplitude*window*cos(phase))`、ties-to-even、检查范围后转little-endian int16。

接收端每个频率使用同窗cos/sin两路I/Q投影，按模板Gram矩阵与输入能量归一化，避免播放通道相位造成单路cos点积零陷。设u=[dot(x,c),dot(x,s)]、G为c/s内积矩阵，评分为`sqrt(u^T inv(G) u / dot(x,x))`；参考/输入去均值策略在B1固定，G退化或输入零能量拒绝。不能用仅余弦的相关结果代表四频能量。

头字节MSB-first，按每字节最高2bit先发送：

```text
byte0 = A6
byte1 = D1
byte2 = (source_id << 4) | slot
byte3 = CRC8(byte0..2)
source_id = 1，本候选槽slot=0..9
CRC8: poly=07, init=00, refin=false, refout=false, xorout=00
```

CRC失败/魔数错误/slot>9/source不符均拒绝；CRC不是概率或身份安全保证，实际误识别率要由负样本测试给出。

独立参考golden：slot0头`A6 D1 10 EA`，slot1为`A6 D1 11 ED`，slot9为`A6 D1 19 D5`。profile_id=0xA6D1与头魔数绑定；将来波形/包布局改变必须增加profile版本并改变编码身份，不能只换文件hash而沿用同一身份。

CRC参考算法：

```python
def crc8(data):
    value = 0
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (((value << 1) ^ 0x07) if value & 0x80 else value << 1) & 0xff
    return value
```

### 1.2 候选20.4秒文件

拟新增`Data/Audio/candidate_coded_v1_10x_20p4s.wav`与`candidate_coded_v1_manifest.json`，不覆盖当前standard文件。

```text
总帧数979200（20.4s）
0..4799：100ms、1200Hz、0.0625FS唤醒音，两端480帧渐变
4800..19199：300ms零
第k包起点=19200+96000*k，k=0..9；每包4560帧
其它所有帧=0
```

首包0.4秒开始；该余量只应对限定的播放器启动，不能保证任意截断。新候选门控不因1200Hz唤醒音施加旧500ms去重屏蔽首包：先判断合法pilot/带内能量，再决定有效包抑制；无效唤醒后≤100ms重新布防。

manifest固定生成器版本、所有帧区间、窗口公式、符号频率/相位、头字节、CRC、每包slot、音量上限和文件SHA；保存解析出的头与生成头逐字节对照。

### 1.3 时间参考和两板身份

- TOA定义为“所选合格路径对应的pilot模板帧0”，不是首次过阈点、全包结束、FSK头结束或全局最高回声峰。
- pilot帧0包含渐变定义，估计器输出模板起点；任何FIR/重采样群延迟必须显式补偿一次。
- 包身份=`profile_id + source_id + decoded_slot`；source_id只是声音文件标签，不是物理扬声器身份。
- A/B本地session/sequence不相等也能正确配对；A漏slot0、B漏slot1时，不得把各自后续第1次检出配在一起。
- 同文件重播复用slot，用共同时间邻域、唯一性、现有3000ms未配对TTL和消费去重隔离。播放编号如需要由A在首次合格pair后分配，不由两板各自猜测。

**统一时间伸缩坐标：**`alpha=每个WAV帧对应的接收PCM帧数`，`stretch_ppm=(alpha-1)*1e6`。第j个符号的接收起点=`toa_frame+alpha*(2640+120*j)`；模板求值坐标=`(rx_frame-toa_frame)/alpha`。pilot、保护间隔、符号边界与模板频率使用同一alpha；它已经包含播放与接收采样钟差，不再额外按47991/48000重复修正。允许范围±5000ppm；扩大保护搜索或对边界最优解报RATE_UNCERTAIN，不把夹在搜索边缘的结果自动当作合法估计。

## 2. B1：候选资产和离线算法资格，先不接固件

**唯一目标：**在有真值的合成/真实PCM上确定候选可识别、可定位且有可承受的生产算法。

**文件范围：**

- 新增`Analysis/prepare_coded_audio.py`：确定性生成和`--check`逐样本验证。
- 新增`Analysis/coded_packet_replay.py`：录音解码、相关响应、候选路径、头字段/CRC、TOA和拒绝原因。
- 新增`Analysis/tests/test_prepare_coded_audio.py`、`test_coded_packet_replay.py`。
- 新增上述候选WAV及manifest；原`standard_selection.json`保持不变。

**拟新增CLI，实现后执行：**

```powershell
python -B Analysis/prepare_coded_audio.py --out Data/Audio --profile coded-v1
python -B Analysis/prepare_coded_audio.py --check --manifest Data/Audio/candidate_coded_v1_manifest.json
python -B -m unittest discover -s Analysis/tests -p test_prepare_coded_audio.py
python -B -m unittest discover -s Analysis/tests -p test_coded_packet_replay.py
python -B Analysis/coded_packet_replay.py --manifest Data/Audio/candidate_coded_v1_manifest.json --input-manifest Logs/Acceptance/<diagnostic-run>/pcm_manifest.json --out Logs/Detector/CodedReplay/<new-run>
```

生成器默认拒绝覆写已存在但hash不符的候选文件；修订产生新profile版本，不静默改原WAV。CRC golden vector由独立参考算法生成，不能只用同一函数编码再解码证明正确。

**算法正确性参考：**

```text
完整独立PCM窗口
 → pilot全范围归一化相关（I/Q或相位不敏感响应）
 → 同时估计小范围时间伸缩，保留多个局部峰
 → 每个候选按该伸缩解FSK头，检查魔数/source/slot/CRC
 → 比较首径候选、旁峰、带内SNR和窗边界
 → 可辨则细化pilot原48k坐标与Q16；不可辨则AMBIGUOUS
```

不能先取全局最大峰再补一个CRC，把强回声当成正确直达。也不能用“最早超过任意低阈值”保证直达；门槛需训练/留出数据冻结，低SNR/路径不可分辨时拒绝。

**生产候选算法与预算决策：**

- 直接全lag×41个伸缩（±5000ppm、250ppm步进）是离线参考，不允许原样塞到每次主循环。
- 生产候选先用已知抗混叠滤波后按3抽取到16kHz作粗搜，保留至多3个局部路径；补偿FIR已知群延迟。2～6kHz位于16k Nyquist内，不能随便抽取到12kHz并忽略6k边缘。
- 伸缩粗网格`-5000/-2500/0/2500/5000ppm`，原48k局部细化±6帧、250ppm网格；最终允许用拟合的连续伸缩和亚帧峰，所有阈值/峰选择需与参考逐例比对。
- 若粗搜丢首径、CRC正确却TOA偏、或实时预算不合格，B1失败。负责人选择更高效相关/缩小有证据的搜索域再做一次资格，不以降低正样本分母通过；两次失败后重新评估该profile，不自动进入B2。
- 必须按实际`Process`调用间隔计算端到端耗时；只给MAC数量或单次kernel微秒数不够。

**测试集与初始资格门槛（设计目标，不是实测结论）：**

| 数据 | 最小覆盖 | 通过/拒绝要求 |
| --- | --- | --- |
| 理想包 | 所有10slot、全部4种符号值及16个符号位置 | 头逐字节正确，TOA参考点无固定错位 |
| 分数延迟 | 0/0.25/0.5/0.75帧；所有块边界 | 无块相位分组偏差；单径TOA误差P95≤0.25原帧，系统偏差≤0.1帧 |
| 采样钟 | 至少±1000ppm正例，压力到±5000ppm边界 | 可辨内部范围能估计伸缩/头正确；边界不确定或超范围明确拒绝，不夹限通过 |
| 通道 | 增益0.25～2、反相、2～6k带限、15/25/35dB带内SNR | 未削波且可辨场景至少1000包，身份/slot正确率≥99%，不能以错TOA通过CRC |
| 多径 | 0.1/0.2/0.5/1/3ms延迟，回声幅度0.3/1/2 | 有可辨首径时正确；不可辨时拒绝，不承诺所有组合精度 |
| 启动/损坏 | 截唤醒/截首包/头损坏/错slot/包乱序 | 坏包拒绝；下一完整包可恢复，不能整轮锁死 |
| 非目标 | 语音、音乐、单音、击掌/点击、旧单chirp、静默 | 训练/留出分开，至少3000固定负窗零错误CODED，另留连续60秒；零样本误报不是任意场景零概率 |
| 真实PCM | 每板至少30个完整包，两个播放设备 | 包括未检出时段与失败样本；参考方法/生产方法逐例一致性及所有差异报告 |

真实目标使用冻结参数留出测试，不能只对同一录音做增益变体后把变体分到训练和测试两边。上述单径亚帧数值不等于物理声学精度，双板误差预算还包含映射、同步、编解码延迟和几何。

**退出产物：**候选hash、冻结算法/参数、训练/留出manifest、参考/生产差异、失败样例、CPU/RAM设计预算。仅资产/离线功能可提交，建议`feat: add qualified coded acoustic packet candidate`，不得写硬件已通过。

## 3. B2：纯C与有界PCM服务，先诊断后发布

**唯一目标：**候选解码在现有音频所有权下可靠、实时地运行，不输出测距。

**文件：**新增`App/Detector/coded_packet.c/.h`、`coded_packet_service.c/.h`、`Tests/Detector/test_coded_packet.c`、`Tests/Detector/run_coded_packet_tests.py`；修改`App/Audio/audio_capture.c`、`App/app_main.c`仅编译模式/sink/镜像标识接线、用户`cmake/audio-layout.cmake`、对应Audio/ChirpService/App测试。布局确需变更时仅修改用户`cmake/linker/stm32f746_audio.ld`并扩充布局测试，绝不改生成链接脚本。

接口要求：

```text
纯解码器：Init / FeedCommittedPCM / ProcessBudget / TakePacket / Reset
服务：StageCopy / Complete(End_ok) / Process / TakeEvent / Invalidate
输出：profile/source/slot/CRC、TOA(frame,Q16)、局部峰与质量、session/epoch/rate_revision、拒绝原因
```

Stage只复制，不相关、不解码、不打印；真实End通过后才可提交。每个8192双声道PCM16块仍只有唯一Begin/End所有者。旧2400帧EventClassifier只说明pilot代理特征，不能当成已解完整95ms包。

**内存/调度预算：**

- 旧ChirpService有3072帧历史，完整4560帧包可能跨块，不能认为“小于8192就不需历史”。候选服务预留6144帧CPU历史，并在随机分块和所有边界测试中证明足够。
- 具体捕获界：候选onset=H，搜索pilot起点H±384原帧；31-tap中心FIR需两侧各15帧。最大伸缩包长`ceil(4560*1.005)=4583`，`job_first=max(0,H-384-15)`，`job_end_exclusive=H+384+4583+15`，正常最大跨度5381帧，小于6144历史和8544活动银行。首部不足不能补零假装直达，报WINDOW_LEFT；搜索边缘最优报EDGE_UNCERTAIN。
- 未缓存任务在最早所需样本被覆盖前必须完成独立复制：满足完整[job_first,job_end)且真实End通过时立即复制，不等开始评分。之后≤200ms评分只读该独占银行，不再依赖会被新块更新的历史；无法及时缓存报HISTORY_EXPIRED。
- 优先复用现有17088字节CPU capture保留段作为活动包/相关任务；与旧服务互斥，不能把它同时给UDP sink和正在评分的任务改写。
- 旧copied为22528字节，历史增加3072帧使其增加6144字节；停用旧5312字节job_samples后理论净增832字节，另加解码/滤波状态。普通RAM净增加目标≤4KiB；必须用实际ELF/map逐项核算，不能把理论抵消当成已实现。
- 新增预算还包括FIR状态、I/Q、抽取样本、相关工作区及伸缩状态。可将原始任务最多5381个int16放8544银行前部，后3163个int16位置用作互斥scratch；按3抽取最多1794个int16，生产仅保留3个局部峰标量。模板常量放Flash，不能隐含另建大float数组或所有伸缩模板。任何复用先证明原始任务未被覆盖且DMA不访问。
- CMake候选模式不定义旧`SBCG_USE_CHIRP`宏，而非设为0；核对app_main的旧sink注册、镜像名和源列表确已互斥。否则旧大数组仍会链接，不能声称释放了内存。
- PCM只在CPU独占段处理，DMA段保持64KiB布局；不新增随机Cache维护，不把CPU大数组塞回DTCM小区间。
- 单次前台计算片段≤2ms；开始可设最多8个2400点评分或等价工作，达到时间/运算上限任一即让出。全包可用后到解码完成目标≤200ms；积压、超时、历史覆盖必须分别计数。
- 只有一个活动PCM任务，额外候选只保留少量标量；队满显式拒绝。若实际循环次数导致200ms超时，回B1优化，不能将更大的阻塞循环藏进ISR。

- [ ] 纯C与Python参考在冻结样例上头/CRC/slot完全相同、TOA在规定误差内。
- [ ] 覆盖8192所有起始相位或可证明等价的穷尽边界、随机分块、跨两块、End拒绝、换session、无后继、FIFO满、暂停门控、任务取消；禁止归还后读DMA。
- [ ] Debug/Release和布局检查通过，实际节段耗时、主循环间隔、音频丢块/日志错误计数可观察。
- [ ] 提供诊断镜像SHA并取得烧录许可；两板各至少30包，记录所有失败。无新DMA/End/历史覆盖/任务超时、PPS故障或DOMAIN撤销。
- [ ] 此阶段只输出`CODED_PACKET_DIAG`，不设置IDENTITY_VERIFIED驱动正式测量。建议提交`feat: add bounded coded packet diagnostics`。

## 4. B3a：采样坐标到共同时间的代际合同

**唯一目标：**新声包的时间参考只用可证明同代际、夹住目标的本板锚点，不靠名义48k外推。

**文件：**`App/Audio/audio_time_anchor.c/.h`、`audio_time_map.c/.h`、`App/Detector/sound_event_time.c/.h`、`App/Sync/time_sync.c`已有消费者失效点；`Tests/Audio/`、`Tests/SoundEventTime/`、`Tests/ClockEpoch/`和App桩。

```text
pilot模板帧0的PCM(frame,Q16)
 → 现有SoundEventTime一次且仅一次+1转NDTR传输完成坐标
 → 同板/boot/session/epoch/rate_revision的左右锚夹读插值
 → 有效本地PTP模型时间
 → 当前DOMAIN资格
 → A/B声包身份+共同时间配对
```

- 本板锚点夹读前后读取`BSP_PTP_GetEpoch()`和`BSP_PTP_GetRateRevision()`，前后不一致拒绝；锚点与事件携带这两个标量。
- PCM代际必须在Stage复制前后绑定并一致，Complete成功提交该副本的代际，后继块须相同；不能在解码完成时现读代际给旧PCM贴新标签。Process、映射及正式发布前再次复核；边界撤销活动包/待映射/待发布。跨变更的DMA半块可保守拒绝，不将整个块标成End时的新代。
- 仅Stage前后代际相同不能证明这个半块在之前生产时没有跨变更。TimeSync现有变更前/后通知须同时记录`BSP_Audio_ReadCursor()`位置边界；首版将包含该不确定区间的半块和所有未消费旧块隔离，直到一个完整新半块后才接受新代PCM。若无法确定安全首帧，报告CLOCK_BOUNDARY并等待健康新块，不用End时标签推断生产时代际。不在DMA IRQ新增PTP读取。
- A与B的epoch数字、rate_revision数字不要求相等；它们只是各板本地分段标签。跨板由已有PTP/PPS和DOMAIN证明可比较，不能用“两个epoch都是1”替代同步。
- PTP相位步进/读时故障必须撤销旧模型和未完成声包；rate改变先保留现有安全失效策略，穿越变速边界的包可明确拒绝，下一包自动恢复。
- 不能为了提高10/10直接删`SoundEventTime_Invalidate`或把新旧rate锚拼接。若常规调频造成持续缺槽，另起“分段锚点”子增量：记录变更前后边界、各段独立夹住目标；变更区间不确定则拒绝。该子增量不改变伺服增益/更新策略/PTP寄存器算法。
- 分段子增量的具体合同：在变更前用旧代再尝试一组NDTR/PTP/NDTR夹读，封存只读旧段；变更后建立新段。仅同epoch且已封闭的旧rate段可用于其严格夹住的TOA，不把新锚插入旧段、不外推到变更区间。音频SAI采样本身未因PTP调频改变，header可在同一连续PCM会话中解码，但TOA代际必须由参考帧所属的封闭段确定，不能完成时现读。phase/FAULT仍全部硬撤销；旧段最多保留覆盖2500ms候选等待所需的有界元数据。参考/随机边界测试证明这一例外后才解除首版保守拒绝，任何未封闭段仍NO_LEFT/NO_RIGHT。若无法在预算内建立边界锚，保留明确丢包，不宣称10/10。
- 无右锚先等待，现有候选有界等待，超时报告NO_RIGHT/EXPIRED；不以“帧号÷48000”替代PTP模型。
- PPS对齐不等于两个SAI采样时钟锁相。播放器与各板采样误差由声包时间伸缩估计处理，但伸缩不能写入SAI PLL、DMA或PTP伺服。

**可选缩短映射等待的具体方案：**若计时合同要求2秒首个可信结果，前台锚点最小尝试间隔可单独由1000ms改为200ms、锚点历史由4扩到8。这是用户模块只读采样频率，不是TIM/RCC/NVIC硬件修改；仍受现有100ms最小插值跨度、夹读宽度和所有权限制。拆出独立ANCHOR_CAPACITY，不能直接把同时控制pending的SOUND_TIME_CAPACITY改8。先跑旧间隔兼容、200ms新用例和代际测试，再测实际读时/日志开销。

现`SoundEventTime_Process()`的250ms门槛会同时节流解析/交接；B3a须把解析/事件交接与诊断日志节流分开，前台每call最多一个结果、状态日志仍≤1Hz。200ms只是最小尝试间隔，右锚实际等待还包括主循环、DHT启动门控和拒绝后重试；记录P99/max及NO_RIGHT原因，不能宣称固定≤200ms。该子增量不改变PTP/PPS算法。

## 5. B3b：有身份的事件配对与单包发布

**唯一目标：**完整合法声包一次即可形成可审计的新测量，不依赖再等两次2秒周期；持续跟踪仍由A路线负责。

**文件：**`App/Measure/sound_pair.c/.h`、`App/Network/sound_event_wire.c/.h`、`udp_sound_event.c/.h`、`App/app_main.c`、`measurement_gate.c/.h`、`App/Detector/sound_event_time.c/.h`、A路线tracker/状态协议及对应Measure/UDP/App/UI测试；新增中立元数据头`App/Detector/acoustic_packet_reference.h`以避免解码器和时间服务循环包含。

中立头定义`AcousticPacketReference`：session/sequence、frame/Q16、profile/source/slot、epoch/rate_revision、quality、peak和valid标量。解码服务`CodedPacketService_TakeEvent(AcousticPacketReference*)`复制元数据；时间服务新增`SoundEventTime_SubmitPacket(now_ms,const AcousticPacketReference*)`，不访问PCM，仍只在时间服务内部做一次+1坐标转换。输出`SoundEventTimeEvent`必须透传声包身份，不能在App里丢掉再按本地序号重建。

编码模式中只有完整包路径可产生正式测量候选；旧SoundDetector/ChirpService若保留能量诊断，不得再向同一测量队列提交第二份事件。现有App的“非CHIRP直接continue”必须按运行profile显式分支，避免CODED被静默丢弃；对应端到端测试证明同一物理包不会双发。

**声音事件v2准确布局：**现有SEVT v1实际36字节，不是32。v2固定48字节，0..35保留原字段含义（version=2），扩展：36..37 profile_id(u16)、38 source_id(u8)、39 slot(u8)、40..43本地epoch(u32)、44..47本地rate_revision(u32)，全部大端。引入明确的CODED事件类型，旧v1接收器不得静默降级成CHIRP。

配对同时要求同DOMAIN、同profile/source/slot、两端身份与TOA有效、时间差在现有量程窗口内、唯一且未消费。slot/CRC/本地序号都不能单独证明同一播放轮次；共同时间邻域和TTL是必要条件。

**门控与首结果：**

- 新接口`MeasurementGate_OnQualifiedPacket()`在域/健康流/完整身份/TOA均有效时授予单包测量资格。旧`StandardSequence`只保留周期诊断，不能继续作为首包发布的3声必要条件。
- 单包身份可信与三次时间一致是两种证据。若G0要求2秒可信首显，可新增`IDENTIFIED_SINGLE`展示状态：一个合格包产生一个正式独立measurement_id，显示单次值并标“稳定确认中”；尚未宣称5点稳定。随后三次独立一致包进入TRACKING。不能把16个FSK符号算成16次独立测距。
- 若新增该状态，A路线RST2的状态枚举、合法flags组合、UI与测试同步扩展；混版本拒绝。单包首次值不靠quality≥90旧代理旁路授予。
- 稳定后出现单个坏包/歧义时进入有限HOLD，不发布新ID；新位置连续确认后自动换簇。周期停声后依照源截止和失效状态撤销，不无限锁屏。

**若采用2秒首显，必须实测的预算目标：**前置为A/B已READY、健康流/本地模型已建立、DOMAIN有效；冷启动同步资格等待单独统计，不塞入声包预算。

| 项 | 规划上限/目标 |
| --- | ---: |
| 文件启动保护（含唤醒/静音） | 400ms |
| 声包完整时长 | 95ms |
| 等下一个8192帧半块 | 约171ms（按实际采样率核算） |
| 包解析/细化总耗时 | ≤200ms |
| 右锚等待（可选200ms方案） | 目标≤300ms，含前台/门控余量；拒绝重试须另报 |
| 网络及结果调度 | 目标≤50ms |
| 测量区域UI及面板呈现 | 目标≤120ms |

这是约1.34秒的工程目标预算，不是已证明的最坏时延；按播放器启动和实际首包两个起点分别报告。若锚点反复拒绝/实际调度超预算则首显项FAIL，不能拿平均值替代。UI仅在revision变化时调度测量区域更新，目标≤100ms检查周期，不能每100ms全屏重画破坏音频。当前1Hz UI加1s锚点等待不具备相同上界，不能保留旧调度又声称保证2秒。

## 6. B4：选择与正式验收

- [ ] B1/B2/B3的纯算法、服务、映射、协议分别完成，软件/布局/实机证据按阶段提交，不一次合并。
- [ ] 完成C的H0～H6，含先负样本再正样本、启动截断下一包恢复、漏slot/乱序/同文件重播、相同代码双出声口诊断、两屏及年龄一致。
- [ ] 参数冻结后用留出设备/摆位测试；目标原始时间误差、拒绝率、每槽覆盖和显示保持分别报告。
- [ ] 只有真实包识别/TOA/负样本资格通过，另一个选择资产增量才更新`standard_selection.json`和标准文件路径；保留旧50ms文件与原hash，不原地改名覆盖实验历史。
- [ ] 按C基础矩阵验证0.20～2.00m、±10mm、独立重复σ≤2mm、两屏≤1mm、方向、确认的时间合同；任何一项失败都不记整机通过。

**编码后仍不理想时：**

1. 头/CRC/slot不稳：只调整FSK符号时长、保护间隔或解码伸缩中的一个因素，产生新profile，先回B1；不先改PTP。
2. 身份稳定但TOA错簇：回看完整相关响应及真实PCM，检查多扬声器、反射不可分、通道时延与几何。更换单发声单元作对照，不能靠编码或历史值稳定声称消除物理歧义。
3. 算法稳定但映射NO_LEFT/跨代：只做B3a分段模型或前台锚点子增量，不重调已通过PPS。
4. 精度稳定偏差而非跳变：测定编解码/通道延迟和几何，单点标定入口是另一个批准增量；不能偷偷用当前320mm拟合各次输出。
5. 两轮profile/检测有根因窄修复后仍不合格：停止扩展量程，提交失败边界与数据，负责人重新选择可满足单点源/带宽/设备适配条件的方法。不能保证仅凭软件在任意多径与多出声口条件下达到任务精度。
