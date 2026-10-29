# U1 SDRAM：Region2修复与全范围读写实物验收通过

<!-- doc-status: completed-evidence-sdram-and-quiet-pipeline; reviewed: 2026-10-24 -->

**当前SBCG-U1-SDRAM-R1已通过A板SDRAM及静默管线验收。** 用户新增Region2后，生成MPU预检查全部通过；BSP改为保存/恢复RNR，只读核对Region1/2后再访问FMC。首次异常和恢复记录保留在下文。LTDC、DMA2D、TouchGFX仍未启用，没有播放声音。

## R1本轮验收结果

- 沿用原ST-LINK配置、核对A UID后单次烧录并Verified OK；随后一次明确复位用于采集启动。未改选项字节。
- `Logs/UI/U1/R1/quiet-01-*`采集95.108秒，只有1次BOOT；选定BOOT后约79.343秒判定窗口。复位前336行原样保留并明确排除；全文件2039行/198880字节，BOOT后1703行严格ASCII、无尾残片，raw/JSON投影一致。
- 初始化：BASE=C0000000、BYTES=8388608、SDCLK_HZ=66666666、CAS=3、REFRESH=1021。全8MB四轮模式在板端50529ms完成，唯一终态PASS、PASSES=4、CHECKED_BYTES=33554432，错误偏移/期望/实际均零；校验字节为四轮累计，不是扩大了物理容量。
- 77条音频RUN/VALID1，末尾461个有效块、3776512帧；DROP/ERR/HAL/DMA全零。77条位置连续/GAP及FAULTS零，77条心跳连续且TX_ERRORS零。
- 77条有效锚点、25组原MAP/CHECK继续运行；38次WM8994 ID8994读取成功。候选/事件为0，不需要发声；不能据此声称标准音频识别或课程抗误触发验收通过。
- PPS末计数EMITTED78/MISSED0/ERRORS0，仅为软件观测，不是波形证据。无网线ETH DOWN、温度UNREADY/TIMEOUT符合当前缺少连接的条件，不宣称网络、温度传感器或外部同步精度在本轮通过。
- `python -B Analysis/sdram_analysis.py --prefix Logs/UI/U1/R1/quiet-01`得到PASS_SDRAM_AND_QUIET_PIPELINE，所有SDRAM与原静默管线检查为true。

R1冻结Debug SHA256=de9fd2ffaa646c5754ddb6889644ccd94f5e3a501bfa4ad6c662939be3571489（2207212字节）；Release=9341c200f4967f7abe3bf07f1efb97c58b76615e2ef355f99445b906bceac10a（158068字节）。目录`build/Firmware/SBCG-U1-SDRAM-R1/`保存126项输入哈希和源码/生成快照；原始采集SHA256=378e13fd5e5965ac7a9e16f96d3c1e7e9e883b5ea5e3a42f46ed9d02c4ce9af0。

主机核心/27项BSP/真实App、原Audio/EventTime/App/UI、Tools120项与Analysis128项全部通过；Debug/Release构建及两配置ETH/Audio布局通过。独立复核通过，并指出预检只在configure执行的缺口；现已将main.c和检查脚本加入CMAKE_CONFIGURE_DEPENDS，两份Ninja重配置依赖均核对存在。不会因仅生成MPU源码而沿用旧预检结果。

本轮提交用户自有SDRAM/分析/测试与构建接入。原有PPS、混合CubeMX生成差异和许可删除继续保留在工作区；冻结文件包含这些明确依赖，不将HEAD独立克隆说成同一实测镜像。生成文件哈希与用户再生成后快照一致，未手改。完成本增量后停止，下一步才配置LTDC。


## 已准备的代码

