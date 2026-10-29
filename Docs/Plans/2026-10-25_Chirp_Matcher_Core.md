# M4.2.1 扫频匹配核心实施计划

<!-- doc-status: archived-plan; reviewed: 2026-10-25 -->

本计划的软件范围已实现并通过验证，见[软件验证记录](../Milestones/M4_2_1_Chirp_Matcher_Core.md)。板端接入、耗时与声学验收不在本轮完成范围，不重复实施本计划。

目标：按用户确认的上一轮方案，实现不依赖HAL的流式模板匹配核心，并在电脑上验证；不接入板端，不烧录。

## 接口与算法约定

- 新建 `App/Detector/chirp_matcher.h/.c`：调用方拥有状态及2400点定长环形缓冲；逐帧输入PCM16、非零session、uint64帧号。模板由调用方提供且整个使用期保持只读有效。无堆分配，无IRQ/日志/PTP依赖。
- 新建 `App/Detector/chirp_template.h/.c`：从已冻结的WAV逐样本导出2400点及SHA256。导出脚本 `Analysis/export_chirp_template.py` 支持只校验模式，不重新合成声音。
- 用去直流归一化相关：C=N·Σ(x·t)−Σx·Σt，Vx=N·Σx²−(Σx)²，Vt同理；评分 |C|/sqrt(Vx·Vt)。整型累加使用64位，乘积转double后开方；输出评分0..1并保留反相标记。
- 实验默认配置：AC RMS门限64 PCM、相关门限0.80、首次越阈后再观察48帧取最高评分、去重24000帧。均为可复制配置，不是实测最优值。截幅只标记，不静默当作高质量测量。
- 事件包含session、模板起始帧、模板末帧、确认帧、评分、AC RMS、反相/截幅。起始帧是匹配窗口原点，不是串口/DMA回调时刻；反射环境中的最高峰不保证直达声。
- session变化、重复/跳帧/倒序会清空窗口和待定峰，消费当前样本并返回DISCONTINUITY。session0清流并拒绝；同会话UINT64_MAX后拒绝直到显式Reset或新session。空输出不消费状态。
- 不足完整模板或缺少48点观察尾部不发布事件；Reset不发布待定候选。分块调用本身不重置状态，所以可跨8192帧边界。
- 本版是逐样本直接相关的正确性基线，最坏O(N)每帧、double数学；不宣称满足STM32实时预算。板端接入前另做耗时、采样率适配和DMA归还安全验证。

## 执行步骤

- [x] 先建头文件、`Tests/ChirpMatcher/test_chirp_matcher.c`、主机桥接和运行器。缺失实现时用明确的失败替身编译并实际运行，确认因Init失败而红灯，不把缺文件/编译失败当行为测试。
- [x] 实现最小流式核心，执行 `python -B Tests/ChirpMatcher/run_tests.py`，以原生GCC的O0/O2及严格警告核验。
- [x] 导出固定模板并增加 `Tests/ChirpMatcher/reference_check.py`：实际WAV的10次事件、幅度/直流/反相、跨块、缺尾、静默、单音、反向扫频、固定种子噪声；与独立NumPy/SciPy相关及精确窗口公式核对。记录数据集范围，不推断物理识别概率。
- [x] 运行已有SoundDetector、SoundDetectorService、SoundEventTime测试；使用现有CMake Debug/Release构建检查旧固件。新增核心未注册到固件，主机构建通过不能当作ARM运行通过。
- [x] 复核差异、记录软件证据与未验收项，更新文档入口/当前状态；仅定向提交本增量，不混入已有PPS/生成文件改动。

## 资源与验收边界

No CubeMX change required。无GPIO/TIM/ETH/SAI/I2C/LTDC、IRQ、DMA或缓存变更。现有采集认领/暂算/归还/提交调用链不变。这里仅验证纯C数字输入输出；实机播放、时延精度、静默误触发、多径、另一设备及两板测距均未验收。
