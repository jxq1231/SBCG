# M2.2.2 实施清单

> 执行：superpowers:subagent-driven-development；电脑工具与固件服务独立分工，集成及复核由主任务负责。沿用用户当前功能分支和项目Docs/Plans约定。

**Goal:** 单板每秒回应专用UDP请求，以同一会话键回报真实RX和ACK TX，电脑保存30组四时刻样本。

**Architecture:** 外层netif包装复用原RX/TX链与BSP，4条CPU记录由Process回报；PC工具独立保存软件/硬件来源。原服务协议不变。

**Tech Stack:** STM32F746、NO_SYS LwIP 2.1.2、C11、GCC/CMake、Python标准库。

日期：2026-10-20。用户已批准 Time_Exchange_IO_Design.md；基线5c0744b，初始工作区干净。仅实施单板时间戳数据链；新固件随后获得专门烧录授权并完成联合实物验收。

## 文件与责任

- App/Network/udp_time_exchange.c/.h：5007服务、输入/输出包装、4条2000ms记录、52字节REPORT、两组1Hz统计。
- App/Network/udp_config.h：5007/6007常量。
- App/app_main.c：先Deinit、后旧包装初始化、最后新Init；Process和M2.2-IO；日志上限256。
- CMakeLists.txt：用户拥有的LwIP分支注册新源。
- Tests/TimeExchange/test_udp_time_exchange.c、run_tests.py：真实旧包装链上的IO替身集成测试。
- Tests/App/test_app_main.c：初始化顺序、版本、调度、长日志边界。
- Tools/Monitor/time_exchange_probe.py、tests/test_time_exchange_probe.py：独立电脑工具及标准库测试。
- Docs/Protocol.md、Docs/Milestones/M2_2_Time_Exchange.md：协议与待实物验收状态。
- 本清单、设计、Docs/Project_State.md、Docs/Project_Roadmap.md、Docs/README.md、README.md：准确进度与入口。

No CubeMX change required。仅复用ETH内部DMA及BSP时间戳接口；GPIO/TIM/NVIC/其他外设/Cache不变。不得修改.ioc、Core、HAL、生成胶水、旧观察器或BSP。

## 执行与验证

- [x] 先建立新原生测试及空实现，观察运行断言失败；实现完整键、上下文一次领取及正常ACK/REPORT。
- [x] 先失败后实现重复/冲突/容量/TTL/回绕、错误路径、PCB重试、Deinit与旧包装互斥。
- [x] 电脑工具先失败后实现；覆盖乱序、迟到、错键、缺项、时间来源及大整数JSON；本地socket测试。
- [x] 应用接入前测试顺序、版本和日志边界，再修改App及用户CMake。
- [x] 全部原生与Python回归；Debug/Release现有工作流构建、布局、PTP宏及PCB检查。
- [x] 独立规格及质量复核；实际Generate Code回归，保留LAN8742许可证。
- [x] 更新准确文档，记录具体ELF SHA256，申请本版本烧录授权。
- [x] 获准后原配置烧录校验；COM3、5005 70秒、旧30请求、新30请求联合验收。
- [x] 实物证据通过后核对diff/身份，以jxq提交并按已有协作授权推送；停止本增量。

## 固定实现约定

协议严格采用设计的36/52字节大端布局、type4/5/6及非零SESSION。输入上下文仅在原input调用期间有效；输出包装不分配、不打印、不发REPORT。REPORT仅Process发送，每轮最多一条。记录完成后保留至原TTL作为重复标记；无效时间不得填充软件时间。

电脑t1/t4来自monotonic_ns，板t2/t3仅来自REPORT。JSON纳秒为字符串；只有同一完整键ACK和REPORT齐全才输出four_stamp_sample。工具hardware_verified恒false，联合验收另给硬件结论。

烧录前不作功能完成提交，不宣称实物通过，不调用估算器或调整时钟。

## 已获证据（2026-10-21状态更新）

177项检查全通过，新服务26组、电脑新增25项、App9组。Debug/Release各119个C单元统一PTP宏，构建无诊断、布局通过；具体哈希见M2_2_Time_Exchange里程碑报告。实际Generate Code的444文件保持一致，LAN8742许可证从快照原样恢复。电脑/App规格及质量、固件规格复核通过，最终固件质量与整体集成复核亦通过。随后本版本明确授权烧录、校验及复位；30组新ACK/REPORT/样本、原30组RX/TX/ACK、70包零缺口及90条PTP/心跳联合验收通过，所有异常0。实物通过后按jxq提交交付。
