# 音频来源与用途

## 下载原件

- 文件：`reference_log_sweep_20hz_21khz_15s.wav`
- 原标题：[Logarithmic Sweep (Chirp) 20 Hz to 21 kHz.wav](https://commons.wikimedia.org/wiki/File:Logarithmic_Sweep_(Chirp)_20_Hz_to_21_kHz.wav)
- 作者：[Auguste von Herzberg](https://commons.wikimedia.org/wiki/User:Auguste_von_Herzberg)
- 许可：[CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/)
- 下载日期：2026-10-25；原始内容未修改，仅更改本地文件名。
- 这是约15秒全频段对数扫频参考素材，不是本项目选定的50ms标准音。
- 实际下载地址、原站元数据、字节数与SHA256见 `reference_source.json`。

## 本项目合成文件

`chirp_2-6khz_50ms_template.wav` 和 `standard_chirp_2-6khz_10x_20s.wav`
由项目脚本按线性扫频数学公式独立合成，没有截取、重采样或改编上述下载原件。
参数、用途、识别能力边界和复现方法见 [标准音频选型说明](../../Docs/Audio_Selection.md)。
它们也不是从论文下载的音频，不能将论文的实验精度或识别表现作为本项目的验收结果。
