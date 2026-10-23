# M2.3.2 双板PTP时差、漂移观测与硬件校时闭环

<!-- doc-status: approved-implementation-design; reviewed: 2026-10-23 -->

用户明确要求在一个增量中实现“观察时差和漂移，加入校时”。本轮单一可验收目标为：同一份程序固定A为参考、B为从钟，通过双方ETH硬件四时刻估计时差和漂移，受控校正B的实际MAC PTP时钟，并观察收敛及失联行为。此方案不止增加观测日志，也不把电脑软件时间映射称作板端校时。

## 已核对的起点

M2.3.1软件/A联合验收已提交6920312；用户随后烧录B，并提供交换机下双板实测。已逐项复核本地两份JSONL与用户粘贴记录完全相同，重解码120个原始UDP负载并重放监测器，summary逐值相同：A60包SEQ163～222、B60包SEQ175～234，零缺口/重复/乱序/身份冲突；B30次请求全部应答。双板IP/MAC与真实UID对应正确。

这只证明网络共存及B请求应答。新B BOOT/BOARD、精确ELF哈希、B RX/TX硬件时间戳及音频业务尚未单独复核，不把A先前完整实测套给B。当前程序仍LOCAL/SYNC=0。

## 方案选择

- 采用既有Hardware_Resources第5.5节确定的PTPv2/UDP方向：固定A/B、单播、two-step，事件319/一般消息320。只实现本项目固定节点闭环子集；不宣称完整IEEE1588配置文件、BMCA、Announce选主、任意第三方互通或交换机透明时钟支持。
- 不采用电脑软件时刻作为四时刻来源，它只能辅助观察。
- 不把专用5008端口的请求/应答/报告协议冒称PTPv2。该方案虽能节省一个PCB，但会偏离已确定的协议方向，不作为默认实现。

## 必须先处理的配置与授权

**CubeMX变更已由用户完成。** 原配置MEMP_NUM_UDP_PCB采用opt.h默认4；本次用户已设6并重新生成。原heartbeat、probe、time exchange共3个PCB，新增319/320两个PCB至少需5个。将`MEMP_NUM_UDP_PCB`设置为6，保留一个余量。此设置只扩大协议栈固定池，不替换旧端口，不新增GPIO/DMA/IRQ；会增加少量静态RAM，实际大小由链接布局核对。

由用户在当前SBCG工程操作：Pinout & Configuration → Middleware and Software Packs → LWIP → General Settings → Protocols Options → MEMP_NUM_UDP_PCB=6；若版本分类不同，可搜索完整参数名。保存并Generate Code后，预计SBCG.ioc的LwIP参数及LWIP/Target/lwipopts.h变化，CubeMX还可能刷新生成元数据。先检查实际差异，不手改.ioc、生成选项或opt.h。

PTP初始配置现由用户自有App/BSP/bsp_ptp负责，本次需要明确扩大该BSP运行时例外：B启用fine模式、调整addend和受控时间覆盖，直接按本机RM0385寄存器语义实现有界请求。HAL原文件/生成代码不改。RCC/HCLK200MHz、RMII、SAI/PLLI2S、GPIO、NVIC、DMA和Cache配置保持。用户已明确批准该BSP运行时扩展；新固件仍须另行确认烧录。

依据根AGENTS第3.1节，LwIP生成选项归CubeMX；第4节要求先处理配置再适配应用。本方案不授权绕开这两条。

## 四个硬件时刻与报文

A发送Sync，在实际linkoutput所用单缓冲描述符取得t1；发送完成后用Follow_Up携带精确t1。B在Sync的同步输入窗口取得t2，随后发送Delay_Req并在实际描述符取得t3；A取得Delay_Req的t4，通过Delay_Resp传回。B拥有(t1_A_TX,t2_B_RX,t3_B_TX,t4_A_RX)，直接调用现有SyncEstimator，正offset表示B领先A。

固定IPv4/UDP端点、expected sourcePortIdentity与UID映射、PTPv2版本/domain、messageType、长度、two-step标志、序号、时间字段及requestingPortIdentity均需校验。报文显式按网络大端编码，不传C结构体。48位PTP秒字段须能落入本机uint32秒范围；纳秒<1e9。首版普通交换机不处理非零correctionField，遇到非零值拒绝并报告，禁止静默忽略后声称兼容。

Sync与Delay_Req各自有序号，固定槽明确记录它们的对应关系；Follow_Up或Delay_Resp乱序只在匹配记录及单调HAL tick有效期内补齐，重复完成仅消费一次。一次发送返回ERR_OK不代表DMA真正发出；没有合格硬件戳的样本不进入伺服。不得用发送调用前时间、UDP回调时间或REPORT到达时间替代硬件时刻。

