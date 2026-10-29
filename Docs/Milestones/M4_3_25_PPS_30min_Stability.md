# M4.3.25 双板PPS共同窗口30分钟长稳

<!-- doc-status: completed-evidence-serial-1800s; reviewed: 2026-10-27 -->

## 结论与范围

2026-10-27，两板共同启动后的完整1800秒串口观察通过：未观察到PPS FAULT，`FAULTS / RECOVERIES / MISSED / ERRORS`始终为0；首次建立共同域后没有`VALID=0`转换记录。B的15条UNQUALIFIED全部位于建域前。

这是串口状态与计数稳定性证据，不是1800秒逐脉冲外部相位测量。已有[示波器10次记录](M4_3_22_PPS_Scope.md)独立保留：最大235ns、均值93.9ns，满足10µs要求；两个BNC转鳄鱼夹未校偏斜，不能据此证明10ns精度。本轮未复测示波器。

## 镜像、连接与复位

- 源码基准：`7722efa77e2eb2249550dcf431f5840e16e1d182`。
- 用户报告的A/B镜像，以及本机Release ELF SHA256：`f369aae6e1dcb1fcd9506ae713500d6e596ec9637b870521413d2b6aeb718a39`。本轮未烧录、未读取Flash回校。
- BOOT：两板均为`SBCG-M4.2-PCMCAP`，HCLK 200MHz。
- A：COM3，UID `001A002D:30365105:36363631`；B：COM5，UID `00380050:35385104:37313532`；两板与电脑经过现有交换机组网。
- 沿用现有OpenOCD配置，按ST-LINK序列号分别选择两板，先暂停再并发释放复位。没有改编程器持久配置、选项字节、引脚、时钟或生成文件。
- 复位命令共同起点：`2026-10-27T09:36:10.925251+00:00`（北京时间17:36:10）。两次主机命令起点差91.2µs；它不是电气复位边沿差。
- 原始采集1830秒，保留30秒余量。两口正常结束并关闭，无额外BOOT；RESET均为PIN=1、CAPTURED=1、CLEAR_OK=1。

## 严格统计窗口

串口工具共享主机单调时钟，A BOOT接收时刻0.402923s，B为0.405411s。正式窗口为：

```text
[max(A_BOOT, B_BOOT), max(A_BOOT, B_BOOT) + 1800)
= [0.405411, 1800.405411) 秒
```

原始复位前缓存A为5行、B为11行，均保留但不计入本轮启动统计。raw与JSONL完整行逐条一致；两文件均无未完成尾行。`$PPS`是周期状态快照，以下条数不是示波器脉冲数，也不是状态转换次数。

| 共同1800秒指标 | A | B |
| --- | ---: | ---: |
| PPS完整快照 | 1799 | 1798 |
| QUALIFIED | 1799 | 1100 |
| RATE_REQUALIFY | 0 | 683 |
| UNQUALIFIED | 0 | 15（均在建域前） |
| FAULT | 0 | 0 |
| ARMED=0 / ARMED=1 | 1 / 1798 | 19 / 1779 |
| 最终ARMED | 1 | 1 |
| EMITTED首值→末值 | 1→1799 | 0→1784 |
| MISSED / ERRORS / FAULTS / RECOVERIES末值 | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |
| 上述四项观察到的正增量 | 全部0 | 全部0 |
| EMITTED及上述四项计数下降次数 | 全部0 | 全部0 |
| LAST_FAULT | 仅0 | 仅0 |
| HEARTBEAT完整快照 | 1799 | 1799 |
| 心跳序号中断、日志错误 | 0 | 0 |

首次建域后的窗口内，A为1784条QUALIFIED；B为1100条QUALIFIED、683条RATE_REQUALIFY，无UNQUALIFIED或FAULT。`ARMED=0`快照不能换算成漏脉冲；本次未做持续电气脉冲记录。

