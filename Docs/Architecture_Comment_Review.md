# 系统梳理、App中文注释与验证记录

<!-- doc-status: completed-maintenance-evidence; reviewed: 2026-10-23 -->

本轮在业务基线ed3ef57上完成架构/调用链/答辩资料和App中文注释。单一目标是可读性与说明，未改功能、初始化顺序、接口签名、标识符、条件编译或硬件配置。不是新的功能里程碑，也没有新实物验收。

## A、B、C. 阅读入口

[系统架构与真实流程](System_Architecture.md)提供目录职责表、构建来源、启动/主循环/IRQ路径和数据流；[答辩稿](Defense_Guide.md)提供30秒、1～2分钟及22个追问参考。流程同时保留Mermaid、可编辑Draw.io及SVG，均为文档资源，不进入固件。

## D. 文件修改清单

全部58个App C/H已检查：28个C、30个H，156个实际函数定义、72个函数声明具备中文Doxygen说明；静态函数、HAL/网络回调、日志回调类型和现有DWT函数式宏均纳入对应检查。文件头说明职责/流程/依赖；函数说明实际形参、单位、返回、状态变化及上下文。无参数函数说明输入来源，不编造参数；公开声明与定义分配接口和实现说明。

| 修改文件 | 文件职责（取自已核对的文件头） | 定义/声明 |
| --- | --- | --- |
| [App/app_main.c](../App/app_main.c) | 协调单板网络、时间观测、温度与音频服务的初始化和前台调度。 | 4/0 |
| [App/app_main.h](../App/app_main.h) | 对外提供应用初始化与主循环推进两个入口。 | 0/2 |
| [App/Audio/audio_capture.c](../App/Audio/audio_capture.c) | 消费音频半缓冲并输出电平、帧位置和运行状态。 | 3/0 |
| [App/Audio/audio_capture.h](../App/Audio/audio_capture.h) | 消费音频DMA半缓冲并输出电平和帧位置。 | 0/2 |
| [App/Audio/audio_frame_index.c](../App/Audio/audio_frame_index.c) | 维护观测到的DMA边界帧坐标与黏滞故障。 | 2/0 |
| [App/Audio/audio_frame_index.h](../App/Audio/audio_frame_index.h) | 维护观测到的DMA边界帧坐标与黏滞故障。 | 0/2 |
| [App/Audio/audio_statistics.c](../App/Audio/audio_statistics.c) | 计算双声道交织PCM16幅值统计。 | 2/0 |
| [App/Audio/audio_statistics.h](../App/Audio/audio_statistics.h) | 计算双声道交织PCM16幅值统计。 | 0/1 |
| [App/Audio/audio_time_anchor.c](../App/Audio/audio_time_anchor.c) | 采集音频传输进度与本地PTP时钟的夹读锚点。 | 6/0 |
| [App/Audio/audio_time_anchor.h](../App/Audio/audio_time_anchor.h) | 采集音频传输进度与本地PTP时钟的夹读锚点。 | 0/3 |
| [App/Audio/audio_time_map.c](../App/Audio/audio_time_map.c) | 由两锚点估计目标帧进度的本地PTP模型时间。 | 2/0 |
| [App/Audio/audio_time_map.h](../App/Audio/audio_time_map.h) | 由两锚点估计目标帧进度的本地PTP模型时间。 | 0/1 |
| [App/Audio/audio_time_map_monitor.c](../App/Audio/audio_time_map_monitor.c) | 用三个顺序锚点检查本地音频时间映射。 | 9/0 |
| [App/Audio/audio_time_map_monitor.h](../App/Audio/audio_time_map_monitor.h) | 用三个顺序锚点检查本地音频时间映射。 | 0/2 |
| [App/Audio/codec_monitor.c](../App/Audio/codec_monitor.c) | 轮询编解码器标识并输出诊断。 | 3/0 |
| [App/Audio/codec_monitor.h](../App/Audio/codec_monitor.h) | 轮询编解码器标识并输出诊断。 | 0/2 |
| [App/BSP/bsp_audio.c](../App/BSP/bsp_audio.c) | SAI2 循环采集、DMA 半块借用与帧索引的板级边界。 | 13/0 |
| [App/BSP/bsp_audio.h](../App/BSP/bsp_audio.h) | SAI2 循环采集、DMA 半块借用与帧索引的板级边界。 | 0/6 |
| [App/BSP/bsp_audio_codec.c](../App/BSP/bsp_audio_codec.c) | WM8994 身份读取和 MIC2 输入通路的同步 I2C 访问。 | 3/0 |
| [App/BSP/bsp_audio_codec.h](../App/BSP/bsp_audio_codec.h) | WM8994 身份读取和 MIC2 输入通路的同步 I2C 访问。 | 0/2 |
| [App/BSP/bsp_console.c](../App/BSP/bsp_console.c) | USART1 控制台同步发送及中断接收队列适配。 | 11/0 |
| [App/BSP/bsp_console.h](../App/BSP/bsp_console.h) | USART1 控制台同步发送及中断接收队列适配。 | 0/7 |
| [App/BSP/bsp_dht11.c](../App/BSP/bsp_dht11.c) | DHT11 开漏启动脉冲和基于 DWT 的五字节波形捕获。 | 8/0 |
| [App/BSP/bsp_dht11.h](../App/BSP/bsp_dht11.h) | DHT11 开漏启动脉冲和基于 DWT 的五字节波形捕获。 | 0/4 |
| [App/BSP/bsp_ethernet.c](../App/BSP/bsp_ethernet.c) | LAN8742 PHY 身份与链路状态的只读板级观测。 | 6/0 |
| [App/BSP/bsp_ethernet.h](../App/BSP/bsp_ethernet.h) | LAN8742 PHY 身份与链路状态的只读板级观测。 | 0/2 |
| [App/BSP/bsp_ptp.c](../App/BSP/bsp_ptp.c) | 本地 ETH PTP 时钟及最近接收帧硬件戳的访问。 | 5/0 |
| [App/BSP/bsp_ptp.h](../App/BSP/bsp_ptp.h) | 本地 ETH PTP 时钟及最近接收帧硬件戳的访问。 | 0/3 |
| [App/BSP/bsp_ptp_tx.c](../App/BSP/bsp_ptp_tx.c) | 同步单缓冲以太网发送的硬件 TX 时间戳交接。 | 5/0 |
| [App/BSP/bsp_ptp_tx.h](../App/BSP/bsp_ptp_tx.h) | 同步单缓冲以太网发送的硬件 TX 时间戳交接。 | 0/3 |
| [App/Console/temperature_console.c](../App/Console/temperature_console.c) | 解析串口温度命令并返回环境快照。 | 7/0 |
| [App/Console/temperature_console.h](../App/Console/temperature_console.h) | 解析串口温度命令并返回环境快照。 | 0/2 |
| [App/Network/ethernet_monitor.c](../App/Network/ethernet_monitor.c) | 轮询以太网PHY状态并输出初始化与链路日志。 | 3/0 |
| [App/Network/ethernet_monitor.h](../App/Network/ethernet_monitor.h) | 轮询以太网PHY状态并输出初始化与链路日志。 | 0/2 |
| [App/Network/udp_config.h](../App/Network/udp_config.h) | 定义应用UDP协议地址、端口、周期和报文长度。 | 0/0 |
| [App/Network/udp_heartbeat.c](../App/Network/udp_heartbeat.c) | 通过LwIP RAW UDP周期发送板端心跳。 | 4/0 |
| [App/Network/udp_heartbeat.h](../App/Network/udp_heartbeat.h) | 通过LwIP RAW UDP周期发送板端心跳。 | 0/2 |
| [App/Network/udp_probe.c](../App/Network/udp_probe.c) | 接收并校验PC探测请求，返回对应UDP应答。 | 4/0 |
| [App/Network/udp_probe.h](../App/Network/udp_probe.h) | 接收并校验PC探测请求，返回对应UDP应答。 | 0/2 |
| [App/Network/udp_time_exchange.c](../App/Network/udp_time_exchange.c) | 关联一次UDP时间交换的板端接收、发送硬件时间与PC提交时刻。 | 19/0 |
| [App/Network/udp_time_exchange.h](../App/Network/udp_time_exchange.h) | 关联一次UDP时间交换的板端接收、发送硬件时间与PC提交时刻。 | 0/3 |
| [App/Sync/ptp_clock.c](../App/Sync/ptp_clock.c) | 初始化并观测本地以太网MAC PTP计时器。 | 4/0 |
| [App/Sync/ptp_clock.h](../App/Sync/ptp_clock.h) | 初始化并观测本地以太网MAC PTP计时器。 | 0/2 |
| [App/Sync/ptp_rx.c](../App/Sync/ptp_rx.c) | 为已校验的v1 UDP探测请求关联MAC接收描述符时间。 | 7/0 |
| [App/Sync/ptp_rx.h](../App/Sync/ptp_rx.h) | 为已校验的v1 UDP探测请求关联MAC接收描述符时间。 | 0/3 |
| [App/Sync/ptp_tx.c](../App/Sync/ptp_tx.c) | 观测v1 UDP探测应答的MAC发送描述符时间。 | 6/0 |
| [App/Sync/ptp_tx.h](../App/Sync/ptp_tx.h) | 观测v1 UDP探测应答的MAC发送描述符时间。 | 0/2 |
| [App/Sync/sync_estimator.c](../App/Sync/sync_estimator.c) | 依据一次配对的四时刻交换计算时差和平均路径延迟。 | 1/0 |
| [App/Sync/sync_estimator.h](../App/Sync/sync_estimator.h) | 依据一次配对的四时刻交换计算时差和平均路径延迟。 | 0/1 |
| [App/Sync/sync_observation.c](../App/Sync/sync_observation.c) | 排队并限速计算已配对四时刻交换的观测结果。 | 6/0 |
| [App/Sync/sync_observation.h](../App/Sync/sync_observation.h) | 排队并限速计算已配对四时刻交换的观测结果。 | 0/3 |
| [App/Temperature/dht11_frame.c](../App/Temperature/dht11_frame.c) | 校验并解析DHT11五字节帧。 | 1/0 |
| [App/Temperature/dht11_frame.h](../App/Temperature/dht11_frame.h) | 校验并解析DHT11五字节帧。 | 0/1 |
| [App/Temperature/sound_speed.c](../App/Temperature/sound_speed.c) | 由温度计算声速近似值。 | 1/0 |
| [App/Temperature/sound_speed.h](../App/Temperature/sound_speed.h) | 由温度计算声速近似值。 | 0/1 |
| [App/Temperature/temperature_config.h](../App/Temperature/temperature_config.h) | 集中定义DHT11采样时序、有效期、数值范围及开发UID白名单。 | 0/0 |
| [App/Temperature/temperature_service.c](../App/Temperature/temperature_service.c) | 协调DHT11采集、温度声速有效性和手动模式。 | 11/0 |
| [App/Temperature/temperature_service.h](../App/Temperature/temperature_service.h) | 协调DHT11采集、温度声速有效性和手动模式。 | 0/6 |

