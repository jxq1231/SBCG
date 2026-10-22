# M3.2.2 本地PTP与音频DMA位置锚点

状态（2026-10-23）：**软件、单次烧录及本地PTP/DMA锚点正常窗口联合验收通过。**
目标和用户批准范围见[实施方案](../Plans/2026-10-22_Audio_PTP_Anchor_Design.md)。本版本SBCG-M3.2-ANCHOR已获明确烧录授权；仍LOCAL/SYNC=0，不代表声学到达时间校准或双板同步通过。

## 已实现的单一目标

在前台用cursor A → 原BSP_PTP_Read → cursor B，给本地PTP读数建立NDTR传输组计数区间。
BSP游标在短临界区读两次NDTR和DMA边界/错误标志，检查半区/generation/运行状态/250ms观察年龄，不消费音频块，不清标志。奇数halfword向下取完整双声道组。
两次游标之间不持续屏蔽中断，PTP不搬入ISR。跨会话、跨边界、倒序、宽度>4或读时失败拒绝；同会话PTP倒退/重复锁存无效，直到新会话恢复基线。
每秒最多一条AUDIO_ANCHOR，DHT起始低电平期间延后；失败省略所有帧位置/会话/PTP字段，不复用旧值。详细字段见[协议](../Protocol.md)。

## 文件变更

| 文件 | 原因 |
| --- | --- |
| App/BSP/bsp_audio.h、bsp_audio.c | 新只读位置接口；保存首次DMA启动HAL tick供年龄检查 |
| App/Audio/audio_time_anchor.h、audio_time_anchor.c | 前台夹取、拒绝/锁存、计数、有界十进制日志 |
| App/app_main.c | 初始化/调度、SBCG-M3.2-ANCHOR版本；保留原功能分支 |
| cmake/audio-layout.cmake | 用户扩展，在音频+LwIP组合启用新模块和宏 |
| Tests/Audio/test_audio_time_anchor.c、test_bsp_audio.c、dma_stubs/main.h、run_tests.py | 游标竞争/停止/标志/年龄、夹取错误、时序、日志和构建测试 |
| Tests/App/test_app_main.c、run_tests.py | 新模块顺序、DHT门控与原调度回归 |
| Analysis/audio_anchor_analysis.py、tests/test_audio_anchor_analysis.py | 全部尝试及计数、宽度/时序、首末和逐相邻有效锚点率检查 |
| Analysis/audio_position_analysis.py | 显式可选预期固件标识；旧默认及位置检查保持兼容 |
| Docs/Plans/2026-10-22_Audio_PTP_Anchor_Design.md、Milestones/M3_2_2_Audio_PTP_Anchor.md、Protocol.md、Project_State.md | 方案、实现状态、字段和验收边界 |

## 调用链、IRQ、DMA与CubeMX

main → App_Init → 原PTP/音频初始化 → AudioTimeAnchor_Init。
主循环 App_Process → 原音频处理 → AudioTimeAnchor_Process → BSP_Audio_ReadCursor → BSP_PTP_Read → BSP_Audio_ReadCursor → 有界日志。

No new interrupt relationship。原DMA2_Stream7 IRQ/SAI半满和满回调仍只交接编号及缓冲区；新服务全部在前台执行。两次游标之间IRQ开放，单次游标短临界区复制64位状态并恢复原PRIMASK。
No new DMA relationship。仍为SAI2-B RX、DMA2 Stream7 Channel0、peripheral→memory、halfword、circular；32768元素/64KiB，半满/满各8192双通道帧。
原SRAM1 0x20010000..0x20020000、32字节对齐缓冲区不变；I/D Cache关闭，无新增缓存维护。
**No CubeMX change required。** 未改.ioc、Core、Drivers、LWIP、生成CMake、链接脚本或硬件初始化；GPIO、TIM、NVIC、时钟、SAI/I2C/ETH/PTP配置均保持。

## 软件验证

现有动作：

