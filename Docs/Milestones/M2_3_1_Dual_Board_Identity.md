# M2.3.1 UID角色与网络身份：A板已验收，B板待用户更新

<!-- doc-status: a-verified-b-pending; reviewed: 2026-10-23 -->

当前唯一目标：同一份固件在两块已登记的STM32F746G-DISCO上自动选择唯一角色、IPv4和MAC，为后续板间同步消除地址冲突。**实现、软件检查及A板授权烧录/联合实物验收已通过；B板尚未更新/验收，双板网络共存及校时未完成。** 用户本轮明确要求“验收A板”，并确认B网线已断开。B板之后自行烧录。

## 身份、接线与固件

| 实体 | 完整UID | 新固件身份 | 用户确认交换机口 |
| --- | --- | --- | --- |
| A | 001A002D:30365105:36363631 | 192.168.10.1 / 02:00:00:00:00:01 | 2 |
| B | 00380050:35385104:37313532 | 192.168.10.2 / 02:00:00:00:00:02 | 1 |
| 电脑 | 无测量时钟角色 | 192.168.10.100/24 | 3 |

角色只由三个UID字精确匹配决定，与交换机口、COM号、启动顺序无关。A的DHT11仍为3.3V/PG7-D4；B不另读DHT11，温度共享未实现。

版本`SBCG-M2.3-ROLE`；同一映像可分别用于A/B，不需要另编译B专版。冻结文件均在本机被Git忽略的生成输出内：

- `build/Firmware/SBCG-M2.3-ROLE/SBCG-M2.3-ROLE-Debug.elf`
- `build/Firmware/SBCG-M2.3-ROLE/SBCG-M2.3-ROLE-Release.elf`
- 同目录`manifest.json`记录身份、哈希和NOT_RUN状态。正常构建输出仍为`build/Debug/SBCG.elf`、`build/Release/SBCG.elf`。

| 配置 | ELF SHA256 | FLASH占用 | 当前状态 |
| --- | --- | --- | --- |
| Debug | 2457a4666a7bb4aa20c0c29097ca5ce92c2f8da93d87c36794b18f466e10590a | 113084字节 | A板烧录/校验及123秒联合验收通过；B未烧录 |
| Release | e88eca5268bc35da988db07d0c4868dfd7a63fcc8408448ec6ebb5a49a248cff | 65764字节 | 构建/布局通过，未烧录 |

源码基线`3240aba`，软件冻结时为未提交工作区；本轮按用户收窄后的A板范围完成实测，再独立提交身份增量。B待验收和双板未完成边界随提交保留，不将本次提交标记为整套M2.3.1/同步完成。冻结文件不能替代Git源码记录；本地清理生成输出可能删除它们，队友应使用验证后的源码提交重新构建或复制并核对哈希。

## 修改文件与职责

| 文件 | 作用 |
| --- | --- |
| `App/BSP/board_identity.c`、`board_identity.h` | 纯C完整UID解析；本机/对端角色、IP、MAC、掩码、网关；拒绝未知/零/重复映射，失败输出清零 |
| `App/BSP/bsp_board_identity.c`、`bsp_board_identity.h` | 从现有BSP信息取得UID并缓存，提供IP/MAC及只读profile |
| `App/app_main.c` | FW/BOARD输出；核对缓存UID，未知身份禁止初始化和轮询业务，原A调度顺序保留 |
| `LWIP/App/lwip.c` | 仅USER CODE 0及IP_ADDRESSES区，LwIP初始化前准备地址，未知身份提前返回 |
| `LWIP/Target/ethernetif.c` | 仅USER CODE 0及MACADDRESS区，HAL_ETH_Init前复制角色MAC |
| `CMakeLists.txt` | 用户层登记两源文件及SBCG_USE_BOARD_IDENTITY，不改生成CMake |
| `Tests/BoardIdentity/run_tests.py`、`test_board_identity.c`、`test_bsp_board_identity.c`、`stubs/console_info_stub.c`、`stubs/console_info_stub.h` | 真实身份、NULL/重复/未知/输出清除和缓存生命周期的原生测试 |
| `Tests/App/run_tests.py`、`test_app_main.c`、`test_app_identity.c` | 保留旧无身份宏分支，新增A/B、未知UID、启动门控、UART失败、重入/卸载失败的应用测试 |
| `Tools/Monitor/dual_board_monitor.py`、`Tools/Monitor/tests/test_dual_board_monitor.py` | 单PC5005套接字分别核对A/B来源、UID和序号；25项主机测试 |
| `Docs/Plans/2026-10-23_Dual_Board_Time_Sync_Design.md`、`Docs/Plans/Dual_Board_Sync_2026_10_23/{task_plan,findings,progress}.md` | 当前设计、决策和进度；更新实际拓扑与先A后B范围 |
| `README.md`、`Docs/README.md`、`Project_State.md`、`Project_Roadmap.md`、`Hardware_Resources.md`、`Protocol.md`、`Document_Status.md`、`System_Architecture.md`、`Defense_Guide.md`及本报告 | 同步现行状态，区分软件就绪/实物基线；旧验收记录不改写；除根README外均位于Docs/ |

