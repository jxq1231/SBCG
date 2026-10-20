# M2.1.2 RX Hardware Timestamp Implementation Plan

> 执行方式：使用superpowers:executing-plans在当前已隔离的jxq功能分支逐项执行；不新增工作树。功能提交仍遵守根AGENTS.md，实物通过前不提交功能完成。

**Goal:** 为30个合法电脑UDP请求关联本板DMA接收时间，保持既有请求应答及心跳。

**Architecture:** BSP只读HAL接收时间缓存与状态；用户接收入口包装建立同步帧上下文，UDP合法请求领取一次后进入固定队列，主循环限速输出。配置仅开启已获用户明确许可的TimestampAll，不改生成代码。

**Tech Stack:** STM32F746、CubeF7 1.17.4、NO_SYS LwIP 2.1.2、C11、GCC/CMake、原生C及Python测试。

文件与数据流程以[已获确认的设计](2026-10-20_PTP_RX_Design.md)为准。

## 1. BSP读取边界

- [x] Tests/Ptp/test_bsp_ptp.c先要求TimestampAll=ENABLE，运行python -B Tests/Ptp/run_tests.py，观察配置断言失败。
- [x] App/BSP/bsp_ptp.h增加接口，App/BSP/bsp_ptp.c先提供返回NOT_READY的桩；测试要求有效缓存可读并观察断言失败。

```c
BSP_PtpStatus BSP_PTP_ReadRx(BSP_PtpTime *time);
```

- [x] 实现TimestampAll、增强描述符、最后描述符CPU所有权、ES汇总、HAL返回值、纳秒与损坏值检查；只在全部通过后覆盖输出。测试bit0扩展状态有效不能误判错误，以及配置丢失/损坏/越界/空指针。
- [x] 复跑原BSP及本地时钟测试，全部退出0。

## 2. 同步帧关联服务

- [x] 新建App/Sync/ptp_rx.h及Tests/Ptp/test_ptp_rx.c、更新Tests/Ptp/run_tests.py；实现文件先放空桩，测试现有netif.input应被包装时失败。

```c
typedef void (*PtpRx_Log)(const char *line, int length);
void PtpRx_Init(uint32_t now_ms, PtpRx_Log log);
void PtpRx_Observe(const uint8_t request[28]);
void PtpRx_Process(uint32_t now_ms);
```

- [x] App/Sync/ptp_rx.c安装/保存原输入函数，按完整IPv4/UDP长度及IP选项定位28字节请求，拒绝分片/VLAN观察上下文但原样交付原输入函数。读取硬件缓存并与后读当前时钟比较；只在原输入调用期间允许完整请求键领取一次。
- [x] 实现4条固定队列、每秒最多一条观察记录及一条累计统计；统计VALID/MISSING/INVALID/OVERFLOW/INIT_ERR，不持有pbuf、不代替HAL时间、不阻止应答。
- [x] 实际测试同步关联、输入返回值、链式pbuf/IP选项、错包与旧帧、片段/VLAN、无效/未来时间、一次领取、队列溢出/排空、tick回绕与重复初始化。python -B Tests/Ptp/run_tests.py应全部退出0。

## 3. 原应用集成

- [x] Tests/UdpProbe/test_udp_probe.c加通知桩，断言合法请求释放前通知、非法请求不通知；先观察未调用时失败，再修改App/Network/udp_probe.c。
- [x] Tests/App/test_app_main.c加接入桩，要求PTP时钟之后初始化、网络与现有观察之后处理及SBCG-M2.1-RX版本；先观察缺少接入时失败，再修改App/app_main.c。
- [x] CMakeLists.txt的现有LwIP分支注册App/Sync/ptp_rx.c；生成CMake不改。
- [x] 完整检查：Tests/Ptp、App、UdpProbe、Udp、Network的run_tests.py，以及python -B -m unittest discover -s Tools/Monitor/tests -v。逐项记录退出码，失败时停止继续阶段。

## 4. 构建、保留性与实物

- [x] python -B Tools/Monitor/autodebug.py build；Release加--preset Release；两种配置分别运行check_eth_layout.py。要求无新未解释诊断、布局通过、公共宏覆盖所有C单元。
- [x] 快照源码/哈希后使用既有CubeMX批处理Generate Code；逐项比较，许可证从快照逐字保留；复跑构建及布局，记录ELF哈希。
- [x] 读取本轮最终差异，按设计取得本固件的烧录/自动验收授权后才写Flash；仍使用现有OpenOCD配置。
- [x] COM3 115200/8N1自动采集；udp_monitor.py运行70秒；udp_probe.py运行30个每秒请求。要求30条完整键匹配RX记录、30个匹配ACK、无观察错误/溢出、至少60条PTP及心跳正常、UDP观察区间零缺口。末尾等待队列排空，保存原文和SHA256。
- [x] Docs/Milestones/M2_1_PTP_RX.md、Project_State、文档入口及根README按实际结果更新，不能把构建代替实测。
- [x] 只读实现复核及实物联合验收通过。最终只暂存本功能文件，以jxq提交并按已有授权推送；提交与远端结果以Git历史核对，本文不自引用本次提交。停止，不开始TX。

本计划已按目标、接口命名、固定队列所有权及验收口径自检。失败测试、构建与实物证据在各阶段实际执行后填写，不预先声称通过。
