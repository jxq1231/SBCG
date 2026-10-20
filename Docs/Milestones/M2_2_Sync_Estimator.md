# M2.2.1 四时间戳偏差与路径时延估算器

更新：2026-10-20。基线1cfb0d8；用户已确认[设计](../Plans/2026-10-20_Sync_Estimator_Design.md)并明确要求实施。**纯C估算器及主机验收通过：新增13组测试、1044次函数调用，含1000组确定性物理模型；原109项检查通过，两配置构建/布局通过。** 独立只读复核未发现阻塞问题，并独立复跑新测试通过。没有烧录或新增板端运行调用，固件仍为SBCG-M2.1-TX、LOCAL/SYNC=0。

## 独立目标与文件

从同一轮交换的t1(A发送)、t2(B接收)、t3(B发送)、t4(A接收)计算B−A时钟偏差和平均单程路径时延。A/B仅表示两个数学时间域；本增量未指定实物角色或接入双板报文。

| 文件 | 用途 |
| --- | --- |
| App/Sync/sync_estimator.h | 四输入、两输出、最大时间范围、状态码和纯函数契约 |
| App/Sync/sync_estimator.c | 范围/顺序/窗口验证、有符号算术及失败输出保持 |
| Tests/Sync/test_sync_estimator.c | 确定性例子、边界、错误顺序及物理模型测试，无HAL/LwIP模拟 |
| Tests/Sync/run_tests.py | 用原生GCC编译并运行真实模块，编译/执行各限时30秒 |
| CMakeLists.txt | 用户拥有的通用target_sources注册新数学源码 |
| Docs/Plans/2026-10-20_Sync_Estimator_Design.md、2026-10-20_Sync_Estimator_Implementation.md | 已批准设计与执行记录 |
| Docs/Milestones/M2_2_Sync_Estimator.md、Docs/Project_State.md、Docs/Project_Roadmap.md、Docs/README.md、README.md | 结果、边界及阅读入口 |

## 接口、符号与数值表示

SyncEstimator_Calculate(exchange,max_exchange_ns,result)输入四个uint64_t整数纳秒；返回SyncEstimatorStatus。每个时间限制在0～4294967295999999999 ns，即当前32位PTP秒域的最大十进制纳秒值。窗口也必须为非零且不超过该上限，由调用者显式提供。

输出均为int64_t：offset_x2_ns=(t2−t1)−(t4−t3)，正数表示B领先A；mean_path_delay_x2_ns=(t2−t1)+(t4−t3)。二者分别为对应纳秒量的两倍，-1表示-0.5 ns，不做除法/舍入或浮点运算；表示能力不等于硬件测量精度。公式来源及假设见已批准设计。

先验证输入，再转有符号数后求差。二阶和/差绝对值上界8589934591999999998小于INT64_MAX，源码以_Static_assert固定该前提。只比较同一时钟域中的先后和时长，跨域差为负可以是合法偏差。错误优先级为：空指针→输入/窗口范围→同域倒序→同域交换超窗→负路径时延。所有失败保持非空输出两个字段原值；负路径时延不取绝对值、不夹为0。

成功只表示给定数值满足数学检查。调用者须负责报文配对、会话/纪元、硬件时间戳有效性，以及交换期间时钟未重启、跳变或回绕。函数没有UID、网络包、锁定状态或校时权限。

## 调用、CubeMX、IRQ、DMA及Cache

主机验收调用链：Tests/Sync/run_tests.py → 原生GCC → test_sync_estimator.main → Expect/Reject → SyncEstimator_Calculate。测试使用真实数学实现，不替换其算术或返回值。

固件构建编译新源码一次，但没有App运行调用，现有gc-sections将函数回收；App_Init/App_Process、版本标识和原网络/PTP路径均未修改。没有新增串口日志，不会出现实时偏差输出或SYNC=1。

No CubeMX change required。.ioc、HAL、生成CMake、Core及LWIP文件未改；GPIO、TIM、DMA、NVIC、ETH、SAI、I2C、ADC、LTDC均无新配置。无新增中断或回调ISR关系，无新增DMA或pbuf所有权关系；Cache/MPU及原ETH描述符/缓冲均保持不变。

## 测试证据

先写完整契约和测试，以空实现运行python -B Tests/Sync/run_tests.py，编译成功后首个有效样本在要求SYNC_ESTIMATOR_OK处断言失败，退出1。再实现范围检查和算术，同一入口退出0；未把编译错误当作功能失败证明。

