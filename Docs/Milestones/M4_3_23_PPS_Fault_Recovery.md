# M4.3.23 PPS故障自动恢复（软件完成）

<!-- doc-status: m4.3.23-pps-fault-auto-recovery-software-verified-short-run; reviewed: 2026-10-27 -->

## 背景

B板曾锁存：

```text
$PPS,ROLE=B,STATE=FAULT,ARMED=0,MISSED=1,ERRORS=1
```

旧实现只要`BSP_PPS_Process`返回非OK，就永久锁存FAULT；只有`PpsService_Init`（板复位）能恢复。单板复位还会破坏A/B PTP重新收敛，导致B长时间不输出。

## 修改

`App/Sync/pps_service.c/.h`：

- 保留故障锁存/停机，但增加**1秒限频自动重试**；
- FAULT后`BSP_PPS_Init`成功且PTP读时正常时：
  - A恢复为`QUALIFIED`；
  - B恢复为`UNQUALIFIED`，必须等后续有效Observe重新取得资格；
  - 不伪造PTP观测，不跳过同步资格；
- `PpsServiceSnapshot`新增：
  ```text
  faults
  recoveries
  last_fault_status
  ```
- `$PPS`日志新增：
  ```text
  FAULTS=...,LAST_FAULT=...,RECOVERIES=...
  ```
- 如果硬件故障持续，仍保持FAULT并按1秒重试，不无限伪装正常。

## 验证

- `Tests/PpsService/run_tests.py` PASS，新增FAULT码、1秒重试、A恢复QUALIFIED、B恢复UNQUALIFIED后再锁定；
- `Tests/BoardSync/run_tests.py` PASS；
- `Tests/App/run_tests.py` PASS；
- Debug/Release构建PASS；
- A/B烧同一Release后短时运行：

```text
A: STATE=QUALIFIED,EMITTED=40,FAULTS=0,RECOVERIES=0
B: STATE=QUALIFIED,EMITTED=21,FAULTS=0,RECOVERIES=0
A/B DOMAIN VALID=1，TOKEN=51004073
```

## 边界

- 短时运行未自然触发新FAULT，自动恢复路径目前由主机测试覆盖；
- 仍需长时间运行确认FAULT根因是否彻底消除；
- PPS外部相位精度仍需示波器；本次不改变±10µs结论。
