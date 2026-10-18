# M0.2 USART1启动信息与心跳

状态：M0.2启动信息与心跳发送验收通过（用户提供SerialWeb实物接收日志）；MCU接收未实现。更新：2026-10-18。

## 目标与设计

沿用已由CubeMX生成的USART1（PA9/TX、PB7/RX、115200 8N1、无流控）。复位后发送一条含固件标识、HCLK和UID的启动行，随后每1000 ms尝试发送一条心跳。只推进当前板，接收命令、回显、网络及声音功能不在本步范围内。

本步采用带25 ms超时的轮询发送；它不依赖UART IRQ/DMA，便于当前阶段观察。IT发送需要新增UART中断，DMA发送需要新的硬件配置和缓冲所有权，均留待实际实时需求出现时评估。当前短文本的阻塞发送不能当作未来声音采集和同步路径的通用日志方案。

`App/app_main.c/.h`负责文本和调度；`App/BSP/bsp_console.c/.h`集中映射huart1、HAL tick、HCLK及UID读取。`main.c`仅在USER CODE区域调用`App_Init()`和`App_Process()`。顶层CMake文件明确声明只生成一次、允许用户修改，使用其中现有用户源文件/包含目录槽位；不改生成的cmake/stm32cubemx文件。

No CubeMX change required。无新增IRQ、DMA、缓存维护或硬件资源配置。

## 本次文件范围

| 路径 | 本次修改原因 |
| --- | --- |
| `App/app_main.c`、`App/app_main.h` | 新增应用初始化、启动行与每秒心跳 |
| `App/BSP/bsp_console.c`、`App/BSP/bsp_console.h` | 新增逻辑串口接口，集中映射USART1及HAL核心服务 |
| `Core/Src/main.c` | 在USER CODE Includes、2、3区接入App，保留用户已生成的UART初始化 |
| `CMakeLists.txt` | 在既有用户扩展槽位注册两个应用源文件及App包含路径 |
| `Tests/App/test_app_main.c`、`Tests/App/run_tests.py` | 新增应用主机行为测试与执行入口 |
| `Docs/Milestones/M0_2_Serial.md`、`Docs/SerialWeb.md` | 保存实施/验证记录、字段定义与实物验收步骤 |
| `Docs/Project_State.md`、`Docs/Project_Roadmap.md` | 更新当前增量和下一待验收动作 |
| `Docs/Development_Workflow.md`、`Docs/Hardware_Resources.md`、`Docs/CubeMX_Step1.md` | 修正“尚无发送逻辑”的旧描述，保留历史实物结论的版本边界 |
| `Docs/README.md`、`README.md` | 更新文档入口和进度摘要 |

用户此前改动的`.mxproject`、`.ioc`、HAL配置/驱动、MSP和生成CMake文件均未由助手手改；相对前一Git提交的这些差异属于用户已生成的UART配置。本次用修改前哈希和main生成区对比确认了保护边界。发送实测通过后，将所需生成配置、UART驱动与应用一起纳入本功能提交，保证队友检出后可构建。

本功能提交同时包含用户生成的`SBCG.ioc`、`.mxproject`、`Core/Inc/stm32f7xx_hal_conf.h`、`Core/Src/stm32f7xx_hal_msp.c`、`cmake/stm32cubemx/CMakeLists.txt`，以及UART驱动`Drivers/STM32F7xx_HAL_Driver/Inc/stm32f7xx_hal_uart.h`、`stm32f7xx_hal_uart_ex.h`、`stm32f7xx_ll_usart.h`和`Drivers/STM32F7xx_HAL_Driver/Src/stm32f7xx_hal_uart.c`、`stm32f7xx_hal_uart_ex.c`；这些是已验收USART1初始化的依赖。

## 实施清单

- [x] 为启动行、1000 ms边界、uint32 tick回绕、延迟后不补发、发送失败后限速恢复编写主机行为测试；缺少实现时启动输出断言失败，接入后5项通过。
- [x] 实现BSP接口与应用层；固定长度缓冲，检查格式化和HAL发送返回值，失败计数、不死循环重试。
- [x] 在main的USER CODE与顶层CMake用户槽位接入；保留用户已有CubeMX生成改动。
- [x] 运行主机行为测试及现有Debug构建，检查生成区与.ioc未被本次修改。
- [x] 更新SerialWeb操作及项目状态，记录本次ELF哈希和构建报告。
- [x] 用户提供启动行及21条连续心跳，满足至少10条的发送验收；不据此声明MCU接收、第二次复位或冷启动通过。

主机行为测试命令为`python -B Tests/App/run_tests.py`，使用本机原生GCC执行真实应用C代码和替代BSP；不访问硬件、不编译固件、不替换现有CMake构建系统。固件仍仅通过`python -B Tools/Monitor/autodebug.py build`构建。

