# U3.1：等价TouchGFX状态页

<!-- doc-status: archived-plan; reviewed: 2026-10-25 -->

本轮显示范围已交付，首窗未知重启保留为待查问题，见[范围与证据](../Milestones/U3_1_TouchGFX_Status_Page.md)。本计划不再重复执行。原目标仅迁移已验收U2状态页；不再次实施色条、SDRAM全片自检或声音实验。

## 唯一目标

在单A、无网线/第二板/示波器、不主动发声条件下，TouchGFX真实显示业务状态，静默运行不破坏音频/时间模型/串口；距离、方向、质量保持待测占位。

## 配置与代码边界

- CubeMX：X-CUBE-TOUCHGFX4.26.1、NoOS、LTDC VSYNC、RGB565/480×272、双帧C0000000/C0040000、CRC及LTDC IRQ9/0。维持9.6MHz、FMC/MPU、音频/温度/网络、I/D Cache关闭；不启用DMA2D/触控。
- `MX_LTDC_Init`与`MX_TouchGFX_Init`延后且全局可调用。CubeMX列表只暴露Init，仍自动生成的Process由空链接包装拦截，真实入口只由应用服务调用；不手改.ioc或生成初始化。
- Designer生成`gui_generated`、字体/文本资源及`cmake/touchgfx`；用户View只绑定共享文本。官方生成命令会读取`.part`并执行`touchgfx update_project`，自动加入根CMake的子目录；自有片段不重复登记框架/字体/引擎。
- 用户BSP在SDRAM显示READY后、音频启动前清双缓冲并调用生成LTDC；应用业务就绪后启动框架。
- 主循环音频归还后调度UI；DHT起始低态跳过整次UI。业务文本约1Hz，框架按VSYNC前台处理。LTDC IRQ只转交框架及原子可见地址，不绘图。
- 背光开启须有同一缓冲的实际绘制完成与可见证据；允许两通知任意先后。无绘图endFrame只计数，不能作为绘制证据。HAL/raw FU/TE错误使显示停止，保留框架换页/IRQ所有权。

## 修改入口

`App/UI/status_snapshot.*`复用旧RGB565只读采集；`touchgfx_service.*`调度与诊断；`App/BSP/bsp_touchgfx_display.*`启动/错误；`App/app_main.c`后端分派；`TouchGFX/gui`、`target/TouchGFXHAL.cpp`安全用户区绑定界面；`cmake/lcd.cmake`与`touchgfx.cmake`衔接官方构建；对应`Tests/TouchGFX*`、配置预检和离线日志分析。

## 验证关口

1. 原生服务/BSP/View/App及布局测试，包含真实GNU链接包装、首帧抢占通知、DHT门控、错误关闭。
2. Debug/Release既有预设构建、ETH/音频布局；原工具/分析与音频/温度/串口回归。
3. 冻结ELF和完整输入快照，经现有ST-LINK先核对A UID，再烧录校验复位。
4. 约90秒静默串口：单新BOOT、UI RUNNING/零错误、更新与帧计数分别推进、PCM无丢块/错误、原模型保持。
5. 手动25/30°C与AUTO恢复，确认UI温度/声速采用真实业务副本。
6. 用户目视状态页正常后才记录实屏通过；无网线/第二板/示波器不扩展为双板或外部精度结论。

执行结果统一登记[U3.1记录](../Milestones/U3_1_TouchGFX_Status_Page.md)，不以本计划代替验收。
