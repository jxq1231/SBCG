# 双板时间同步工作计划

<!-- doc-status: current-plan; reviewed: 2026-10-23 -->

目标：A为参考时钟、B从钟，以板间四个硬件时间戳建立时差观测，再逐项实现受控相位/频率校正与独立验收。一次仅实现一个可验收增量。

当前唯一实现：M2.3.1 同固件UID角色与唯一网络身份，固件SBCG-M2.3-ROLE；此增量不校时，SYNC仍0。

- [x] 核对现有代码和实际UID/拟用拓扑。
- [x] 纯C身份映射/BSP地址选择，先红后绿。
- [x] 两处LwIP用户区接入与App身份日志/未知UID禁用路径，先红后绿。
- [x] 双板共用5005接收端按IP/UID分别跟踪，主机负例测试。
- [x] 原模块回归、Debug/Release与布局、生成区保护/独立复核。
- [x] 按最新要求冻结同一份A/B固件，当前先交付A板程序，B板由用户之后烧录。
- [x] 用户“验收A板”授权后，仅A烧录/校验/复位一次，123秒角色/IP/MAC/UDP及原业务通过；B网线已断开，未连接/烧录。
- [ ] 用户之后烧录B板，再单独完成双板共存验收；未完成之前不宣称M2.3.1全范围通过。
- [x] 本次验证提交限A板实测范围，B未验收/双板未完成边界保留；不提前启动下一增量。

后续独立目标：双板全部HW的四时刻观测；B相位校正/时钟代际；频率伺服/失联恢复；独立同步误差/PPS验收。后续不是当前实现授权范围的自动并行扩张。

文件范围：App/BSP/board_identity.c/.h、bsp_board_identity.c/.h；App/app_main.c；LWIP/App/lwip.c和LWIP/Target/ethernetif.c的安全用户区；用户CMakeLists.txt；Tests/BoardIdentity、Tests/App；Tools/Monitor/dual_board_monitor.py及测试；本计划和状态/协议文档。
硬件：No CubeMX change required。应用网络身份选择不改变GPIO/RMII/RCC/SAI/DMA/NVIC/Cache/.ioc；不改HAL或生成初始化主体。
