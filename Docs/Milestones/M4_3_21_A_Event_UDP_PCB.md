# M4.3.21 A板事件UDP PCB池耗尽诊断

<!-- doc-status: m4.3.21-a-event-udp-pcb-exhaustion-cubemx-fixed-hardware-verified; reviewed: 2026-10-27 -->

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

## CubeMX修复与硬件验证

用户已在CubeMX把`LWIP.MEMP_NUM_UDP_PCB`从`6`改为`10`，并重新生成：

```text
SBCG.ioc: LWIP.MEMP_NUM_UDP_PCB=10
LWIP/Target/lwipopts.h: #define MEMP_NUM_UDP_PCB 10
```

构建后烧A/B，run05实物结果：

```text
A: INIT_ERR=0，$SOUND_EVENT_BIND=0条
B: INIT_ERR=0
A: $DOMAIN,VALID=1 at 19.27s
B: $DOMAIN,VALID=1 at 19.07s
两板TOKEN=3171538039
60秒内DOMAIN仅1条建立记录，无掉域
```

A板事件UDP绑定问题已解决；B板在`TRACKING/RATE_REQUALIFY`下共同域保持稳定，M4.3.20修复同时得到实物验证。

## 边界

- 本诊断不改变音频、测距或同步结论；
- 未播放标准声，未完成音频闭合；
- 未完成标准声身份、配对距离、PPS示波器或物理精度验收。