`App/app_main.c`还以独立块注释标注构建/日志状态、一次初始化和前台调度，说明DHT门控和网络优先级；保留原执行顺序。原bsp_ethernet.h中“after CubeMX MX_ETH_Init”的英文说明泛化为生成ETH初始化，避免与当前MX_LWIP_Init内部路径矛盾。版权和许可声明保留。

## E. 关键函数与异步职责

| 入口/函数 | 实际职责 |
| --- | --- |
| Reset_Handler / SystemInit | 复位与芯片基础环境，之后准备C数据区和构造器 |
| main | MPU、HAL、系统/外设时钟、生成外设初始化，然后App_Init和无限主循环 |
| App_Init | 网络包装依赖、身份/日志和各服务初始化；外层所有权保护不变 |
| App_Process | 每轮先LwIP，再网络/PTP、温度/命令、受门控的codec/音频/锚点/MAP及日志排空 |
| HAL_UART_RxCpltCallback / ErrorCallback | IRQ中交接单字节/错误，前台命令解析 |
| HAL_SAI_RxHalfCpltCallback / RxCpltCallback / ErrorCallback | 音频DMA完成块/错误状态交接，前台安全领取统计 |
| AudioTimeAnchor_Process / TakeObservation | 游标夹取一次PTP、保留有效性/会话/序号的单槽CPU快照 |
| AudioTimeMapMonitor_Process / AudioTimeMap_Estimate | 三点非重叠留出自检与纯整数模型，非声音事件检测 |
| TemperatureService_Process / TemperatureConsole_Process | 数字DHT状态推进、整数声速/手动模式与前台UART命令 |
| UDPTimeExchange与SyncObservation | 前台RAW协议收发、四时刻关联与offset/delay观测，无时钟闭环 |

