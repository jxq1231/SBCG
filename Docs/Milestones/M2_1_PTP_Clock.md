# M2.1.1 单板MAC本地PTP硬件时钟

更新：2026-10-20。基线`1d8b777`已统一启用HAL_ETH_USE_PTP；本轮完成第四步用户模块PTP时钟与限速观察。**82项主机检查、Debug/Release构建/布局、实际Generate Code保留性回归，以及经用户授权的烧录、校验和实物运行均通过。** 新固件SBCG-M2.1-CLOCK取得88条连续PTP状态/心跳、70个零缺口UDP包及30/30匹配应答。这是本地计时，不是两板时间同步，也未取得TX/RX硬件事件时间戳。

## 配置与所有权

- 沿用CubeMX生成的ETH句柄、RMII、200 MHz HCLK及现有帧DMA；在MX_LWIP_Init之后，由BSP调用HAL_ETH_PTP_SetConfig配置PTP。
- 生成代码的__HAL_RCC_ETH_CLK_ENABLE只包含MAC/TX/RX，不包含ETHMACPTP。BSP在配置PTP前通过__HAL_RCC_ETHMACPTP_CLK_ENABLE开启专用门控；读时同时校验门控仍开启。这一门控属于本次已说明的PTP扩展，不改变系统PLL或200 MHz频率。
- 使用coarse更新、十进制纳秒回绕；每个HCLK增量5 ns，计算为1e9/200e6。5 ns是配置的计数步长，不是实测绝对精度或同步误差。
- HCLK不是200000000时报告BAD_CLOCK，拒绝按错误频率配置；不调整主PLL。fine/addend频率校正留给后续同步增量。
- TimestampAll、IPv4/IPv6/Ethernet帧分类及addend更新保持关闭，本步只观察MAC时钟，不额外请求帧时间戳。不初始化TIM2、不启用PPS或PTP事件IRQ，不绑定319/320端口。
- HAL的SetConfig包含无超时的addend等待，本步不启用该分支。调用前检查无待处理PTP命令；调用后等待初始化/更新位清零，最多20 ms或100000次轮询。失败清除本模块TSE并标记未配置，不复位ETH，不调用Error_Handler。
- HAL GetTime本身没有跨秒保护；BSP在读完秒/纳秒后再核对硬件秒，不一致最多重试3次。所有失败都不覆盖调用方输出，纳秒必须小于1e9。

本步采用前文讨论的PTP专用代码扩展。No CubeMX change required：不改变硬件配置；实际再生成只刷新了.ioc的functionlistsort元数据，生成C源码、HAL配置、生成CMake、驱动和中间件内容未改。根目录CMake只在既有LwIP分支注册两个用户源码，第三步的公共宏继续传递到全部编译目标。宏存在会影响ETH句柄布局，仍必须同时用于应用、HAL和LwIP。

