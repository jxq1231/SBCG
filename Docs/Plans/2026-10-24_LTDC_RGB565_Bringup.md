# U0/U1 LTDC RGB565状态页与CubeMX起步

<!-- doc-status: current-display-plan-awaiting-cubemx; reviewed: 2026-10-24 -->

用户选择：先LTDC RGB565，再优化成TouchGFX；不能使用声音继续测试。用户指定参考聊天`codex://threads/01a14061-e262-7d91-9ed3-4a54cbab0ff7`。本次读取该聊天的本地记录，仅提取相关显示经验，并只读核对`D:/electronic_design/GUI_FOR_TEST/GUI_FOR_TEST.ioc`和生成main.c。该工程是参考，不是SBCG硬件真值；未复制其216MHz、TIM2/ADC、单帧重绘、独立I2C轮询或TouchGFX业务。

## 已完成的软件准备U0

`App/UI/status_view.*`定义带单位/有效标志的状态快照和文本；`rgb565_canvas.*`提供调用方内存上的有界RGB565矩形/5x7 ASCII；`status_screen.*`绘制480x272页面并支持行带。模型与视图分离，以后TouchGFX可复用数据层，原RGB565绘图层可替换。当前未接App调度、未登记固件源列表，不访问0xC0000000，不创建虚假的LCD运行证据。

页面包含BOARD角色、待测距离、同步/时差、外部精度未验证、网络、温度/声速、音频及事件数。未知/无效值用`--`，快照>=2秒标STALE；即使INTERNAL LOCK也不显示外部同步精度通过。当前没有测距，默认距离不是0。首次小字库只覆盖ASCII，中文/触控由后续TouchGFX增量处理。

测试：`python -B Tests/UI/run_tests.py --preview Docs/Assets/status_rgb565_preview.png`。真实C输出RGB565像素，主机导出预览；格式边界、负温/回绕、RGB565通道、裁剪、跨度填充、canary、分16行/整屏像素一致均通过。预览中的A/NO LINK/RUN/EVENTS10为离线快照示例，不能证明屏幕或当前实时数据已接通。

## 下一独立硬件目标U1：先验SDRAM，暂不点亮

**CubeMX change required**。根AGENTS.md第4节要求硬件配置由CubeMX生成。当前SBCG无FMC/LTDC，MPU Region0仍禁止访问0xC0000000；不能在应用里临时初始化引脚/时钟来绕过。

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

这是参考GUI_FOR_TEST参数配合较低SDCLK的起步设计，不声称已实测SBCG或覆盖任意换装SDRAM型号。再生成后核对芯片/时序，再运行内存验收。旧Hardware_Resources中100MHz/CAS2/0x0603属于历史BSP参考，**本轮不使用**。

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

区域必须是普通、不可执行、非缓存内存。I/D Cache保持当前关闭；不照抄参考工程额外0xA0000000区域，不改变ETH/音频内部RAM属性。

### 第三步：保存并Generate Code

**本次只生成FMC与SDRAM MPU区域，不启用LTDC、DMA2D、TouchGFX或新DMA/IRQ。** 生成后应有hsdram1、MX_FMC_Init、FMC GPIO AF12、HAL_SDRAM/FMC原厂源及Region1；主时钟/SAI/ETH/串口/PPS保持。随后由应用BSP执行SDRAM上电命令序列和内存模式/地址测试，重新构建/按授权烧录验收，再进入下一步LTDC。

`MX_FMC_Init`只配置控制器，芯片仍需Clock Enable→等待→Precharge All→8次Auto Refresh→Mode Register→周期刷新。计划CAS3模式字0x0230。若器件为4096次/64ms刷新，66.667MHz下刷新计数按`floor(SDCLK_Hz*0.064/4096)-20`重算为1021，不能照抄参考72MHz的1105或历史100MHz的0x0603；该计数在后续BSP实现前再次核对实际生成频率/器件规格。本轮未写任何SDRAM命令或刷新寄存器。

## SDRAM验收后才配置LTDC（现在不执行）

面板和参考工程/ST RK043FN48H驱动一致时，采用以下现行基线。旧文档H1/43/8、V10/12/4作废，不与这组混用。

| 项目 | 配置值 |
| --- | --- |
| 有效尺寸 / 引脚模式 / 图层格式 | 480x272 / RGB565 / RGB565 |
| HSYNC / HBP / HFP | 41 / 13 / 32 |
| VSYNC / VBP / VFP | 10 / 2 / 2 |
| HAL核对值 | HorizontalSync40, VerticalSync9, AccumulatedHBP53, AccumulatedVBP11, ActiveW533, ActiveH283, TotalWidth565, TotalHeigh285 |
| 极性 | HS/VS/DE Active Low，Pixel Clock IPC |
| PLLSAI | N192/R5/DivR4，PLLM25，LCD9.6MHz；保持音频PLLI2S不变 |
| 图层 | Layer0、窗口0..480/0..272、Alpha255/Alpha0=0、CA/CA、黑背景 |
| 帧缓冲 | 外部SDRAM 0xC0000000，RGB565 261120字节；预留第二块0xC0040000供后续防撕裂 |
| 显示/背光 | PI12/LCD_DISP、PK3/LCD_BL_CTRL，输出初始低，SDRAM和图层准备好后才打开 |

LTDC有效脚为PJ2..PJ6/R3..R7、PJ9/PJ10/PJ11/PK0/PK1/PK2/G2..G7、PJ15/PG12/PK4/PK5/PK6/B3..B7，加PI10/HS、PI9/VS、PI14/CLK、PK7/DE。PG12是AF9，其余所列LTDC脚AF14。低位颜色脚PI15、PJ0、PJ1、PJ7、PJ8、PE4、PJ13、PJ14作为普通推挽输出初始低。原PH7/PH8共享I2C、SAI2、PG7温度、PA15 PPS均不改。

初期先CPU绘制/静态色条，不启用DMA2D或新DMA/NVIC；后续实测需要加速、双缓冲/垂直消隐换页时再独立配置。全帧首次初始化不得发生在SDRAM上电前；长期运行采用有界分片/脏区刷新，不在IRQ整屏绘制。首版按颜色/边缘/时序/无闪烁和原音频零丢块验收，不以背光亮或主机预览作为点屏成功。

## 参考与移植边界

- [ST面板参数](https://github.com/STMicroelectronics/stm32-rk043fn48h/blob/main/rk043fn48h.h)：480x272与41/13/32、10/2/2。
- [ST板级LCD](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_lcd.c)：面板与图层控制参考。不可调用会重写本项目引脚/时钟或ARGB8888默认层的初始化来覆盖CubeMX。
- [ST SDRAM参数](https://github.com/STMicroelectronics/32f746gdiscovery-bsp/blob/main/stm32746g_discovery_sdram.h)：8MB地址窗、模式字字段，100MHz刷新仅为原示例，不作为当前计数。
- 参考聊天中的触控越界读取修正、减小invalidate重绘区和视频/随机图表示例是后续注意事项，不是本次引入TouchGFX、触控中断、ADC/TIM2重分配或演示数据的授权。
