# M4.3.1 固定窗口TOA接入本地PTP模型（软件完成）

<!-- doc-status: m4.3.1-software-complete-hardware-pending; reviewed: 2026-10-26 -->

## 结论

M4.3.1软件增量已完成并提交：`94d1881 feat: add Q16 window TOA local PTP event time`。

- 检测服务保存的`anchor + window_position`被拆成**零基绝对帧floor + Q16子帧**，在真实End成功后才随候选提交；
- `SoundEventTime`新增窗口TOA路径，调用`AudioTimeMap_EstimateQ16`得到本地PTP模型时间/上下界；
- 旧`SOUND_TIME`（FIRST+1）语义、旧测试和旧分析器保持兼容；新增独立`$SOUND_TOA_TIME`与`$SOUND_TOA_STATE`；
- TOA缺失/非法明确失败，不产生模型时间，**不用FIRST+1冒充TOA成功**；
- 未改TOA估算器、门控、0.80、CubeMX、时钟/DMA/GPIO/NVIC/缓存，未烧录。

## 实现边界

| 项 | 处理 |
|---|---|
| 坐标 | Q16 `frame`为模型进度；`fraction_q16`为1/65536帧；n→n+1只加一次 |
| Q16=0 | 与旧整数`AudioTimeMap_Estimate`逐项一致 |
| 边界 | 针对量化后Q16坐标；一LSB分辨率单独说明，不并入LO/HI，且不是物理精度 |
| 数值 | NaN/Inf/负数/越窗/绝对帧溢出拒绝；商余分解，不使用absolute_frame<<16或128位 |
| 交接 | Stage仅在当前块完整256帧估算；End成功才入队；只传标量副本，无PCM指针 |
| 失效 | 复用CLOCK/STREAM/ANCHOR/SESSION/ORDER/GAP/STALE/TIMEOUT |
| 日志 | 旧`$SOUND_TIME`不变；新行<256字节；新状态1Hz；日志队列复用32行 |
| B板 | 未改变time_sync/PPS用户改动；调频/相位仍先失效旧模型；本地模型不证明跨板同步 |

## 软件验证

已运行（均无硬件）：

- `python -B Tests/Audio/run_tests.py` → 0 failed；
- `python -B Tests/SoundEventTime/run_tests.py` → 旧event/reference + 新Q16两优化级别PASS；
- `python -B Tests/SoundDetectorService/run_tests.py --phase GREEN` → O0/O2旧服务/TOA边界/新handoff PASS；
- `python -B Tests/App/run_tests.py` → PASS；
- `python -B Tests/ChirpService/run_diagnostics_tests.py` → PASS；
- `cmake --build --preset Debug` / `Release` → PASS（仅既有RWX链接警告）；
- `python -B Tools/Monitor/check_audio_layout.py` → `AUDIO_LAYOUT_OK (host only)`。

## 关键测试覆盖

- Q16=0与旧整数模型逐项一致；1/4、1/2、65535、端点/越界/大帧；
- 检测服务完整窗→`SoundToaPosition{frame=7945,Q16=16384}`交接；
- End失败不提交；不完整窗只交接MISSING；无PCM指针；
- 新TOA缺失/非法、CLOCK/STREAM失效、队列容量/最长日志；
- `sound_event_time_analysis.py`可对含新记录的采集做Q16精确Fraction复核，旧采集仍可分析。

## 未解决/下一步

- 硬件未烧录、未实板验收；A板至少30条有效记录 + B板联合验证需明天另行授权。
- 标准声识别仍MATCH0；本地模型正确不等于物理精度或识别通过。
- 下一软件增量：两板同一声事件的UDP携带/配对/Δt基础链路（只做诊断事件，不把UDP接收时刻当声音到达；正式测距仍需标准声身份/质量）。
