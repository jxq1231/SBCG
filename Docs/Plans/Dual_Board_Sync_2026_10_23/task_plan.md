# 双板时间同步工作计划

<!-- doc-status: current-plan; reviewed: 2026-10-23 -->

目标：A为参考时钟、B从钟，以板间四个硬件时间戳建立时差观测，再逐项实现受控相位/频率校正与独立验收。一次仅实现一个可验收增量。

当前实现：M2.3.2 双板时差/漂移及B硬件校时闭环，软件与1分钟运行内部观测通过；此前M2.3.1双板网络共存已通过。用户已手动烧录两板SBCG-M2.3-SYNC，外部精度尚未验证。

- [x] 核对现有代码和实际UID/拟用拓扑。
- [x] 纯C身份映射/BSP地址选择，先红后绿。
- [x] 两处LwIP用户区接入与App身份日志/未知UID禁用路径，先红后绿。
- [x] 双板共用5005接收端按IP/UID分别跟踪，主机负例测试。
- [x] 原模块回归、Debug/Release与布局、生成区保护/独立复核。
- [x] 按最新要求冻结同一份A/B固件，当前先交付A板程序，B板由用户之后烧录。
- [x] 用户“验收A板”授权后，仅A烧录/校验/复位一次，123秒角色/IP/MAC/UDP及原业务通过；B网线已断开，未连接/烧录。
- [x] 用户随后更新B；60秒双板各60包、B30请求和IP/MAC/UID对应通过。网络目标通过，B完整外设/PTP另验。
- [x] 本次验证提交限A板实测范围，B未验收/双板未完成边界保留；不提前启动下一增量。

后续独立目标：双板全部HW的四时刻观测；B相位校正/时钟代际；频率伺服/失联恢复；独立同步误差/PPS验收。后续不是当前实现授权范围的自动并行扩张。

文件范围：App/BSP/board_identity.c/.h、bsp_board_identity.c/.h；App/app_main.c；LWIP/App/lwip.c和LWIP/Target/ethernetif.c的安全用户区；用户CMakeLists.txt；Tests/BoardIdentity、Tests/App；Tools/Monitor/dual_board_monitor.py及测试；本计划和状态/协议文档。
硬件：No CubeMX change required。应用网络身份选择不改变GPIO/RMII/RCC/SAI/DMA/NVIC/Cache/.ioc；不改HAL或生成初始化主体。

## 用户追加：M2.3.2一次实现观察与校时

- [x] 逐包重放双板原始日志并核对B应答，编写[闭环方案](../2026-10-23_Dual_Board_Clock_Closed_Loop.md)。
- [x] 用户将CubeMX MEMP_NUM_UDP_PCB改6并重新生成；核对差异。
- [x] 明确批准BSP fine/有界寄存器运行时扩展；不改HAL/生成初始化。
- [x] 一个闭环增量内完成PTPv2四时刻、漂移、B相位/频率及代际隔离，先红后绿。
- [x] 回归、两配置构建/布局、复核和冻结。
- [x] 用户手动烧录两板，按其要求缩短为1分钟；短时内部闭环及原业务观测通过，保留冷启动/外部精度/故障注入未测边界。
- [x] 按软件及1分钟运行观测的限定范围作验证提交；冷启动、故障注入和外部精度保留未测。