本版工程值：A每至少250ms发送Sync；B最多保留一项在途/待消费记录，繁忙时可见拒收；HAL tick超时1000ms；同钟四时刻交换最大历时100ms；已确认过期不自动以同一序号重发。未收敛/链路丢失继续推进主循环，不阻塞等待网络。

PTPv2固定clockIdentity映射不是启动唯一标识。首版采用启动/检测对端重启后的6秒静默隔离及旧记录清空，只在受控局域网、主循环正常推进和旧包驻留受限的条件下保护正常重启。序号大回退/长失联清锁并重新获取；不能承诺防任意跨重启恶意重放。若严格跨断电唯一会话成为要求，需要另加持久化代际或真正随机源，不能用UID+tick伪称唯一。

## B硬件时钟与有界写入

A保留现有coarse/5ns计数，绝不接受从节点校正。B在启动阶段直接初始化fine：HCLK200MHz、子秒增量10ns、nominal addend=0x80000000，对应200MHz×10ns×addend/2^32=1。避免运行时从coarse热切换导致瞬时倍率错误。

现有HAL的SetConfig包含无界addend等待，并会紧接请求update/init；AddTimeOffset又把纳秒偏差混入addend。不得直接接入闭环。用户BSP按RM0385顺序操作，任一命令前确认TSSTI/TSSTU/TSARU空闲，命令后采用20ms与100000次轮询双预算；HAL tick冻结也必须退出。

频率接口的正ppb表示B加快，使用nominal addend计算绝对目标，不在上次目标反复累加。初始限幅±100000ppb，64位中间计算并检查范围。

初次大相位对齐使用TSSTI覆盖：读取新鲜B快照，计算校正后的完整非负时间，写秒/纳秒更新寄存器且TSUPNS=0，再请求initialize；不把HAL SetTime（实际触发相对TSSTU）误当绝对设时。正负相位都经完整目标计算，避免依赖未实测的减法寄存器编码。覆盖命令的执行延迟进入初次对齐误差，后续fine闭环继续收敛，不声称该写入本身纳秒无误差。

已发起硬件请求后超时不能声称已回滚：时钟标记不可用、代际提升、伺服FAULT并停止校正，保留故障日志。所有写入只能在前台、输入/输出回调及BSP TX Begin/End之外执行。

## 漂移、校正与状态

完整有效样本先检查路径时延和单调性，输出B−A offset_x2_ns和delay_x2_ns。漂移以参考A的历时归一化，明确单位ppb；B领先或相对A越来越快时，修正方向必须使B减慢。

状态为ACQUIRING、TRACKING、LOCKED_INTERNAL、HOLDOVER、UNSYNCED、FAULT。起始积累至少3个一致有效样本再允许一次大相位对齐；对齐后清历史重新采样。之后用有界频率控制消除残余时差/漂移，不逐包任意跳时间。A只输出参考状态。

锁定门槛预先冻结：连续8个合格样本，|offset|≤10us、残余漂移≤500ppb、平均路径时延≤2ms；数据不足或不符合条件不能进入LOCKED_INTERNAL。2秒无有效样本撤销内部锁，最多5秒HOLDOVER，之后UNSYNCED。估计时效/交换历时不合格、负路径时延、平均路径时延超2ms、对端重启、时钟失效或命令失败立即撤锁并报告；不另设未定义的相对延迟跳变阈值。频率控制增益与分段窗口须先通过真实纯C仿真，硬件失败时记录失败，不能事后放宽门槛制造通过。

LOCKED_INTERNAL仅表示本协议估算和控制一致；不等同独立±10us课程验收。新的SYNC控制日志区分内部锁与独立验证状态，既有音频LOCAL/SYNC=0语义不在缺少独立证据时悄悄升级。

## 时钟代际与原业务保护

时钟初始化、相位覆盖或命令结果不确定都提升clock_epoch。频率变化维护rate_revision。校时前确认TX token空闲，在前台统一失效：本次PTP在途配对/漂移历史、旧UDPTimeExchange配对、SyncObservation待算队列、PtpClock上次读时、AudioTimeAnchor待领取快照与单调性历史、AudioTimeMapMonitor三点窗口。

只判断时间是否前进不足以发现正向跳变；必须由显式代际隔离。音频DMA/PCM继续采集，不能为了校时重新初始化整个App、DMA或codec。模型不跨相位或rate_revision插值；频率持续调整时允许输出等待稳定，不能输出伪造连续模型。

