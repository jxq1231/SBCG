# M4.3.27 TOA跨块优化后的标准声验证

<!-- doc-status: m4.3.27-toa-cross-block-hardware-validated-distance-preliminary-pass; reviewed: 2026-10-27 -->

## 镜像与条件

```text
镜像 SHA256：13184660a1cf019b975cc6975c3a4b2df527517bc1b6b1f1ba30c0fa5522f48d
HEAD：4ddfdc9
真实麦克风间距：约320mm
手机固定、等高、延长线附近播放标准WAV
```

## TOA结果

标准声期间A/B的`SOUND_CAND`：

```text
TOA_US全部有值
块尾TOA_US=NA不再出现
```

`SOUND_TOA_STATE`最终：

```text
A: QUEUED=42,OK=35,REJECT=7,MISSING=7
B: QUEUED=52,OK=43,REJECT=9,MISSING=6
```

剩余MISSING来自播放前/后背景候选，不是标准声后续chirp的块尾缺失。

## 稳定配对

```text
316,316,318,317,315,318,318,315,317 mm
```

统计：

```text
n=9
平均=316.67mm
最小=315mm
最大=318mm
样本标准差≈1.22mm
相对320mm误差=-4～-2mm
```

温度声速修正后估算屏幕值约：

```text
317.4～320.9mm
```

与操作者观察的0.317～0.320m一致。

另有播放结束后：

```text
61mm
```

属于标准序列RESET后的背景配对，不应显示为正式结果。

## 判断

```text
TOA跨块补窗：硬件验证PASS
标准声期间块尾NA：消失
稳定配对：315～318mm
温度修正后屏幕：约317～320mm
基础距离误差：≤±10mm（本组）
重复标准差：<2mm（本组）
两屏一致性：待现场最终确认
```

## 边界

- 本组稳定配对9次，仍需正式累积至少10次有效显示；
- 61mm等RESET后/播放前值不纳入统计；
- 标准序列前3次预热仍不显示，属于门控设计；
- 未完成0.5m标定、多点距离和静默最终验收。