```powershell
python -B Tools/Monitor/autodebug.py build --preset Debug
python -B Tools/Monitor/autodebug.py build --preset Release
```

两种构建BUILD_OK，无新增编译/链接warning。Debug FLASH107788B，Release62784B；音频DMA仍64KiB。

- Debug SHA256：add1e06a892bf2a9027c28662c7a269eed5cfe2a97be2ede9862ba976d61c634
- Release SHA256：bcee73517bb4085ff3ae4adeaa28947232c7dc7fcf635ed59a15117763679f1c

10组原生C模块回归（Audio包含8个测试程序）、Tools unittest83项、Analysis最终61项、两种配置ETH/音频布局均通过。
新锚点分析8项测试包括中间时间+100ms而首末不变的负例；先观察旧端点检查漏检，再补逐相邻有效锚点检查。所有段率与长窗口率均使用Fraction精确算术，并保守计入两端一帧量化。
原位置读取/锚点服务在缺实现时先确认测试失败，再完成实现。BSP覆盖双NDTR改变、边界/TE/FE/DME、奇数halfword、PRIMASK、250ms过期和停止等情况；服务覆盖PTP失败/非法纳秒、会话/generation变化、区间过宽、前进/倒退锁存、日志上限和DHT避让。
独立只读审查及逐段率补强复核无P1/P2阻断；审查未连接硬件、改代码或重建固件。

证据位于Logs/Audio/M3_2_2/software-01：checks.json（16项），supplemental-checks.json（最终两种构建、Audio/Analysis及四种布局共8项），各自完整日志、cli-check.json和preflash-review.json。
CLI用60条合成锚点从BOOT开始完成端到端检查；报告hardware_verified=false，且既存输出不能覆盖。该合成例不是实机数据。

## 不确定性与尚未验证范围

