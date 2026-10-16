# 协作开发指南

本指南用于两板声波测距项目的团队沟通。工程修改还须遵循[AGENTS.md](AGENTS.md)，硬件资源以[Hardware_Resources.md](Docs/Hardware_Resources.md)为约定。

## 分支与提交

- `master`：共享文档和已验证的软件增量；查看[Project_State.md](Docs/Project_State.md)了解固件验收状态，不把分支名当作硬件验证证明。
- `jxq/<topic>`：本仓库维护者的开发或审阅分支。一次分支聚焦一个目标；其他成员的前缀由团队约定。
- `jxq/m0-clock-review`：200 MHz/HSE Bypass生成快照，编译通过、实物待验收，保持草稿PR；不是M0.1功能完成提交。

建议开始顺序：

```powershell
git status
git fetch origin
git switch master
git pull --ff-only origin master
git switch -c jxq/your-topic
```

先保存已有工作，再切换分支。若本地与远程分叉，先检查差异并与队友沟通；不通过强推、重写历史或删除本地改动解决。

提交格式采用`<type>: <subject>`，例如：

```text
feat: add udp heartbeat communication
fix: correct signed timestamp calculation
docs: record clock startup observations
```

本仓库维护者提交身份为`jxq`，使用本人确认的邮箱；其他贡献者使用真实身份，不冒用他人身份，不添加工具生成署名。一次提交只包含本次增量，暂存前检查`git status`和`git diff`。

## 本机工具配置

当前`.vscode/settings.json`和`launch.json`保留维护者电脑上的绝对路径。队友需要检查：

| 文件 | 本机相关项 |
| --- | --- |
| `.vscode/settings.json` | `clangd.path`、`clangd.arguments`中的`--query-driver` |
| `.vscode/launch.json` | `gdbPath`、`serverpath` |
| 系统PATH | Python、CMake、Ninja、ARM GCC；烧录时还需OpenOCD |

这些路径不是板卡硬件配置。优先通过本机用户设置或个人工作区安排工具路径；当前launch/settings中的显式值仍可能覆盖用户级设置，必须实际核对生效值。若调整共享JSON，只提交团队确认的改动，不将其他人的安装路径直接替换成自己的路径。

修改PATH后重启VS Code，先运行`STM32: Check Environment`和Debug构建。无需运行旧的`setup-vscode.ps1`来创建编译数据库符号链接；当前clangd已直接读取`build/Debug`。

## CubeMX与共享资源

1. 保持STM32F746NGH6 / TFBGA216，团队统一CubeMX和CubeF7版本。
2. 更改`.ioc`前，在Issue或PR中说明引脚、时钟、DMA、NVIC的影响，避免两人同时改共享资源。
3. 只通过CubeMX修改硬件配置并生成代码；不要手改`.ioc`文本或生成初始化来解决冲突。
4. 生成后核查`.ioc`、`main.c`、MSP、中断及构建支持文件的实际差异，保留USER CODE区。
5. `.ioc`发生冲突时，由资源负责人在CubeMX重做并生成共同结果；不在冲突标记之间随意拼接参数。
6. 应用层统一通过逻辑资源访问硬件。ETH/PTP时间、TIM2 PPS、PLLSAI显示时钟和共享温度模型不得分别重复管理。

## 提交PR需要的证据

至少分别填写：

| 验证层次 | 记录内容 |
| --- | --- |
| 主机测试 | 执行命令、结果；日志可摘要，原始数据留在Logs |
| 固件构建 | Debug/Release、工具链及新增警告 |
| 烧录 | 是否执行、板卡、使用的固件提交或ELF校验值 |
| 实物运行 | 观察变量、日志、示波器结果、测试条件和预期 |

主机工具或纯文档改动可将硬件验证写为“不适用”。硬件功能缺少实测时使用草稿PR，并明确尚缺哪些观察结果；不标记为功能完成。CI的Host checks仅验证Python主机工具，不验证固件构建、引脚接线或测距精度。

每次PR只实现当前里程碑的一个独立目标。两板身份、温度版本、时间单位、字节序和测量快照必须在相关实现PR中明确。

## 项目沟通与实验记录

- 开发任务：使用“开发任务”Issue模板，填写目标、范围、资源和验收条件。
- 运行问题：使用“问题与实验记录”模板，提供复现步骤、板卡信息及实际结果。
- 合并讨论：使用PR模板列出差异、验证证据及未完成事项。
- 持久结论：写入`Docs/`，项目进度更新`Docs/Project_State.md`；原始串口/网络日志保存在忽略的`Logs/`。
- 请勿提交build目录、固件二进制、编译数据库、临时日志或凭据；第三方驱动原有许可证保留。

分工由团队在Issue中认领，当前不预填负责人姓名、UID、COM号或硬件测试结果。
