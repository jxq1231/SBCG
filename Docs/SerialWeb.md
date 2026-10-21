# USART1配置核对与SerialWeb使用

当前进展（2026-10-22）：T1.2 串口手动设温已通过主机测试、Debug/Release 构建、布局及本版本90秒联合实物验收。操作和完整边界见 [T1.2 手动设温](Milestones/T1_2_Manual_Temperature.md)。USART1 接收中断已由用户通过 CubeMX 生成为优先级 5/0，无 UART DMA。当前板运行 SBCG-T1.2-MANUAL；25℃手动保持16250ms、零/负温、非法输入、分段命令及恢复自动均通过，UART错误0。

在本页所用 SerialWeb 的文本发送框输入 `TEMP?`、`TEMP SET 25.0`、`TEMP AUTO` 或 `PING`，开启结尾补换行并选 CRLF。手动设温范围 -20.0～60.0℃，最多一位小数；发送后等待响应。手动模式会保持设定值，切回 AUTO 后重新等待 DHT11 预读/采样。不会自动保存到 Flash。请勿在 R1 固件上把“发送成功”当作板端命令执行成功。

以下为早期 M0.2/M1.x 发送测试的历史记录，其中“未实现接收/未开启 IRQ”等描述仅对应当时版本；当前实现及待验状态以上述 T1.2 为准。

