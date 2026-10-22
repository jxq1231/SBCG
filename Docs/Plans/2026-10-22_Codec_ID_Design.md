# M3.0 — 板载 WM8994 只读身份检查

<!-- doc-status: archived-plan; reviewed: 2026-10-23 -->

> **已完成的历史方案，请勿重复实施。** 仅保留当时范围、授权和生成检查；其中“当前/下一步/待验”均按原日期理解，不构成新授权。验收见[对应报告](../Milestones/M3_0_Codec_ID.md)，现行入口见[文档导航](../README.md)。

执行状态：2026-10-22 已完成本版本授权烧录与联合实机验收，结果见
[M3.0记录](../Milestones/M3_0_Codec_ID.md)。以下保留实施前确认的设计和验收要求。

用户已确认实施。本增量只验证现有 I2C3 能读出板载音频芯片 ID；
不初始化音频通路，不启用 SAI、DMA 或音频中断，不修改 CubeMX 配置。

## 依据和边界

- 本机 STM32Cube_FW_F7_V1.17.4 官方源码：
  `Drivers/BSP/Components/wm8994/wm8994.h` 定义 ID `0x8994` 和寄存器 `0x0000`；
  `wm8994.c:wm8994_ReadID()` 调用该寄存器读取。
- `Drivers/BSP/STM32746G-Discovery/stm32746g_discovery.h` 定义
  `AUDIO_I2C_ADDRESS=0x34`（HAL 使用左移后的地址，7 位地址为 `0x1A`）。
- 同目录 `stm32746g_discovery.c:AUDIO_IO_Read()` 使用 16 位寄存器地址、
  两字节高位在前的数据。官方读 ID 路径不依赖 SAI/MCLK。
- 沿用工程生成的 `hi2c3`、PH7/PH8、Timing=0x00C0EAFF。
  不引入官方完整 BSP：它有独立 I2C 初始化及故障重初始化副作用。
- 本次寄存器事务只有指向地址的写阶段和两字节读取，没有寄存器数据写入；
  尤其不写 ID/软件复位寄存器，不扫描其他地址、不复位共享总线。

## 实现

1. `App/BSP/bsp_audio_codec.*`：唯一 I2C3 映射，返回 OK、BUSY、IO_ERROR、
   ID_MISMATCH 或 INVALID_ARG；携带原始 HAL 错误位、有效 ID 和耗时 ms。
   失败时不得把未完成读取的数据解释为 ID。
2. `App/Audio/codec_monitor.*`：初始化不访问硬件；每 2000 ms 最多一次读取。
   只有主循环明确允许 I/O 时才访问。DHT11 启动低电平期间延后，恢复后不补发突发。
   使用 uint32 差值支持 tick 回绕。串口 `$CODEC` 带序号、状态、累计成功/失败；
   没有完整读到 ID 时省略 ID 字段。
3. `App/app_main.c`：在温度与命令处理后调用监视服务，读取前重新检查 DHT11
   低电平状态；统一使用原有延迟日志队列。固件名 SBCG-M3.0-CODEC。
4. 顶层用户拥有的 `CMakeLists.txt` 注册两个源文件；生成文件保持原样。

HAL 轮询传输使用 2 ms 超时参数；进入 HAL 前检查 handle READY 和硬件 BUSY。
**2 ms 不是严格的整个调用耗时上限**：本地 HAL 的初始 BUSY 等待及错误收尾
存在内部超时（如 25 ms），SysTick/其他中断也会影响耗时。通过 READ_MS 记录实际
耗时，并在实机验收核对温度采样、网络序号和旧协议结果。失败只报告并按周期重试。

## 测试和验收

- 先写主机 C 测试并观察失败，再实现：地址/寄存器宽度/字节序、错误与部分读、
  BUSY 不触发事务、空指针、ID 不符；服务限频、延迟、恢复、tick 回绕和无效 ID 输出。
- 应用集成测试确认初始化/主循环顺序、DHT11 避让及原网络所有权失败处理。
- 运行现有主机回归，Debug/Release 构建与 ETH 布局检查，验证 CubeMX 再生成保持性。
- 构建和复核后记录新 ELF SHA256，再另行请求该版本的烧录授权。
- 实机同步采集约 90 s：至少 30 条 `RESULT=OK,ID=8994`，无新增 CODEC 错误、
  DHT11 正常更新，串口 TX_ERRORS=0；UDP 基线无新增序号缺口，原请求/时间交换各
  30 组通过并与本机 C 参考计算一致。无需拔插传感器，也不接触第二块板。
- 实机通过后更新 Docs、创建功能提交并核实 Git 作者；完成此增量后停止。

## 计划文件清单

- 新增 BSP、Audio 服务：上述四个文件。
- 新增 `Tests/Audio/run_tests.py`、`Tests/Audio/test_bsp_audio_codec.c`、
  `Tests/Audio/test_codec_monitor.c`、`Tests/Audio/stubs/main.h`。
- 修改 `Tests/App/test_app_main.c`、`App/app_main.c`、`CMakeLists.txt`。
- 更新 `Docs/Project_State.md`、`Docs/Protocol.md`、`Docs/Hardware_Resources.md`；
  新增 `Docs/Milestones/M3_0_Codec_ID.md`，记录实测或明确待验收状态。