新增PTP包装必须先于旧UDPTimeExchange卸载，安装在旧链之外；卸载验证两个钩子及回调不活动。旧5005/5006/5007业务保留；未有可靠时间戳时保留错误与拒绝计数，不声称原业务成功。

## 计划修改的文件及资源

| 用户文件/测试 | 用途 |
| --- | --- |
| App/Network/ptp_wire.c/.h | 固定PTPv2子集的显式编码、长度/字段/身份校验 |
| App/Network/ptp_board_sync.c/.h | 两PCB、固定记录、同步输入/输出硬件戳包装、两角色收发 |
| App/Sync/clock_servo.c/.h | 纯C时差/漂移过滤、有界控制和状态机，不直接操作硬件 |
| App/BSP/bsp_ptp.c/.h | 明确批准后的B fine、有界频率/相位操作及代际 |
| App/Sync/ptp_clock.c/.h、sync_observation.c/.h | 时钟变化识别及旧观测失效 |
| App/Network/udp_time_exchange.c/.h | 清除跨代际的旧PC配对，不改变原wire布局 |
| App/Audio/audio_time_anchor.c/.h、audio_time_map_monitor.c/.h | 清除跨代际/速率分段的历史，DMA保持运行 |
| App/app_main.c、用户CMakeLists.txt | 生命周期/前台调度/统一失效及构建登记 |
| Tests/BoardSync/、Tests/Ptp/、Tests/Sync/、Tests/Audio/、Tests/App/ | 真实核心、寄存器桩、网络包装和原业务回归 |
| Tools/Monitor/及Analysis/下对应采集/校核工具 | 两板串口原始日志、四时刻与真实C对照、漂移/锁定汇总 |
| Docs/当前入口、协议、资源和本计划 | 状态、具体接口、证据和限制 |

不新增ISR或DMA配置。ETH沿用内部DMA增强描述符，SAI2/DMA2缓冲区/IRQ保持；Cache仍关闭，不新增随机clean/invalidate。PC只用于开发观测，不参与板端校时计算。

## 实施和验收顺序

1. 用户处理CubeMX UDP池并明确BSP运行时扩展；核对生成差异，必要时重建/布局，既有用户区保留。
2. 先红后绿实现纯wire/servo、BSP有界操作和时钟代际负例；覆盖时差符号、漂移/限幅、整数边界、冻结tick、busy零写入、超时失效。
3. 接入PTP收发；覆盖两个角色、真实硬件戳关联、延迟ARP、链式pbuf拒绝、缺戳、错身份/端口/序号、乱序/重复/过期、钩子所有权及重入。
4. 接入统一失效；测试正/负相位均不会混用旧配对/模型，fine频率正负方向正确，A永不收到校正。
5. 原模块回归、Debug/Release及ETH/音频布局、独立复核，冻结ELF和验收门槛。本步骤完成前不烧录。
6. 新固件取得明确烧录/实物验收授权；若B由队友操作，交付相同ELF并记录B BOOT/UID及文件哈希。先验证B fine时钟、正负频率与相位方向，再进入闭环采集。
7. 正常窗口至少10分钟，记录从上电到内部锁耗时（目标≤30秒）、全部原始四时刻、有效/拒绝/异常计数、offset/漂移、实际addend及epoch；复验原UDP/温度/音频，采集结束核对原始哈希和关闭端口。断网/重启恢复单独通知用户配合。
8. 仅按实际证据作验证提交；内部锁收敛不替代后续PPS/外部仪器独立测量。LTDC/TouchGFX和测距本轮不加入。

配置及BSP扩展已获用户批准，协议/纯C控制器/协调器和旧消费者失效已实现，已通过软件集成复核，交付见[M2.3.2记录](../Milestones/M2_3_2_Dual_Board_Clock_Software.md)。软件冻结时新版本尚未烧录；用户随后自行烧录两板，本次1分钟运行观测见下方更新。硬件依据为[ST RM0385](https://www.st.com/resource/en/reference_manual/dm00124865.pdf)，具体HAL行为以仓库版本为准。


## 用户后续调整及本次验收边界

用户报告两板均已手动烧录，明确将10分钟闭环窗口缩短为1分钟。已按此执行A/B各60秒运行串口和60秒稳定双板网络，窗口部分重叠；236组raw经真实C/Fraction复核，内部估算通过。没有覆盖BOOT/首次相位步进、外部精度或故障注入；初始准备网络窗口的B序号回退FAIL保留。详细结果见交付记录。本次用户调整覆盖上方原10分钟计划，不重复要求采集10分钟。