| 组 | 验证内容 |
| --- | --- |
| 1 | 设计六个样本：正/负/零偏差、跨域负差、半纳秒分子及路径不对称偏差 |
| 2 | 正负半纳秒分子均保留，不发生截断 |
| 3 | 全零输入与零路径时延可计算 |
| 4 | 六个样本共同加4×10^18 ns后输出不变 |
| 5 | 跨秒交换仍按连续纳秒计算 |
| 6 | 输入上限、最大正负时钟差、近上限样本及最大路径分子均无溢出 |
| 7 | 空输入/输出及错误优先顺序 |
| 8 | 四个输入各自上限+1及UINT64_MAX均拒绝 |
| 9 | 窗口0、上限+1和UINT64_MAX拒绝 |
| 10 | A域/B域倒序及范围优先于倒序 |
| 11 | 窗口恰好边界接受，两域各自超过1 ns拒绝，倒序先于超窗 |
| 12 | 负路径时延拒绝且输出哨兵不变 |
| 13 | 种子20261020、1000组已知偏差/两向时延/驻留时间的物理模型 |

Expect/Reject直接核对状态及两个输出字段；每个失败样本都检查输出未被改动。随机模型从真实偏差及各方向时延构造时刻，再从物理参数给出期望结果，未用同一输入差分式作为唯一参考。范围与种子均在设计中明确。

原生测试合计13组、1044次调用，1000组物理模型包含在这些调用中，不另外相加。既有PTP 38组、App 5组、请求应答7组、UDP发送6组、PHY双模式19组及Python 34项全通过；沿用原统计口径合计122组/项。

```powershell
python -B Tests/Sync/run_tests.py
python -B Tests/Ptp/run_tests.py
python -B Tests/App/run_tests.py
python -B Tests/UdpProbe/run_tests.py
python -B Tests/Udp/run_tests.py
python -B Tests/Network/run_tests.py
python -B -m unittest discover -s Tools/Monitor/tests -v
python -B Tools/Monitor/autodebug.py build
python -B Tools/Monitor/check_eth_layout.py
python -B Tools/Monitor/autodebug.py build --preset Release
python -B Tools/Monitor/check_eth_layout.py --preset Release
```

## 构建、运行内容与边界

两配置构建退出0，无新未解释诊断，ETH内存布局检查通过，各118个C单元统一PTP宏，编译清单中数学源码恰好一项。对象文件含SyncEstimator_Calculate定义，最终ELF符号中没有该函数，确认未接入运行而被回收。

| 配置 | Flash字节 | ELF SHA256（前后完全一致） |
| --- | ---: | --- |
| Debug | 63548 | d17578c1ca71589ee4ec1dd838df091b4025ee9f51790eb8d3da2f94c64736a5 |
| Release | 40416 | 0f78a4e56591d00a32e052af8dcf76a3c19148bf5d6c58a839ced53e7bc2f987 |

两份ELF经arm-none-eabi-objcopy -O binary提取加载内容后，也与构建前逐字节一致：Debug共63548字节，SHA256=cf09f1b4626c23659b01017bfe43ded421a77bf754de11614c354f1337448644；Release共40416字节，SHA256=a25661dd2964f122b6b42efeb3485f6e1e7319f932cd9818694b69e46d06c15a。

构建报告为Logs/AutoDebug/20261020T134402-9208c74d/report.json及20261020T134402-8d79fcdb/report.json。比较摘要、前后bin及各回归/布局原文保存在Git忽略的Logs/M2_2_Sync_Estimator下，分别见before-build.json、build-verification.json、*-regression.log及*-layout.log；独立复核的新测试原文为native-green.log。没有执行本轮Generate Code或硬件访问；未改变的生成文件按Git及哈希检查确认，不把上一轮再生成实验写成本轮实验。

本次纯算法完成标准不要求烧录；Flash和Runtime标为不需要/未执行。旧M2.1.3实物结果只对应其板端功能，不能证明本估算器已在板上运行。仍无双板同步、校频、锁定、收敛或±10 μs精度结论，路径不对称无法由单组四时间戳独立检测。正式课程验收仍需双板、交换机及规定观测。

完成本增量后提交并停止，后续真实报文配对、样本观察、校时/校频、失锁控制和PPS各另立目标；本轮不检查第二板。
