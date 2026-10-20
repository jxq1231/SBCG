# M2.2.1 Four-Timestamp Estimator Implementation Plan

> 使用superpowers:executing-plans在现有jxq功能分支按清单执行；不新建工作树。实现后按superpowers:requesting-code-review进行只读复核。

**Goal:** 用纯C实现已确认的四时间戳偏差/路径时延估算器，并独立验证数值和失败边界。

**Architecture:** SyncExchange提供两个时钟域内的一轮四个时间，SyncEstimate输出两倍纳秒整数分子；SyncEstimator_Calculate只做范围、顺序、窗口、路径时延验证及算术，不接入运行调用。

**Tech Stack:** C11、原生GCC、Python标准库测试入口、现有GCC/CMake Debug/Release固件构建。

依据：[用户已确认设计](2026-10-20_Sync_Estimator_Design.md)。实现、13组/1044次新测试、原109项回归、两配置构建/布局、加载区域比较及只读复核均通过。验证与文档完成，按jxq提交并停止本增量；硬件不在本次验收范围。

## 1. 契约及失败测试

文件：新建App/Sync/sync_estimator.h/.c、Tests/Sync/test_sync_estimator.c、Tests/Sync/run_tests.py。

- [x] 头文件定义SYNC_ESTIMATOR_MAX_TIMESTAMP_NS=UINT64_C(4294967295999999999)，以及设计列出的六个状态、四字段SyncExchange、两字段SyncEstimate和函数声明。源文件初始仅返回BAD_ARGUMENT，使用(void)参数消除空桩警告。
- [x] 测试先定义下列有效样本与错误输出哨兵检查，再写头文件/空桩，使失败来自断言而不是编译错误：

```c
const SyncExchange x = {1000000000, 1000150000, 1000350000, 1000400000};
SyncEstimate result = {123456789, -987654321};
assert(SyncEstimator_Calculate(&x, UINT64_C(1000000000), &result) == SYNC_ESTIMATOR_OK);
assert(result.offset_x2_ns == 100000);
assert(result.mean_path_delay_x2_ns == 200000);
```

- [x] 补齐设计的六个确定性样本、正负半纳秒分子、零值、大共同纪元、跨秒及输入上限附近/最大正负偏差。对每个失败用例都核对预期状态及两个输出哨兵保持不变；覆盖两个空指针、四个输入越界、非法窗口、两域倒序、窗口边界/超过1 ns、负路径时延及错误优先顺序。
- [x] 用uint32_t状态、种子20261020的确定性LCG产生1000组设计范围内物理模型：构造t1、t2=t1+前向时延+偏差、t3=t2+驻留、t4=t1+前向时延+驻留+反向时延，按物理参数校核两个输出分子。
- [x] run_tests.py按现有Tests入口风格查找原生GCC，以C11/Wall/Wextra/Werror编译真实模块及测试到Logs/M2_2_Sync_Estimator，编译/执行各30秒限时，保留非零退出码。运行python -B Tests/Sync/run_tests.py，记录有效样本断言失败。

## 2. 最小数学实现

- [x] 用下列实现替换空桩，不增加其它行为；运行同一测试入口直至通过：

```c
_Static_assert(SYNC_ESTIMATOR_MAX_TIMESTAMP_NS <= (uint64_t)INT64_MAX / 2U,
               "Second-order timestamp arithmetic must fit int64_t");

SyncEstimatorStatus SyncEstimator_Calculate(const SyncExchange *exchange,
    uint64_t max_exchange_ns, SyncEstimate *result)
{
    if (exchange == NULL || result == NULL) { return SYNC_ESTIMATOR_BAD_ARGUMENT; }
    if (max_exchange_ns == 0U || max_exchange_ns > SYNC_ESTIMATOR_MAX_TIMESTAMP_NS ||
        exchange->t1_a_tx_ns > SYNC_ESTIMATOR_MAX_TIMESTAMP_NS ||
        exchange->t2_b_rx_ns > SYNC_ESTIMATOR_MAX_TIMESTAMP_NS ||
        exchange->t3_b_tx_ns > SYNC_ESTIMATOR_MAX_TIMESTAMP_NS ||
        exchange->t4_a_rx_ns > SYNC_ESTIMATOR_MAX_TIMESTAMP_NS)
    { return SYNC_ESTIMATOR_OUT_OF_RANGE; }
    if (exchange->t4_a_rx_ns < exchange->t1_a_tx_ns ||
        exchange->t3_b_tx_ns < exchange->t2_b_rx_ns)
    { return SYNC_ESTIMATOR_BAD_ORDER; }
    if (exchange->t4_a_rx_ns - exchange->t1_a_tx_ns > max_exchange_ns ||
        exchange->t3_b_tx_ns - exchange->t2_b_rx_ns > max_exchange_ns)
    { return SYNC_ESTIMATOR_WINDOW_EXCEEDED; }
    const int64_t forward_ns = (int64_t)exchange->t2_b_rx_ns - (int64_t)exchange->t1_a_tx_ns;
    const int64_t reverse_ns = (int64_t)exchange->t4_a_rx_ns - (int64_t)exchange->t3_b_tx_ns;
    const int64_t delay_x2_ns = forward_ns + reverse_ns;
    if (delay_x2_ns < 0) { return SYNC_ESTIMATOR_NEGATIVE_DELAY; }
    *result = (SyncEstimate){forward_ns - reverse_ns, delay_x2_ns};
    return SYNC_ESTIMATOR_OK;
}
```

所需头文件为自有sync_estimator.h及stddef.h。范围证明、B−A正号、半纳秒含义及调用者配对责任必须在头文件/源码注释中明确。测试不使用HAL/LwIP或模拟寄存器。

## 3. 构建与复核

文件：修改用户拥有的CMakeLists.txt，仅在现有通用target_sources块加入App/Sync/sync_estimator.c。不改App调度、生成CMake或硬件文件。

- [x] 注册源码，运行Tests/Sync新测试及原Tests/Ptp、App、UdpProbe、Udp、Network入口、Tools/Monitor/tests的unittest，记录实际测试组数和模型调用数。
- [x] 执行python -B Tools/Monitor/autodebug.py build、build --preset Release及两配置check_eth_layout.py，检查诊断、时间戳公共宏和编译清单确实包含新源码。
- [x] 比较构建前后ELF加载区域（arm-none-eabi-objcopy生成的bin）而非仅比较ELF文件：新函数没有运行调用，应被既有gc-sections回收；按实际哈希、符号及Flash占用记录，Debug调试信息变化不当作固件运行变化。
- [x] 按requesting-code-review提供设计、变更、测试/构建证据及基线1cfb0d8，请只读复核边界/符号、物理模型参考和作用域；不访问硬件。处理阻塞问题后完成最终差异检查。

## 4. 文档及Git收口

文件：新建Docs/Milestones/M2_2_Sync_Estimator.md；更新本清单、设计状态、Docs/Project_State.md、Docs/Project_Roadmap.md、Docs/README.md、README.md。

- [x] 按实际结果记录文件职责、测试调用链、无新增IRQ/DMA/Cache关系、构建/布局、加载区域保持性、未接入运行及LOCAL/SYNC=0边界。无烧录动作，也不引用旧实物结果宣称本算法已在板上运行。
- [x] git status及diff核对后只暂存本增量文件，以jxq提交feat: add bounded four-timestamp offset estimator，核对作者/提交者并按既有协作授权推送当前分支，确认远端一致与工作区干净。
- [x] 停止本增量，不开始真实报文配对、校时/校频、失锁控制、PPS或第二板。
