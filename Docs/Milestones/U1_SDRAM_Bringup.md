# U1 SDRAM：软件已准备，实物启动被MPU阻断

<!-- doc-status: in-progress-awaiting-cubemx-region2; reviewed: 2026-10-24 -->

**SDRAM未通过实物验收。** 用户已生成FMC/MPU Region1，并修正PH2→PC3及tRCD16→2。软件与构建通过后，A板实测发现计划遗漏FMC寄存器MPU窗口；等待CubeMX新增Region2。未启用LTDC、DMA2D、TouchGFX，也没有播放声音。

## 已准备的代码

- `App/BSP/bsp_sdram.c/.h`：核对生成参数，HAL芯片上电Clock Enable→1ms→PALL→8次Auto Refresh→CAS3模式0x0230→刷新1021。本版本HAL未实现发命令Timeout等待，BSP补充2ms/20000次BUSY双限界，失败不重试。
- `App/BSP/sdram_test.c/.h`：独占目标内存；数据位/16位/字节写检查，8MB四轮常量/互补/地址/互补地址；每轮填完等待至少100ms再校验，每片最多128字，首错停止。完成应为4轮、累计校验33554432字节。
- `App/app_main.c`：初始化在音频启动前，主循环在音频归还后推进一片；DHT低态不访问SDRAM。日志最多1Hz和终态一次，沿用每轮至多一行UART队列。
- `cmake/sdram.cmake`及顶层引用：用户拥有的再生成安全源登记。未手改任何生成初始化/HAL。
- `Tests/Sdram/`：真实内存核心、19项HAL桩BSP场景、真实App集成；`Analysis/sdram_analysis.py`检查全范围PASS及原静默管线，新增7项拒绝/正常测试。
- `Tools/Monitor/check_sdram_mpu.py`：只读展开生成MPU字段继承，按最高编号与子区掩码核对FMC寄存器和SDRAM数据，新增5项回归。

计划调用链：`main → MX_FMC_Init → App_Init → BSP_SDRAM_Init`；前台`App_Process → 音频归还 → BSP_SDRAM_Process → SDRAM_TestStep`。**实物在MX_FMC_Init异常，未到App_Init，未执行内存测试。** 无新增IRQ/DMA，缓存保持关闭、无维护调用。

## 软件验证和冻结

缺失行为先观察断言失败，再完成核心/BSP/App。`python -B Tests/Sdram/run_tests.py`通过；Debug/Release预设构建及两配置ETH/Audio布局通过；原Audio、SoundEventTime/64组Fraction、UI、App、Tools115/Analysis121回归通过，新分析7项和MPU5项通过；最终完整Tools120项、Analysis128项全部通过。独立复核执行SDRAM运行器通过，但此前同样漏查App前寄存器可达性，不作为硬件证明。

冻结目录`build/Firmware/SBCG-U1-SDRAM/`保存125项输入哈希、源码/生成快照及工作区差异（忽略输出）：

| 构建 | ELF SHA256 | 字节 |
| --- | --- | ---: |
| Debug | 21b434ee7cfe8eaf4d7e576981b9a0fbd998ae3d96d13a84b53837f8256b3987 | 2206604 |
| Release | b5b02ea0bd03350b02dddcb7f7c32862b48f05188f7c066698408b3aaae51712 | 158068 |

冻结对应含受保护PPS及用户FMC/MPU生成内容的工作区，不等于单独HEAD。U1功能代码暂不创建完成提交；既有PPS、许可删除及混合生成差异保留，不混入文档提交。

## 实物失败与恢复

沿用原ST-LINK配置，核对A板96位UID后烧录Debug，Verified OK，未改选项字节。`Logs/UI/U1/quiet-01-*`90.055秒仅16原始字节、0完整行、无BOOT，不作SDRAM/音频验收。两次短诊断复位单独保存在fault-diagnosis日志。

CFSR=0x00000082（DACCVIOL+MMARVALID），MMFAR=0xA0000140，异常栈PC=0x080049E4。冻结ELF定位为`FMC_SDRAM_Init`的`stm32f7xx_ll_fmc.c:807`，链路为`MX_FMC_Init → HAL_SDRAM_Init → FMC_SDRAM_Init`。

Region0为4GB No Access、子区禁用掩码0x87，子区5的0xA0000000–0xBFFFFFFF仍被禁止。Region1仅放行0xC0000000的8MB数据；不覆盖FMC控制寄存器，PRIVILEGED_DEFAULT不能绕过已匹配的禁止区域。此前“不需要额外A000区域”的指引错误，已撤销。

已恢复`SBCG-M4.1-TIME-R1` Debug（789c0c92ef2f…），烧录校验通过。恢复后8.036秒采集172完整行：8条心跳TX_ERRORS=0、8条音频RUN/VALID1且DROP/ERR/HAL/DMA=0、8条位置连续。只确认恢复，不替代原67.933秒R1验收。

原始SHA256：quiet-01=ec3bb01be4a9496de086c866496aa29c94b85590771918178b77b829c9ab8ede；recovery-TIME-R1=40e84ee713fae5aa286a8c2d1183b6030462bbfdbcd7de8d5481fa269ce238b0。原始数据/失败摘要保存在忽略目录`Logs/UI/U1/`。

## 下一必需操作

**CubeMX change required**。保留Region0/1，新增Region2：Enabled、0xA0000000、4KB、SubRegionDisable=0、TEX level0、ALL ACCESS PERMITTED、Instruction Access DISABLE、Shareability ENABLE、Cacheable DISABLE、Bufferable ENABLE。共享Device类型，仅覆盖0xA0000000..0xA0000FFF，预期RASR=0x13050017。

再生成后先运行`python -B Tools/Monitor/check_sdram_mpu.py`；当前应失败，修正后应全部通过。随后BSP取消`RNR==1`假设，保存/恢复选择器，只读核对Region1/2后才读取FMC，补选择器恢复及Region2缺失/错误测试，再构建/复核/实测。依据根AGENTS.md第4节，等待用户CubeMX，不在应用/HAL绕过。引脚/RCC/SAI/TIM2/DMA/NVIC不需改变，SDRAM通过后才配置LTDC。

[ST MPU区域优先级与内存属性说明](https://www.st.com/resource/en/application_note/an4838-managing-memory-protection-unit-in-stm32-mcus-stmicroelectronics.pdf)。
