# U3.3 500mm标定设置页

日期：2026-10-30。**软件实现、23项验证命令及独立代码审查通过；A板曾烧录974ed3c并完成120秒无声运行和无有效样本时的实屏保护。标定代码已在4c65a85提交（作者和committer均jxq）。500mm正向标定、启停/清除后的物理数值变化仍待实机验收，尚不能声明±5mm物理精度通过。** 基线为057dcb1，分支jxq/ui-navigation。

## 功能与操作

在A板SETUP页面接通CALIBRATE、ENABLE/DISABLE、CLEAR三个按钮。每项操作先进入CONFIRM/CANCEL确认页，松开确认按钮后才向应用提交一条请求。拖出、取消、触控读取故障、离开页面均不会误提交。没有新鲜TRACKING测量时CALIBRATE为灰色；没有保存系数时启停、清除为灰色。

- CALIBRATE：固定参考500mm，新系数=500−当前未标定温补距离；替换旧系数并自动启用。
- DISABLE：暂时应用0mm偏移，保留保存系数。
- ENABLE：重新应用保存系数，不重新取样。
- CLEAR：删除保存系数并停用；重复清除不制造新版本。

设置仅存RAM，复位后恢复未标定；未加入Flash持久化。页面显示保存/实际应用偏移、版本、当前可用原始距离及最近操作反馈。B板设置页只读显示A传来的应用偏移/版本，不猜测A是否还保存着停用系数，也不能修改A。

## 正确性边界

原始标定距离取自测量模块的当前滤波时间差和当前有效温度声速，使用原有毫米舍入公式，不取约1Hz UI文本、不取最近单个pair诊断、不重复叠加标定。只有健康A板、TRACKING、has_value、fresh且尚未达到真实共同时间新鲜截止的样本才合格；获取态、HOLD、过期、失域或B远程结果不可取样。

确认页保存measurement_id、state_revision、time_domain、authority_clock_epoch和source_common_ns。应用实际执行前重新刷新健康与期限；确认期间来源/版本改变则返回SAMPLE_CHANGED，旧系数保持。当前检测输出并不具备FORMAL标志，因此不会用该标志错误地永久禁用标定；这也不意味着本次已完成标准声身份或物理精度验收。

任何实际标定合同变化，都沿已有MeasurementResult_SetCalibration撤旧值/清跟踪器、等待新独立声音；不会把同一旧声音直接改成500mm。普通切页不改变测量。启停状态实际变化、清除已有系数及成功标定推进版本；无状态变化的重复启停/清除不推进。

## 文件与调用关系

修改/新增：

- `App/Calibration/calibration.c/.h`：保存系数、启用状态、版本和清除。
- `App/Calibration/calibration_control.c/.h`：纯前台单请求交接、权限、确认样本身份及反馈。
- `App/Measure/measurement_result.c/.h`：新增受检未标定样本只读接口。
- `App/app_main.c`：刷新真实测量后处理请求，实际变更后同步测量合同；按动作输出一次$CALIBRATION。
- `cmake/touchgfx.cmake`：用户扩展注册控制器及SBCG_USE_CALIBRATION_CONTROL宏。
- `TouchGFX/gui/src/screen1_screen/Screen1View.cpp`、`TouchGFX/gui/include/gui/screen1_screen/Screen1View.hpp`：按钮资格、二次确认、反馈与B只读。
- `Tests/Calibration/run_tests.py`、`test_calibration.c`、`test_control.c`：纯模型/控制器用例。
- `Tests/Measure/test_ranging_presentation.c`：受检样本、温补及生命周期用例。
- `Tests/App/test_app_udp_integration.c`、`run_app_udp_integration.py`：真实A/B应用标定、RST2同步及新声音恢复。
- `Tests/TouchGFXView/run_tests.py`、`run_navigation_layout.py`：确认/取消/手势、无变化不重绘及布局。
- `Docs/Plans/2026-10-30_UI_Calibration.md`与本记录。

调用：`View确认 → CalibrationControl_Request → 下一轮App RefreshRanging → MeasurementResult_Update（实际共同时间/健康）→ GetCalibrationSample → CalibrationControl_Process → Calibration_* → 实际变更时SetCalibration/Update撤值 → 既有RST2向B发布 → 新独立声音恢复数值`。

**No CubeMX change required。无新IRQ/DMA关系。** 不修改GPIO、TIM、NVIC、SAI、ETH、LTDC、I2C、RCC、MPU或cache配置；沿用U3.2触控轮询、SDRAM前端存储及原网络协议。未改生成区或HAL。UI不领取PCM或消费检测队列。

## 软件验证证据

证据：`Logs/UI/U3_3/software-20261030-a6y_ree2/verification.json`及01–23输出，全部exit 0；原生A/B命令、报文和快照见同目录native_A.jsonl/native_B.jsonl。Logs不提交。

- 模型/控制器：O0/O2均通过，保存/启停/清除/版本、零偏移、重新标定、失败保持、单请求占用、五项样本身份变化、B权限拒绝。
- Measure：10个目标在O0/O2共20次运行通过。受检原始距离不含既有偏移，当前温度变化会改变raw；HOLD/过期、获取态、无效域/温度及B均拒绝，getter不改变展示或期限。
- UI：原13文本和四页导航保留；两次点击确认、取消/离页、确认样本身份冻结、长按不提前提交、错误释放/拖出不提交、B只读；不变状态下20次tick不增加设置按钮失效计数。
- 真实A/B应用：75项通过，包括原47项和新增28项标定断言。仅替代HAL/音频/PTP/LwIP边界，生产App、控制器、模型、结果缓存和线协议真实执行。
- TouchGFX服务/BSP/App、旧UI App、触控、配置/生成预检、UDP波形证据、原位构造和两种ELF的音频/ETH/前端布局均通过。
- 页面及确认状态使用实际生成字符宽度检查；近似离线预览仅为测试输入，不是实屏照片。
- 独立只读审查结论：PASS — SOFTWARE ONLY，未发现需修正的P1/P2问题。

