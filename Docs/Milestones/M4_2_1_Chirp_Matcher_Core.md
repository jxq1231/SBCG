# M4.2.1 扫频模板匹配核心：软件验证记录

<!-- doc-status: completed-software-evidence; reviewed: 2026-10-25 -->

## 结论

纯C流式模板匹配核心及冻结模板的电脑端验证已通过。实际20秒WAV的10次事件全部识别为一个匹配事件，起始帧逐一等于文件定义。**没有接入固件，没有播放/录音或实机验收；没有新的识别率、到达时刻精度或测距精度结论。** 当前板上仍是 `SBCG-U3.1-RESET`。

## 文件与职责

| 文件 | 本轮职责 |
| --- | --- |
| `App/Detector/chirp_matcher.h` | 配置、事件、调用方持有的流状态及输入生命周期契约 |
| `App/Detector/chirp_matcher.c` | 去直流归一化相关、能量门限、有界峰观察、去重与断流清理 |
| `App/Detector/chirp_template.h` | 冻结模板常量接口、长度和源文件身份 |
| `App/Detector/chirp_template.c` | 从现有WAV逐点导出的2400个PCM16值及SHA256 |
| `Analysis/export_chirp_template.py` | 按源文件散列/格式校验后导出C常量；支持只校验模式 |
| `Tests/ChirpMatcher/test_chirp_matcher.c` | 原生C接口/流边界、最大窗口/PCM极值等测试 |
| `Tests/ChirpMatcher/reference_bridge.c` | 主机进程运行真实C，输入的模板须与编译进来的常量逐点相同 |
| `Tests/ChirpMatcher/reference_check.py` | 独立批量FFT相关、精确整数窗口统计与已知位置对照 |
| `Tests/ChirpMatcher/run_tests.py` | 原生GCC O0/O2严格警告构建与运行，日志写入被忽略的Logs |
| `Docs/Plans/2026-10-25_Chirp_Matcher_Core.md` | 本轮实现范围、接口设计及检查记录 |
| `Docs/Audio_Selection.md` | 保留原素材交付边界，补充核心已验证但未上板 |
| `Docs/Project_State.md` | 区分新增源码与未改变的板上固件 |
| `Docs/README.md`、`Docs/Document_Status.md` | 登记本报告与归档计划，避免重复实施 |

## 算法与资源边界

调用链仅存在于主机测试：`run_tests.py → C测试/参考桥接 → ChirpMatcher_Init → 逐帧ChirpMatcher_Push → 数字匹配事件`。当前固件仍是 `AudioCapture_Process → SoundDetectorService_Stage → BSP_Audio_EndBlock → Complete`；没有暗中替换能量候选或接入新模板。

模板源WAV SHA256为 `c37114332a698910e12ac855b47b566df229a3c7ec01414371e32969da3e084d`，保留已有渐变和零首点，模板起始坐标没有因去掉静音/首零点而平移。源WAV及循环文件均未修改。

N≤2400，去直流方差为 `N*sum(x*x)-sum(x)^2`，最大不超过 `2400²*32768² < 2^53`；相关分子、样本和、平方和使用64位整数；方差乘积先转double，避免整数乘积溢出。评分取相关绝对值0..1，并记录反相；RMS为窗口AC强度。样本有INT16极值时标记截幅，调用方仍须决定是否接受测量，不能把高相关度等同于未失真。

默认门限是实验参数：RMS64 PCM、评分0.80、首次越阈后再观察48帧、按最佳窗口起点去重24000帧。这些长度以帧为单位，不在核心中假设真实采样率。无完整模板或观察尾部时不发布；边界清理会丢弃未确认峰。相关事件是观察范围内最优匹配窗口，不保证声学直达声，较强反射可能改变选择。

模板只读借用；调用方必须保证生命周期，不能与状态/输出重叠。输入逐点复制到定长环形缓冲，不保留DMA/PCM指针，不分配堆。MinGW主机上单个状态4936字节，不能直接视为ARM ABI尺寸。

