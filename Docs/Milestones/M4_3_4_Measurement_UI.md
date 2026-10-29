# M4.3.4 权威测量结果与状态页（软件完成）

<!-- doc-status: m4.3.4-measurement-result-ui-software-complete-hardware-pending; reviewed: 2026-10-27 -->

## 结论

在两板事件UDP/配对之后，增加唯一权威测量结果，并接入现有LCD/TouchGFX状态快照：

- [measurement_result.c](../../App/Measure/measurement_result.c)：只接受有效配对结果；用当前温度声速重算距离；携带测量ID、距离、A/B方向、质量、温度版本和2s有效期；
- `$SOUND_PAIR`配对结果经`UDPSoundEvent`结果回调进入`MeasurementResult`；
- `UI_StatusSnapshot`/`UI_StatusFormat`读取同一结果；距离、`SIDE A/B`、`QUALITY nn` 均来自该副本，不再由各显示后端自行计算；
- 无有效温度/声速、越界、无效/超龄结果输出占位，不把未知结果补成0。

## 边界

- 当前`time_domain=1`、事件类型CHIRP、quality=100仍是**诊断设置**，`$SOUND_PAIR`带`DIAG=1/SYNC=0`；
- 尚未接真实两板/交换机/烧录；UDP接收、同步收敛、真实温补、标准声识别均未硬件验证；
- 温度无效时`MeasurementResult`拒绝生成有效距离；手动/自动温度来源必须由TemperatureService先发布；
- 两屏显示仍需同一权威结果在两块板上的分发/比较；当前软件只准备单板状态页读取路径；
- 不改TOA估算器、门控、0.80、CubeMX、时钟/DMA/GPIO/NVIC/缓存。

## 软件验证

- `Tests/Measure/run_tests.py`：新增测量结果ID/超龄/温度版本/方向/声速/越界PASS；
- `Tests/UI/run_tests.py`：距离、SIDE A/B、QUALITY文本和格式PASS；
- `Tests/TouchGFXService/run_tests.py`：真实status_snapshot+TouchGFX服务PASS；
- Audio/SoundEventTime/SoundDetectorService/App/Sdram/UI/ChirpService回归PASS；
- Debug/Release构建、audio layout PASS；未烧录。

## 下一步

1. 明天两板+交换机+同步/PPS实测；
2. 用硬件同步结果替换诊断`time_domain`，明确无效/未同步时不出测量结果；
3. 标准声身份/质量门控（当前MATCH0）与击掌分类；
4. 双屏统一结果/同一测量ID的实物一致性；
5. 温度补偿、0.20–2.00m卷尺测距、重复性、方向、10m提高项按任务书逐项验收。
