# U0/U1 LTDC RGB565状态页与CubeMX起步

<!-- doc-status: completed-u1-reference-see-status-page-plan; reviewed: 2026-10-24 -->

> **U1已完成；U2现行计划见[LTDC状态页直接验收](2026-10-24_LTDC_Status_Page.md)。用户要求跳过独立色条实验。** 用户已补Region2，R1保存/恢复区域选择器，8MB四轮校验通过；BOOT后约79秒音频零丢块/零错误。下方FMC/MPU配置已完成，不重复生成；首版MPU故障与修复见[U1记录](../Milestones/U1_SDRAM_Bringup.md)。

## U1执行清单（2026-10-24）

用户已完成FMC/MPU生成，并修正自动选出的PH2→PC3/SDCKE0及tRCD16→2。当前唯一目标为8MB SDRAM上电及有界破坏性内存测试；LTDC仍不启用。已有PPS未提交修改、CubeMX输出及LAN8742许可删除单独保护，生成前快照保存在忽略目录`Logs/UI/U1/prechange/`。

- [x] 检查38个FMC脚、CAS3、HCLK/3、2/8/5/7/3/2/2、MPU Region1普通非缓存XN；保留音频/RCC/串口/网络/DMA/NVIC。
- [x] `Tests/Sdram/`先验证缺失实现失败：命令顺序/失败止步、BUSY有界等待、配置拒绝、刷新计数1021、内存边界/故障/保持等待/暂停及完成后停止访问。
- [x] `App/BSP/bsp_sdram.c/.h`只读取生成配置，HAL发送Clock Enable→1ms等待→PALL→8次Auto Refresh→CAS3模式0x0230→刷新1021。HAL本版本SendCommand的Timeout未实现等待，BSP对SDSR BUSY做毫秒及轮询次数双重限界，不修改HAL；R1已保存/恢复区域选择器并核对Region1/2。
- [x] `App/BSP/sdram_test.c/.h`独占调用方内存，单轮最多128个32位字；数据位/16位/字节掩码检查后，4轮常量/互补/地址/互补地址填充，分别等待至少100ms再逐字校验。首次错误保存偏移/期望/实际并终止，不将内存FAIL写成READY。
- [x] `App/app_main.c`用`SBCG_USE_SDRAM`接入：在音频启动前初始化芯片，前台音频处理之后、DHT非低态才推进一个分片；最多1Hz诊断、完成即时报告。`cmake/sdram.cmake`为用户源登记点，由顶层一次生成的CMake引入，不改生成CMake。
- [x] 原生测试→Debug/Release现有预设构建→ETH/音频布局→独立复核→按当前硬件授权仅A烧录，静默串口约90秒核对SDRAM全范围PASS及音频DROP/ERROR0，无声音/网线/示波器依赖。
- [x] 更新报告/入口，提交本轮用户源码/测试/文档；原PPS、许可删除与混合生成差异保留在工作区，冻结快照记录完整依赖。

用户选择：先LTDC RGB565，再优化成TouchGFX；不能使用声音继续测试。用户指定参考聊天`codex://threads/01a14061-e262-7d91-9ed3-4a54cbab0ff7`。本次读取该聊天的本地记录，仅提取相关显示经验，并只读核对`D:/electronic_design/GUI_FOR_TEST/GUI_FOR_TEST.ioc`和生成main.c。该工程是参考，不是SBCG硬件真值；未复制其216MHz、TIM2/ADC、单帧重绘、独立I2C轮询或TouchGFX业务。

## 已完成的软件准备U0

`App/UI/status_view.*`定义带单位/有效标志的状态快照和文本；`rgb565_canvas.*`提供调用方内存上的有界RGB565矩形/5x7 ASCII；`status_screen.*`绘制480x272页面并支持行带。模型与视图分离，以后TouchGFX可复用数据层，原RGB565绘图层可替换。当前未接App调度、未登记固件源列表，不访问0xC0000000，不创建虚假的LCD运行证据。

页面包含BOARD角色、待测距离、同步/时差、外部精度未验证、网络、温度/声速、音频及事件数。未知/无效值用`--`，快照>=2秒标STALE；即使INTERNAL LOCK也不显示外部同步精度通过。当前没有测距，默认距离不是0。首次小字库只覆盖ASCII，中文/触控由后续TouchGFX增量处理。

测试：`python -B Tests/UI/run_tests.py --preview Docs/Assets/status_rgb565_preview.png`。真实C输出RGB565像素，主机导出预览；格式边界、负温/回绕、RGB565通道、裁剪、跨度填充、canary、分16行/整屏像素一致均通过。预览中的A/NO LINK/RUN/EVENTS10为离线快照示例，不能证明屏幕或当前实时数据已接通。

## 已完成硬件目标U1：SDRAM配置参考，不重复操作

**CubeMX change required**。根AGENTS.md第4节要求硬件配置由CubeMX生成。当前FMC/Region1/Region2均已由用户生成，SDRAM实物通过；LTDC仍未启用，后续新增资源继续由CubeMX生成。

请在**当前SBCG.ioc**内操作，保留主PLL/HCLK200MHz、ETH/RMII、USART1、DHT11 PG7、SAI2/PLLI2S及TIM2/PPS。不要创建另一工程或加载参考工程的全套配置。

### 第一步：FMC → SDRAM1

在Pinout & Configuration进入Connectivity/Memory一类的 **FMC**，选择 **SDRAM Bank1 / SDNE0 + SDCKE0**，16-bit数据；8列、12行、4个内部Bank。确认这不是异步SRAM/NOR。