- `App/BSP/bsp_sdram.c/.h`：核对生成参数，HAL芯片上电Clock Enable→1ms→PALL→8次Auto Refresh→CAS3模式0x0230→刷新1021。本版本HAL未实现发命令Timeout等待，BSP补充2ms/20000次BUSY双限界，失败不重试。
- `App/BSP/sdram_test.c/.h`：独占目标内存；数据位/16位/字节写检查，8MB四轮常量/互补/地址/互补地址；每轮填完等待至少100ms再校验，每片最多128字，首错停止。完成应为4轮、累计校验33554432字节。
- `App/app_main.c`：初始化在音频启动前，主循环在音频归还后推进一片；DHT低态不访问SDRAM。日志最多1Hz和终态一次，沿用每轮至多一行UART队列。
- `cmake/sdram.cmake`及顶层引用：用户拥有的再生成安全源登记。未手改任何生成初始化/HAL。
- `Tests/Sdram/`：真实内存核心、19项HAL桩BSP场景、真实App集成；`Analysis/sdram_analysis.py`检查全范围PASS及原静默管线，新增7项拒绝/正常测试。
- `Tools/Monitor/check_sdram_mpu.py`：只读展开生成MPU字段继承，按最高编号与子区掩码核对FMC寄存器和SDRAM数据，新增5项回归。

计划调用链：`main → MX_FMC_Init → App_Init → BSP_SDRAM_Init`；前台`App_Process → 音频归还 → BSP_SDRAM_Process → SDRAM_TestStep`。**首版实物在MX_FMC_Init异常，未到App_Init，未执行内存测试；R1已完成该链路及自检。** 无新增IRQ/DMA，缓存保持关闭、无维护调用。

## 首版软件验证和冻结（失败版本，仅追溯）

缺失行为先观察断言失败，再完成核心/BSP/App。`python -B Tests/Sdram/run_tests.py`通过；Debug/Release预设构建及两配置ETH/Audio布局通过；原Audio、SoundEventTime/64组Fraction、UI、App、Tools115/Analysis121回归通过，新分析7项和MPU5项通过；最终完整Tools120项、Analysis128项全部通过。独立复核执行SDRAM运行器通过，但此前同样漏查App前寄存器可达性，不作为硬件证明。

冻结目录`build/Firmware/SBCG-U1-SDRAM/`保存125项输入哈希、源码/生成快照及工作区差异（忽略输出）：

| 构建 | ELF SHA256 | 字节 |
| --- | --- | ---: |
| Debug | 21b434ee7cfe8eaf4d7e576981b9a0fbd998ae3d96d13a84b53837f8256b3987 | 2206604 |
| Release | b5b02ea0bd03350b02dddcb7f7c32862b48f05188f7c066698408b3aaae51712 | 158068 |

冻结对应含受保护PPS及用户FMC/MPU生成内容的工作区，不等于单独HEAD。首版实物失败时未创建功能完成提交；既有PPS、许可删除及混合生成差异保留。R1的完成范围见页首。

## 首版实物失败与恢复（历史，不是当前板上状态）

沿用原ST-LINK配置，核对A板96位UID后烧录Debug，Verified OK，未改选项字节。`Logs/UI/U1/quiet-01-*`90.055秒仅16原始字节、0完整行、无BOOT，不作SDRAM/音频验收。两次短诊断复位单独保存在fault-diagnosis日志。

CFSR=0x00000082（DACCVIOL+MMARVALID），MMFAR=0xA0000140，异常栈PC=0x080049E4。冻结ELF定位为`FMC_SDRAM_Init`的`stm32f7xx_ll_fmc.c:807`，链路为`MX_FMC_Init → HAL_SDRAM_Init → FMC_SDRAM_Init`。

Region0为4GB No Access、子区禁用掩码0x87，子区5的0xA0000000–0xBFFFFFFF仍被禁止。Region1仅放行0xC0000000的8MB数据；不覆盖FMC控制寄存器，PRIVILEGED_DEFAULT不能绕过已匹配的禁止区域。此前“不需要额外A000区域”的指引错误，已撤销。

已恢复`SBCG-M4.1-TIME-R1` Debug（789c0c92ef2f…），烧录校验通过。恢复后8.036秒采集172完整行：8条心跳TX_ERRORS=0、8条音频RUN/VALID1且DROP/ERR/HAL/DMA=0、8条位置连续。只确认恢复，不替代原67.933秒R1验收。

