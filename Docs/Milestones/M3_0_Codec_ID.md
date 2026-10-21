# M3.0 — 板载 WM8994 身份读取

更新：2026-10-22。**应用实现、主机回归、Debug/Release 构建以及本版本约90秒联合实物验收通过。**
本轮只处理当前单板，不改变前序 LOCAL/SYNC=0，不采集声音。
设计及官方源码依据见[已确认方案](../Plans/2026-10-22_Codec_ID_Design.md)。

## 目标与接线

通过现有 `hi2c3`（PH7/PH8）读取板载 WM8994：7 位地址 0x1A，HAL 地址 0x34，
16 位寄存器地址 0x0000，两字节大端数据应为 0x8994。
这是开发板上已焊接的音频芯片，**无需在蓝色扩展板插入新模块或调整温度模块**。
保留用户确认的 DHT11 3.3V/PG7-D4 接线、USB 和网线。

不调用完整 ST 音频 BSP 初始化、软件复位或 I2C 错误重初始化，不扫描总线。
不启用 SAI、MCLK、音频 DMA、音频 IRQ 或麦克风通路。
本次读事务的地址定位阶段不是寄存器数据写入，尤其不能对寄存器 0 写复位值。

## 文件清单和调用关系

| 文件 | 作用 |
| --- | --- |
| `App/BSP/bsp_audio_codec.h`、`.c` | 唯一 I2C3 映射；只读 ID、错误状态和实际耗时 |
| `App/Audio/codec_monitor.h`、`.c` | 每两秒一次读取；状态统计和 `$CODEC` 输出 |
| `App/app_main.c` | 初始化与主循环接入，版本 SBCG-M3.0-CODEC，DHT11 避让 |
| `CMakeLists.txt` | 用户拥有的源文件注册点 |
| `Tests/Audio/stubs/main.h` | 可控 HAL 测试接口 |
| `Tests/Audio/test_bsp_audio_codec.c` | 事务参数/字节序/错误/部分读取/总线忙 |
| `Tests/Audio/test_codec_monitor.c` | 周期/避让/计数/恢复/回绕/输出边界 |
| `Tests/Audio/run_tests.py` | 有时限原生 GCC 编译和运行 |
| `Tests/App/test_app_main.c` | 全应用顺序、重初始化及避让集成检查 |
| `Docs/Plans/2026-10-22_Codec_ID_Design.md` | 范围、来源和验收约定 |
| 本文、`Docs/Project_State.md`、`Docs/Hardware_Resources.md`、`Docs/Protocol.md` | 状态、接线与输出字段 |

```text
main → CubeMX MX_I2C3_Init → App_Init → CodecMonitor_Init（无 I/O）
App_Process → 原网络/PTP → TemperatureService_Process → TemperatureConsole_Process
            → 重新检查 DHT11 start-low → CodecMonitor_Process
              → BSP_AudioCodec_ReadId → HAL_I2C_Mem_Read(hi2c3)
              → SendLine → 原有延迟日志队列 → BSP_Console_Write
```

无新增中断关系；I2C 使用主循环轮询，SysTick 提供既有超时基准，USART1 接收中断保持。
无新增 DMA；ETH 原描述符、缓冲和网络保留区不变。
I/D Cache 启用状态不变，不增加缓存维护。
CubeMX **无需变更**，`.ioc` 及全部生成初始化保持原样。

## 验证

- 新增 C 测试先因缺失实现失败，随后两个 Audio 程序通过严格 GCC 编译与执行。
  覆盖 ID 大端/反序/零值，HAL 完整、零字节和仅一字节失败，NACK/超时/BUSY，
  非 READY/硬件 BUSY 不触发事务，以及限频、DHT11 避让、恢复、tick 回绕。
- App、Console、Network、Udp、UdpProbe、Ptp、Sync、TimeExchange、Temperature
  原生测试通过；Tools/Monitor 74 项和 Analysis 42 项 Python 测试通过。
  包括 Audio 共 12 组命令均返回 0，记录 `Logs/Audio/M3_0/regression.json`。
- `python -B Tools/Monitor/autodebug.py build --preset Debug` 和 `--preset Release`
  通过，无新增编译警告。`check_eth_layout.py --preset Debug/Release` 通过：
  MSP 仍为 0x20047000..0x20048000，LwIP/ETH 保留区地址不变。
- 已独立只读复核 ST 官方 BSP、HAL 超时与应用集成，未发现阻断问题；
  建议补足 HAL 部分返回测试，已加入单字节/零字节失败用例并复跑。
- 实际 CubeMX Generate Code 检查通过：493 个纳入检查的文件无字节变化，
  无新增文件；生成器删除的 LAN8742 许可证按快照原样恢复，记录
  `Logs/Audio/M3_0/regen/result.json`。再生成后的 Debug/Release 重新构建通过。
