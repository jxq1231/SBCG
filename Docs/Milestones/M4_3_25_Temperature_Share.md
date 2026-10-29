# M4.3.25 A/B温度共享与B屏显示（实物验证）

<!-- doc-status: m4.3.25-temperature-share-hardware-verified; reviewed: 2026-10-27 -->

## 目标

保持“一只DHT11在A板”的统一温度设计，让B板无需第二只传感器即可显示A板温度/声速；两板使用同一温度和同一声速。

## 修改

### 1. 新增20字节温度线协议

`App/Network/temperature_wire.c/.h`：

```text
magic "TEMP"
version=1
valid/source
temperature_milli_c int32
sound_speed_mm_s uint32
temperature_version uint32
```

解码校验温度范围、版本非0、声速与本地整数公式一致。

### 2. 复用5009事件UDP

不新增LwIP PCB：

```text
A: UDPSoundEvent_SendTemperature() 1Hz发送
B: UDP Receive识别20字节TEMP并回调
```

温度消息不依赖DOMAIN；声音事件/结果/域协议保持原长度和语义。

### 3. B本地温度服务增加远程缓存

`TemperatureService_SetRemote()`：

- 仅B角色应用；
- 版本新鲜时拒绝旧版本；同版本允许刷新接收时间；
- B本地DHT仍由UID白名单禁用，不产生第二套温度；
- A本地值优先，B本地DHT禁用时使用15秒内的A共享值；
- 超龄后UI无效，不伪造0℃。

### 4. App接线

A主循环1Hz调用发送；B设置温度sink并写入远程缓存；UI仍通过`TemperatureService_GetSnapshot()`读取，自动显示REMOTE温度。

## 实物验证

镜像：

```text
SHA256 13e17fb79c73db85f2dc979fcdbf799f9e856a7a31bd2ec238bbde90e98586c9
```

A串口：

```text
$TEMP,STATE=OK,VERSION=6,T_MILLI_C=23500,RH_MILLI_PCT=28000,C_MM_S=345541
$UI,ROLE=A,TEMP_SOURCE=AUTO,TEMP_VALID=1,TEMP_MC=23500,C_MM_S=345541
```

B串口：

```text
$TEMP,STATE=REMOTE,VERSION=6,T_MILLI_C=23500,C_MM_S=345541,
AGE_MS=433,FRESH=1,LAST=REMOTE,MODE=AUTO,SOURCE=BOARD_A
$UI,ROLE=B,TEMP_SOURCE=AUTO,TEMP_VALID=1,TEMP_MC=23500,C_MM_S=345541
```

结果：

```text
B屏不再显示 -- C
B与A温度相同：23.500℃
B与A声速相同：345541 mm/s
B无第二只DHT11
```

## 测试

```text
Tests/Temperature：0 failed
  新增远程温度显示/版本/超龄/同版本刷新
  新增temperature_wire编解码边界
Tests/Udp：PASS
  新增A发送/B接收共享温度，且不依赖声音域
Tests/App / App UDP / Sdram / UI / ChirpService：PASS
Debug/Release构建PASS
```

## 边界

- 温度值/版本共享是软件链路；DHT11±2℃精度仍需单独验收；
- B显示A温度，不代表B板自身环境温度相同；
- 温度超龄时UI撤销显示，不伪造0℃；
- 本次未播放标准声、未做距离/标定验收。