**CubeMX：No CubeMX change required。** Core、HAL、LwIP生成代码、.ioc、CMake/链接配置均未修改。**无新IRQ/DMA关系**：USART1优先级5/0，音频SAI2及DMA2 Stream7 Channel0优先级4/0、半字循环64KiB、半满/满8192帧，全部保留；I/D Cache状态和已有ETH维护也未改变。

## F. 验证结果

- 文件/函数覆盖：58/58文件、156定义及72声明；逐形参@param、@brief/@return及UTF-8检查通过，未发现缺漏或替代字符。
- 与任务前App快照逐文件比较：包含字符串/字符字面量和多字符操作符的词法序列完全一致；生成层、.ioc、构建/链接、第三方与测试工具跟踪文件哈希不变。
- 沿现有流程实际执行Debug/Release构建，均编译链接成功，无新增编译warning；两配置ETH/audio布局检查全部通过。

```text
python -B Tools/Monitor/autodebug.py build --preset Debug
python -B Tools/Monitor/check_eth_layout.py --preset Debug
python -B Tools/Monitor/check_audio_layout.py --preset Debug
python -B Tools/Monitor/autodebug.py build --preset Release
python -B Tools/Monitor/check_eth_layout.py --preset Release
python -B Tools/Monitor/check_audio_layout.py --preset Release
```

