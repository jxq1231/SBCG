# M4.3.26 TOA跨块补窗（软件完成，短时实物验证）

<!-- doc-status: m4.3.26-toa-cross-block-software-complete-short-hardware-check; reviewed: 2026-10-27 -->

## 问题

分类器已支持跨块2400帧，但TOA仍只在当前音频块内寻找完整256帧窗。块尾候选：

```text
TOA_US=NA
```

之后没有补算，导致标准声后续chirp漏配、屏幕少显示。

## 修改

`App/Detector/sound_detector_service.c/.h`：

- TOA候选不足256帧时保留，不下发NA；
- 下一连续块到达后，用：
  ```text
  已提交CPU尾部2400帧 + 当前8192帧块
  ```
  拼出完整256帧TOA窗；
- 仍不补零、不裁到块首、不读取未来DMA数据；
- End失败/断流/换会话清历史；
- 没有后继块时窗口保持pending，1秒流龄后丢弃，不伪造TOA；
- 分类与TOA共用已提交尾部，但不改变分类阈值/TOA算法。

## 测试

```text
Tests/SoundDetectorService：PASS
  partial-tail → 下一块补齐后VALID
  cross-block-left → 历史尾部+当前块补齐
  pending-expiry → 无后继块不伪造NA，过期丢弃
  complete-boundary/End拒绝/长帧/非法数值保持原语义
```

构建Debug/Release均PASS。

## 实物短验

镜像：

```text
SHA256 13184660a1cf019b975cc6975c3a4b2df527517bc1b6b1f1ba30c0fa5522f48d
```

手机标准声结果：

```text
标准序列：DETECTED
标准声期间SOUND_CAND TOA_US全部有值
此前块尾TOA_US=NA未再出现
稳定配对：321,320,317,320,319,320,320,323 mm
另有388mm等高度/反射异常值和播放前背景配对
```

## 边界

- 本轮采集开始时手机播放较晚，只覆盖部分标准chirp；完整10次重复仍需下一轮；
- 388mm等异常值由声源高度/反射/多路径造成，不是TOA缺失；
- 两屏显示需现场确认；
- 未完成0.5m标定、距离精度和静默最终验收。