### 真实应用主机回归的数值链

| 动作后等待新独立声音 | A显示 | B显示 | 保存偏移 | 应用偏移 |
|---|---:|---:|---:|---:|
| 初始未标定 | 309mm | 309mm | 无 | 0mm |
| CALIBRATE到500mm | 500mm | 500mm | +191mm | +191mm |
| DISABLE | 309mm | 309mm | +191mm | 0mm |
| ENABLE | 500mm | 500mm | +191mm | +191mm |
| CLEAR | 309mm | 309mm | 无 | 0mm |

每次实际合同变化先使两板旧值撤销，再由新声音恢复。B直接提交四种动作都被A_ONLY拒绝；过期/无效时钟CAPTURE保留原系数。以上为受控主机数据，不是卷尺物理测距或±5mm精度证明。

### 构建与内存

现有命令`cmake --build --preset Debug --parallel 4`、Release同名preset均通过。

| 项目 | Debug | Release |
|---|---:|---:|
| FLASH | 361544B | 230748B |
| ITCM | 14688B | 14688B |
| 前端SDRAM对象 | 5792B/8192B | 5792B/8192B |

RAM链接汇总仍为317920B，含固定高地址网络保留区，不能作为普通BSS可用量。前端仍位于0xC0080000，原音频/ETH/帧缓冲布局不变。首次宏变更导致全量重编译，显现未修改的status_view.c格式截断、udp_chirp_capture.c格式类型及既有RWX链接警告；本次修改文件未引入新编译警告。

ELF SHA256：

- Debug：`974ed3cd9ec877bd2c770d7187f17b2fb77d8bbc8e29bc4f1d1a473080873ad7`
- Release：`a2d563a234daea91a5b6a3b11aa8fbb319a05695fddddda7e63549d2571fc7e8`

## 实机步骤与未闭合项

用户已明确要求“烧录”，A板本版974ed3cd9ec8已沿原ST-LINK配置写入、Verified OK、复位成功，不修改选项字节。B板本次未烧录。镜像身份以实际校验记录和SHA256为准，原BOOT的FW标签仍为SBCG-A2B-RANGE-R1。

### A板无声初验（已通过）

证据目录：`Logs/UI/U3_3/hardware-20261030-vp3mi2o0/`，包含flash.json/txt、A串口原始字节/JSONL、采集元数据和analysis.json。采集150.012秒，新BOOT后覆盖124.143秒，分析限定为新BOOT后的120秒；UID为001A002D:30365105:36363631。

| 指标 | 120秒结果 |
|---|---|
| 音频 | RUN，末条OK=701，DROP=0、ERR=0、HAL=0、DMA=0 |
| UI | ERRORS=0，末条FRAMES=6710，最大UPDATE_MS=20 |
| 前台 | LOOP_MS最大46，AUDIO_MS最大29，UI_MS最大1，FLUSH_MS最大16；各自最大值不能相加 |
| A本地PPS | QUALIFIED，末条EMITTED=119，MISSED=0、ERRORS=0、FAULTS=0、RECOVERIES=0 |
| 重启 | 仅一次烧录后的BOOT，之后未再次启动 |
| 网络/动作 | NET=DOWN；$CALIBRATION动作记录0条 |

用户实屏反馈：“显示与灰色保护符合描述，四页切换正常”。观察提示包括NOT CALIBRATED、保存/应用+0mm、NO FRESH TRACKING、三个按钮灰色且点按不改设置。该证据只证明无有效测量时的界面和保护，不证明标定正向路径或精度。

烧录复位发生在旧PROFILE2行发送途中，原始JSONL首个BOOT前保留了截断旧行及替换字节。分析只从该行实际$BOOT标记之后识别新启动，原始字节和记录均未清理；后续运行窗口未发现同类替换字节。

### 正向路径仍需补验

1. 单板无声初验已完成，见上文；此项不替代下列正向标定。
2. 正向标定：两板接同一交换机，卷尺量麦克风孔中心间距500mm，声源在A外侧并共线；待共同域、音频及测量TRACKING有效。
3. 播放标准音期间，在A SETUP看到RAW新鲜且CALIBRATE可用，点击后核对500mm提示，再CONFIRM。若提示SAMPLE CHANGED，等待下一次新鲜值再确认；不得放宽新鲜校验来强行通过。
4. 确认成功时旧值先撤为--，后续独立声音两板应同显约500mm。DISABLE保存偏移不变，后续声音恢复未标定值；ENABLE恢复校正；CLEAR后不能再ENABLE旧系数。每项都检查两板有效偏移/版本和同次结果一致。
5. 异常/保护：确认时取消、拖出、离页均不改系数；过期/断域无新样本不能标定；B按钮只读；复位后标定清空（本版本无持久化）。
6. 保存操作/串口/实屏证据后才能声明相应硬件路径通过并定向提交，作者/提交者jxq。用户已有LICENSE删除及Analysis/Docs/nul保留，不stage、不push。

现场标定功能通过不等于任务书0.20–5.00m全程±5mm通过；后者还需独立多点卷尺验收。击掌与实时同步波形仍是后续独立增量。
