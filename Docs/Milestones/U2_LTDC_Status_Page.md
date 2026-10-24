# U2：LTDC RGB565真实状态页

<!-- doc-status: completed-evidence; reviewed: 2026-10-24 -->

当前仅A板；无B板/网线/示波器，不播放声音。目标是直接显示真实状态，不做独立色条。8根未用颜色低位由用户明确暂缓；不手改.ioc或生成硬件初始化。

## 已完成的软件范围

- 显示模式SDRAM READY与破坏性U1诊断互斥；READY不是本次8MB自检PASS。
- 首帧完整绘制后才调用CubeMX全局MX_LTDC_Init并打开显示/背光。
- RGB565两块外部缓冲C0000000/C0040000，480×272、每帧261120字节；CPU最多八行/轮、约1Hz，单份快照，超2000ms丢弃后台帧。
- VBR轮询换页，100ms超时停止新发布；扫描/待换入缓冲不能改写。HAL初始化的LTDC中断源关闭，无新NVIC/DMA；I/D Cache仍关闭，无随机缓存维护。
- 网络/同步只读缓存，A参考偏差--，内部锁不等于外部精度；温度AUTO/MANUAL/无效值；音频RUN及丢块/错误；距离m、方向/质量尚无结果，显示占位。
- CMake只登记用户源，预检查延后调用、参数、20根AF/速度、两根控制输出及初始LOW。原音频唯一PCM消费者和PPS调度保持。

## 初版实测失败记录（保留）

初版SBCG-U2-STATUS：Debug SHA256 45217dc9f413513f495083455a0f0f76e1bbb0d78dfeb06ba7473a3bf3abf69b；Release a6681b4599b2cffb83c8b811b57da64627bd575184113b0b61abcb0d9392407b。冻结文件和138项输入位于build/Firmware/SBCG-U2-STATUS，包含受保护的原PPS及CubeMX工作区，不能把源码提交HEAD单独视为相同硬件快照。

UID确认A，ST-LINK烧录Verified OK。Logs/UI/U2/runtime/quiet-01记录95秒原始串口；捕获启动边界后SDRAM READY，但UI进入ERROR、FRAMES=0，未点亮。该记录判FAIL_OR_INCOMPLETE，不覆盖或美化；末候选状态仍有1项待处理，因此原管线完整排空检查也未通过，不能把局部零丢块等同整窗PASS。

只读诊断failure-read-memory.txt显示LCD config_errors=1，FU/TE/timeout/HAL计数均0；RCC CR=3F078A83、PLLCFGR=22406419、PLLSAICFGR=54003000、DCKCFGR1=00410000，可推导25MHz/25×192/5/4=9.6MHz。根因是本地HAL_RCCEx_GetPeriphCLKFreq没有LTDC分支而返回0，BSP误用该查询；主机桩曾返回9.6MHz而掩盖这个接口限制。修复仅用户BSP读取分频推导，不改时钟、引脚、HAL或CubeMX生成区。

## R1修复与R2实物验收

R1改为RCC现值只读推导，32项LCD主机测试及独立复核通过，实物开始RUNNING且错误0；Debug整页约1.7秒，记录在Logs/UI/U2/R1。随后R2仅优化行带外文字早退；GCC函数插桩证明行带外不再调用矩形绘制，原整帧/分带像素逐字节一致。没有改硬件配置或改为色条。

当前A板运行SBCG-U2-STATUS-R2：

| 项目 | 结果 |
| --- | --- |
| Debug SHA256 | 14fdabe9bec9695d3bd14a7e6954cb8e23a83b9ca5a29fff5ed7202afa4cfb67 |
| Release SHA256 | 9a67524fd6c94fe09690c377204519c60173a6e4c0afb680ce04cc100d1111fb |
| 冻结输入 | build/Firmware/SBCG-U2-STATUS-R2，138项源输入及原PPS/生成配置工作区快照 |
| 烧录 | 同一ST-LINK、UID确认A，Verified OK，不改选项字节 |
| 静默采集 | Logs/UI/U2/R2/quiet-01，97.132秒；唯一BOOT前387行保留并明确排除，BOOT后名义79.340秒，无尾部截断 |
| UI | 78条诊断，末77帧，ERRORS=0、DISCARDS=0；页面快照启动间隔1001～1026ms，平均1022.987ms |
| SDRAM | 显示READY，CHECKED_BYTES=0，不运行破坏性测试，不宣称本次全范围自检PASS |
| 音频 | 77条RUN/VALID=1，末461有效半块/3776512双声道帧；DROP/ERR/HAL/DMA=0；77条位置连续，GAP/FAULTS=0 |
| 原业务 | 心跳77条连续、TX_ERRORS=0；codec38次；模型自检25组；PPS末EMITTED78/MISSED0/ERRORS0仅软件观测 |
| 候选传输模型 | 46条候选均有结果，45条OK与Fraction一致，1条明确拒绝；没有主动发声，不宣称标准声音识别 |
| 温度页面联动 | temperature-01：25°C→346450mm/s、30°C→349480mm/s；两次SET及AUTO回复OK，33条UI持续无错误，最后AUTO/无有效传感器值；串口RX错误/溢出/重装错误0 |
| 实屏 | 用户明确回复“状态页显示正常”，确认文字可读且无明显闪烁/撕裂；8根颜色低位仍按用户要求暂缓 |

