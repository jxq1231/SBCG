# B板瞬态同步故障恢复

日期：2026-10-30。当前增量仅修复同步协调器一次故障后的软件锁存，暂不宣称现场 NOT SYNCED 的唯一根因已确定。

## 目标与边界

B板故障后每至少1000ms检查一次；只有TX空闲、fine时钟配置和已确认addend匹配、没有未决命令且本地读时成功，才撤销旧消费者并以实际已确认频率重新获取。恢复本身不写时钟、不恢复旧偏差或旧距离，新的合格四时刻才能重新取得同步资格。保留累计诊断计数。

实际硬件更新超时会由既有BSP关闭PTP时钟；这类状态继续FAULT，不自动初始化时钟或复位MCU。恢复检查失败不占用额外等待循环，下一次至少间隔1000ms。A不执行B恢复流程。保持原2秒HOLDOVER、5秒UNSYNCED、同步伺服参数及协议隔离窗口。

## 修改文件

- `App/BSP/bsp_ptp.c/.h`：新增只读ReadKnownFrequency接口，复用现有fine配置/请求位检查和本地读时。
- `App/Sync/time_sync.c/.h`：FAULT恢复节拍、旧消费者清理、实际频率重新获取及独立恢复日志；维护接口说明。
- `Tests/Ptp/test_bsp_ptp_fine.c`：健康检查的正反例及零寄存器写入证明。
- `Tests/SyncController/test_time_sync.c`：先复现原锁存，再检查恢复、重试间隔、旧交换丢弃、角色和计数。
- `Tests/PpsIntegration/test_controller_pps.c`：恢复后等待新Observe，不恢复旧PPS资格。
- `Tests/SoundEventTime/test_clock_time.c`：恢复前撤销旧声音模型。
- 本计划及对应验证记录：区分软件验证与后续双板实机验证。

**No CubeMX change required。** 不改生成区、HAL、GPIO、TIM、NVIC、DMA、ETH/SAI/LTDC配置或cache；BSP新增接口只读取现有寄存器。不覆盖当前标定/UI未提交增量或其他用户文件。

## 执行与验收

- [x] 先修改协调器测试，将原“读时恢复后仍永久FAULT”期望改为1000ms后ACQUIRING，运行`python -B Tests/SyncController/run_tests.py`记录断言失败。
- [x] BSP子任务先添加健康检查断言并记录失败，再实现只读接口；运行`python -B Tests/Ptp/run_tests.py`。
- [x] 实现恢复入口；执行协调器、PPS交接、声音模型、UI同步快照、纯伺服与板间协议回归。
- [x] 沿现有Debug/Release预设构建并检查音频/ETH/TouchGFX布局。
- [x] 独立审查差异；记录确切镜像SHA256和软件证据，不将主机替代视为实物同步验收。结果见`Docs/Milestones/Sync_Recovery_2026_10_30.md`。
- [x] 用户于2026-11-01要求“测试”，依AGENTS第20节仅烧录B并双板采集：正常跟踪→B网线中断约9.6秒→无需复位恢复TRACKING/共同域，追加90秒稳定观察。无需播放声音。
- [ ] FAULT恢复分支的实机触发与恢复尚未验证；本次断网只有UNSYNCED，没有FAULT，不能混为一项通过结论。
- [ ] 实机通过后，只定向提交本增量，作者/committer为jxq；不带入未提交标定，不push。

## 现场前置事实

只读观察时电脑仅COM3可用，A连续25秒报告ETH LINK=DOWN；电脑有线网卡Disconnected，B串口未接入。用户另报告B曾从TRACKING退回NOT SYNCED且需复位。代码和旧日志表明UNSYNCED本身可重新获取；必须与FAULT锁存及持续缺失硬件四时刻分开验证。
