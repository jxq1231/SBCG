# U3.1：TouchGFX真实状态页

<!-- doc-status: completed-scoped-evidence-with-open-issue; reviewed: 2026-10-25 -->

**本轮TouchGFX显示范围通过，首窗未解释重启仍待查。** 单A板，未接网线/B/示波器，不主动发声；温度模块未连接。用户确认新状态页显示正常。独立复位窗口82.55秒静默及34秒手动温度命令窗口通过，不把首次双BOOT窗口改为成功。

## 本轮实现

- TouchGFX4.26.1/NoOS，保留LTDC RGB565/480×272/9.6MHz与两块261120字节SDRAM缓冲；CPU软件绘图，没有DMA2D、触控或缓存策略更改。
- `App_Init → BSP_SDRAM_InitDisplay → BSP_TouchGFXDisplay_Init`在音频启动前关闭背光并清A/B；之后初始化业务及框架。
- `App_Process → 音频归还 → UI_TouchGFX_Process → 共享快照/文本 → __real_MX_TouchGFX_Process → Screen1View`。业务副本约1Hz，变化控件才失效；无新PCM或候选消费者。
- 自动生成的main中Process调用经`--wrap`落到空函数。CubeMX界面只有Init行，用户已正确延后LTDC/TouchGFX初始化；不要求寻找不存在的Process设置。
- `LTDC_IRQHandler → HAL_LTDC_IRQHandler → 生成LineEventCallback`负责VSYNC与换页；自有可见通知只作原子交接。前台确认实际绘制完成与可见地址一致后开DISP/BL。无新DMA关系；音频DMA2 Stream7/Channel0/循环/半满满回调及IRQ4/0保持，UART IRQ5/0保持，LTDC IRQ为用户生成9/0。
- 错误合并原始FU/TE与HAL锁存码，错误终态关闭显示，不抢占框架扫描/换页。I/D Cache仍关闭，没有增加Clean/Invalidate调用。
- 角色、网络、同步/内部估计偏差、AUTO/MANUAL温度/声速、音频状态来自真实副本；距离/方向/质量未实现，仍占位。外部精度始终未验收。

## 软件检查与修复

Debug/Release既有CMake预设均通过；Flash使用262260/171636字节。链接器原有RWX LOAD警告保留，无新增源码警告。ETH/音频布局两配置通过；MSP仍20047000..20048000，音频SRAM1 64KiB、固定网络堆/描述符/RX池均未重叠。链接输出RAM97.02%包含固定保留窗，不等于静态数据只剩9KiB：Debug `_ebss=20007A9C`，新libc堆起点仍在音频区域之后。

主机工具125项、离线分析150项通过；服务13场景、BSP15场景、View十三个绑定、App后端互斥/顺序、真实字形宽度与页面边界通过；旧U2服务及音频、串口、温度回归通过。独立规格/质量复核通过。

独立复核发现并修复首帧竞态：父类endFrame允许IRQ换页后、用户完成通知之前，原实现会丢掉真实可见地址；静态页可能一直黑屏。现于父类调用前捕获实际绘图地址，父类返回后发布完成位，可见回调始终保存最新地址；前台按地址关联，不屏蔽音频IRQ。`race/initial_address/no_draw`先观察旧实现失败，再修复通过，已纳入默认回归。

Designer空白模板初始application.config为1bpp；已将资产输入设为RGB565/16bpp并经官方生成获得4bpp字体及USE_BPP16。预检阻止错误1bpp进入整机；未手改生成字体/CMake。

## 固件及依赖

冻结目录`build/Firmware/SBCG-U3.1-TOUCHGFX`，894项源/配置/SDK输入及源码快照：

- Debug SHA256：`7d9d1e22fe09ccf5f8fea6e74d5316ad3e10c2fc1db12b2c9a0a89829459d0f2`。
- Release SHA256：`33f6583aced2d924ae20792645734b6ae36ec4d1f97c9fd084333bbe40728e56`。
- 当前完整硬件工作区含此前受保护PPS及CubeMX生成改动；不能把当前UI功能提交单独当作同一硬件快照。

第三方SDK位于`Middlewares/ST/touchgfx`，作为本地安装/生成依赖忽略，不把整个SDK及模拟器输出混入应用提交。复现需安装TouchGFX4.26.1，用Designer打开`TouchGFX/SBCG.touchgfx`执行Generate Code/Assets，复制本地框架及生成`cmake/touchgfx`，再运行现有CMake Debug/Release预设。Designer的Makefile只用于字体/资产生成，不替代MCU构建。提交保留界面源、字体输入及所需生成资源。

## 实物记录

沿用原ST-LINK配置与探针，写入前核对A UID `001A002D:30365105:36363631`，烧录Verified OK并复位；不改选项字节。用户反馈“状态页显示正常”。原始数据在`Logs/TouchGFX/U3/runtime`。

| 窗口 | 实际结果 |
| --- | --- |
| quiet-01，97.070秒总采集 | t27.887650和34.306938各一条U3 BOOT，间隔6.419288秒；用户确认无RESET/USB操作，**本窗不通过，原因未知**。没有删除、择取后一次启动将整窗判绿。 |
| temperature-01，34.09秒 | 无BOOT；34条UI和34条音频零错误；25°C→346450mm/s、30°C→349480mm/s，UI副本与业务一致；恢复AUTO后无传感器，显示无有效温度。 |
| quiet-02，97.059秒总采集 | 不再写Flash；助手明确执行一次`init; reset run; shutdown`。14.447111秒出现一次BOOT，之后82.552889秒无再次BOOT，原管线及UI分析全部通过。 |
| quiet-02 UI | 81条诊断，末FRAMES3966、UPDATES81、ERRORS0、DISCARDS0；FRAMES为框架endFrame次数，UPDATES为约1Hz副本版本，不能混为81次绘图。副本间隔1002～1030ms，均值1022.5375ms。 |
| quiet-02音频/模型 | 80条有效音频及位置，末479有效块/3923968帧，DROP/ERR/HAL/DMA均0；心跳连续，40次codec成功，26组原MAP/CHECK，8个自然候选均与Fraction一致，无标准音频识别结论。 |

quiet-02复位中断旧UART行，BOOT之前312行及10字节前缀`24434f4445432c5345513db1`仅作为旧数据排除；原始bin与JSON逐行投影一致，BOOT后严格ASCII、无残尾。分析先在**整份原始完整数据**查目标BOOT恰好一次，再建立新窗口，不改写源日志。新增残尾检查拒绝未换行的第二条BOOT或`$BO`等BOOT前缀，避免将其当普通残尾排除；对应回归通过。自动分析始终保留`physical_panel_verified=false`，实屏通过来自独立用户反馈。

首窗问题未消失于结论中：源码未找到显式系统复位调用，看门狗模块未启用，异常处理为死循环；本地OpenOCD内嵌program流程只有一次reset run，现有证据不能归因为工具、软件或供电。后续排查应先记录复位原因和精确主机时间线，不能直接宣布已排除重启风险。

本轮没有网线，因此仅检查原网络服务在LINK_DOWN下继续运行；没有新增联网实测、双板同步或PPS电气波形证明。模拟器运行不在本轮验收范围，界面绑定由native桩及MCU实屏验证。

## 之后补测

B板同步状态/两屏一致性、联网业务回归、标准声音识别与测距、外部PPS同步精度、DHT实际温度变化均不在当前无设备静默范围内。8根颜色低位按用户要求仍暂缓；没有把它们改成输出或宣称已接线。
