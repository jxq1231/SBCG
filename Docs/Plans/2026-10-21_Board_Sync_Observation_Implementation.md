# M2.2.4 实施清单

<!-- doc-status: archived-plan; reviewed: 2026-10-23 -->

> **已完成的历史方案，请勿重复实施。** 仅保留当时范围、授权和生成检查；其中“当前/下一步/待验”均按原日期理解，不构成新授权。验收见[对应报告](../Milestones/M2_2_Board_Observation.md)，现行入口见[文档导航](../README.md)。

日期：2026-10-21。用户已批准设计并要求实施。基线：b246fba；实施前工作区干净。

唯一目标：当前板运行四时间戳整数估算，30组主循环观测与电脑真实C参考逐条一致；保持LOCAL、SYNC=0。

## 文件和资源

按同目录 Board_Sync_Observation_Design 的文件表实施。新增 Sync 模块及其测试；网络层扩展type7；应用负责Init/Process；顶层用户CMake注册源码；电脑工具增加显式模式；协议、状态和验收文档记录结果。

No CubeMX change required。复用现有ETH内部DMA和硬件快照；GPIO、TIM、DMA配置、NVIC、SAI、I2C、ADC、LTDC、Cache及MPU不变。Core、HAL、生成胶水和旧PTP模块不改。

## 集成约定

- `SyncObservationInput`复制sequence、client_tick_ms、uid_words[3]、session_hi/lo、原created_ms和SyncExchange。
- `SyncObservation_Init(now_ms, log)`清队列和统计；`Submit(input)`只复制；`Process(now_ms)`计算、格式化和限速输出。
- 队列4项，TTL=2000ms，窗口800000000ns由Sync头统一定义。每次Process至多一项，结果间隔至少1000ms；初始化后首项可立即处理。到期输入输出STATUS=6且无数值。
- 网络缓存区分REPORTED、SUBMITTED和失败DONE。只有REPORTED可首次提交；队列满时保留REPORTED，可由显式新提交再领取，不自动重试。
- `$OBS_RX`统计ACCEPTED/BAD/FOREIGN/NO_PAIR/EARLY/DUPLICATE/CONFLICT/QUEUE_FULL，旧TS_IO/TS_PAIR字段及计数含义保持不变。
- `$SYNC_OBS_STATS`统计QUEUED/CALC_OK/CALC_ERR/EXPIRED/QUEUE_FULL/FORMAT_ERR。成功结果只输出本次计算的两个有符号整数分子；错误结果不附带旧值。
- type7长度52、大端，t1导出tick必须匹配；只保存电脑原t1和首个有效ACK的t4，不为提交帧申请硬件时间戳。

## 检查点

- [x] 读取规则、设计、相关源码及构建入口，保护452份源文件和原ELF哈希。
- [x] 网络type7及状态领取：测试先失败，再实现通过。
- [x] Sync队列、真实C计算、过期及整数文本：测试先失败，再实现通过。
- [x] 主循环接线、电脑显式提交和默认兼容：测试先失败，再实现通过。
- [x] 原219项及新增回归；独立规范审查后进行质量审查。
- [x] Debug/Release构建、ETH布局、PTP统一宏与最终ELF保留估算函数。
- [x] 实际Generate Code及源码逐字节回归，必要时仅恢复被删除的原LAN8742许可证。
- [x] 审阅新Debug ELF及SHA256，另行取得本版本烧录授权。
- [x] COM3/两路旧UDP/新模式联合实物验收，30组与原生C一致。
- [x] 验证后按jxq提交、推送、校验远程，停止本增量。

原始证据存放忽略目录 `Logs/M2_2_Board_Observation/`。完成软件构建不等于实物通过；新固件未经本版本授权不烧录。

软件检查结果：248项PASS；实际再生成457文件不变（原样恢复LAN8742许可证）；Debug SHA256前缀5afe448fa408。本版本获明确授权烧录一次，Flash/Runtime PASS；30组与真实C逐条一致，旧网络70包/30组和90条PTP/心跳通过；功能提交8ecaea3已推送到jxq/m0-clock-review并核对远程一致，本增量停止。