采集余量也保留：整个BOOT后约1829.76秒内，A共1829条QUALIFIED，B共1118条QUALIFIED、695条RATE_REQUALIFY、15条UNQUALIFIED；文件末尾EMITTED分别为1829、1814，四项故障计数仍均0。此口径不混入上表1800秒计数。

## DOMAIN和关键原始行

```text
A @15.647230s:
$DOMAIN,ROLE=A,VALID=1,LOCAL=1,PEER=1,TOKEN=51004073,PEER_AGE_MS=0
B @15.652726s:
$DOMAIN,ROLE=B,VALID=1,LOCAL=1,PEER=1,TOKEN=51004073,PEER_AGE_MS=0

A @1799.878020s:
$PPS,ROLE=A,STATE=QUALIFIED,QUALIFIED=1,ARMED=1,TARGET_SEC=1800,EMITTED=1799,MISSED=0,ERRORS=0,FAULTS=0,LAST_FAULT=0,RECOVERIES=0,SYNC=0
B @1799.954459s:
$PPS,ROLE=B,STATE=QUALIFIED,QUALIFIED=1,ARMED=1,TARGET_SEC=1800,EMITTED=1784,MISSED=0,ERRORS=0,FAULTS=0,LAST_FAULT=0,RECOVERIES=0,SYNC=0
```

首次建域相对各自BOOT接收时刻为15.244307s和15.247315s，令牌一致。`$DOMAIN`只记录有效性变化：准确表述为“首次建域后未记录VALID=0转换”，不将单条日志当成每秒独立的域或精度采样。

## 证据和验证

原始日志与实验附件均留在忽略目录，不提交：

```text
Logs/Acceptance/pps30-20261027T173610/
  manifest.json, combined-run.json
  A-serial.bin, A-serial.jsonl, A-run.json, A-openocd-reset.log
  B-serial.bin, B-serial.jsonl, B-run.json, B-openocd-reset.log
  pps_summary.py, pps-summary.json
  host-checks/summary.json
  analysis-tests/test_pps_summary.py, pps-selfcheck-results.json
```

raw SHA256：

- A：`f2ab3e031e34b1ecb3e12488e2fbfc780d1df0735c847c4c85bbbda0df945866`，5,519,204字节，49,260完整行。
- B：`4ccad1323c1a070ff845b095cae39d9b9bdef2fb69ad597ec9e1745a996db5e9`，6,157,602字节，56,436完整行。

本轮实际执行：

```text
cmake --build --preset Debug --parallel                  PASS
cmake --build --preset Release --parallel                PASS
python -B Tests/PpsService/run_tests.py                  PASS
python -B Tests/BoardSync/run_tests.py                   PASS
python -B Tests/App/run_tests.py                         PASS（4种组合）
python -B Analysis/verify_standard_selection.py           PASS（文件契约）
python -B .../analysis-tests/test_pps_summary.py          PASS（15项）
python -B .../pps_summary.py                             PASS_SERIAL_OBSERVATION_ONLY
```

标准声文件契约通过不表示声音识别通过。没有新增源码、调用链、IRQ、DMA或Cache变更；CubeMX无需修改，构建产物与采集工具不代替实测。

## 未闭合项与下一步

- 本轮无自然FAULT，因此故障后自动恢复的硬件路径未被触发，不能以RECOVERIES=0宣称恢复验收通过。
- 长稳状态通过，不替代外部精度与连续逐秒脉冲证据；不重做已有10次示波器测试。
- 下一独立目标是用原标准WAV闭合双板STANDARD_SEQUENCE→SOUND_PAIR→权威结果及两屏观察。无正式配对前不做距离精度结论。
- 本轮不修改测量时间契约；CLAP不纳入正式验收；0.5m标定缺板端触发入口，保持未验收。
- 旧[600秒记录](M4_3_24_PPS_Long_Stability.md)按当时采集口径归档，不把其缓存或启动阶段条数混入本次共同1800秒统计。
