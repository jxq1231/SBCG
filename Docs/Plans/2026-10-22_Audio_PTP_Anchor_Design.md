# M3.2.2 音频位置与本地PTP锚点

状态：2026-10-23用户已批准本版本烧录；软件、单次烧录及本地锚点正常窗口联合验收通过。详见 ../Milestones/M3_2_2_Audio_PTP_Anchor.md。

## 单一目标与接口

在前台用“DMA位置A → BSP_PTP_Read → DMA位置B”建立PTP读数处的音频DMA进度位置区间。证明可观测、可拒绝歧义，并相对本地PTP估计帧率；不做校时、双板同步、检测或物理声学到达时间换算。

位置为本次SESSION内按NDTR折算的完整双通道传输组计数，uint64；一帧为两个halfword。不是SRAM写入可见性的证明，也不是取样引脚或codec输入端的精密时间，WM8994/SAI/FIFO/DMA固定及可变延迟尚未标定。PCM读取权仍由原Begin/End管理。

依据[ST AN4031](https://www.st.com/resource/en/application_note/dm00046011.pdf)第1.2、2.2.2及3.1.1节：NDTR按外设事件/传输拍递减；外设访问与内存写入是分开的阶段，即使Direct模式也不应将游标直接解释为物理声学时间或内存授权。

## 读取与一致性

- BSP只读位置接口在原有短临界区内检查运行/会话/编号状态，读取两次NDTR及边界/错误标志。拒绝0、越界、reload、跨半区、递增NDTR、待处理HT/TC/TE/FE/DME、半区与已观察边界奇偶矛盾、DMA停止及观察超过250ms未更新。保持原PRIMASK，绝不清DMA标志或消费音频块。
- 对稳定半区，帧组数=已观察完成半块帧数+本半区NDTR已计halfword数向下除2。奇数NDTR不将单个声道误算成完整帧。溢出拒绝；失败输出清零。
- 两次位置之间不持续屏蔽中断。PTP仍经现有前台有界读取接口，秒回绕一致性由原BSP维护。
- 拒绝任一位置无效、SESSION变化、边界generation变化、帧数倒退、区间宽度大于4帧或非法PTP纳秒。一次周期一次尝试，不为凑样本重试。
- 相邻已接受锚点，同会话PTP及帧位置必须前进；PTP倒退/重复锁存为BACKWARD，直到新会话重新建立基线；帧位置不前进拒绝为ORDER。SESSION不跨BOOT唯一。
- F_LO/F_HI是PTP取读时刻完整DMA帧计数的闭区间；WIDTH=F_HI-F_LO。即使WIDTH=0仍有一帧量化区间，不能称零时间误差。继承M3.2.1对未观测完整DMA循环及HAL tick漏计的边界，不保证排除全部漏采。

## 输出及调度

新增独立服务AudioTimeAnchor_Init/Process，只在音频和LwIP均启用时接入App；位于原AudioCapture之后。每1000ms最多一次，DHT起始低电平期间延后，不追赶突发。

`$AUDIO_ANCHOR`字段：SEQ、STATUS、VALID、成功时SESSION/F_LO/F_HI/SEC/NSEC/WIDTH、SYNC=0、OK/REJECTS累计计数；PTP失败保留错误码。失败省略所有位置和时间字段，不复用旧值。SEQ为uint32模递增；OK/REJECTS为饱和uint32；所有64位字段有界转十进制，不依赖newlib-nano的long-long printf。单行含CRLF必须小于256字节。旧AUDIO/AUDIO_POS及UDP格式不变。

## 文件及资源

- App/BSP/bsp_audio.h、bsp_audio.c：只读位置；不修改已有DMA回调逻辑。
- App/Audio/audio_time_anchor.h、audio_time_anchor.c：锚点夹取、相邻锚点检查、每秒日志。
- App/app_main.c：初始化、调度及版本SBCG-M3.2-ANCHOR；无音频/PTP组合保留原构建分支。
- cmake/audio-layout.cmake：用户扩展，在TARGET LwIP存在时登记新源及功能宏。
- Tests/Audio和Tests/App：位置竞争、PTP错误、区间/时序拒绝、调度和有界格式测试。
- Analysis/audio_anchor_analysis.py及测试：从BOOT开始核对每次尝试、有效区间、计数、相对PTP帧率；沿用原位置核对时允许显式预期固件标识。
- Docs/Protocol.md、Project_State.md、本计划和本轮milestone：状态与验收边界。

No CubeMX change required。SAI2-B RX、DMA2 Stream7 Channel0、circular/halfword、32768元素/64KiB/32字节对齐SRAM1缓冲区保持；IRQ、引脚、所有时钟和ETH网络/PTP配置不变。I/D Cache关闭，不增加维护。PTP函数不搬入IRQ。所有锚点计算、日志、分析均在前台或电脑，IRQ只沿用原交接。

## 验证步骤与预定正常窗口门槛

- [x] 位置测试先失败，再实现；覆盖双NDTR竞争、flags、奇数halfword、初始/半满/满位置、PRIMASK及不消费现有块。
- [x] 锚点/日志测试先失败，再实现；覆盖跨边界/会话、PTP失败/非法纳秒、倒退、宽区间、计数和日志上限；旧业务测试通过。
- [x] 分析负例先失败，再实现；从帧区间推导长窗口及逐相邻有效锚点相对PTP平均率范围，两者均检查约47991.071Hz±1%健全性带；计入一帧量化，不拟合/隐藏失效记录。
- [x] Debug/Release、原10组模块、Tools/Analysis unittest、两配置ETH及音频布局通过；无新增warning。
- [x] 独立只读复核、源码/ELF冻结，新版本取得烧录授权。
- [x] 沿原配置单次flash/verify/reset，约90秒COM3及原三路UDP联合验收；无需声音、改线或拔插。
- [x] 正常窗口至少60条有效锚点、总尝试至少60、有效占比≥90%；允许CURSOR/BOUNDARY/WIDE这三种明确的瞬态拒绝并逐条记录。PTP/BACKWARD/SESSION/ORDER不接受为正常窗口通过；同一BOOT及SESSION，PTP和帧范围持续前进，WIDTH≤4。长窗口帧率区间处于现有约47991.071Hz的±1%健全性范围，实测全部宽度/拒绝原因公开。
- [x] 同时至少60对原音频位置有效、原UDP≥70包零缺口，两组30请求/板端真实C对照、温度及codec无新增错误；记录前台心跳/codec轮询抖动，不沿用旧2050/1020ms经验值为精密硬门槛。
- [x] 实机通过后记录、以jxq提交本独立增量；停止，不直接实现映射器或声音检测。

异常/跨会话/溢出首先是主机测试证据；正常窗口不代替实物故障注入。所有原始数据保留Logs，主机检查结果不单独宣称硬件通过。
