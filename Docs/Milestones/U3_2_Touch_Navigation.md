# U3.2 四页触控导航

**日期：2026-10-30。状态：A板四页导航增量已验收；软件、烧录、120秒运行及用户实屏确认通过。双板声音业务回归另列待验。**

基线 `71ea739`，实施分支 `jxq/ui-navigation`。本增量只接通触摸和页面导航。击掌测距、实时波形及标定按钮的业务操作仍未接通，不能把页面出现当成这些提高项通过。

## 当前行为

480×272现有TouchGFX屏底部新增RANGE / CLAP / WAVE / SETUP。选中项高亮，按下后在同一按钮内松开才切页；拖离、取消、无效触控读取不切页。进入其他页不停止后台测距；原13项业务文本仍按原版本更新，返回RANGE显示最新副本。SETUP的CALIBRATE / ENABLE / CLEAR以灰色禁用并明确标记未接通，不调用标定API。

现有ASCII字体继续使用，未修改Designer生成基类、字体、文本数据库或`.touchgfx`。离线预览为测试数据与近似字体渲染，不是实屏截图。

## 修改文件

- `App/BSP/bsp_touch.c/.h`：FT5336身份、限频单点读取、诊断副本。
- `TouchGFX/target/STM32TouchController.cpp/.hpp`：仅安全用户区内适配BSP。
- `TouchGFX/gui/src/screen1_screen/Screen1View.cpp`、`TouchGFX/gui/include/gui/screen1_screen/Screen1View.hpp`：四个逻辑页面、导航和原文本绑定。
- `TouchGFX/gui/include/gui/common/FrontendHeap.hpp`：显示READY后显式清零、原位构造前端存储。
- `cmake/touchgfx.cmake`、`cmake/linker/stm32f746_touchgfx.ld`：用户扩展点注册源及专用CPU显示状态区。
- `Tests/TouchInput/run_tests.py`、`test_touch.c`、`test_adapter.cpp`、`stubs/main.h`、`stubs/platform/driver/touch/TouchController.hpp`：实际BSP/HAL边界、真实适配器回归。
- `Tests/TouchGFXView/run_tests.py`、`run_navigation_layout.py`、`check_frontend_layout.py`、`run_frontend_storage.py`：导航手势、真实字符宽度、实际ELF布局及首次存储清零/构造检查。
- `Tests/UI/test_app_lcd.c`、`Tests/TouchGFXService/run_app.py`：修复既有主机测试链接组织问题，恢复TouchGFX实际App调度门控检查；不改应用业务代码。
- `Docs/Plans/2026-10-30_UI_Navigation.md`、本记录：设计、实施及待验收边界。

## 调用、总线和资源

`App_Process → UI_TouchGFX_Process（原DHT门控）→ 真实TouchGFX前台 → STM32TouchController::sampleTouch → BSP_Touch_Sample → HAL_I2C_Mem_Read`。

触控与WM8994复用已生成I2C3（PH7/PH8），前台顺序读取；没有I2C初始化、控制器写入、总线恢复或复位。首次一笔身份读，正常至少50ms一笔坐标读，忙/身份不符/IO错误后至少250ms再尝试。坐标按本地官方CubeF7 V1.17.4的TS_SWAP_XY直接交换原始像素，不按4096缩放。

最近采样`sample_valid`区分正常松手与无效输入：多点、越界和读失败取消手势。2ms仅为HAL超时参数；HAL内部BUSY等待可能更长，实际共享总线耗时仍需上板观察。

**No CubeMX change required。无新IRQ、DMA关系，无SAI/ETH/PTP/PPS/引脚/时钟/MPU/cache配置修改。** 继续沿用现有LTDC中断与双帧缓冲；UI不读取音频DMA指针或消费检测队列。

## 必需的内存修复

首轮构建发现新增控件使片内`.bss`结束于`0x200102EC`，与音频DMA起点`0x20010000`重叠，原链接断言正确阻止输出镜像。

完整TouchGFX前端对象改存独立`.TouchGFXState`：`0xC0080000`，NOLOAD/NOBITS，实际5760字节，上限8192字节；位于第二帧缓冲末尾`0xC007FC00`之后。沿已有SDRAM READY及I/D cache关闭条件，在`touchgfx_init`首次获取前端时显式清零后placement-new。无运行堆分配，也不依赖启动代码清零外存。原音频和网络保留区不变，两种ELF布局检查通过。

## 软件证据

原View、布局和服务主机基线通过；新增导航用例在旧View上出现预期失败：松开CLAP后测距控件仍可见。适配器在旧固定false实现上亦出现预期失败，随后真实新实现通过。

已通过：

- TouchInput八组BSP用例及真实STM32TouchController适配用例。
- View原13项文本、四页往返、按下/松开、长按、拖出后返回、CANCEL、错误释放、间隔/屏外、隐藏期间文本更新、禁用设置操作。
- 实际生成字体字符覆盖/宽度与页面边界检查；导航不覆盖业务文本。
- TouchGFX服务、BSP、App真实前后台顺序、DHT保护及独占显示；旧RGB565 App调度测试亦通过。
- 原真实A/B双进程47项断言，包括结果一致、ACK重试、6.5s撤值、掉块及失域恢复；原UDP波形证据回归。
- TouchGFX配置五项测试及LTDC配置11项变异检查。
- Debug/Release的前端、音频和ETH布局检查。
- 真实FrontendHeap原位构造：先用0xA5填充实际独立存储，检查构造前已全部清零、仅构造一次、重复访问保留新状态；去掉清零的Logs负控副本按预期失败。