1. NDTR是外设传输进度，不能据此授予PCM读取权或证明SRAM写入可见；PCM仍必须经原Begin/End。依据[ST AN4031](https://www.st.com/resource/en/application_note/dm00046011.pdf)第1.2、2.2.2和3.1.1节，外设事件计数与内存写入为不同阶段。
2. F_LO/F_HI只约束PTP取读时刻的NDTR帧组计数；WIDTH=0仍有一帧量化。WM8994/SAI/FIFO/DMA延迟未标定，不能把区间直接作为麦克风物理到达时间误差。
3. 同会话已经观察到的倒退会锁存；两次观测之间完全未观察到的时钟重启/变化、IRQ完整循环合并或HAL tick漏计，仍不能由本软件独立排除。
4. 首末及相邻有效锚点之间的率均为区间平均率，跨拒绝样本也如此；±1%仅健全性门槛，不宣称精密瞬时率、绝对频率或课程精度通过。
5. 原音频位置、心跳/PTP和网络业务仍须联合验收单独检查；锚点工具维护旧日志配对顺序，不代替这些数值验收。主机报告hardware_verified恒false。
6. 本版本没有校时/校频、双板同步、样本到声学时间的精密映射、检测或测距。异常注入和跨会话等当前仅有主机测试证据。

## 已执行的本版本实机验收步骤

使用冻结Debug ELF和原ST-LINK配置进行一次program/verify/reset，不改选项字节；同步采集约90秒COM3（115200/8N1，DTR/RTS关闭）与原UDP5005、6006、6007。
保持DHT11 PG7/D4、3.3V接线；无需发声、改线或拔插。

门槛在烧录前固定：至少60条有效锚点、有效率≥90%，WIDTH≤4、会话不变、帧和PTP持续前进，逐段及长窗口率在约47991.071Hz±1%内；CURSOR/BOUNDARY/WIDE瞬态拒绝逐条统计，PTP/BACKWARD/SESSION/ORDER阻断正常窗口通过。
同时≥60对原AUDIO/POS正常、≥70UDP无缺口、两组30请求及板端/真实C核对、自动温度和codec正常；前台心跳/codec间隔如实记录，不把旧1020/2050ms经验值冒充精密硬门槛。
采集持续时间、原始串口字节、BOOT/UID、ELF和源码哈希须联合核对。本次正常窗口结果如下，随代码以jxq提交归档，停止于M3.2.2。


## 本次实物结果（2026-10-23）

用户明确批准Debug SHA256前缀add1e06a892b的本版本，沿原配置单次program/verify/reset通过。
烧录后捕获90.071402秒，全程93.299825秒；COM3及三个UDP子进程全部关闭，无附加复位、发声、改线或拔插。

| 检查 | 实测结果 |
| --- | --- |
| BOOT/UID | SBCG-M3.2-ANCHOR，HCLK200MHz；001A002D:30365105:36363631 |
| 锚点 | SEQ1～90，SESSION=1；90/90有效，拒绝0，PTP/帧位置持续前进 |
| 夹取宽度 | 83条WIDTH=1、7条WIDTH=0；最大1帧，低于预定4帧门槛；0仍不代表零时间不确定性 |
| 长窗口 | PTP跨度89.143671025秒；相对PTP的DMA进度平均率范围47991.045811881～47991.090683266Hz |
| 逐相邻有效锚点 | 平均率区间总体最小47988.249223841、最大47993.908091666Hz；全部在±1%健全性带内 |
| 率中点 | 47991.068247573Hz；相对推导47991.071Hz约−0.057353ppm，仅为区间计算，不是绝对频率准确度或声学延迟标定 |
| 原采集/位置 | 90对有效AUDIO/POS，H/F各264、528块、4325376帧；DROP/ERR/HAL/DMA/GAP/FAULTS=0；粗帧率47953～48018Hz |
| 原UDP | 80包SEQ6～85无缺口/重复/乱序/非法/外来包；逐包tick与串口一致，995～1005ms |
| 原请求/硬件戳 | 30组RX/TX/ACK完整，板端相关统计无错误 |
| 时间交换/估算 | 30组ACK/REPORT/四时刻完整；30个板端估算与真实C内核逐项一致，仍LOCAL/SYNC=0 |
| 温度 | 17个连续有效自动版本，25.5～25.8℃、声速346753～346935mm/s；温度全部错误计数0 |
| codec | 45次ID=8994、错误0，READ_MS0～1；轮询间隔2000～2068ms，平均2003.091ms |
| 心跳/PTP | 各90条连续，UART/PTP错误0；心跳1000～1014ms，平均1000.461ms |

### 证据与边界

原始串口144834字节、1512行JSONL可逐字节重构一致，无残尾。唯一新版BOOT前5条烧录前记录只在解释时排除，原始文件未改；派生窗口的原始哈希、排除位置和窗口哈希在serial-window-provenance.json。

Logs/Audio/M3_2_2/runtime-01保留runtime-run.json、原串口/三路UDP/flash日志、acceptance.json、anchor-window-check.json、requirements-comparison.json及真实C输入/输出。新CLI直接对派生BOOT窗口检查通过，报告仍hardware_verified=false；硬件结论由授权/烧录/BOOT/原始字节/源码和ELF哈希及全部业务联合建立。
基于本次真实捕获的7种负例（宽度、会话、非法纳秒、伪造累计、错误区间、uint64越界及局部100ms时间偏移）均被拒绝，不改原始数据。

codec最大2068ms超过旧2050ms经验比较值；本次烧录前冻结门槛已规定记录前台轮询抖动，不将2050/1020ms作为精密硬上限。45次ID读取及全部锚点/音频/网络正常，保留该数值，不宣称精密轮询周期，也不把抖动归因到未经测量的具体服务。

本轮没有受控声音或实物故障注入；同会话倒退锁存、跨会话重建、待处理错误等仍为主机测试证据。NDTR游标不是SRAM可读授权，未标定codec/SAI/FIFO/DMA到物理采样或声学事件的延迟。相对本地PTP的区间平均率不等于绝对频率校准或双板同步精度。
本增量完成后停止，不自动实现精密样本时间映射、检测或测距。
