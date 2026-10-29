# M2.3.3 PTP整秒输出：软件交付与A单机验收

<!-- doc-status: software-implemented-awaiting-hardware; reviewed: 2026-10-23 -->

> 后续证据：原冻结SBCG-M2.3-PPS文件未单独烧录。用户后续授权测试的SBCG-M4.1-CAND-R1已包含同一PPS实现，90秒末EMITTED89/MISSED0/ERRORS0；无示波器，仍不能确认电气波形或同步精度。详见[M4.1.2报告](M4_1_2_Sound_Candidate_Integration.md)。下文未烧录边界按原交付时理解。

本增量只实现PTP驱动的PPS，不增加检测、测距、LCD或TouchGFX。用户已手动CubeMX生成且补齐PA15下拉；当前仅A板（UID001A002D:30365105:36363631），B暂不参与。新固件为SBCG-M2.3-PPS，软件验证通过，**尚未烧录、没有本版波形实测，不宣称±10us或≤30秒收敛通过**。上一已验收版本仍按[M2.3.2记录](M2_3_2_Dual_Board_Clock_Software.md)理解。

## 接口与真实触发路径

- PPS_OUT：**PA15 / AF1 / TIM2_CH1 / 绿板Arduino D9 / CN7第2针**。示波器尖端接D9，地夹接本板GND（例如CN6第6/7针，按丝印核对）。蓝色扩展板D9组三芯口应取信号9端，不能把供电端当信号；DHT11仍用D4/PG7和3.3V，不改线。
- TIM2输入100MHz，PSC99计数1MHz，PWM1/CCR100生成标称100us高脉冲。ARR为0xFFFFFFFF，Reset/ITR1 remap到ETH **目标时间触发**。
- 主循环提前≥100ms装载下一整秒PTPTTHR/PTPTTLR并开启一次TSITE；整秒比较事件由硬件经ITR1复位CNT产生边沿。并非主循环翻转GPIO，也不是PTPPPSCR独立PPS直接进入TIM2。
- 启动时先关CC1E、UG装载、CNT100，有界预运行到101后启通道，避免软件首脉冲。停止先关CC1E再关CEN，取消TSITE并读PTPTSSR清状态；停用低电平依赖已生成Pull-down。
- 前台以TIM2.TIF、ETH.MACSR.TSTS、TSITE自清、目标秒及epoch共同判断已触发；不使用与RM位号冲突的CMSIS TSTTR宏。EMITTED只是寄存器证据统计，必须另测电气波形。

## 资格及校时边界

A在本地PTP健康时独立输出参考脉冲，无需B在线。B首次内部锁定且|offset_x2_ns|≤20000、delay_x2_ns在0..4000000、|drift_ppb|≤500后取得资格。接受样本完成年龄<2000ms；一次≤1125ppb纯变频可进入<4000ms重新资格，仍须有年龄<2000ms的好样本，第二次变频不能延长期限。1125ppb是现有锁态控制律500+10000/16的上界，不是外部精度保证。

Observe后、Commit前先交接本次瞬时锁证据，避免变频Commit清锁导致首次资格丢失。拒绝、B对端重置、相位覆盖、未知时钟代际及故障撤资格；A对端重置不改变其本地参考时钟。时钟BSP在初始化、相位TSSTI覆盖或失效前也同步停PPS；停止失败不发出相位命令。正常纯频率命令不无条件停输出。未知本地代际后A需要显式重新初始化；锁存FAULT不隐式恢复。

剩余资格须覆盖目标距离+100ms，预算不足取消，不补发过去整秒。DHT起始低态只禁止新排程，安全检查和已到达目标消费每轮仍执行，日志沿用原安全发送队列。运行后TIM时钟门意外丢失不能报告安全停止，不擅自重新配置时钟。

## 文件、调用链与资源关系

