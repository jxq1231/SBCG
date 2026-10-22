# 开发、构建与调试工作流

<!-- doc-status: current-workflow; reviewed: 2026-10-23 -->

> 已核对当前代码及分版实测范围。现行状态只见[Project_State](Project_State.md)；本页早期实验按原日期理解，后续设计不等于已配置。

更新：2026-10-23。适用于现有SBCG GCC/CMake工程；硬件状态集中记录在[Project_State.md](Project_State.md)。用户已确认VS Code Build正常，本机不再有待复验的`$gcc`引用问题。

## 1. 环境与配置

用SBCG根目录打开VS Code。Python建议3.9及以上；CMake、Ninja、ARM GCC和Python须在VS Code继承的PATH中。烧录/调试还需OpenOCD、ARM GDB及ST-LINK。修改系统PATH后重启VS Code。

| 配置 | 实际作用 |
| --- | --- |
| `CMakePresets.json` | 已有Debug/Release预设；复用原有构建系统 |
| `.vscode/settings.json` | clangd读取`build/Debug/compile_commands.json`；GCC query-driver与clangd路径需按本机核对 |
| `.vscode/tasks.json` | 构建、环境检查、地址定位及独立硬件任务 |
| `.vscode/launch.json` | Cortex-Debug launch；使用`build/Debug/SBCG.elf`，保留本机GDB/OpenOCD路径 |
| `.vscode/openocd-stm32f746.cfg` | Flash、Run、F5共享；加载目标脚本后设1800 kHz、硬件复位下连接，要求NRST实际连通 |

三个编译任务内置完整GCC问题匹配器，不依赖C/C++扩展注册`$gcc`。匹配文件、行、可选列、warning/error及消息；Debug/Release相对路径按对应构建目录解析。代码提示由clangd承担；独立`sbcg-gcc`诊断不会覆盖clangd诊断。无源码位置的链接错误仍需查看终端。

## 2. VS Code任务

| 入口 | 行为 | 访问设备/写Flash |
| --- | --- | --- |
| STM32: Check Environment | 检查项目与工具，保存报告 | 否/否 |
| STM32: Configure | 配置Debug | 否/否 |
| STM32: Configure Release | 配置Release | 否/否 |
| STM32: Build（Ctrl+Shift+B） | 先配置，再Debug构建 | 否/否 |
| STM32: Build Release | 先配置，再Release构建 | 否/否 |
| STM32: AI Debug | 保留入口名；运行Python主机构建诊断 | 否/否 |
| STM32: Resolve Address | 用当前Debug ELF解释输入地址 | 否/否 |
| STM32: Test Host Tools | 执行Python行为测试 | 否/否 |
| STM32: Clean | 调用已有CMake clean目标，清除构建产物 | 否/否 |
| STM32: Flash | 构建后编程、校验、复位 | 是/是 |
| STM32: Run | 复位并运行已烧录固件 | 是/否 |
| F5：STM32F7: Debug | 构建、下载Debug ELF，运行至main | 是/是 |

F5是launch，不能当作“不写入固件”的连接入口。当前没有已配置的attach任务。构建与主机脚本不会隐式连接或烧录设备；设备操作按用户授权执行。

## 3. 命令行与报告

在工程根目录运行：

```powershell
python -B Tools/Monitor/autodebug.py doctor
python -B Tools/Monitor/autodebug.py build
python -B Tools/Monitor/autodebug.py build --preset Release
python -B Tools/Monitor/autodebug.py build --json
python -B Tools/Monitor/autodebug.py symbolize 0x08000100
python -B -m unittest discover -s Tools/Monitor/tests -v
```

