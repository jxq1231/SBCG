# T1.2 串口手动设温实施方案

目标：当前单板通过串口查询、手动设置环境温度并恢复 DHT11 自动测温，统一使用整数声速模型。用户已确认此目标并完成 USART1 NVIC 5/0 的 CubeMX 生成。

## 行为约定

- `PING` 返回 `$CMD,NAME=PING,RESULT=OK`。
- `TEMP?` 返回 `$ENV`，包括 MODE、SOURCE、VALID、VERSION、T_MILLI_C、C_MM_S；无有效值时省略温度和声速。
- `TEMP SET <T>` 支持可选负号、1～2 位整数和可选一位小数；范围 -20.0～60.0℃，无空白扩展、指数或额外参数。成功返回 `$CMD,NAME=SET,RESULT=OK` 和 `$ENV`。
- `TEMP AUTO` 恢复自动模式，返回 `$CMD,NAME=AUTO,RESULT=OK` 和 `$ENV`。已是 AUTO 时幂等。
- 手动模式停止 DHT11 采集，值持续有效直至切换模式、再次设定或复位；不持久化。重复 SET 也产生新版本。
- MANUAL→AUTO 清除手动值，不冒充自动结果；沿用最后一次起始时刻的 5 秒间隔并重新预热一帧，第二个合格帧发布自动值。版本在模式切换时保留，只在发布有效值时递增。
- 自动模式既有 `$TEMP` / `$TEMP_STATS` 保持兼容；`$ENV` 明确模式与来源。手动 `$TEMP` 不输出伪造湿度，STATE=MANUAL。
- 非法输入不改变模式、有效值或版本。超过 63 字节、非 ASCII、UART 错误/缓冲溢出均丢弃至 LF；CR 只允许在 LF 前。支持 LF 和 CRLF。
- 单次主循环最多读取 64 字节、执行一条命令，另有 100 ms 命令执行间隔以限制响应负载。DHT11 DATA 低期间推迟处理命令，接收中断仍工作。
- USART1 使用 HAL 单字节中断接收，256 字节有界环形缓冲；错误/重启失败计数可通过查询观察。中断不解析、不发送、不等待。主循环短临界区保护缓冲及重试，不改变 NVIC。

## 文件与任务

- [x] 核对用户生成的 SBCG.ioc、Core/Inc/stm32f7xx_it.h、Core/Src/stm32f7xx_it.c、Core/Src/stm32f7xx_hal_msp.c：只启用 USART1_IRQn 5/0。
- [x] 恢复 CubeMX 删除的 Drivers/BSP/Components/lan8742/LICENSE.txt 原文。
- [x] Tests/Temperature/test_temperature_service.c 先增加手动值持久、零/负温、非法不变、切回自动预热及版本测试；扩展 App/Temperature/temperature_service.c/.h。
- [x] Tests/Console/test_temperature_console.c 与 run_tests.py 先覆盖分段、粘连、溢出/坏字节重同步、范围及限流；新增 App/Console/temperature_console.c/.h。
- [x] Tests/Console/test_bsp_console_rx.c 与 stubs/main.h 检查 HAL 接收完成、错误、续收失败、环形溢出、外来 UART；扩展 App/BSP/bsp_console.c/.h。
- [x] Tests/App/test_app_main.c 检查初始化、低电平避让及每循环调度；修改 App/app_main.c 与用户 CMakeLists.txt 注册。
- [x] 更新 Docs/SerialWeb.md、Docs/Protocol.md、Docs/Project_State.md、Docs/Milestones/T1_2_Manual_Temperature.md，明确软件验证与实物待验边界。
- [x] 运行原有全部相关测试、Debug/Release 构建及 ETH 布局检查，复核差异及 CubeMX 再生成保护。本地复核已做，独立审查工具本次不可用。
- [x] 新 Debug ELF 已固定 SHA256 824d3c3cecaa17bf93827e9b639d625cb37cd917ca9d23935b40f2d64c6519d3，两配置构建/布局通过；已准备通知用户实机验收。
- [x] 已获得新版本烧录授权；沿用现有烧录配置，此前 R1 的批准未代替本次新版本批准。
- [x] 实机依次 SET 25.0→保持至少 15 秒→SET 0→SET -10.0→非法输入不变→AUTO，核对声速 346450/331300/325240 mm/s；AUTO 两帧后恢复有效；同步复验原 UDP/PTP。按用户要求不做传感器拔插测试。
- [x] 实物已通过，已核定本增量的定向提交清单；提交作者/提交者 jxq。

- [x] 栈复核：响应采用静态缓冲；cmake/linker/stm32f746_eth.ld 将主栈预留从 1 KiB 增为 4 KiB，栈顶不变；Tools/Monitor/check_eth_layout.py 和 Tools/Monitor/tests/test_eth_layout.py 拒绝 USART1 IRQ 下不足 4 KiB 的布局。

## 资源边界

CubeMX change required：用户已完成。无需进一步修改引脚/时钟/外设配置。新增 USART1 IRQ；无新增 DMA。既有 ETH DMA、网络保留区及缓存状态保持原样；用户链接扩展将 MSP 下界从 0x20047C00 移至 0x20047000，栈顶仍为 0x20048000。无第二板、LCD、距离算法或时钟校准实现；串口手动设温本身不代表课程 B5 的 LCD/测距要求已全部完成。