| 文件 | 变化 |
| --- | --- |
| App/BSP/bsp_pps.c、bsp_pps.h | 校验生成资源、单次目标、安全启停及诊断快照 |
| App/BSP/bsp_ptp.c | PPS条件编译保护：初始化/相位/失效前停用 |
| App/Sync/pps_service.c、pps_service.h | A/B资格、时效、有界变频重新资格和每秒日志 |
| App/Sync/time_sync.c、time_sync.h | 生命周期、瞬时锁证据及动作前后交接；更新卸载说明 |
| App/app_main.c | SBCG-M2.3-PPS标识及逐轮安全处理 |
| CMakeLists.txt | 顶层用户扩展登记新源文件及SBCG_USE_PPS宏 |
| Tests/PpsBsp/、PpsService/、PpsIntegration/ | 真实实现的寄存器模型、资格边界和协调器交接测试 |
| Tests/App/run_tests.py、test_app_sync.c | PPS构建组合及DHT低态仍处理检查 |
| Tests/Ptp/run_tests.py、test_bsp_ptp_pps.c | 初始化/相位停止失败阻止写入、变频及失效保护 |
| Docs/Plans/2026-10-23_PTP_PPS_PA15.md、Document_Status.md、Project_State.md、Protocol.md、Hardware_Resources.md、Project_Roadmap.md、README.md及根README.md | 当前状态、协议、单机后续与历史标注 |

用户生成文件另有SBCG.ioc、.mxproject、Core/Inc/main.h、stm32f7xx_hal_conf.h、Core/Src/main.c、stm32f7xx_hal_msp.c、cmake/stm32cubemx/CMakeLists.txt，以及原版HAL TIM的tim.h/tim_ex.h/ll_tim.h/tim.c/tim_ex.c；这些是用户CubeMX产物，未由应用补丁手改。原LAN8742许可已恢复为HEAD原字节。

调用：`main → MX_TIM2_Init → App_Init → TimeSync_Init → PpsService_Init → BSP_PPS_Init`；运行时`App_Process → TimeSync_Process（观测/动作交接）→ PpsService_Process → BSP_PPS_Process → ETH目标比较 → TIM2 ITR1 → CH1/PA15`。

**IRQ：无新增。DMA：无新增。** 既有SAI2/DMA2 Stream7 Channel0环形采集和ETH内部DMA保留，PPS不占用DMA缓冲。I/D Cache仍未启用，不新增缓存维护。硬件资源由本次用户CubeMX生成，应用只校验/使用，不重写模式、引脚、分频或NVIC。

## 软件验证与证据边界

- 18个Tests原生运行器全部退出0，含PPS3组及原模块；伺服原9412断言、协调器原12组继续通过。工具113项、Analysis98项通过。
- 两配置按现有工作流构建：`python -B Tools/Monitor/autodebug.py build --preset Debug`及`--preset Release`。均无新增编译警告，Debug Flash139572B、Release80840B。
- 两配置ETH和音频布局共4次通过；生成快照7个文件SHA逐个保持，应用补丁未改生成配置。**新增应用接入后未再实际Generate Code**，目前证明的是用户扩展结构与生成文件保持，不冒称新一轮再生成实测。
- 本地证据在Logs/PPS/M2_3_3：regression-summary.json、各组-regression.log、layout-final.json、生成快照及保持检查。构建原报告在Logs/AutoDebug/20261023T120112-0acd7bd2和20261023T120112-05f8ff1b。
- 审查：独立复核发现读取跨整秒触发的竞态，已先新增读取点注入测试观察失败，再改为最多32次/2ms双重有界一致性复核。TIF/TSTS/TSITE/CNT四读取点注入全部通过，原错误/冻结tick边界仍通过；独立复核确认修复，无新增重要问题。修复后PPS3组及App/PTP重新通过；详见post-review-regression.json和bsp-trigger-race-RED.log。本版Flash/Runtime/SCOPE均NOT_RUN，尚无“功能完成”提交；按AGENTS第19/23节，硬件证据齐后再提交独立增量，保留当前用户生成改动。

本方案依赖前台逐秒续装目标；若CPU完全停止约71.6分钟，TIM2自然回绕仍可能产生额外PWM，不承诺无限停顿容错。软件计数及1Hz读数均不能单独证明PTP整秒相位或两板精度。

## 冻结固件（未烧录）