详细逐命令输出及退出码：`Logs/UI/U3_2/navigation-20261030-v3eif0ap/verification.json`及同目录`.txt`，共19个检查命令，全部exit 0。功能范围及代码质量两次独立只读审查通过。Logs不提交。

构建：`cmake --build --preset Debug --parallel 4`、`cmake --build --preset Release --parallel 4`均通过。保留既有RWX LOAD段链接警告，无新编译警告。

| 项目 | Debug | Release |
|---|---:|---:|
| FLASH | 356128 B | 227820 B |
| ITCM | 14688 B | 14688 B |
| TouchGFX专用SDRAM实际对象 | 5760 B | 5760 B |

链接器RAM汇总317920B含固定ETH等高地址保留区，不能把该百分比直接当成BSS剩余空间。

ELF SHA256：

- Debug：`f9f8d5caebadecd0a4ae297f4efb89a0009ffb587aea2a11a904366ce83dae2d`
- Release：`4573950e5e67afc057895d2a485b749fdb378bd4c884f39bff7c3b2de4ba5eb4`

## 实机验收与提交条件

软件验证结束时仅有ELTIMA虚拟COM1/COM2。随后用户接回A板，已确认COM3及ST-LINK序列号`066EFF3832394D3457253156`；B板COM5尚未出现。只读采集原程序6秒得到185条日志，ROLE=A、音频DROP=0/ERR=0、UI ERRORS=0，NET=DOWN；这只是原程序连接基线，不是新UI验收。

连接基线：`Logs/UI/U3_2/navigation-20261030-v3eif0ap/A-connected-baseline-*`。该基线采集不复位、不播放；随后用户明确授权本镜像烧录。

### A板已取得的运行证据

沿原`.vscode/openocd-stm32f746.cfg`对唯一在线A板执行`program {build/Debug/SBCG.elf} verify reset exit`，退出0、Verified OK；ELF SHA256为本文Debug值`f9f8d5caebad…`。不改选项字节。原固件FW标签未因本UI增量修改，镜像身份以实际写入/校验记录及SHA256为准。

采集共150.003秒，包括烧录前基线及烧录过程；新BOOT后覆盖124.530秒。分析严格限定为该BOOT后的120秒，UID匹配A，仅一次新BOOT。起始首条UI为READY，随后119条均RUNNING，这是正常初帧就绪过程。

| 指标 | 新BOOT后120秒结果 |
|---|---|
| 音频 | RUN，末条OK=700；DROP=0、ERR=0、HAL=0、DMA=0 |
| 显示 | ERRORS=0，末条FRAMES=6775；最大UPDATE_MS=20 |
| 主循环 | 最大LOOP_MS=46、AUDIO_MS=33、UI_MS=2、FLUSH_MS=16；分段最大值不是同轮相加值 |
| A本地PPS | QUALIFIED，末条EMITTED=119、MISSED=0、ERRORS=0、FAULTS=0、RECOVERIES=0 |
| 温度 | 已从启动预热恢复OK，末条24.0°C，声速345844mm/s |
| 网络/同步边界 | NET=DOWN、PTP=LOCAL；未验证双板共同域或测距 |

证据目录：`Logs/UI/U3_2/hardware-20261030-rxe9aawb/`，包含`flash.json`、`flash.txt`、`A-serial.bin/.jsonl`、采集元数据及`analysis.json`。没有主动播放声音。`max_read_ms`未在本镜像串口输出，本次只能报告真实UI/主循环总体耗时，不能宣称已单独测得触控事务耗时。

用户已按RANGE→CLAP→WAVE→SETUP→RANGE检查，并明确反馈：“四页切换正常，显示清晰，取消手势正常”。该反馈作为实屏观察证据，与串口运行证据共同关闭本次A板触控导航增量；不能据此认定击掌测距、实时波形或标定业务通过。

1. 接回A板供电和ST-LINK，确认串口/probe身份。依据AGENTS第20节，取得本新镜像烧录授权后，才沿原配置校验和复位。
2. 无需发声：先确认四角方向和四个导航按钮；长按、拖出、设置灰色控件不得误操作。检查从任意页返回RANGE。
3. 连续120秒切页和停留，核对音频DROP/ERR、PROFILE LOOP/UI、原PTP/PPS/DOMAIN及串口活性；记录触控诊断的max_read_ms，不能把2ms参数当实际耗时证明。
4. B板更新同一镜像后，方便发声时复验原标准声测距、两屏一致及静默撤值。切页期间原业务须连续运行；当前主机测试不能替代此项。
5. 用户实屏确认已取得，按本次文件清单定向提交触控导航。现有LICENSE删除及用户未跟踪Analysis/Docs/nul全部保留，不stage、不push。

下一独立增量为A板标定按钮业务；实时同步波形、完整击掌链及对应课程精度另行实现验收。
