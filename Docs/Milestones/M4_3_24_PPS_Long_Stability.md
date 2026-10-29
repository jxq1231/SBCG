# M4.3.24 PPS双板10分钟长稳测试

<!-- doc-status: m4.3.24-pps-long-stability-10min-pass; reviewed: 2026-10-27 -->

## 条件

- 镜像：`f369aae6e1dcb1fcd9506ae713500d6e596ec9637b870521413d2b6aeb718a39`
- HEAD：`6564df0`
- A/B同时复位，串口采集600秒；
- 不播放标准声，只观察PPS/PTP/DOMAIN。

日志：

```text
Logs/Acceptance/2026-10-27_ppslong/A-serial.jsonl
Logs/Acceptance/2026-10-27_ppslong/B-serial.jsonl
```

## A板

```text
$PPS采样599次，STATE=QUALIFIED 599次
最终：EMITTED=592,MISSED=0,ERRORS=0,FAULTS=0,RECOVERIES=0
$DOMAIN：21.07s建立，600s内仅1条记录，无VALID=0
```

## B板

```text
$PPS采样598次：
  QUALIFIED=361
  RATE_REQUALIFY=224
  UNQUALIFIED=13
最终：ARMED=1,EMITTED=580,MISSED=0,ERRORS=0,FAULTS=0,RECOVERIES=0
$DOMAIN：19.65s建立，600s内仅1条记录，无VALID=0
```

## 结论

- 600秒内A/B均未进入PPS FAULT；
- A/B均未累计MISSED/ERRORS；
- B在`RATE_REQUALIFY/短暂UNQUALIFIED`间切换，但未锁死，恢复后继续输出；
- A/B共同域保持VALID=1，未出现周期性掉域；
- 本次没有自然触发FAULT，因此自动恢复路径仍由主机测试覆盖，不能把“零FAULT”外推为任意硬件故障自动恢复已验证。

## 边界

- 长稳只到600秒，不是无限时长；
- 外部PPS相位差仍需示波器；本测试只证明软件状态/计数/域长时间稳定；
- B UNQUALIFIED期间是否每个PPS脉冲都出现在引脚上，需要示波器长时间观察，串口不能替代。