| 配置 | 文件 | SHA256 |
| --- | --- | --- |
| Debug | `build/Firmware/SBCG-M2.3-PPS/SBCG-M2.3-PPS-Debug.elf` | `b595ce7768731bd28da83f2752aa91339ce679ca87e5814e17eb4712cead37a1` |
| Release | `build/Firmware/SBCG-M2.3-PPS/SBCG-M2.3-PPS-Release.elf` | `6965b96c3cad6fc97118460237867c5324f7b3dff457f146eff6ca2b143c7692` |

`build/Firmware/SBCG-M2.3-PPS/manifest.json`绑定104个源输入文件SHA，基线提交7d91dd9，状态UNCOMMITTED_AWAITING_HARDWARE_ACCEPTANCE。构建和原始日志按仓库规则不提交。用户已答复“先不烧录验收，继续推动下一步”；本版PPS保持冻结、未烧录，当前转M4.1.1纯算法。此前版本的批准不自动复用。

## A单机验收步骤（用户已暂缓）

1. 冻结本版Debug ELF及SHA后，取得本版明确烧录授权，或由用户按现有任务手动烧录同一文件。沿用原ST-LINK配置校验/复位，不改选项字节。
2. 采集约60秒串口及原三路UDP。核对BOOT=SBCG-M2.3-PPS、UID为A、ROLE=A，PPS为QUALIFIED，EMITTED推进、ERRORS=0，并复验温度/音频/旧网络。此步只能证明寄存器路径及原业务运行。
3. 示波器10×高阻，探头D9/地GND，观察0～3.3V、约1Hz及100us高脉宽；从复位开始捕获首脉冲，检查无软件启动窄脉冲。至少连续观察10次，记录实测周期、脉宽及截图。没有外部PTP参考时，单板周期/宽度不能独立证明绝对PTP整秒相位。
4. 在A无B条件下保持参考PPS；不要用PC软件时间戳与D9的差值替代两板精度。PPS异常时保存原始日志/波形并定位，不跳到下一功能。

## 下一步单机实现规划（未实施）

原建议在PPS的A单机范围验收后进入；用户现已明确暂缓PPS并先推进其中纯算法步骤：**M4.1 单板声音事件检测**，目标是从已有PCM定位候选声音的帧序号并可靠去重。先核对课程标准音频及播放条件，不预设一种频率或把普通说话作为标准音频验收。

1. 先设计纯C分块检测内核、状态/复位规则，用静默、已知位置、跨DMA半块、重复脉冲、噪声、削顶及断流进行可复现测试。首次只证明候选帧定位，不许宣称±100us或测距通过。
2. 再接入现有AudioCapture唯一的Begin/End所有权：在前台、End验证前计算候选，End成功后才发布。不可由Detector另行领取同一DMA缓冲，也不可保存DMA指针；会话/帧缺口清除历史。当前半缓冲8192帧，处理时延必须计入预算，不能在IRQ打印或检测。
3. 单独补事件帧→本地PTP时间查询服务。现有AudioTimeMapMonitor只是三锚点留出自检，尚不是PCM事件查询；不能把DMA回调或主循环tick直接当声音到达时刻。相位/速率代际变化使旧事件模型失效，仍标LOCAL/SYNC=0。
4. 最后安排安静60秒无误报、标准音频重复≥10次的事件/去重及原网络联合实机验收，记录帧号、会话、质量和时间有效性。音频定位重复性与物理声学延迟另校准，不由现有14.884us模型残差推导精度通过。

以上分成后续独立增量，未自动实施；暂不新增LTDC/TouchGFX/FMC配置。单板可完成检测与本地事件时间验证；两板TDOA、方向、距离及两屏一致结果要等B板回到网络后再验。


2026-10-24阅读补充：本报告末尾单机M4规划已由M4.1.1～M4.1.3分增量执行，原失败和未测范围各自保留。当前进入U0显示软件准备，后续FMC/MPU需另由CubeMX生成，见[现行方案](../Plans/2026-10-24_LTDC_RGB565_Bringup.md)；这不表示PPS波形已通过。
