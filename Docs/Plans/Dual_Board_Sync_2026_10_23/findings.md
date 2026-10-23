# 双板方案证据

<!-- doc-status: current-work-notes; reviewed: 2026-10-23 -->

- 基线3240aba，初始工作区干净；M3.2.4单板已验收，本轮用户已改变优先级，第二板现在纳入范围。
- A UID：001A002D:30365105:36363631；B UID：00380050:35385104:37313532（用户提供其SBCG-M3.2-MAP/200MHz BOOT）。板型均STM32F746G-DISCO。
- 用户后续确认接线已完成：交换机A=2号口、B=1号口、PC=3号口；最新范围为先交付/验收A板程序，B之后由用户烧录。物理接线不构成双板网络验收；A验收时B旧固件须先隔离，避免旧默认地址冲突。
- 当前生成IP=192.168.10.1、MAC=02:00:00:00:00:01；同固件两板会冲突，先按UID选A=.1/MAC尾1、B=.2/MAC尾2，PC沿用.100/24。
- LWIP/App/lwip.c有IP_ADDRESSES用户区，在lwip_init/netif_add之前；ethernetif.c有MACADDRESS用户区，在HAL_ETH_Init之前。仅从用户模块拷贝应用身份，不改生成区主体或.ioc。
- 当前PTP是coarse 5ns，ConfigMatches明确禁止fine；PTP观察器和音频锚点会拒绝停滞/倒退，不能直接加入校时而不处理时钟代际/在途报文/音频模型。
- 本地HAL_ETH_PTP_AddTimeOffset还会调用含无界等待的AddendUpdate，且将时间偏差混入addend；不能未经核对直接接为伺服。后续需受控BSP扩展，HAL原文件不改。
- 角色方案已见Hardware_Resources 5.8：同源同固件、96位UID允许名单；不按启动顺序或传感器响应猜角色。未知/配置重复必须拒绝联网业务。
- ST RM0385官方手册：https://www.st.com/resource/en/reference_manual/dm00124865.pdf；网页工具因大于10MB无法全文打开，当前实现以仓库本地HAL为准；不拿上游master替换本地驱动。