## 真实调用链与资源边界

```text
main → MX_LWIP_Init
       → USER IP_ADDRESSES → BSP_BoardIdentity_ConfigureIp
                           → BSP_Console_GetInfo → BoardIdentity_Resolve
       → lwip_init → netif_add → low_level_init
                                → USER MACADDRESS → BSP_BoardIdentity_CopyMac
                                → HAL_ETH_Init
main → App_Init → BOOT → BSP_BoardIdentity_Get → 核对UID → BOARD
               → 已配置：原网络/PTP、温度、音频等初始化
               → 未配置：返回
main → App_Process → 身份门控 → 原前台调度
```

No CubeMX change required。未修改.ioc、Core初始化、引脚、RCC、GPIO、SAI、I2C、LTDC或HAL。网络身份是用户应用配置，生成区主体去除USER CODE内容后与基线相同；未实际再次操作CubeMX，不能写成完成了本版再生成实验。

无新增IRQ或DMA关系；沿用既有ETH内部DMA、音频DMA2 Stream7 Channel0及UART/SAI IRQ，ISR职责不变。未更改Cache开关、缓冲区地址/对齐/所有权，也未新增Cache维护。角色表只在启动/前台串行访问，未支持在线热切换身份。

## 软件验证

- 身份/BSP原生7组场景通过；App旧分支及身份宏分支两个程序通过，包含真正的A/B UID、未知/不一致、正常重入、卸载失败保持旧服务。
- 全部11个原生模块测试入口通过：App、Audio、BoardIdentity、Console、Network、Ptp、Sync、Temperature、TimeExchange、Udp、UdpProbe。
- Tools108项、Analysis87项通过；其中新增双板监测25项。PASS要求完整窗口、两板均足量且无缺包/重复/乱序/窗口前包、无非法或外来包；身份冲突FAIL，中断/不足/异常序列REVIEW，不静默重置基线。UDP v1没有启动会话，疑似重启与旧包无法可靠区分时保守失败。
- 两种配置沿现有`python -B Tools/Monitor/autodebug.py build --preset Debug/Release`工作流构建通过，无新编译警告。每个配置分别执行`check_eth_layout.py`与`check_audio_layout.py`，均通过。
- 独立代码复核通过，无未解决P1/P2。已补齐中文文件/函数说明及身份开启时重入覆盖。
- 证据位于`Logs/TimeSync/M2_3_1/`：`regressions.json`、`builds.json`、`app-reviewed-green.txt`、监测严格判定红/绿灯记录及`review/`；属于主机检查，不是实物数据。

## A板实物验收步骤（本轮已执行）

1. A保留交换机2号口和ST-LINK USB；电脑3号口/192.168.10.100不变。B尚运行旧固件时先断开其网线，避免旧默认IP/MAC重复。本次不烧录B、不要求其参加验收。
2. 依据根AGENTS第20节取得本版本A板program/verify/reset及采集授权；若两探针同时连接，先按探针序列号与实际UID确认A，不能依赖默认选中的探针。沿现有OpenOCD配置，不改选项字节。
3. 核对Debug ELF完整哈希并烧录A，记录BOOT的FW/真实UID；BOARD应为ROLE=A、IP=.1、MAC尾01、PEER_IP=.2、SYNC=0。BOARD只是配置声明，不当作网口MAC读回。
4. 确认LINK=UP后，电脑ping 192.168.10.1，并用`Get-NetNeighbor`或ARP核对实际邻居MAC为02:00:00:00:00:01；记录输出，不能只凭ping证明UID。
5. 运行下方60秒单板监测；结合原始串口/UDP核对至少50包、源IP/UID正确、零缺口/重复/乱序/非法，完整采集窗口。原工具的PASS较宽，验收仍逐项看计数；不把STACK_OK当电脑收到。

```powershell
python -B Tools/Monitor/udp_monitor.py --source 192.168.10.1 --uid 001A002D:30365105:36363631 --duration 60 --min-packets 50 --log Logs/TimeSync/M2_3_1/a-only-01.jsonl
```