- Debug Flash镜像：111484B，修改前后字节完全一致；SHA256 `71fc5e9c346bd2061f8b0be7fbc2004ab6829410a489b2032247f76b39ce3d3c`。本轮ELF SHA256 `98e3126f56d9b23760e0f2df9dadda8cfa1b6fa6864f5b46bca2a33b67aeecfa`。
- Release Flash镜像：64872B，修改前后字节完全一致；SHA256 `cc6bc1f097745a661293165b8c37ff5fa994999e9d4a780444f344ec37e896ad`。本轮ELF SHA256 `99566cbc92a16c935e2ece7890c0ec5b696330bf95ea4b55d3c3045bce726253`。

Debug含源码行号等调试信息，增添注释后ELF哈希会变；相同工具链objcopy导出的完整可烧录镜像相同。Release ELF也保持原哈希。板上仍是之前已验收的Debug固件SHA前缀9921f102cc60，**本次没有连接、复位、烧录、采集或硬件运行测试**。原M3.2.4的120秒、40组实物结论保留原哈希和版本边界；不把本次构建称为新实物验收。

本机证据位于Logs/Architecture/2026-10-23：before.json与before/App、comment-audit.json、compilation-inventory.json、6个构建/布局日志、firmware-equivalence.json及模块只读复核笔记。Logs不随源码提交。Draw.io工具初始缺依赖、默认新版依赖不兼容，在忽略目录隔离工具并固定兼容版本后结构/XML验证通过；SVG预览暴露导出裁切/穿线，规范化同一11节点10边的几何和多行文本后已重新目视确认。未改项目依赖或构建环境。

## G. 实际发现与后续建议

1. **网络发送返回值存在既有限制**：生成ethernetif.c的low_level_output未向上传递HAL_ETH_Transmit失败，因此STACK_OK/ACK_OK/REPORT_OK不能单独证明物理发送。后续如修复，需选再生成安全扩展点并单独验证；本轮只如实说明，M3.2.4电脑实际收包仍是有效实物证据。
2. **无“全系统无阻塞”保证**：串口同步发送、I2C与DHT脉冲读取仍有有界耗时。当前门控/状态机不等于RTOS或硬实时调度；精确事件时间应继续使用硬件戳/音频模型而非前台日志时刻。
3. **本地模型不是声学校准**：NDTR进度、归还后元数据及三点自检均未接上声音检测/TDOA消费链；需后续独立设计事件时间查询、延迟标定与双板共同时间。
4. **显示尚未进入当前系统**：后续应先经CubeMX完成SDRAM/LTDC及基础帧缓冲验收，再决定TouchGFX和屏幕内容。不能直接拿本轮文档流程图当作已运行的GUI架构。
5. **CI范围需明确**：现有.github/workflows/host-checks.yml主要覆盖Tools/Monitor与VS Code JSON，路径过滤并不自动为每次App修改构建固件。当前本地验证通过不能等同于已覆盖全部固件的远端CI。
6. **现有状态恢复边界需尊重**：温度失败立即撤销fresh，旧值不是仍可用的保证；音频编号故障与同会话BACKWARD有锁存策略，不能把下次心跳或一次重试当作所有故障已清除。未发现需要在本轮擅自改变初始化顺序的证据。

审查发现的文档函数名和图片范围错误已修正；独立只读复核未留下新增P1/P2。完成文档/注释增量后停止，保持M3.2.4为最近业务实物基线。
