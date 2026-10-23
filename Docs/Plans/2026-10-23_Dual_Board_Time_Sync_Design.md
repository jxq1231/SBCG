# 双板时间同步设计与首个增量

<!-- doc-status: current-design; reviewed: 2026-10-23 -->

目标沿用项目已确定的ETH/PTP唯一测量时间轴、固定A主/B从、同源同固件和真实UID映射。第二板现已具备，原“暂不检查第二板”仅是历史实验边界。本轮不加入屏幕、检测或测距。

| 实体 | UID | 网络身份（待新固件应用） | 时钟角色 |
| --- | --- | --- | --- |
| A原板 | 001A002D:30365105:36363631 | 192.168.10.1、02:00:00:00:00:01 | 参考主时钟 |
| B新板 | 00380050:35385104:37313532 | 192.168.10.2、02:00:00:00:00:02 | 待收敛从时钟 |
| PC | 不作为测量时钟 | 192.168.10.100/24 | 开发/观测 |

用户已确认同一交换机接线：A接2号口、B接1号口、PC接3号口。随后要求本次先完成A板程序，B板之后自行烧录；物理接好不等于双板通信已验收。B旧固件仍可能与A同址，本次A单板验收前先断开B网线，待B更新后再接回。角色由三UID字精确决定，不依赖哪个先上电、哪个COM号或DHT11是否响应。

## M2.3.1：身份与网络基线（当前唯一实现）

- 新增纯C BoardIdentityMap（A/B UID）和BoardIdentityProfile（角色、本机/对端UID、IPv4/MAC/掩码/网关），BoardIdentity_Resolve(map,uid,profile)验证非零/不重复的UID表；失败清零非NULL输出。
- 默认映射仅含上表两块真实板。地址固定如上，网关为0；未知UID/重复映射不回退到A。
- BSP_BoardIdentity_ConfigureIp(ip,mask,gw)从BSP_Console_GetInfo取实际UID，解析并缓存，成功复制IPv4参数；失败跳过LwIP初始化。BSP_BoardIdentity_CopyMac(mac)在HAL_ETH_Init前复制6字节；Get返回缓存的只读profile，未配置时NULL。
- LWIP/App/lwip.c只在Includes/IP_ADDRESSES用户区调用上述函数；ethernetif.c只在Includes/MACADDRESS用户区复制MAC。生成主体、.ioc和硬件资源不变。
- App始终可输出BOOT/实际UID与BOARD角色；有效profile才启用网络、温度、音频/PTP服务。未知profile不进入MX_LWIP_Process或业务，避免未初始化网卡和默认地址冲突。BOARD中的地址为应用配置声明，实际IP/MAC需用收包/ARP及双板实机核对。
- 固件SBCG-M2.3-ROLE。A/B仍各自LOCAL、SYNC=0；此步不实现时差交换或校时。原A测温允许名单保持，新B不擅自读取第二只DHT11。
- 新双板监测工具在一个5005套接字收包，分别验证A=.1/UID_A、B=.2/UID_B、源端口5005，分别记录序号缺口/重复/乱序/重启和来源冲突。主机PASS只说明采集窗口，不自动等于硬件验收。

先测真实UID匹配、未知/空指针/重复配置拒绝和未知角色的全服务禁用，再做App调度/原模块回归、两配置构建和布局。原始数据在Logs/TimeSync/M2_3_1。本版烧录另行明确版本授权，实机通过后才作功能完成提交。

## 后续增量与同步闭环边界

1. 板间四时刻全部使用硬件戳：t1=A TX、t2=B RX、t3=B TX、t4=A RX，采用序号/双方UID/启动会话精确关联，TX戳完成后用后续报告传递。复用SyncEstimator计算B相对A的offset与平均delay；不能复用PC软件t1/t4冒充板间同步。
2. 先观察真实双板偏差和漂移，再设计B的受控相位对齐。任何时间步进都必须处理在途交换、PTP观察器及音频锚点/模型代际，禁止沿用步进前结果。A作为参考不接受从板的时钟改写。
3. 再实现有界频率校正及ACQUIRING/LOCKED/HOLDOVER/UNSYNCED状态，明确锁定阈值、连续样本数、失联与失锁行为；不能因一次offset接近0就把SYNC置1。当前coarse模式不足以直接套用fine addend伺服；所有等待都需时间/轮询双预算。
4. 独立核验同步：交换残差可作内部健康观测，精度结论还需后续PPS/外部仪器或其它独立路径。PPS/TIM2引脚/IRQ等若需新增，必须另走CubeMX，不在本增量手写初始化。

同交换机链路仍可能存在非对称排队；硬件时间戳不自动消除该误差。最终指标按课程路线图核对，不在未测之前承诺纳秒或微秒锁定。ST硬件依据见[RM0385](https://www.st.com/resource/en/reference_manual/dm00124865.pdf)，本机驱动行为以已核对的仓库版本为准。

当前进度见[工作计划](Dual_Board_Sync_2026_10_23/task_plan.md)。
