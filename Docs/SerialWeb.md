# USART1配置核对与SerialWeb使用

更新：2026-10-18。串口工具按用户指定采用[SerialWeb](https://conductance-lab.xyz/SerialWeb/)，作者为Conductance-lab。网址末尾不要包含中文逗号。

## 1. 本次检查结论

**USART1初始化配置已生成，参数正确，Debug构建通过；尚无串口发送/接收应用，未进行新固件烧录或串口收发实测。**

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

当前调用链为`main → MPU_Config → HAL_Init → SystemClock_Config → MX_GPIO_Init → MX_USART1_UART_Init → 空主循环`。无新增应用IRQ、回调或DMA缓冲。没有`HAL_UART_Transmit`、`HAL_UART_Receive`调用或实际printf输出逻辑；浏览器发送内容也不会自动获得回显。

## 2. 本机端口与页面

本机当前枚举为`STMicroelectronics STLink Virtual COM Port (COM3)`；COM编号可能随电脑、USB口或驱动变化，以设备名称及实际枚举为准。使用板载ST-LINK对应CN14 USB连接，无需另接USB转串口模块。

本轮已用Edge打开指定HTTPS页面，标题为“SerialWeb 网页串口调试与图形解析工具 | Conductance-lab”，页面显示在线版本v2.0。检查时`window.isSecureContext=true`、`navigator.serial`存在，页面提示“Web Serial API已就绪，等待连接”。这只证明网页具备连接条件，不代表COM3已经打开或板端发送已通过。

页面随后记录一次`NetworkError: Failed to execute 'open' on 'SerialPort': Failed to open serial port`，界面仍为“当前未连接”。网站提示串口可能被占用，但该提示不能确定实际原因，也不能证明是MCU串口参数错误。先关闭其它占用同一端口的串口工具，再手动选择ST-LINK VCP连接；本轮未成功连接或验证收发。

## 3. 连接步骤

1. 在Edge中打开[指定页面](https://conductance-lab.xyz/SerialWeb/)。首次显示版本信息时先关闭该弹窗，进入“监视”界面。
2. 核对参数：波特率`115200`，数据位`8`，停止位`1`，校验位“无”，关闭“RTS/CTS流控”。首轮无需切换DTR、RTS或BREAK。
3. 字符编码选`UTF-8`，文字观察时不勾选“十六进制接收”。“换行符设置”选`\r\n`；该选项配合“结尾补换行符”影响网页发送的内容，不会替固件接收输出补换行。后续固件文本应自行发送完整行结束符。
4. 点击“连接设备”，在浏览器设备选择窗口中选当前ST-LINK Virtual COM Port（本机为COM3），完成授权。
5. 使用“串口监视器”观察接收。首轮不要启用“定时发送”或“触发发送”；当前固件还没有命令解析逻辑。
6. 若提示端口被占用，关闭占用同一COM口的其它串口窗口后重连；不要仅因收不到数据就反复烧录。
7. 后续真正产生数据后，可使用页面的时间线录制/导出功能保存原始日志至本地`Logs/`；网页处理只用于开发观察，不承担正式测距计算。

页面支持“自动重连”“失焦自动断连”等选项。首次排查建议关闭自动重连，手动连接并确认状态，避免反复连接掩盖首次错误。本轮未发送测试命令。

## 4. 为什么现在可能没有输出

启用USART外设并调用`HAL_UART_Init`不会自动产生串口文本。当前主循环仍为空，新UART版本在本轮也未烧录。因此页面显示“已连接”但接收为空，不能单凭这一点判断PA9/PB7或115200设置错误。

下一独立目标是编写用户应用层的启动信息和限速心跳，再按授权烧录并通过SerialWeb验收：复位后收到启动版本/UID，每秒收到递增tick，无乱码、无持续阻塞。该逻辑尚未实现，也不能先将串口功能标记为完成。RX命令和回显另立可验证增量，不在本次配置检查中假定存在。

## 5. 本次记录与固件边界

- 构建动作：`python -B Tools/Monitor/autodebug.py build`；21项编译/链接步骤完成，未见编译警告。
- Debug ELF：`build/Debug/SBCG.elf`；SHA256为`2ce991e1b5e1a7876595724320e3feb346505fcfca387f9873ba1662f3d975cb`。
- 构建报告：`Logs/AutoDebug/20261018T093258-332234ee/report.json`。
- 参数检查摘要：`Logs/SerialWeb/configuration-audit.json`；页面观察记录保存在该目录下的本机浏览器工具日志，均由Git忽略。
- 当前CubeMX生成变化及新增UART HAL文件尚未作为已验证硬件功能提交；本轮仅提交文档，保留用户生成文件。
- [M0.1报告](Milestones/M0_1_Startup.md)对应的是此前系统基线ELF，其哈希、断点地址及实物运行结论不能直接推广到本次新增USART1的ELF。

本页来源为用户指定网站的实际页面与本机代码/构建检查。工具进一步用法见页面中的[项目入口](https://github.com/Conductance-lab/SerialWeb)和工具手册链接。