静默原始串口SHA256：24d61c554261f7127dc5d789e7c2a5aa4633301449b5b0d2ea4fe6595d37b954。温度原始串口SHA256：9f0ba53cae8cbd879f2906758d4aec28537848aa74465e73c345b148909a609b。原始数据、命令时点、完整投影和分析保存在Logs，不提交大段日志。

## 分析边界与已修正的主机检查

旧通用分析器用最后周期计数与更晚的全窗事件数比较，在本轮得到原样保留的FAIL：末周期Q45/OK44/REJECT1/PENDING0，之后又完成候选46，因此不能把旧快照45强行等于未来46。U2工具只在旧检查唯一失败为这个末快照等式时，以每条周期状态对其自身UART前缀逐项核对来替代；77条状态全部符合，各候选/结果无重复且窗口末46=46。其余旧检查仍须全通过；旧FAIL嵌入结果不改写。该方法及“计数超过期限才前进”等反例经过独立复核，不丢弃事件或隐藏硬件错误。

自动分析保持physical_panel_verified=false，因为它不能证明面板；本轮实屏结论单独来自用户目视。没有网线/B板/示波器或主动声音：本次不复验UDP收发、双板同步±10us、标准音频到达、测距、方向计算或温度传感器实测。AUTO恢复后无有效值是传感器当前未连接的正确状态。

## 调用链及资源关系

main → CubeMX原初始化（含FMC，延后LTDC）→ App_Init → BSP_SDRAM_InitDisplay → 原业务初始化 → UI_StatusService_Init。App_Process在音频归还后调用UI_StatusService_Process，单份快照→八行绘制→完整首帧→BSP_LCD_Present→生成MX_LTDC_Init→显示/背光；后续只改影子地址并请求VBR，轮询确认。

无新IRQ/DMA；音频仍DMA2 Stream7 Channel0、原半满/全满回调交接。LTDC自行读外部SDRAM，I/D Cache继续关闭，Region1非缓存XN、Region2共享Device未改。PPS及许可删除等原改动继续保护，不混入显示提交。

## 复验入口

```powershell
cmake --preset Debug
cmake --build --preset Debug --parallel
cmake --preset Release
cmake --build --preset Release --parallel
python -B Tests/LcdBsp/run_tests.py
python -B Tests/Sdram/run_tests.py
python -B Tests/UI/run_tests.py
python -B Tests/UI/run_snapshots.py
python -B Tests/UI/run_service.py
python -B Tests/UI/run_app.py
python -B Tests/UI/run_band_work.py
python -B Tests/UI/run_preflight.py
python -B Analysis/ui_status_analysis.py --prefix Logs/UI/U2/R2/quiet-01
```

另已通过16项主机运行器（UI/App/Network/SyncController/Audio/检测/温度/Console/BoardSync/PPS等）、Tools120项及Analysis140项、两种构建/布局及独立复核。专项LCD32、SDRAM原29场景、UI服务7场景、预检查11种危险突变均通过。无新增编译警告。

默认显示构建不执行整片破坏性测试。仅需重新做U1专用诊断时，可在原预设配置SBCG_SDRAM_DIAGNOSTIC=ON；它关闭LCD接入、保留U1测试，需独立授权和验收，不在当前显示会话切换或复用内存。

下一独立目标尚未开始。保持本状态页；TouchGFX、触控、波形页及测距算法均另立目标。

## 本轮文件清单

以下是本轮提交的确切文件；原PPS/生成配置工作区未混入。

```text
Analysis/tests/test_ui_status_analysis.py
Analysis/ui_status_analysis.py
App/BSP/bsp_lcd.c
App/BSP/bsp_lcd.h
App/BSP/bsp_sdram.c
App/BSP/bsp_sdram.h
App/Network/ethernet_monitor.c
App/Network/ethernet_monitor.h
App/Sync/time_sync.c
App/Sync/time_sync.h
App/UI/rgb565_canvas.c
App/UI/status_screen.c
App/UI/status_screen.h
App/UI/status_service.c
App/UI/status_service.h
App/UI/status_view.c
App/UI/status_view.h
App/app_main.c
CMakeLists.txt
Docs/Defense_Guide.md
Docs/Document_Status.md
Docs/Hardware_Resources.md
Docs/Milestones/U2_LTDC_Status_Page.md
Docs/Plans/2026-10-24_LTDC_Status_Page.md
Docs/Project_State.md
Docs/Protocol.md
Docs/README.md
Docs/System_Architecture.md
README.md
Tests/LcdBsp/run_tests.py
Tests/LcdBsp/stubs/main.h
Tests/LcdBsp/test_bsp_lcd.c
Tests/Sdram/run_tests.py
Tests/Sdram/test_bsp_sdram.c
Tests/UI/run_app.py
Tests/UI/run_band_work.py
Tests/UI/run_preflight.py
Tests/UI/run_service.py
Tests/UI/run_snapshots.py
Tests/UI/test_app_lcd.c
Tests/UI/test_band_work.c
Tests/UI/test_service.c
Tests/UI/test_snapshots_network.c
Tests/UI/test_snapshots_sync.c
Tests/UI/test_status.c
Tools/Monitor/check_ltdc_config.py
cmake/lcd.cmake
```
