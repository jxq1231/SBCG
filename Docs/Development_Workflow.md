# SBCG开发与主机诊断工作流

更新：2026-10-16。当前固件仍处于M0.1，200 MHz生成配置已构建通过，实物启动待验证。本工作流不改变CubeMX配置。

## 1. 与参考方案的关系

参考[TaoCosmo-Dev/STM32_AutoDebug_Universal_Kit](https://github.com/TaoCosmo-Dev/STM32_AutoDebug_Universal_Kit/tree/8434f26644ba4c677dc7db489d3a9570db00cf71)的阶段化执行、退出码、结构化诊断及匹配固件的地址定位思路。本项目独立实现标准库Python工具，仍使用现有CMake/GCC、OpenOCD和Cortex-Debug；没有复制Keil构建、工程注入或自动修复模块，不依赖该仓库的安装脚本或Python包。

参考方案的完整闭环需要固件运行输出和明确的验收判据。当前SBCG没有USART业务输出、通过令牌或已验证的故障现场采集，因此这里仅实现主机诊断阶段，不声称具备自动上板验收。

## 2. 环境和配置

以SBCG根目录打开VS Code，按`.vscode/extensions.json`推荐安装CMake Tools、C/C++、clangd、Cortex-Debug。C/C++扩展保留GCC问题匹配器，代码提示由clangd负责，避免两个语言服务同时工作。

- CMake使用已有Debug/Release预设，不增加构建系统，不修改生成CMake文件。
- clangd显式读取`build/Debug/compile_commands.json`，保留本机已知编译器路径。使用Release构建不自动切换clangd数据库。
- GDB、OpenOCD和clangd保留现有本机路径；CMake/Ninja/GCC及Python须在VS Code继承的PATH中。修改系统PATH后应重启VS Code。
- 调试和烧录明确使用`SBCG.elf`，与CMake目标一致，不再依赖文件夹名称。
- 烧录、运行和F5共用`.vscode/openocd-stm32f746.cfg`：芯片脚本加载后设置初始1800 kHz，并使用硬件复位下连接。该设置已通过当前实物的连接、复位/暂停和寄存器读取验证，见第8节；未验证Flash写入。
- CubeMX的Trace Asynchronous Sw保留PA13/PA14/PB3；当前launch未开启SWO接收。

## 3. VS Code入口

通过“终端 → 运行任务”选择：

| 任务 | 行为 | 是否访问硬件 |
| --- | --- | --- |
| STM32: Check Environment | 检查项目文件和PATH工具位置，保存报告 | 否 |
| STM32: Configure | `cmake --preset Debug` | 否 |
| STM32: Build | 先Configure，再Debug构建；GCC错误可跳转源码 | 否 |
| STM32: Build Release | 先Configure Release，再Release构建 | 否 |
| STM32: AI Debug | 保留原入口名，执行配置/构建诊断并保存报告 | 否 |
| STM32: Resolve Address | 根据输入地址查询当前Debug ELF | 否 |
| STM32: Test Host Tools | 执行Python工具行为测试 | 否 |
| STM32: Clean | 先配置，调用现有CMake clean目标 | 否 |
| STM32: Flash | 先Debug构建，再调用原有OpenOCD烧录/校验/复位流程 | 是 |
| STM32: Run | 调用原有OpenOCD复位运行流程 | 是 |

F5选择`STM32F7: Debug`，先构建再启动现有Cortex-Debug/OpenOCD流程，下载Debug固件并运行到main。Flash、Run和F5由用户主动执行；工具验证仅运行前面的主机任务，不隐含执行这些硬件动作。

## 4. 命令行用法

脚本只依赖Python标准库，推荐Python 3.9及以上。默认根据脚本位置定位SBCG根目录，也可以通过`--project-root`明确指定包含SBCG.ioc及CMake文件的工程目录。

```powershell
python -B Tools/Monitor/autodebug.py doctor
python -B Tools/Monitor/autodebug.py build
python -B Tools/Monitor/autodebug.py build --preset Release
python -B Tools/Monitor/autodebug.py build --json
python -B Tools/Monitor/autodebug.py symbolize 0x08000100
python -B -m unittest discover -s Tools/Monitor/tests -v
```

上例地址只是输入格式示例，应替换为与当前ELF对应的真实PC或函数地址。符号解析不连接芯片、不采集异常现场；不能将输入地址或当前处理函数PC自动认定为故障指令。函数指针的Thumb最低位在查询前清零，报告同时保留原地址和查询地址。

脚本默认每阶段超时120秒，可用`--timeout 240`调整。超时或中断时终止本次启动的进程树，不自动重试。`doctor`检查工具是否在PATH中，不能证明ST-LINK连接或板上固件正常；GDB/OpenOCD/addr2line/clangd缺失作为可选工具信息列出，不阻止仅需CMake/Ninja/GCC的构建。symbolize必须有addr2line和非空ELF。

Release预设当前为`-Os -g0`，可能只能得到函数名，无法定位源码行；地址查询默认使用带调试信息的Debug固件。脚本不重新构建symbolize使用的ELF，必须由使用者保证地址来自该固件；报告中的SHA256可辅助比对。

## 5. 结果和日志

每次调用建立独立目录：

```text
Logs/AutoDebug/<UTC时间>-<随机标识>/
  report.json
  configure.log   # build动作
  build.log       # 配置成功后执行
  symbolize.log   # symbolize动作
```

Logs已由Git忽略。报告记录动作、预设、工具路径、各阶段命令/退出码/耗时/日志、GCC诊断及成功构建产物的SHA256。配置失败不再构建，构建失败不接受旧ELF为成功结果，找不到非空SBCG.elf也返回失败。

| 退出码 | 含义 |
| --- | --- |
| 0 | 请求的主机动作成功：ENVIRONMENT_OK、BUILD_OK或SYMBOLIZED |
| 1 | 配置/构建/工具执行失败，或成功构建后未找到非空ELF |
| 2 | 命令行参数错误 |
| 4 | 子进程超时 |
| 6 | 项目、依赖、进程启动或报告写入错误 |
| 7 | 地址定位没有匹配ELF，或至少一个地址无法定位到源码行 |
| 130 | 用户中断 |

所有报告始终写明`flash=NOT_RUN`、`runtime=NOT_RUN`、`hardware_verified=false`。退出码0只证明当前请求的主机动作完成，与参考工具完整闭环的TEST_PASSED不是同一契约。

## 6. 当前验收顺序

1. 运行环境检查和Debug构建，确认没有新的无法解释的警告。
2. 用户执行或明确授权烧录/调试后，按`CubeMX_Step1.md`第2.6节检查SystemCoreClock、uwTick和异常状态。
3. 记录实物结果后提交M0.1生成代码增量；主机工作流提交不代表硬件已验收。
4. 后续由CubeMX启用所需观测外设、定义验收日志及超时条件后，再单独实现串口或其他实物闭环；不在当前阶段预先注入固件逻辑。

## 7. 本增量验证记录

2026-10-16主机验证结果：

| 检查 | 结果 |
| --- | --- |
| Python行为测试 | 10项通过，包括真实子进程非零退出和超时终止；独立审查再次运行通过 |
| 环境检查 | 所需构建工具及列出的可选工具均在本机PATH找到 |
| Debug / Release | 通过脚本分别配置并构建成功；Release构建输出无警告 |
| clangd | 使用build/Debug数据库和现有query-driver检查Core/Src/main.c，退出0 |
| 地址定位 | 从Debug ELF查询main符号，成功定位Core/Src/main.c:66 |
| 配置与报告 | JSON及任务依赖有效；报告均保留NOT_RUN/false硬件状态；日志及Python缓存由Git忽略 |
| 烧录 / 调试 / 实物运行 | 主机工作流初次验收未执行；后续仅连接诊断见第8节，未烧录当前固件，不能据此验收M0.1 |

本机验证不代表其它电脑的工具安装、VS Code扩展运行或实际ST-LINK连接已验证；相关工具路径和实物操作按前文分别检查。

## 8. OpenOCD连接故障与修复记录

2026-10-16用户报告烧录前出现`Cortex-M CPUID: 0x1a is unrecognized`及`Unable to reset target`。构建正常，错误发生在目标识别阶段，尚未写入当前固件。

用户授权连接、复位/暂停和寄存器读取，明确暂不写入固件。使用本机xPack OpenOCD 0.12.0-7、ST-LINK V2J33M25对照测试：

| 连接设置 | 观察结果 |
| --- | --- |
| 原配置：速度设置在芯片脚本前，无硬件复位声明 | 复现CPUID=0x1a，识别失败；脚本将请求速度覆盖为2000 kHz，探针实际使用1800 kHz |
| 芯片脚本后设置1000 kHz，仍不声明硬件复位 | 实际950 kHz，仍以CPUID=0x1a失败；单独降速无效 |
| 1000 kHz，硬件复位下连接 | Cortex-M7 r0p1识别和复位/暂停成功 |
| 保留1800 kHz，硬件复位下连接 | 同样成功，CPUID=0x410fc271、DBGMCU_ID=0x10016449；读取后恢复运行 |

修复采用共享配置文件，防止Flash、Run和F5的速度、复位策略再次分离。其`reset_config srst_only srst_nogate connect_assert_srst`依赖板载ST-LINK到NRST的实际连接；不要将该配置未经检查复制到不接NRST的自制板。

芯片脚本的reset-start等事件仍可能请求2000 kHz，日志中显示探针改用1800 kHz属于支持档位提示，本次并非致命错误。Cortex-M7硬件勘误提示由OpenOCD输出，工具已声明相应处理；不通过屏蔽提示判断连接成功。

原始诊断日志保存在`Logs/OpenOCD/cpuid-20261016T034733/`，由Git忽略。连接验证只说明当前板卡恢复了调试访问：本次未烧录、未擦除、未修改选项字节，不能据此证明200 MHz ELF已在板上运行。下一步由用户重新执行STM32: Flash，再按CubeMX第一步指南记录启动验收结果。
