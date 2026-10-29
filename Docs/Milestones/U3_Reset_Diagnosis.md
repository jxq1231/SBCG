# U3：额外启动排查与复位标志记录

<!-- doc-status: diagnostic-increment-verified-root-open; reviewed: 2026-10-25 -->

**历史额外启动仍未定位，不能称已修复。** 本轮原固件复现及新诊断版均未出现额外BOOT；完成了经实机验证的启动原因记录。当前A为`SBCG-U3.1-RESET`。旧quiet-01两次BOOT的失败记录不变。

## 判断边界

原quiet-01两次U3 BOOT间隔6.419288秒，用户确认未按RESET或插拔USB。当时没有逐启动原因记录；后读RCC.CSR=0E000000仅说明累计BOR/PIN/POR标志，不能倒推历史单次原因。应用未找到显式系统复位调用，看门狗HAL模块未启用，异常处理为死循环；这些检查不能排除所有软件/硬件故障。

## 受控实机证据

原始bin/JSON、主机monotonic/UTC时间线和Tcl操作保存在`Logs/ResetDiagnosis/2026-10-25`。全部限A UID `001A002D:30365105:36363631`、原ST-LINK探针；未接B/网线/示波器，不发声，DHT未连接。

| 实验 | 实际结果 |
| --- | --- |
| 原冻结固件flash-01 | Verified OK8.873443秒，BOOT9.030818秒，shutdown9.044177秒；70.034秒总采集只有一次BOOT，未复现旧6秒后启动。 |
| 诊断版diag-flash-01 | 烧录Verified OK；100.142秒总采集、BOOT后90.766341秒只有一次BOOT/RESET，首次CSR0E000000、CLEAR_OK1。这仍是旧累计标志，不作唯一根因。 |
| 不复位附加observe | 12.041秒零BOOT/RESET，CSR前后0；仅本次命令覆盖connect_deassert_srst，未改配置文件。 |
| 明确外部reset run | 12.045秒一组BOOT/RESET，CSR/FLAGS04000000，仅PIN1；CLEAR_OK1，CSR操作前后0。 |
| 明确AIRCR SYSRESETREQ | 12.046秒一组BOOT/RESET，CSR/FLAGS14000000，PIN1/SFT1；CLEAR_OK1，CSR操作前后0。该软件复位是助手有记录的对照。 |

两个带时标的烧录窗口独立，不拼成长时稳定性试验。新诊断版89条UI、88条有效音频/位置/心跳、44次codec成功、29组MAP/CHECK、2自然候选Fraction匹配，UI/PCM/串口零错误；无联网、主动声音或标准声音识别结论。本轮未另做用户目视，不能把旧显示版的目视当作新实验。

## 改动与调用关系

- `App/BSP/bsp_reset.c/.h`：单次复制完整CSR及七类标志，再用HAL既有RMVF宏清状态并读回；Get只复制，不重复清除。
- `Core/Src/main.c`：仅USER Includes/1加入受宏控制的早期捕获。
- `App/app_main.c`：FW标识及BOOT后有界RESET日志，沿现有前台队列发送。
- `CMakeLists.txt`：用户根文件登记BSP源和SBCG_USE_RESET_DIAGNOSTIC。
- `Tests/ResetBsp/run_tests.py`、`test_reset.c`、`stubs/main.h`、`run_app.py`、`test_app.c`：单次捕获、全部位/混合位、低位保留、清除失败、空输出、日志及早期hook。
- `Tests/TouchGFXService/test_app.c`：仅测试入口复用保护。
- `Analysis/touchgfx_status_analysis.py`及其测试：新固件须显式选择，旧默认不变，BOOT完整性检查不放宽。

调用链：`Reset_Handler → SystemInit/运行库初始化 → main USER CODE1 → BSP_Reset_Capture → CubeMX初始化 → App_Init → BOOT/ReportReset → 原前台日志队列`。

无新增IRQ/DMA。只使用内部静态存储，不读写音频、ETH或SDRAM缓冲，不改变Cache。RMVF是清复位状态，HAL读改写保留LSI低位控制；不是改PLL/外设配置。未改.ioc；与本轮前快照对比，Core生成区完全一致。

## 软件验证与冻结固件

BSP12场景先观察缺实现失败再通过；App2场景/早期hook、旧TouchGFX/U2 App及TouchGFX服务13场景通过；分析152项、Debug/Release既有CMake构建及两种布局检查通过。仅原RWX LOAD链接警告，无新增源码警告。

冻结目录`build/Firmware/SBCG-U3.1-RESET`含896项输入与源码快照，保留原PPS/CubeMX工作区上下文；功能提交单独不是完整硬件快照。

- Debug SHA256：`eed5faf23c7ffd5e50e37e8b981c776a0e811039bac4095c0c0e21feb1a2f0a9`。
- Release SHA256：`e5eebfd786f32ed60f52f2afcf75f8be71e551f3d608edda997a8b3329db2ee1`。
- 分析：`python -B Analysis/touchgfx_status_analysis.py --prefix Logs/ResetDiagnosis/2026-10-25/diag-flash-01 --firmware SBCG-U3.1-RESET`。

## 后续如何观察

BOOT后应有一条RESET，保留完整前后串口及主机操作时间。CAPTURED1/CLEAR_OK1表示成功保存并建立下一次观察边界；任一为0时不直接判原因。PIN1不等于有人按键，软件对照也有PIN1；BOR/POR组合不提供具体电压波形。

若无对应主机动作而再次BOOT，依据新位图决定调查软件/看门狗、外部复位线或电源；标志只缩小来源，不自动定位函数或电路。发生在串口初始化前且未能输出的极早期故障仍不受当前串口证据覆盖。当前没有新增复现，历史问题保持未关闭。
