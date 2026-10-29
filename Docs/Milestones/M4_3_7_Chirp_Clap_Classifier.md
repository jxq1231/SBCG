# M4.3.7 标准chirp/击掌波形-质量分类（软件接入）

<!-- doc-status: m4.3.7-event-classifier-integrated-hardware-pending; reviewed: 2026-10-27 -->

## 结论

新增纯C波形分类器 `event_classifier`，不再只依赖2s周期：

- 输入候选onset后的PCM窗口（最多2400帧/50ms，立体声按声道跨步读取）；
- 提取峰值、包络活动长度、活动段前/后1/4过零率、一阶差分能量比；
- 分类：
  - 活动长度≥1200帧且后段过零率相对前段明显上升 → `EVENT_CLASS_CHIRP`；
  - 短促或无明显上升 → `EVENT_CLASS_CLAP`；
  - 活动不足/静音/样本不足 → `UNKNOWN`或非法；
- 输出quality 0–100（span+trend代理，不是识别概率）。

接入链：

```text
SoundDetectorService Stage
  → 候选onset后PCM窗口分类
  → SoundCandidate.event_class/quality
  → SoundEventTimeEvent
  → App只允许CHIRP进入标准序列/事件UDP/配对
```

- App对非CHIRP事件直接跳过，不喂标准序列、不发UDP；
- 事件`quality`改用分类器值，不再固定100；
- 测量仍要求标准序列+quality≥50+共同时间域+有效温度。

## 边界

- 仅合成chirp/短clap主机测试；**没有真实标准声/真实击掌录音验证**；
- 过零率/包络阈值是工程代理，房间多径、播放设备、麦克风频响可能误分类；
- 不是模板相关识别，没有降低0.80门限，也没有宣称标准声身份通过；
- 击掌若满足长包络+上升过零率仍可能被误判；需要真实负样本标定；
- 未烧录、未两板实测；当前验收分仍未增加。

## 软件验证

- `python -B Tests/Detector/run_event_classifier_tests.py`：O0/O2，chirp/短clap/静音/长度不足；
- `Tests/SoundDetectorService`、`Tests/Audio` 加入分类源后PASS；
- SoundEventTime/Measure/UI/TouchGFX/Sdram/App/ChirpService回归PASS；
- Debug/Release构建、audio layout PASS；未烧录。

## 下一步

1. 用真实标准声/单次击掌/重复击掌采集验证并标定阈值；
2. 两屏权威结果交换：同一测量ID/距离≤1mm；
3. 静默/瞬态/温度补偿/0.5m标定；
4. 明天两板/交换机/PPS实测。