更新：2026-10-19。串口工具按用户指定采用[SerialWeb](https://conductance-lab.xyz/SerialWeb/)，作者为Conductance-lab。网址末尾不要包含中文逗号。

## 1. 本次检查结论

**USART1参数正确；启动信息和每秒心跳代码已接入，5项主机行为测试及Debug构建通过。用户已提供1条启动行和21条连续心跳，板端发送验收通过；MCU接收命令与回显未实现。**

上述实测对应M0.2。用户现已回传`SBCG-M1.1`的`$ETH`及心跳日志，PHY地址0、100 Mbps全双工、DOWN→UP→DOWN→UP和最后一段98条心跳已核对；详见[M1.1实测结果](Milestones/M1_1_Ethernet_Setup.md#10-用户实物日志验收结果)。本页保留M0.2原始验收边界，串口参数不变；助手未执行新固件烧录。

最新`SBCG-M1.2-INT`日志也已核对：HCLK=200 MHz、UID匹配、PHY识别正常、100 Mbps全双工，SEQ=1～12连续、相邻tick差1000 ms、TX_ERRORS=0；同时用户提供ping 4/4回复。详见[M1.2基本验收](Milestones/M1_2_UDP_Setup.md#8-用户实物验收2026-10-19)。串口参数保持115200、8N1；UDP与MCU串口接收仍未实现。

| 项目 | .ioc / 生成代码核对值 | 结论 |
| --- | --- | --- |
| 外设与模式 | USART1，Asynchronous，TX_RX | 符合当前目标 |
| TX | PA9，AF7，复用推挽 | 符合板载ST-LINK VCP目标路径 |
| RX | PB7，AF7，复用推挽 | 符合板载ST-LINK VCP目标路径 |
| 波特率 | 115200 | 正确 |
| 数据位 / 停止位 / 校验 | 8 / 1 / None，即8N1 | 正确 |
| 流控 / 过采样 | None / 16 | 正确 |
| 内核时钟 | PCLK2，工程为100 MHz | 正确，系统时钟函数与前一提交相同 |
| 初始化调用 | MX_GPIO_Init之后调用MX_USART1_UART_Init | 已接入main |
| HAL / CMake | HAL_UART_MODULE_ENABLED；uart.c和uart_ex.c已加入生成构建 | 编译、链接通过 |
| NVIC / DMA | 未启用USART1 IRQ和UART DMA，无USART1_IRQHandler | 符合本增量计划 |

主要核查位置：`SBCG.ioc`、`Core/Src/main.c`、`Core/Src/stm32f7xx_hal_msp.c`、`Core/Inc/stm32f7xx_hal_conf.h`、`cmake/stm32cubemx/CMakeLists.txt`。其中部分串口参数采用CubeMX默认值，.ioc未逐项列出；实际生成的初始化值已逐项核对。

当前调用链为`main → CubeMX初始化 → App_Init`及`主循环 → App_Process → BSP_Console_Write → HAL_UART_Transmit`。应用源文件位于`App/`，USART1映射集中在`App/BSP/bsp_console.c`。仅在main的USER CODE区域增加调用，未改生成初始化。无新增IRQ、回调、DMA缓冲或缓存维护。发送使用25 ms有限超时；没有接收命令、回显或printf重定向。

## 2. 本机端口与页面

本机当前枚举为`STMicroelectronics STLink Virtual COM Port (COM3)`；COM编号可能随电脑、USB口或驱动变化，以设备名称及实际枚举为准。使用板载ST-LINK对应CN14 USB连接，无需另接USB转串口模块。

本轮已用Edge打开指定HTTPS页面，标题为“SerialWeb 网页串口调试与图形解析工具 | Conductance-lab”，页面显示在线版本v2.0。检查时`window.isSecureContext=true`、`navigator.serial`存在，页面提示“Web Serial API已就绪，等待连接”。当时仅确认网页具备连接条件；随后用户提供了本版本的实物接收日志，发送路径已通过。

此前页面记录过一次`NetworkError: Failed to execute 'open' on 'SerialPort': Failed to open serial port`。当时原因未确定；本次用户已成功接收启动信息和心跳，该历史错误不再是当前阻塞。若以后复发，先关闭占用同一端口的其它串口窗口并手动重连，不直接归咎于MCU参数。

## 3. 连接步骤

1. 在Edge中打开[指定页面](https://conductance-lab.xyz/SerialWeb/)。首次显示版本信息时先关闭该弹窗，进入“监视”界面。
2. 核对参数：波特率`115200`，数据位`8`，停止位`1`，校验位“无”，关闭“RTS/CTS流控”。首轮无需切换DTR、RTS或BREAK。
3. 字符编码选`UTF-8`，文字观察时不勾选“十六进制接收”。“换行符设置”选`\r\n`；该选项配合“结尾补换行符”影响网页发送的内容，不会替固件接收输出补换行。后续固件文本应自行发送完整行结束符。
4. 点击“连接设备”，在浏览器设备选择窗口中选当前ST-LINK Virtual COM Port（本机为COM3），完成授权。
5. 使用“串口监视器”观察接收。首轮不要启用“定时发送”或“触发发送”；当前固件还没有命令解析逻辑。
6. 若提示端口被占用，关闭占用同一COM口的其它串口窗口后重连；不要仅因收不到数据就反复烧录。
7. 后续真正产生数据后，可使用页面的时间线录制/导出功能保存原始日志至本地`Logs/`；网页处理只用于开发观察，不承担正式测距计算。

页面支持“自动重连”“失焦自动断连”等选项。首次排查建议关闭自动重连，手动连接并确认状态，避免反复连接掩盖首次错误。本轮未发送测试命令。

## 4. M0.2实测记录与串口复验

本次用户提供的实物日志已包含FW=SBCG-M0.2的启动行及SEQ=1至21的心跳，TICK_MS从1007递增至21007，所有相邻差均为1000 ms、TX_ERRORS全为0。启动时报告HCLK_HZ=200000000，UID与当前板先前记录一致。发送验收通过；完整证据和限制见[M0.2报告](Milestones/M0_2_Serial.md)。以下步骤用于后续复验。

1. 在VS Code执行已有`STM32: Flash`任务，确认编程和校验成功。本轮未由助手执行烧录。
2. 打开SerialWeb，选ST-LINK VCP（此前枚举COM3），使用115200、8N1、无流控、UTF-8；确认界面确实显示已连接。
3. 保持串口连接，按板上复位键；先连接再复位才能可靠观察一次性的启动行。
4. 保存启动行及至少10条连续心跳，检查无乱码、SEQ递增、TICK_MS约每1000 ms增加、TX_ERRORS=0。再次复位后应重新出现BOOT且SEQ从1开始。

以下是格式示例，尚非实物接收记录；实际tick按启动耗时变化，UID以当前板为准：

```text
$BOOT,FW=SBCG-M0.2,HCLK_HZ=200000000,UID_W0=001A002D,UID_W1=30365105,UID_W2=36363631
$HEARTBEAT,SEQ=1,TICK_MS=1008,TX_ERRORS=0
$HEARTBEAT,SEQ=2,TICK_MS=2008,TX_ERRORS=0
```

固件行尾为CRLF。SEQ是发送尝试序号；TX_ERRORS为此前格式化/发送失败计数，不等同于网页接收成功。失败后不会连续重试，至下一周期再尝试。若页面已连接仍为空，先核对是否烧录了本次ELF、程序是否运行，再检查VCP路径，不能仅凭无数据认定串口参数错误。完整字段和测试边界见[M0.2记录](Milestones/M0_2_Serial.md)。RX命令和回显仍另立增量。

## 5. M0.2构建记录与固件边界

- 构建动作：`python -B Tools/Monitor/autodebug.py build`；9项编译/链接步骤完成，未见编译警告，RAM 2144 B、Flash 14784 B。
- Debug ELF：`build/Debug/SBCG.elf`；SHA256为`f77768c038b406b54530ac0dd2a45392ae0595249a44020351911c043bcf0352`。
- 本次构建报告：`Logs/AutoDebug/20261018T094517-373deb27/report.json`；5项应用主机行为测试通过。
- 先前纯初始化版本的SHA256为`2ce991e1b5e1a7876595724320e3feb346505fcfca387f9873ba1662f3d975cb`，原参数摘要为`Logs/SerialWeb/configuration-audit.json`，它不覆盖本次新增应用代码；本机页面日志也在该目录下，均由Git忽略。
- M0.2启动/心跳发送已按用户实物接收日志验收，所需CubeMX生成快照、UART驱动、应用及测试一并纳入功能提交；接收、回显、再次复位和冷启动不在本次通过范围。
- [M0.1报告](Milestones/M0_1_Startup.md)对应的是此前系统基线ELF，其哈希、断点地址及实物运行结论不能直接推广到本次新增USART1的ELF。

本页来源为用户指定网站的实际页面与本机代码/构建检查。工具进一步用法见页面中的[项目入口](https://github.com/Conductance-lab/SerialWeb)和工具手册链接。
