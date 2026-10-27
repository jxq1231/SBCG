# M4.3.21 A板事件UDP PCB池耗尽诊断

<!-- doc-status: m4.3.21-a-event-udp-pcb-exhaustion-confirmed-cubemx-fix-required; reviewed: 2026-10-27 -->

## 现象

A板运行：

```text
$SOUND_EVENT_STATE,TX_OK=0,TX_ERR=...,RX_OK=0,INIT_ERR持续增长
```

B板同镜像`INIT_ERR=0`。A板因此不发DOMAIN REQ，两板无法建立DOMAIN。

## 诊断改动

`App/Network/udp_sound_event.c`在`udp_new`/`udp_bind`失败时限速输出：

```text
$SOUND_EVENT_BIND,RESULT=NEW_FAIL
$SOUND_EVENT_BIND,RESULT=BIND_FAIL,ERR=...,LOCAL_IP=...
```

A板实物日志：

```text
$SOUND_EVENT_BIND,RESULT=NEW_FAIL
```

每秒一次，`INIT_ERR`持续增长，确认是`udp_new()`返回NULL，而不是`udp_bind`返回错误。

## 根因

当前`SBCG.ioc`/`LWIP/Target/lwipopts.h`：

```text
LWIP.MEMP_NUM_UDP_PCB=6
```

A板初始化时已占用6个UDP PCB：

```text
PtpBoardSync：319、320，共2个
UDPHeartbeat：1个
UDPProbe：1个
UDPChirpCapture：1个
UDPTimeExchange：1个
合计6个
```

`UDPSoundEvent`需要第7个PCB，因此A板`udp_new()`失败。B板可能有一个可选socket尚未实际绑定，所以暂时未触发；这不是A/B固件差异。

## 需要CubeMX修改

在CubeMX LwIP配置中把：

```text
MEMP_NUM_UDP_PCB
```

从`6`改为至少`8`（建议`10`，留余量），重新生成工程后构建、烧两板。

**不得手改`.ioc`或生成的`lwipopts.h`。** 本仓库规则要求LwIP选项由CubeMX维护。

## 边界

- 本诊断不改变音频、测距或同步结论；
- A/B当前烧录的是含诊断日志的同一Release镜像；
- B域连续性修复尚未在A板能绑定后被硬件验证；
- 未完成音频闭合、标准声、PPS示波器或距离验收。
