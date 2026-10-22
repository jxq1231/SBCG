# M3.2.3 帧位置到本地PTP时间估算实施方案

<!-- doc-status: archived-plan; reviewed: 2026-10-23 -->

状态：2026-10-23已完成纯软件验证；文档审计先以cff9758提交，随后按本方案实施。保留本方案作追溯，勿重复执行；实测结果见[本轮软件报告](../Milestones/M3_2_3_Audio_Time_Map.md)。执行记录见[M3.2.3工作计划](M3_2_3/task_plan.md)。

目标：实现纯C、有界、失败清零的DMA帧位置→LOCAL PTP时间估算内核，使用M3.2.2既有90锚点做留出回放；不接入固件运行，不访问硬件。

## 1. 模型、单位及边界

输入两锚点各包含valid(bool)、session(uint32)、frame_lo/frame_hi(uint64帧)、sec/nsec(uint32秒/纳秒)，另有目标session(uint32)和frame_position(uint64帧)。caller须先以唯一BOOT及UID隔离同一启动、同一板；SESSION本身不能证明跨BOOT身份。C模块不拥有日志或全局状态。

锚点连续NDTR进度模型取闭包[a,b]=[frame_lo,frame_hi+1]，保守计入一帧量化；不是PCM缓冲读取权限。目标N是**NDTR连续帧坐标的整数位置**，不是自动认定零基PCM样本N的物理采样时刻：完整消费样本N对应完成计数N+1，物理通道/codec/SAI/FIFO延迟另行标定。

假设两端之间DMA进度相对本地PTP为线性、正斜率。只接受N≥左锚点frame_hi+1且N≤右锚点frame_lo，即对任意端点相位均被包围；不外推。若左区间[L0,U0]，右区间[L1,U1]，dt=t1−t0，则：

```text
model_lower_ns = t0 + floor(dt*(N-U0)/(U1-U0))
model_upper_ns = t0 + ceil (dt*(N-L0)/(L1-L0))
estimate_ns    = model_lower_ns + (model_upper_ns-model_lower_ns)/2
```

端点闭包与floor/ceil只覆盖**线性模型下的游标量化及整数取整**；不能约束未建模的瞬时率变化/codec延迟，也不是物理误差上限。WIDTH=0仍非零不确定性。输出valid仅表示该模型输入合法，不表示同步或课程精度通过。

无浮点/堆内存/HAL/64位编译器扩展：先减绝对帧数和时间，再乘有界差值。最多10秒窗口、最少100ms、连续帧跨度≤500000、WIDTH≤4。frame_hi==UINT64_MAX因+1不可表示而拒绝。所有差值在检查顺序后才相减；时间来自合法uint32秒和<1e9纳秒。

两端率区间[(L1−U0)*1e9/dt,(U1−L0)*1e9/dt]须整体落在47511.160～48470.982Hz中（对推导47991.071Hz±1%向外取整至毫赫兹，仅健全性带）。交叉相乘使用帧差*1e12与dt*毫赫兹，最大≤5e17，uint64安全。插值乘积≤5e15。

状态枚举固定：OK=0、BAD_ARGUMENT=1、INVALID_ANCHOR=2、SESSION=3、ORDER=4、WINDOW=5、RATE=6、OUTSIDE=7、RANGE=8。失败时所有输出字段清零；没有旧结果或隐式默认时刻。有效输出包含bool valid、uint64 estimate_ns/model_lower_ns/model_upper_ns。

## 2. 文件与调用链

- App/Audio/audio_time_map.h/.c：AudioTimeMap_Estimate(left,right,target_session,frame_position,result)纯函数。
- Tests/Audio/test_audio_time_map.c、run_tests.py：先失败，再验证基本算例、区间取整、会话、窗口、率、越界、NULL、秒边界、大帧编号及失败清零；保留原8程序。
- Analysis/audio_time_map_cli.c：host-only真实C适配器；每行15个无符号十进制token：id、左6字段、右6字段、target_session、target_frame。左/右字段顺序valid/session/lo/hi/sec/nsec。输出6token：id/status/valid/estimate/lower/upper。严格拒绝负号、溢出、非法token/超长行。
- Analysis/audio_time_map_analysis.py、tests/test_audio_time_map_analysis.py：编译host适配器；与独立Fraction参考逐项比较；核验原始capture及acceptance哈希，唯一BOOT/UID及原检查器；写新目录，不覆盖。
- Docs/Milestones/M3_2_3_Audio_Time_Map.md及现行入口：如实报告回放与未实测边界。

调用链为主机replay → native CLI → AudioTimeMap_Estimate，**main/App_Process无新调用**，CMake固件源列表不登记新内核。因此两种固件ELF和原加载内容应保持完全一致，仍运行SBCG-M3.2-ANCHOR。

No new interrupt relationship。No new DMA relationship。No CubeMX change required。不修改硬件/生成文件/IRQ/Cache；没有Flash或新硬件验收。

## 3. 分步执行与门槛

- [x] 文档阶段已提交后，写C测试，执行 `python -B Tests/Audio/run_tests.py`，先观察新模块缺失导致失败；然后实现内核并让9个程序通过。
- [x] 写独立Fraction参考/真实CLI/失败输入/留出回放测试；先确认缺失适配器或模块失败，再完成主机工具。覆盖负号/布尔/字段、异常路径及报告不覆盖。
- [x] 合成数值按闭包公式精确核对上下界与中点；随机相位/超大基数的数千例比较真实C；不只用Python重写函数替代C执行。
- [x] 原始M3.2.2串口capture与acceptance绑定，唯一新版BOOT后90个有效锚点正常窗口。采用固定互不重叠三元组(1,2,3)、(4,5,6)…(88,89,90)：仅1/3拟合，2为留出，30组。中间锚点取phase区间中点nearest integer，恰半向下；不用中间时间拟合/选参。
- [x] 所有C结果与Fraction完全一致；30组有效。统计留出时间相对估计的有符号/绝对残差和模型区间覆盖。预设经验检查最大绝对残差≤100000ns仅作回放健全性检查，**不是课程检测重复性或声学精度验收**。超界记录失败，不修改参数迁就日志。模型区间覆盖不设100%硬要求，报告实际未覆盖数和最大区间外偏差，不把未知动态误差藏进区间。
- [x] Tools原83项、Analysis原61项及新增测试、原C模块回归、Debug/Release及ETH/audio布局；检查旧配置/所有原固件源和两种ELF哈希不变。
- [x] 复核代码、数学、证据和文档状态，提交该独立纯软件增量、正常推送后停止。

## 4. 证据与复现

原始数据只读：Logs/Audio/M3_2_2/runtime-01/capture-serial.jsonl 与 acceptance.json，不改写。新构建/测试/回放结果到Logs/Audio/M3_2_3；可提交小型已处理摘要但不提交原始日志。工具hardware_verified恒false，既有实物输入不等于本增量已上板。

数学验证不依赖外部网络、第二板、声音或传感器拔插。±100us经验门槛在看留出残差之前固定；不可在失败后隐藏失败或把门槛改成满足日志的数值。