- 两个负向检查均能拒绝错误行为：在忽略目录临时副本中反转 ID 字节序、
  去掉 DHT11 避让门控，测试均以 1 退出；实际源文件不受修改。
  记录 `Logs/Audio/M3_0/negative-checks.json`。

## 本次验收固件标识

- Debug SHA256：`7af53f1c9d9920567003fc82c2b7edd102c22cd2215d58b00892bf92181078f2`。
- Release SHA256：`5ea243b2589cfa1624b156973f00d170b5fbc5b0934338760e016e59d4202943`。
- 软件检查、官方源码来源哈希和验收辅助脚本冻结清单：
  `Logs/Audio/M3_0/preflash-review.json`。该文件保留烧录前状态，实际授权和结果分别记录，
  不覆盖成事后验证状态。

## 实物验收流程（本次已执行）

使用现有 ST-LINK 配置对固定 SHA256 的 Debug ELF 编程一次、校验并复位，不改选项字节。
同步采集约 90 秒 COM3、UDP 5005、旧请求 6006 和时间交换 6007。

要求新 BOOT 唯一、HCLK=200000000 和 UID 正确；至少 30 条 CODEC 为
RESULT=OK、ID=8994，CODEC ERRORS=0。确认 READ_MS 实测范围及串口 TX_ERRORS=0；
自动温度至少 12 个正式版本、错误计数 0，原 UDP 窗口不少于 60 包且无序号缺口，
两组请求各 30 成功，30 组板端结果与真实 C 参考逐条一致，PTP 保持 LOCAL/SYNC=0。
不做传感器拔插，不检查第二板。

本次用户明确批准上述 SHA256 后，按此流程烧录一次。Flash/Verify/Reset 均通过，
不修改选项字节。自动联合分析状态 **PASS_CODEC_ID_JOINT**。

| 观测 | 本次结果 |
| --- | --- |
| BOOT | 唯一 SBCG-M3.0-CODEC，HCLK=200000000，UID=001A002D:30365105:36363631 |
| WM8994 | SEQ 1～45，全为 ID=8994、RESULT=OK、HAL_ERR=0，ERRORS=0 |
| 读取耗时/间隔 | READ_MS=0～1，平均约0.822ms；间隔2000～2007ms |
| 自动温度 | 版本1～17，均25.7℃、湿度38%、声速346874mm/s；ATT=18、OK=17、PRIME=1，其余错误0 |
| UDP基线 | 70秒70包，SEQ 6～75，无缺口、重复、乱序、非法或外来包 |
| UDP周期 | 板tick差991～1009ms；同SEQ串口与UDP时间逐条一致 |
| 旧请求 | 30组请求/ACK/RX/TX完整关联，超时/错误0 |
| 新时间交换 | 30组ACK/REPORT/四时刻，键一致，回报/观测错误0 |
| 板端计算 | 30组与本机编译的原C参考内核逐条完全一致 |
| 持续运行 | 90条心跳与90条PTP序号连续，TX_ERRORS=0、PTP ERRORS=0、LOCAL/SYNC=0 |
| 采集完整性 | 原始串口字节与JSONL完全对应，无首部丢弃、BOOT碎片或未完成尾行，所有采集正常结束 |

UDP采用原固定时隙调度，延后可使下一周期少于1000ms；实测991～1009ms不表示序号缺失。
启动首个UDP时隙PHY未连通，SEQ=1为LINK_DOWN；此后持续UP/100Mbps/FULL。
电脑窗口从SEQ=6开始，不把窗口外启动等待记为网络丢包。

关键实际输出：

```text
$CODEC,SEQ=20,TICK_MS=40023,ADDR7=1A,RESULT=OK,ID=8994,HAL_ERR=00000000,READ_MS=1,OK=20,ERRORS=0
```

原始证据前缀 `Logs/UDP/m30-codec-1792602640079`：flash.log、serial.bin、
serial.jsonl、monitor.jsonl、probe.jsonl、time.jsonl；文件SHA256保存在
`Logs/Audio/M3_0/baseline-acceptance.json`。完整键及本机C逐条对照见同目录
`runtime-comparison.json`，授权见 `authorization.json`；采集过程见
`runtime-run.json`。该过程文件停留在 COMPLETE_PENDING_JOINT_ANALYSIS，
后续通过结果写入独立验收文件，不篡改原始过程记录。忽略目录中的数据不随功能提交上传。

**Flash：PASS；Runtime：PASS（限本增量范围）。** 已关闭COM3及本轮UDP采集。
本轮保持传感器连接，没有复验物理拔插；前序拔插证据仍属于T1.2固件。
当前只证明板载音频芯片控制接口可读，麦克风、SAI/DMA采样率、双板同步与测距均待后续独立增量。