**No CubeMX change required。无新IRQ或DMA关系，无缓存维护变更。** GPIO/TIM/ETH/SAI/I2C/LTDC、生成文件、现有CMake注册均未改动。新源文件尚未加入固件目标；直接相关每个候选窗口O(N)，含64位点积和double开方，最坏仍逐采样计算。电脑测试不能证明200MHz STM32可实时承受，板端接入前须做有界搜索/计算预算、采样率适配、缓冲所有权与拒绝路径设计。

## 已执行的验证

### 新核心

```powershell
python -B Analysis/export_chirp_template.py --check
python -B Tests/ChirpMatcher/run_tests.py
```

先建立公开接口与C行为断言；缺失实现替身可编译运行，但在 `ChirpMatcher_Init` 应成功的断言失败，记录 `Logs/Detector/M4_2_1/RED-O0.txt`，随后才实现核心。不能把最初缺少后续参考脚本的中间状态当作通过；以下结果来自全部文件准备后的完整运行。

- `-std=c11 -Wall -Wextra -Werror -pedantic`，O0/O2各3001个C断言通过，无编译警告。
- C接口覆盖：非法/非有限配置、零方差模板、输出为空不消费、配置自身复制、无事件清输出、会话/跳帧/重复帧清理、session0、待定峰重置、64位帧号、最大2400点窗口的PCM极值、确认恰落在UINT64_MAX及拒绝回绕。
- 每种优化级别19组真实C/独立参考用例，共38组。逐事件核对起点、模板末帧、确认帧、RMS、反相/截幅；相关度与参考相差小于2e-6。
- 实际循环WAV：10个起始帧48000、144000、…、912000，全部准确一致；跨8192帧边界的窗口也不需要重置。
- 正样本：不同帧对齐、低增益加直流、反相、受控截幅、固定种子加噪、推导采样率偏差、去重和大于32位坐标。
- 负样本：静默、恒定直流、固定种子白噪声、3kHz单频、反向扫频、单点脉冲、不完整模板、观察尾部不足和低于能量门限。

测试范围是确定的数字数据集。单点脉冲不是实际击掌，白噪声不是教室录音；没有测真实语音/音乐误分类率。推导采样率偏差测试只覆盖一个构造条件，不等于实现了时钟重采样或证明所有设备兼容。

参考输出：`Logs/Detector/M4_2_1/reference_results.json`，原始/派生测试日志不纳入Git。

### 原业务回归与固件构建

```powershell
python -B Tests/SoundDetector/run_tests.py
python -B Tests/SoundDetectorService/run_tests.py
python -B Tests/SoundEventTime/run_tests.py
cmake --build --preset Debug --parallel
cmake --build --preset Release --parallel
```

均退出0。原检测器175组精确Fraction参考、服务5组及事件时间64组真实服务/Fraction参考通过。两种固件构建均 `ninja: no work to do`，新核心未纳入这两种固件，构建不算新增核心的ARM编译证明。

- Debug完整ELF SHA256：`eed5faf23c7ffd5e50e37e8b981c776a0e811039bac4095c0c0e21feb1a2f0a9`，前后相同。
- Release完整ELF SHA256：`e5eebfd786f32ed60f52f2afcf75f8be71e551f3d608edda997a8b3329db2ee1`，前后相同。
- Flash：未执行。Runtime：仅主机数字输入验证，无新实机结果。

独立只读复核覆盖核心、模板导出、测试和设计边界，未发现阻塞问题；复核者另行完整重跑新测试通过。复核不扩大以上有限数据集与电脑运行的证据范围。

## 下一增量

先设计满足实时预算的板端匹配接入：限制计算工作量、明确接收采样率处理、沿唯一DMA认领/归还校验路径复制或计算，匹配元数据仅在数据验证后发布。之后才接入本地PTP模型并安排手机10次/60秒静默及另一设备的声学验证。本轮不自动实施后续增量。