6. 使用已有原业务验收流程复核温度、音频/PTP及5006/5007服务（同一接收端口不能重复占用），保留对应版本/采集窗口证据。预期仍LOCAL/SYNC=0，原音频连续、温度正常，无错误增长。具体结果见下方本轮实测；不将历史受控发声/拔插结果算作本版重复执行。

## B板后续由用户烧录与双板共存

B之后使用同一份Debug映像，核对其UID以及BOARD的ROLE=B、IP=.2、MAC尾02。B旧固件隔离到更新完成后再接回交换机1口；A无需因B加入而改编译参数。B不接第二只DHT11，当前没有温度UDP共享。

更新后另开独立双板验收窗口：两个IP/UID及ARP MAC均正确，再运行下方工具。日志名每次取新值，不覆盖旧证据。

```powershell
python -B Tools/Monitor/dual_board_monitor.py --duration 60 --min-packets 50 --log Logs/TimeSync/M2_3_1/dual-board-01.jsonl
```

两板各至少50包、无地址/UID冲突及缺包/重复/乱序后，才可记录双板网络共存通过。仅A通过时不能把完整M2.3.1或双板同步标记完成。下一独立目标是四个时刻全部来自板端ETH硬件的板间交换/偏差观测，其后才设计B校时/校频与锁定/失锁恢复；当前SYNC=0保持。

## A板本轮实物结果（2026-10-23）

沿原OpenOCD配置，仅A板烧录一次。写入前在同一连接读取0x1FF0F420/424/428并比对完整UID，匹配后才program/verify/reset；未写选项字节、未改探针配置。B由用户确认已断网，本轮未连接/烧录B。

| 检查 | 本轮结果 |
| --- | --- |
| FW与UID | SBCG-M2.3-ROLE；200MHz；001A002D:30365105:36363631；唯一新BOOT |
| 角色/网口 | BOARD为A、.1、MAC尾01、对端.2、SYNC=0；ping4次成功，电脑邻居记录为02-00-00-00-00-01且Reachable |
| 采集窗口 | 复位后123.144秒；COM3 115200/8N1；串口、子进程与三路UDP均已关闭，A保持运行 |
| UDP5005 | 110秒110包，SEQ14～123；缺口/重复/乱序/窗口前包/非法/外来均0 |
| 启动链路 | SEQ1～3因LINK_DOWN跳过，属于启动阶段；稳定窗口无额外缺包，不将它们写成网络丢包0覆盖的范围 |
| 旧请求/硬件戳 | 30组ACK/RX/TX全部对应，所有错误计数0 |
| 时间交换/板端观测 | 30组ACK/REPORT/四时刻及30次板端估算与真实C完全一致；PC t1/t4仍为软件时间，不代表双板HW同步 |
| 音频 | 123对连续位置、123个有效锚点、拒绝0；41组MAP/CHECK与真实C/Fraction完全一致；最大绝对残差17393ns，不作为声学/同步精度 |
| 温度 | 24个有效AUTO版本，33.0～33.1℃、23～25%RH、声速351298～351359mm/s；一次PRIME，全部错误0；未做拔插或传感器精度校准 |
| Codec/PTP/心跳 | 61次ID8994，错误0；123条PTP及心跳连续；PTP仍LOCAL/SYNC=0 |
| 实测前台周期 | 心跳最大1015ms，codec最大2050ms，UDP tick986～1014ms；保留实测抖动，不宣称精密周期 |

原始证据在`Logs/TimeSync/M2_3_1/a-runtime-01/`：flash log、serial.bin/jsonl、monitor/probe/time JSONL、ping.bin、neighbor.json、runtime-run.json。`raw-freeze.json`的12份文件哈希在分析后均相同；121份冻结源码/配置/工具未变。

串口raw SHA256为`4547be40e7f7b4ad5609d1ade4e4b1222b0142facea12bfde03d0d15a2465441`；2121行，排除新BOOT前14行旧固件记录后2107行，raw/JSON投影一致，无非ASCII、残片或新BOOT后再次启动。原始数据未重写。

离线联合报告`acceptance.json`为`PASS_M231_A_IDENTITY_AND_ORIGINAL_SERVICES`，hardware_verified=true仅限本次A窗口；各主机工具自身的hardware_verified=false保持。复用原板端MAP分析器时只将其期望FW设为本版，不改raw或原检查条件。Windows PowerShell邻居输出按其CP936编码读取，数值State=5对应Reachable；原始JSON字节保留。

## 当前交付结论

Build/软件回归：PASS；Flash：A PASS、B NOT_RUN；Runtime：A PASS，双板共存待B更新；Git：按A板已验证范围提交，不宣称B实物或双板同步通过。停在本增量，不自动开始校时实现。
