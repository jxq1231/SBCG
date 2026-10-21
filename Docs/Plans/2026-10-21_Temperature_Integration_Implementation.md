# T1.1 DHT11与声速 Implementation Plan

> 按逐项实施与验证流程执行；可使用superpowers:executing-plans。项目AGENTS.md的CubeMX、单增量和实物后功能提交要求优先于通用技能示例。

**Goal:** 当前板DHT11的有效温度转换为统一整数声速并可经串口验收。

**Architecture:** 使用生成的PG7开漏配置，BSP有界捕获、纯帧解码、纯整数模型和主循环状态服务分离。保留既有网络/PTP，不迁移队友LCD/工具链。

**Tech Stack:** STM32F746、HAL/CMSIS DWT、现有GCC/CMake、native GCC测试及Python观测工具。

范围已获用户批准；[设计、接线与CubeMX步骤](2026-10-21_Temperature_Integration_Design.md)。实施起点8763768。**PG7及软件检查已通过，用户已反馈实际3.3V，R0授权烧录后得到10个正式样本但7次START错误，整体未通过；R1已获授权烧录并通过正常联合窗口；用户延后拔插，本轮仅收尾正常采样子增量。**

## 1. 前置硬件配置

- [x] Git读取队友两个分支，锁定99eb65f；确认仅有DHT11 GPIO配置。
- [x] 核对当前PG7未配置，确认无ETH/I2C3/PA15资源冲突。
- [x] 读取并查看奥松V1.3数据格式，复核统一声速整数模型。
- [x] 用户在SBCG.ioc设置PG7：GPIO_Output / DHT11_DATA / High / Output Open Drain / No Pull / Low；Generate Code。
- [x] 检查SBCG.ioc、Core/Inc/main.h、Core/Src/main.c及元数据差异，保护无关用户修改；确认旧配置和构建入口保持。

完成以上检查后，才进入第2步。无需重复确认已经批准的单板采集范围。

## 2. 纯数据与声速

新建`App/Temperature/dht11_frame.c/.h`、`sound_speed.c/.h`，对应`Tests/Temperature/test_dht11_frame.c`、`test_sound_speed.c`及`run_tests.py`。

约定接口：

```c
typedef struct {
    int32_t temperature_milli_c;
    uint32_t relative_humidity_milli_pct;
} Dht11Sample;
typedef enum { DHT11_FRAME_OK, DHT11_FRAME_ARGUMENT,
    DHT11_FRAME_CHECKSUM, DHT11_FRAME_RANGE } Dht11FrameStatus;
Dht11FrameStatus Dht11Frame_Decode(const uint8_t frame[5], Dht11Sample *out);
bool SoundSpeed_Calculate(int32_t temperature_milli_c, uint32_t *sound_speed_mm_s);
```

- [x] 先写固定数据测试：`{53,0,24,4,81}`→24400/53000/346086；`{53,0,10,129,192}`→-10100/53000/325179。对错误校验、非法小数/湿度/温度、NULL及模型越界检查输出不变。
- [x] 用最小stub构建native测试，观察期望断言失败，记录RED。
- [x] 实现解码与int64_t中间量、恰半远离零取整；测试20℃/25℃/-10℃、边界-20℃/60℃和有效0℃；观察GREEN。

## 3. BSP与采样状态

新建`App/BSP/bsp_dht11.c/.h`、`App/Temperature/temperature_service.c/.h`、`temperature_config.h`。新建`Tests/Temperature/test_bsp_dht11.c`、`test_temperature_service.c`和所需IO stub，仅替换硬件IO。

- [x] 先测试总线卡高/卡低、40位合法与非法脉宽、DWT回绕、超时整体上限，以及所有结束路径释放引脚；观察RED再实现真实有界捕获。GPIO配置只读取/使用生成标签，不在BSP重新初始化模式。
- [x] 先测试20ms起始状态等待期间不捕获、不HAL_Delay；实际低电平超30ms时释放并拒绝；上电等待1秒、间隔5秒和预读阶段；观察RED再实现状态机。
- [x] 先测试首次UNREADY无假值、0℃有效、成功版本增加、失败保持旧值及年龄、15秒STALE、恢复、UID未知禁用和tick回绕；观察RED再实现快照/日志。
- [x] 将时序、读取周期、有效范围、时效和唯一测试UID集中在temperature_config.h，避免散落阈值；不增加角色/温度网络框架。

接口约定：`TemperatureService_Init(uint32_t now_ms, const uint32_t uid_words[3], TemperatureLog log)`、`TemperatureService_Process(uint32_t now_ms)`，日志回调为`void(const char *, int)`；BSP的开始、捕获和释放职责按设计分离，所有硬件状态失败均返回明确错误。

## 4. 应用与构建

修改`App/app_main.c`、用户拥有的根`CMakeLists.txt`及`Tests/App/test_app_main.c`。

- [x] App测试先断言新Init/Process次序、版本、重初始化和旧调度，观察失败。
- [x] 仅接入温度模块并注册用户源码。FW使用独立`SBCG-T1.1-TEMP`标识，保留旧网络/PTP调用；不把模块算法写入main.c。
- [x] 运行`python -B Tests/Temperature/run_tests.py`与`python -B Tests/App/run_tests.py`，全部GREEN。
- [x] 原248项及新增回归，规范复核通过后再进行质量复核；发现问题先加复现测试再修复。
- [x] `python -B Tools/Monitor/autodebug.py build --preset Debug`及Release；两配置ETH布局通过，无新增未解释警告；统一PTP宏与原数学内核保持。
- [x] 实际Generate Code保留性回归，记录新ELF和软件源文件哈希；更新`Docs/Project_State.md`、`Docs/Protocol.md`及新`Docs/Milestones/T1_1_Temperature.md`，区分软件通过和实物未测。

## 5. 实物与交付

- [x] R0已通知接线、固件SHA并获授权烧录；90秒初次实测记录保留。
- [x] 针对R0起始期间UART缺陷完成有界日志队列修复、RED/GREEN及R1软件复核。
- [x] 获得R1新固件明确烧录授权并完成90秒正常联合窗口。
- [x] 取得17个有效正式版本并逐条核对温度、整数声速及错误计数。
- [ ] 用户配合断开/恢复、验证HOLD_LAST/STALE及恢复：用户明确延后，本轮不做。
- [x] 与原UDP/PTP/心跳及30组时间交换并行采集，记录真实调度间隔和全部错误；不借用M2.2.4旧结果认定新固件通过。
- [x] 文档记录原始证据、固件/源文件哈希、启动旧片段重同步、资源/IRQ/DMA/cache及用户延后范围。
版本控制与交付范围：以jxq记录本次正常采样子增量，不把延后的故障恢复记为完成；正常推送后停止，不自动开始下一项。

软件步骤已执行并通过；R0已实测但整体未通过，R1当前版本正常窗口已实测通过，用户延后的故障恢复仍未实测；当前只交付正常采样子增量，不引用旧M2.2.4验收代替。