ETH已生成时，构建后另运行`python -B Tools/Monitor/check_eth_layout.py`；Release追加`--preset Release`。该检查需要PATH中的ARM nm/readelf，仅检查ELF和.ioc，不连接设备，失败退出1。独立链接扩展、内存边界和RAM跨度说明见[M1.1内存记录](Milestones/M1_1_Ethernet_Setup.md#8-gcc内存布局修复记录)。

地址仅为格式示例，必须替换为与所用ELF一致的真实地址。symbolize不会采集设备异常或重建ELF；Thumb函数指针最低位查询前清零，报告保留输入值和查询值。Release当前为`-Os -g0`，可能只能得到函数名，没有源码行。

脚本默认每阶段超时120秒，可用`--timeout 240`调整；`--project-root`可指定工程。配置失败即停止，构建失败不接受旧ELF；构建成功还须存在非空`SBCG.elf`并记录SHA256。超时或中断只终止本次启动的进程树，不自动重试。

每次报告位于`Logs/AutoDebug/<UTC时间>-<随机标识>/`，包含`report.json`及本次实际执行阶段的日志。Logs由Git忽略。构建完成不证明固件已在设备运行。

| 退出码 | 含义 |
| --- | --- |
| 0 | 当前主机动作成功：ENVIRONMENT_OK、BUILD_OK或SYMBOLIZED |
| 1 | 配置/构建/工具失败，或缺少预期ELF |
| 2 | 参数错误 |
| 4 | 子进程超时 |
| 6 | 项目/依赖/启动/报告写入错误 |
| 7 | 无匹配ELF，或至少一个地址不能定位源码行 |
| 130 | 用户中断 |

主机报告始终包含`flash=NOT_RUN`、`runtime=NOT_RUN`和`hardware_verified=false`。这不否定独立硬件实验，而是说明该脚本只完成主机阶段。硬件结论单独存入Milestones，不把主机退出0改写成整机通过。

## 4. 已确认问题与解决方式

| 问题 | 当前处理及证据 |
| --- | --- |
| OpenOCD CPUID=0x1a、识别失败 | 原连接和单独降速均失败；硬件复位下连接在1800 kHz正常，CPUID=0x410fc271。用户随后提供烧录校验成功日志 |
| `$gcc`引用无效 | 三个任务改为独立内联定义；格式/路径检查通过，用户确认Build现在正常 |
| 请求速度与实际档位不同 | 当前日志中2000/8000 kHz分别回退至1800/4000 kHz，未阻止该次编程及校验；不是本次连接故障的修复判据 |
| M7勘误提示 | OpenOCD报告并声明相应处理；不通过屏蔽提示判断硬件通过 |

详细单板启动证据见[M0_1_Startup.md](Milestones/M0_1_Startup.md)：Flash加载段匹配、SystemCoreClock、主循环、tick和复位复验均有记录，结束时恢复运行。本轮文档整理不重复设备操作。

## 5. 验证边界与工具来源

初期10项主机行为测试、Debug/Release构建、clangd及地址定位已有当时通过记录；当前工具测试83项、Analysis61项（M3.2.2基线），本轮新增验证见当前里程碑；2026-10-18再次Debug构建和单板调试复位启动通过，随后用户确认VS Code Build界面正常。其它电脑的路径、驱动和扩展仍需按实际环境核对。

主机脚本参考[STM32_AutoDebug_Universal_Kit](https://github.com/TaoCosmo-Dev/STM32_AutoDebug_Universal_Kit/tree/8434f26644ba4c677dc7db489d3a9570db00cf71)的阶段化诊断思路，独立实现CMake/GCC适配。当前没有自动采集固件运行日志、异常现场或自动修复闭环；不能把脚本名称理解成这些功能已具备。

新增外设后，先明确当前独立目标和CubeMX变化，再构建、按授权测试、记录实物证据并提交。完整顺序见[Project_Roadmap.md](Project_Roadmap.md)，不要重新按已完成的旧实施计划执行。

## 6. 串口观察：SerialWeb

按用户指定使用[SerialWeb](https://conductance-lab.xyz/SerialWeb/)。本轮已在Edge打开并检查页面，Web Serial API可用；本机ST-LINK Virtual COM Port枚举为COM3。页面设置115200、8N1、无RTS/CTS流控、UTF-8，点击“连接设备”后由用户选择当前ST-LINK串口。

此前页面曾记录`SerialPort.open`失败，原因尚未追溯确定；用户现已回传BOOT及21条心跳，发送路径通过。若以后重现，关闭自动重连及其它占用同一端口的串口窗口，再手动连接。不要把网页能打开或端口已枚举当作收发通过。

USART1的PA9/TX、PB7/RX、PCLK2及生成参数已核对，Debug构建通过。本次启动信息/每秒心跳已接入，5项应用主机行为测试通过；用户实物日志已确认本版本启动信息和21条心跳接收正常；MCU接收/手动设温已在T1.2及后续音频联动验收；操作见SerialWeb页首。需要观察BOOT时先连接再复位；仅检查现有输出时无需重新烧录。具体操作、检查表及ELF边界见[SerialWeb.md](SerialWeb.md)。当前COM编号只代表这台电脑的枚举，不写死为所有人的端口。
