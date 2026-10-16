# SBCG开发工作流实施计划

当前范围：M0.1的主机工具支持，不推进下一固件里程碑。

目标：复用现有CMake预设和OpenOCD任务，使环境检查、构建及诊断结果可从VS Code和命令行获取。

设计依据：用户已要求采用前轮配置比较中的改进，并参考STM32_AutoDebug_Universal_Kit的阶段化诊断思路。参考版本为8434f26644ba4c677dc7db489d3a9570db00cf71；独立实现CMake/GCC适配，不导入Keil工程修改和固件注入功能。

## 文件与职责

- `.vscode/tasks.json`：Configure → Build依赖、GCC错误定位、Debug/Release构建、环境检查、构建报告、地址定位；保留独立烧录/运行入口及原有1800 kHz参数。
- `.vscode/settings.json`：保留现有用户设置，显式指定build/Debug编译数据库；记录setting.json到settings.json的重命名。
- `.vscode/extensions.json`：推荐CMake Tools、C/C++、clangd、Cortex-Debug。
- `.vscode/launch.json`：明确SBCG.elf和器件，保持现有GDB/OpenOCD路径、复位策略和main入口。
- `Tools/Monitor/autodebug.py`：仅依赖Python标准库，实现doctor、build、symbolize命令；命令分阶段记录日志、退出码和JSON报告。
- `Tools/Monitor/tests/test_autodebug.py`：覆盖失败短路、缺少工具/产物、超时、Windows GCC定位及无法解析地址；不依赖实物。
- `Docs/Development_Workflow.md`：用户操作入口、结果含义、验证方法及限制。
- `.gitignore`：补充Python缓存忽略，复用已有Logs忽略规则。

## 实施与验证

- [x] 先写脚本行为测试，运行`python -B -m unittest discover -s Tools/Monitor/tests -v`，确认因脚本缺失失败。
- [x] 实现doctor/build/symbolize；build按顺序调用`cmake --preset Debug`及`cmake --build --preset Debug --parallel`（Release同理），前一步失败即停止。
- [x] 构建成功还须存在非空ELF，报告记录SHA256；构建结果始终与硬件验收分离。
- [x] 所有脚本子进程有超时；超时终止该次进程树，不重复尝试，不启动OpenOCD或连接目标板。
- [x] 更新VS Code配置并补充操作文档，任务继续使用已有预设。
- [x] 运行行为测试、doctor、Debug/Release真实构建及Debug ELF地址定位，核对任务依赖、路径和报告。
- [x] 检查Git差异，确定本次提交范围及jxq身份；现有.ioc、main.c和.mxproject改动排除在本次提交之外。

执行结果及限制见`Docs/Development_Workflow.md`第7节。独立审查未发现需修复项，重新运行10项测试通过；当前固件的实物启动验收仍未执行。

## 验收边界

脚本退出0只表示请求的主机动作成功，报告必须包含flash=NOT_RUN、runtime=NOT_RUN及hardware_verified=false。当前没有UART验收输出，不提供全自动上板闭环或自动修复。地址定位仅解释与ELF一致的已知地址，不证明该地址是异常现场。烧录和调试需通过独立任务由用户执行或另行明确授权；本次不做实物操作。
