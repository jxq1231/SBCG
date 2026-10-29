# M4.3 双板事件链阻断修复

<!-- doc-status: implementation-complete-software-tests-pass; reviewed: 2026-10-27 -->

**范围：** 用户确认本轮仅修复三个已复现阻断。基线 `26c511e`；原有
`Drivers/BSP/Components/lan8742/LICENSE.txt` 删除保留，不混入提交。

**目标：** 移除正常两板发布时间差、当前音频块尾截短和过期应用测试桩造成的软件阻断。
这不表示真实标准声识别、同步精度、测距精度或两板实物验收通过。

**架构：** 维持现有前台调度和 Begin/Stage/End/Complete 所有权边界。
使用统一软件时间预算及有界 CPU 自有分类窗；不保存 DMA 样本指针。
不修改 `.ioc`、生成初始化、引脚、时钟、DMA、NVIC、缓存或相关门限。

## 1. 配对等待与资格撤销

- [x] 在真实 UDP 模块测试中先复现 500～2999ms 发布延迟漏配。
- [x] `App/Detector/sound_event_timing.h` 统一模型等待2500ms和网络/调度余量500ms。
- [x] `sound_event_time.c` 与 `App/Network/udp_sound_event.c` 共用预算；配对等待3000ms。
- [x] 保持40ms声学时差窗口、4条队列和歧义拒绝规则。
- [x] UDP新增当前有效域同步接口；撤域/换域清待配对项及未消费结果，保留累计统计。
- [x] `App/app_main.c` 在声音发送/配对前同步资格；结果回调再校验当前共同域。
- [x] 测试覆盖双方先后顺序、3000ms到期、计数回绕、撤域后同令牌恢复、换域和旧项隔离。
- [x] 运行 `Tests/Udp/run_udp_sound_event_tests.py`、`Tests/Measure/run_tests.py`、
  `Tests/SoundEventTime/run_tests.py`、`Tests/App/run_app_udp_integration.py`，构建并审查后提交。

## 2. 完整分类窗跨块交接

- [x] 新增真实检测服务测试，先复现同一50ms chirp在块尾误判。
- [x] 在 `App/Detector/sound_detector_service.c/.h` 使用固定2400帧已验证历史和
  Stage工作区，仅完整2400帧才调用原分类器；不足时有界等待后继块。
- [x] `cmake/linker/stm32f746_audio.ld` 增加CPU独占NOLOAD `.SoundClassSection`：
  两个2400帧PCM16缓冲共9600字节，从 `0x20024400` 起保留10KiB，堆移至实际段末；
  不放入接近上限的DTCM，不与DMA共享，不添加缓存维护调用。
- [x] `Tools/Monitor/check_audio_layout.py` 及 `tests/test_audio_layout.py` 验证新段、
  对象/边界符号、堆起点及与已有音频/捕获/静态区的隔离；保留原有布局检查。
- [x] 元数据、PCM尾部和分类结果均在真实 End 成功后提交；拒绝/断流/换会话清旧历史。
- [x] 未就绪队首不越序；等待也受原1秒候选年龄限制，超龄明确计入丢弃。
- [x] 不改变分类阈值、TOA估算算法和现有TOA完整窗/MISSING语义，不补零、不拼接不连续PCM。
- [x] 覆盖单块/跨块相位、跨块确认、双声道、End拒绝、gap/session/超龄以及队列边界。
- [x] 适配旧测试的候选等待时序，保留其所有权/TOA边界断言；运行检测/音频/事件时间回归，
  Debug/Release及布局检查，独立审查后提交。

## 3. 应用回归接口

- [x] 重新运行 `python -B Tests/App/run_tests.py` 并记录缺符号失败。
- [x] 在 `Tests/App/test_app_sync.c` 补齐真实接口对应的快照和时间域测试桩，
  验证角色、资格输入、初始化/处理顺序；不得删除失败用例或关闭功能宏。
- [x] 复跑旧应用回归与真实App结果交换边界测试，审查后单独提交。

## 验证及交付边界

- 测试使用真实业务模块，硬件/网络IO仅在现有主机运行器中替换。
- RED/GREEN新证据位于忽略目录 `Logs/Detector/Event_Blockers_20261027/`；不改写旧声学采集。
- 每个独立软件增量由 `jxq` 提交；不推送、不改写历史，不烧录。
- 首次2秒出数契约、现场标定、击掌测距、标准声身份及物理精度另行实现/实测。