## 验收约定

连接SerialWeb后复位，观察`$BOOT`与`$HEARTBEAT`。固件ASCII文本是UTF-8子集，行尾为CRLF。HCLK_HZ单位为Hz；UID_W0/W1/W2各为8位十六进制的uint32 UID字；SEQ、TICK_MS、TX_ERRORS为十进制uint32，TICK_MS单位为ms。

SEQ表示心跳尝试序号，失败也递增。TX_ERRORS统计此前格式化/发送失败次数，因此当前行发送失败会在下一条成功行中体现；不代表USB/网页已收到数据。HAL成功仅代表UART发送完成。tick和计数按uint32回绕；超过周期后只发送一次，并从当前tick重新计时，不补发积压日志。需至少每个tick回绕周期内调度应用。

开启页面时间戳能观察大致秒级间隔，但浏览器与USB批量接收不是测频仪器。验收要求无乱码，SEQ连续增加、TICK_MS正常增加约1000、TX_ERRORS=0，且复位后重新出现启动行。先打开网页连接再复位，以免错过启动信息。

助手硬件权限仍仅含连接诊断、不写Flash。本步由用户完成板端操作并回传接收结果，助手未重新烧录。M0.2发送目标验收后停止，不自动开始下一里程碑。

## 本次主机验证记录

- 主机行为测试：`python -B Tests/App/run_tests.py`，5项PASS；仅替代BSP，不验证HAL或引脚电平。
- 固件构建：`python -B Tools/Monitor/autodebug.py build`，9项编译/链接完成，未见编译警告；RAM 2144 B、Flash 14784 B。
- 构建报告：`Logs/AutoDebug/20261018T094517-373deb27/report.json`。
- ELF SHA256：`f77768c038b406b54530ac0dd2a45392ae0595249a44020351911c043bcf0352`。
- 调用链：`main → App_Init → BSP_Console_Write → HAL_UART_Transmit`；循环为`main → App_Process → BSP_Console_Write → HAL_UART_Transmit`。
- IRQ：沿用SysTick_Handler → HAL_IncTick，主循环读tick；不在ISR中发送，无新IRQ或回调。
- DMA/Cache：无新DMA、共享缓冲或缓存维护；发送栈缓冲在HAL返回前消费完。
- 再次构建复查：`Logs/AutoDebug/20261018T094942-ee366d21/report.json`，BUILD_OK，ninja无需重新编译，ELF哈希未变；5项应用主机测试再次通过。
- Flash：助手NOT_RUN；用户已提供该版本板端启动与输出日志，未附本次编程校验终端日志。Runtime：PASS（用户观察的启动信息和UART发送）。
- Git：实物发送证据满足本增量要求，复查相关配置、代码、测试及文档后，以jxq提交本功能；提交标识以Git历史为准。
- 格式检查：自有代码与文档的暂存差异检查通过；5个CubeMX复制的原版UART驱动文件各有一个文件末尾空行提示。为保留第三方驱动原样，未手工整理这些文件；该提示不是编译警告。

## 用户实物发送结果

来源：用户于2026-10-18粘贴的网页串口接收文本，包含1条BOOT和21条HEARTBEAT。以下为原记录节选，不是模拟输出：

```text
$BOOT,FW=SBCG-M0.2,HCLK_HZ=200000000,UID_W0=001A002D,UID_W1=30365105,UID_W2=36363631
$HEARTBEAT,SEQ=1,TICK_MS=1007,TX_ERRORS=0
$HEARTBEAT,SEQ=2,TICK_MS=2007,TX_ERRORS=0
$HEARTBEAT,SEQ=21,TICK_MS=21007,TX_ERRORS=0
```

逐条核对全部21条：SEQ为1至21，无缺号；TICK_MS为1007至21007，20个相邻差均为1000 ms，覆盖20 s的HAL tick跨度；所有TX_ERRORS=0，UID与此前只读寄存器结果一致。文本无可见乱码。由此确认当前板MCU TX→ST-LINK VCP→网页接收路径及启动/心跳应用通过本次验收。

日志中的独立`RX`是网页的接收方向标识，表示PC收到数据，不表示MCU已经实现RX接收或命令回显。HCLK_HZ是软件报告值，tick差是固件时基值，不是独立绝对测频或双板同步精度。用户未提供再次复位、冷启动或持续长时间运行记录，均不纳入本次通过范围。

完整粘贴文本保存在本机忽略目录`Logs/M0_2/user-serial-20261018.txt`，解析摘要为`Logs/M0_2/runtime-summary.json`；上述可复核摘要保存在本报告中用于团队协作。此前网页打开串口失败是历史事件，本次接收成功，原因未追溯确定，不继续把它列为当前阻塞。

用户验收操作与输出示例见[SerialWeb指南](../SerialWeb.md)。