原始SHA256：quiet-01=ec3bb01be4a9496de086c866496aa29c94b85590771918178b77b829c9ab8ede；recovery-TIME-R1=40e84ee713fae5aa286a8c2d1183b6030462bbfdbcd7de8d5481fa269ce238b0。原始数据/失败摘要保存在忽略目录`Logs/UI/U1/`。

## 已完成的MPU补正（历史，不重复生成）

用户已通过CubeMX保留Region0/1，新增Region2：Enabled、0xA0000000、4KB、SubRegionDisable=0、TEX0、Full Access、XN、Shareable、Not Cacheable、Bufferable。共享Device类型，RASR=0x13050017。Region1保持0xC0000000/8MB普通非缓存XN。

BSP现在保存RNR，分别读取Region1/2并恢复原选择器，不写RBAR/RASR；无论成功失败都不遗留选择器状态。无效MPU在任何FMC读取前拒绝。新增selector0/1/2/7、Region2缺失/地址/缓存、MPU关闭及零FMC读取测试。生成源预检查已接入现有CMake流程。

## 下一步与边界

SDRAM已通过本轮有限时长/四模式/100ms保持测试，不代表任意温度、时钟或长期负载均已验收。当前诊断固件每次启动会独占8MB约50秒；U2必须明确测试与帧缓冲的所有权交接，不能在破坏性自检时让LTDC读取同一区域，也不能在芯片上电命令完成前启用LTDC取数。下一独立目标为CubeMX配置LTDC RGB565后点屏，按[现行显示计划](../Plans/2026-10-24_LTDC_RGB565_Bringup.md)的后半部分操作；仍不启用TouchGFX或DMA2D。本轮没有LCD运行证据、测距结果、标准音频到达或双板同步精度结论。

[ST MPU区域优先级与内存属性说明](https://www.st.com/resource/en/application_note/an4838-managing-memory-protection-unit-in-stm32-mcus-stmicroelectronics.pdf)。

## 本轮重复验证入口

```powershell
python -B Tools/Monitor/check_sdram_mpu.py
python -B Tests/Sdram/run_tests.py
cmake --preset Debug
cmake --build --preset Debug --parallel 4
cmake --preset Release
cmake --build --preset Release --parallel 4
python -B Tools/Monitor/check_eth_layout.py --preset Debug
python -B Tools/Monitor/check_audio_layout.py --preset Debug
python -B Tools/Monitor/check_eth_layout.py --preset Release
python -B Tools/Monitor/check_audio_layout.py --preset Release
python -B Analysis/sdram_analysis.py --prefix Logs/UI/U1/R1/quiet-01
```

实物动作使用`.vscode/openocd-stm32f746.cfg`，先在同次连接核对UID，再`program {build/Firmware/SBCG-U1-SDRAM-R1/SBCG-U1-SDRAM-R1-Debug.elf} verify reset exit`。随后以`Tools/Monitor/serial_monitor.py --port COM3 --duration 95 --log-prefix Logs/UI/U1/R1/quiet-01`打开采集，再用既有Run配置`init; reset run; shutdown`建立明确BOOT边界。既有日志不覆盖；复测必须选新prefix，烧录遵守当前授权。

本轮精确源码/测试变更：`App/BSP/bsp_sdram.c/.h`、`App/BSP/sdram_test.c/.h`、`App/app_main.c`的SDRAM条件块、`cmake/sdram.cmake`及顶层引用、`Tests/Sdram/run_tests.py`、`test_sdram_test.c`、`test_bsp_sdram.c`、`test_app_sdram.c`、`stubs/main.h`、`Analysis/sdram_analysis.py`、`Analysis/tests/test_sdram_analysis.py`及原`Analysis/sound_event_time_analysis.py`的显式固件参数。日志字段见[Protocol的U1节](../Protocol.md)。MPU只写RNR选择器用于诊断并恢复；不写区域属性，生成MPU不被应用替代。