| 参数 | 当前起步选择 |
| --- | --- |
| SDClockPeriod | HCLK / 3，200MHz下约66.667MHz；不照抄参考工程216/3=72MHz |
| CAS Latency | 3 |
| Write Protection | Disabled |
| Read Burst | Enabled |
| Read Pipe Delay | 0 |
| LoadToActiveDelay / tMRD | 2 cycles |
| ExitSelfRefreshDelay / tXSR | 8 cycles |
| SelfRefreshTime / tRAS | 5 cycles |
| RowCycleDelay / tRC | 7 cycles |
| WriteRecoveryTime / tWR | 3 cycles |
| RPDelay / tRP | 2 cycles |
| RCDDelay / tRCD | 2 cycles |

这是参考GUI_FOR_TEST配合较低SDCLK的设计，现已由U1-R1完成本板有限时长实物验收；不覆盖任意换装SDRAM型号。更换器件或时钟需重新核对规格/时序并验收。旧Hardware_Resources中100MHz/CAS2/0x0603属于历史BSP参考，**本轮不使用**。

38个FMC脚必须使用板上连线：A0..A5=PF0..PF5、A6..A9=PF12..PF15、A10=PG0、A11=PG1；BA0/1=PG4/PG5；D0/1=PD14/PD15、D2/3=PD0/PD1、D4..D12=PE7..PE15、D13..D15=PD8..PD10；NBL0/1=PE0/PE1；SDCKE0=PC3、SDNE0=PH3、SDNWE=PH5、SDNRAS=PF11、SDNCAS=PG15、SDCLK=PG8。AF12、推挽、No Pull、High/Very High；PH7/PH8继续I2C3，不选作FMC。

### 第二步：CORTEX_M7 → MPU

保留Region0及其0x87子区掩码，新增高优先级 **Region1**：

| 参数 | 值 |
| --- | --- |
| Enable / Base / Size | Enable / 0xC0000000 / 8MB |
| SubRegionDisable | 0x00 |
| AccessPermission | Full Access |
| DisableExec | Instruction Access Disable（XN） |
| TEX / Cacheable / Bufferable | TEX Level1 / Not Cacheable / Not Bufferable |
| Shareable | Not Shareable |

Region1是普通、不可执行、非缓存内存。I/D Cache保持关闭，不改变ETH/音频内部RAM属性。此前忽略A000寄存器区域的指引错误，现补正如下。

**Region2：FMC寄存器，必须新增。**

| 界面参数 | 值 |
| --- | --- |
| MPU Region | Enabled |
| Base Address / Size | 0xA0000000 / 4KB |
| SubRegion Disable | 0x0 |
| TEX field level | level 0 |
| Access Permission | ALL ACCESS PERMITTED |
| Instruction Access | DISABLE |
| Shareability | ENABLE |
| Cacheable | DISABLE |
| Bufferable | ENABLE |

这与Region1的普通SDRAM数据区域不同：Region2为共享Device，放行FMC控制寄存器，不开放整个外部设备区域。保留Region0/1。`python -B Tools/Monitor/check_sdram_mpu.py`现已全通过；BSP区域选择器已适配并通过实物验收。

### 第三步：保存并Generate Code

**本次只生成FMC与SDRAM数据/控制寄存器MPU区域，不启用LTDC、DMA2D、TouchGFX或新DMA/IRQ。** 生成后应有hsdram1、MX_FMC_Init、FMC GPIO AF12、HAL_SDRAM/FMC原厂源及Region1/2；主时钟/SAI/ETH/串口/PPS保持。随后由应用BSP执行SDRAM上电命令序列和内存模式/地址测试，重新构建/按授权烧录验收，再进入下一步LTDC。

`MX_FMC_Init`只配置控制器，芯片仍需Clock Enable→等待→Precharge All→8次Auto Refresh→Mode Register→周期刷新。计划CAS3模式字0x0230。若器件为4096次/64ms刷新，66.667MHz下刷新计数按`floor(SDCLK_Hz*0.064/4096)-20`重算为1021，不能照抄参考72MHz的1105或历史100MHz的0x0603；该计数在后续BSP实现前再次核对实际生成频率/器件规格。上电BSP与刷新计数已在R1实物验证，首版寄存器窗口故障保留在报告中。

## U2现行配置入口

本页U1配置和实物证据保留作参考，不重复实施。后续LTDC按[U2状态页直接验收计划](2026-10-24_LTDC_Status_Page.md)执行：固定20根AF/10根GPIO、9.6MHz、RGB565 Layer0、CubeMX取消自动调用并取消static、SDRAM显示模式与自检分离、首帧绘好后启动。独立色条实验已由用户明确跳过。

上述U2仍待用户CubeMX生成和应用接入，本页不作为当前已点屏或默认自动初始化LTDC的依据。

## 参考与移植边界

- [ST面板参数](https://github.com/STMicroelectronics/stm32-rk043fn48h/blob/main/rk043fn48h.h)：480x272与41/13/32、10/2/2。
- [ST板级LCD](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_lcd.c)：面板与图层控制参考。不可调用会重写本项目引脚/时钟或ARGB8888默认层的初始化来覆盖CubeMX。
- [ST SDRAM参数](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_sdram.h)：8MB地址窗、模式字字段，100MHz刷新仅为原示例，不作为当前计数。
- 参考聊天中的触控越界读取修正、减小invalidate重绘区和视频/随机图表示例是后续注意事项，不是本次引入TouchGFX、触控中断、ADC/TIM2重分配或演示数据的授权。