依据为[ST RM0385](https://www.st.com/resource/en/reference_manual/dm00124865.pdf)的PTP控制/子秒增量说明，以及本机CubeF7 V1.17.4的HAL实现。启动后是当前硬件的本地纪元，不定义UTC、两板共同纪元或相对于HALtick的固定零点。

## 本次文件

| 文件 | 用途 |
| --- | --- |
| `App/BSP/bsp_ptp.h` | 状态码、显式秒/纳秒类型和主循环接口 |
| `App/BSP/bsp_ptp.c` | 200 MHz检查、HAL配置、有限等待和跨秒快照 |
| `App/Sync/ptp_clock.h` | 限速日志服务接口 |
| `App/Sync/ptp_clock.c` | 每秒LOCAL/SYNC=0状态、单调性及停走检查；兼容nano格式化库 |
| `App/app_main.c` | 启动/主循环接入及SBCG-M2.1-CLOCK版本 |
| `CMakeLists.txt` | 注册两个新源码 |
| `Tests/Ptp/stubs/main.h` | 引用真实HAL/CMSIS声明，只替换寄存器内存 |
| `Tests/Ptp/test_bsp_ptp.c` | 初始化、超时、错误及跨秒行为测试 |
| `Tests/Ptp/test_ptp_clock.c` | 日志调度、异常、恢复、缓冲边界及不支持64位格式化的约束测试 |
| `Tests/Ptp/run_tests.py` | 原生编译运行两组测试 |
| `Tests/App/test_app_main.c` | 验证新版本及在原网络服务之后接入 |
| `SBCG.ioc` | CubeMX自动刷新的初始化函数清单元数据，未经手改 |
| `README.md` | GitHub进度标识及实测入口 |
| `Docs/Plans/2026-10-20_PTP_Clock.md` | 本增量实施与验收检查表 |
| `Docs/Milestones/M2_1_PTP_Clock.md` | 本页操作与证据边界 |
| `Docs/Project_State.md` | 记录本地时钟实物验收及后续边界 |
| `Docs/README.md` | 增加阅读入口 |

## 调用、IRQ、DMA与Cache

初始化：`main → MX_LWIP_Init → App_Init → PtpClock_Init → BSP_PTP_Init → HAL_ETH_PTP_SetConfig`。实际PTP配置发生在BOOT输出之后；首条PTP状态在约1秒后输出。

主循环先执行`MX_LWIP_Process`、原UDP发送、心跳、PHY观察和UDP应答统计，再执行`PtpClock_Process → BSP_PTP_Read → HAL_ETH_PTP_GetTime + 秒寄存器复核`。日志每秒最多一条，延迟不补发突发；HALtick只用于超时/调度，输出时间来自MAC。

无新增IRQ。PTP读时及日志都在主循环，SysTick仍为HAL时基。无新增DMA关系或配置，沿用ETH内部帧DMA及原pbuf所有权，不增加音频DMA、DMA1/2流或半完成回调。I/D Cache保持未启用、MPU及网络内存布局不变，无新Cache维护调用。

## 主机验证

```powershell
python -B Tests/Ptp/run_tests.py
python -B Tests/App/run_tests.py
python -B Tests/UdpProbe/run_tests.py
python -B Tests/Udp/run_tests.py
python -B Tests/Network/run_tests.py
python -B -m unittest discover -s Tools/Monitor/tests -v
python -B Tools/Monitor/autodebug.py build
python -B Tools/Monitor/check_eth_layout.py
python -B Tools/Monitor/autodebug.py build --preset Release
python -B Tools/Monitor/check_eth_layout.py --preset Release
```

PTP BSP 6组、时钟观察5组、应用5组、UDP应答7组、原发送6组、PHY双模式19组、Python34项，共82项通过。BSP空实现、应用未接入、未拒绝待处理PTP命令和专用门控缺失时，均先观察到对应断言失败，再实现/修复。HAL和寄存器IO在原生测试中被替代，主机测试本身不证明实物时钟运行。

首次实物检查发现DELTA_NS输出为lu且后续参数错位：目标链接nano.specs，其格式化库不支持本次64位转换，主机完整库没有暴露这一差异。测试加入格式化依赖约束后先观察到断言失败，再将uint64_t增量转换为有界十进制字符串，以%s输出；全宽增量4294967294999999994也逐位核对。不改变链接库、时钟或协议。首次记录保留在Logs/UDP/m21-auto-1792483747328-*，该固件未通过PTP日志验收；下表和下方实测均对应修复后固件。

| 构建 | 结果 | Flash占用 | ELF SHA256 |
| --- | --- | --- | --- |
| Debug | 构建/布局通过，诊断为空 | 58772字节 | `bc96b505ecfa55c903539c2299e57a408cd6aad463710ac02b75c29c427ce7ab` |
| Release | 构建/布局通过，诊断为空；未烧录Release | 37448字节 | `373ab46a5769c5e64f8ba9edbe1bd06ba828d173c140f3e0e956fa3ed78ef14c` |

最终再生成后报告为`Logs/AutoDebug/20261020T081421-099b9562/report.json`及`Logs/AutoDebug/20261020T081421-1586c798/report.json`；每种配置114个C编译单元均具有PTP宏。描述符、固定堆、RX池和MSP布局检查通过。RAM的97.02%打印仍为固定高地址段的跨度，不是普通数据实际用量。

## Generate Code实际回归

按本机CubeMX手册的无界面批处理方式，在项目快照后执行config load、project generate、exit，无配置修改指令。首次快照/比较在Logs/M2_1_PTP/regeneration-20261020T080345Z；CubeMX只刷新ProjectManager.functionlistsort，使初始化清单反映LwIP拥有ETH初始化，所有硬件配置键未变。该.ioc变化由CubeMX生成，未手改。

CubeMX会删掉LAN8742组件LICENSE.txt，已从再生成前快照逐字恢复并核对哈希。最终日志修复后再次回归，证据在Logs/M2_1_PTP/regeneration-final：恢复许可证后412个受检文件全部字节一致，无新增/缺失文件；根CMake、公共宏、PTP/App源码、main用户区、生成网络胶水和链接扩展均保留。再构建两种配置，ELF哈希与修复后再生成前一致。

## 实物验收（2026-10-20）

用户明确授权通过现有ST-LINK烧录并自动验收。先打开COM3、115200/8N1、DTR/RTS关闭，再使用现有.vscode/openocd-stm32f746.cfg和program {build/Debug/SBCG.elf} verify reset exit。修复日志前后共烧录两次，均退出0、Programming Finished、Verified OK、Resetting Target；不改选项字节。最终验收仅统计第二次复位后的新BOOT区间，串口开头旧固件残留保留原文但排除在统计之外。

| 项目 | 修复后观察结果 |
| --- | --- |
| 固件/时钟/UID | SBCG-M2.1-CLOCK；HCLK=200000000；001A002D:30365105:36363631；新BOOT仅一次 |
| PTP本地时钟 | SEQ=1～88，共88条；STATE=LOCAL、SYNC=0、ERRORS=0；SEC/NSEC组合严格递增，纳秒范围合法且为5 ns倍数 |
| 采样增量 | TICK_MS相邻差1000 ms；首条PREV=0/DELTA_NS=0，后续PREV=1，输出增量与相邻秒/纳秒计算值逐条相符；范围999716985～1000173000 ns |
| 串口心跳 | SEQ=1～88连续，TX_ERRORS始终0 |
| PHY | 启动协商后UP、100 Mbps、FULL |
| UDP发送 | 70.001秒观察到SEQ=14～83共70包，缺口/重复/乱序/非法/外来均0；69个板端tick间隔均1000 ms，并与串口发送序号/tick逐条匹配 |
| 请求/应答 | 30发30收，SEQ=1～30；来源/UID/请求tick回显匹配，超时及错误项均0；RTT最小/平均/最大0.407/0.565/1.257 ms |
| 板端接收统计 | 最终VALID=30、ACK_OK=30，BAD/FOREIGN/ACK_ERR/INIT_ERR均0；发送ERRORS/MISSED均0 |

实际首尾PTP原文：

```text
$PTP,STATE=LOCAL,SYNC=0,SEQ=1,TICK_MS=1033,SEC=1,NSEC=17090385,DELTA_NS=0,PREV=0,ERRORS=0
$PTP,STATE=LOCAL,SYNC=0,SEQ=88,TICK_MS=88033,SEC=88,NSEC=17226395,DELTA_NS=1000001410,PREV=1,ERRORS=0
```

原始文件均在忽略的Logs/UDP/，前缀为m21-fixed-1792483907577；验收计算及原文SHA256另存Logs/M2_1_PTP/runtime-acceptance.json：

| 文件后缀 | SHA256 |
| --- | --- |
| -serial.txt | 97c4341dcabff6780a0c990ef42a3466126eaea7930462a09222c6ee371d5b17 |
| -flash.txt | a16cb66e4faa562440e28d5019261584cb1bdcd270ea7e8933210f148e0cbc8a |
| -heartbeat.jsonl | ffa17f711685b5103ab0032f29a6ab97ef629f93ac6131a2a8e9b24755b23672 |
| -probe.jsonl | 3bd16242194ae4ebbc4478d62e1ccd5af396a2830ada6e61db588d75f14c8372 |

工具原summary的hardware_verified=false保持原样，表示工具仅判断网络包；本次实物结论来自烧录校验、BOOT、串口和两路UDP联合核对。正常窗口没有拔插；旧M1.4恢复结论只覆盖旧版本，本轮未复测新固件的拔插、冷启动或长时间压力。HALtick与PTP同源，采样差值不是独立测频或同步精度证明，RTT也不是时钟偏差。

## 复验操作

在VS Code使用既有Build/Flash任务。用[SerialWeb](https://conductance-lab.xyz/SerialWeb/)以115200/8N1保存BOOT及至少60条PTP/心跳，保持网线连接、不复位；新测试使用独立日志名。同时运行：

```powershell
python -B Tools/Monitor/udp_monitor.py --uid 001A002D:30365105:36363631 --duration 70 --log Logs/UDP/m21-clock-heartbeat-01.jsonl
python -B Tools/Monitor/udp_probe.py --uid 001A002D:30365105:36363631 --count 30 --log Logs/UDP/m21-clock-probe-01.jsonl
```

格式示例：

```text
$PTP,STATE=LOCAL,SYNC=0,SEQ=1,TICK_MS=1017,SEC=1,NSEC=10000000,DELTA_NS=0,PREV=0,ERRORS=0
$PTP,STATE=LOCAL,SYNC=0,SEQ=2,TICK_MS=2017,SEC=2,NSEC=10000000,DELTA_NS=1000000000,PREV=1,ERRORS=0
```

首条PREV=0、DELTA_NS=0表示尚无前值，后续PREV=1的增量应为正、随约1秒调度大约增加1e9 ns。串口轮询发送及主循环调度会使间隔抖动，不要求每次增量严格等于1e9；HALtick与PTP都源自本机时钟，二者比对仅作计时合理性检查，不是独立测频或绝对精度证明。

验收要求：SEC/NSEC组合单调增加、NSEC始终0～999999999、SYNC始终0、ERRORS不增长，心跳连续且TX_ERRORS=0；正常5005至少30包零缺口，探测30发30收，来源/UID/请求标记匹配。旧M1.4的恢复结论仅覆盖旧固件，不能代替本版本复验。

若出现STATE=ERROR，保存原文：BAD_CLOCK检查HCLK；NOT_READY检查ETH状态或已有PTP命令；INIT_TIMEOUT检查初始化位是否清零；BAD_CONFIG检查TSE/十进制回绕/coarse/增量；BAD_SNAPSHOT检查读取及纳秒范围；STALLED/BACKWARD表示时钟停走或回退。失败路径不自动反复重置ETH，先诊断再继续。

本功能按AGENTS.md第19节完成实物核对后提交。到此停止；后续独立目标可评估TX/RX硬件事件时间戳，不在本增量实现同步伺服、TIM2/PPS、音频或第二板检查。
