# CLAP与共同时间波形：软件实现计划

用户于2026-11-01明确要求先实现后续功能，再集中实机验收和排错。本轮允许依次完成多个软件增量；软件测试和构建仍逐项执行，提交只表示软件检查点，不声明实物精度通过。

## 范围与设计

1. 保留RANGE标准音业务、同步闭环、温度和现有标定；页面导航只影响显示，不启停测量。SETUP现有标定不重新实现。
2. 接通CLAP：使用已有分类和TOA时间事件，CLAP配对不走CHIRP模板波形精化或标准序列门控；A独立计算一次击掌距离/方向/质量，B显示A权威副本。结果与RANGE分开保存。共用RSC2资格，失域撤值，源时间决定2.5s新鲜/6.5s撤值，重复包不续租。CLP1 80B结果和CLA1 24B确认，精确键确认，500ms重试含失败退避。
3. 接通WAVE：在既有唯一PCM借用中复制左声道，真实End通过后提交到CPU自有历史环；UI不领取DMA块。A根据稳定周期/参考过零确定共同历史窗口，B采用相同窗口而不独立重置相位。无共同模型时仅显示明确LOCAL波形。A计划WVP1 44B，200ms节流，使用既有声音UDP端点，不增加PCB。
4. TouchGFX四页保留；CLAP显示独立结果，WAVE使用定容Widget绘制真实256点波形，只在新frame_seq时重绘。新增控件后检查FrontendHeap不超过8KiB。

## 允许修改的文件

- 新增`App/Measure/clap_ranging.c/.h`、`App/Network/clap_wire.c/.h`、`Tests/Clap/`：独立模型、协议和主机测试。
- 修改`App/Measure/sound_pair.c/.h`及其测试：输出类型，CLAP绕开chirp专用精化，保留同类型配对规则。
- 新增`App/UI/waveform_service.c/.h`、`Tests/Waveform/`：CPU历史、共同时间计划和快照测试。
- 修改`App/Audio/audio_capture.c`：在已有Begin/End路径扇出Stage/Complete，不增加采集消费者。
- 修改`App/app_main.c/.h`：生命周期、现有锚点扇出、CLAP结果路由和UI只读接口。
- 修改`App/Network/udp_sound_event.c/.h`：固定端点、magic+长度分派新短包，复用既有PCB。
- 修改`TouchGFX/gui/include/gui/screen1_screen/Screen1View.hpp`、对应cpp及`Tests/TouchGFXView/`：独立页面绑定和轻量绘图。
- 修改用户自有`cmake/audio-layout.cmake`及新增SDRAM链接扩展：注册源码和CPU波形历史保留区；不改CubeMX生成CMake/链接脚本。
- 修改真实A/B应用runner与测试：新增通道闭环，保留原标准音/标定75项。

## 资源与复杂度

**No CubeMX change required。** 不改引脚、RCC、TIM、DMA、NVIC、SAI、ETH、LTDC、HAL或cache配置。沿现有SDRAM8MiB非缓存区，在0xC00A0000保留128KiB CPU波形状态，含32768帧环和8192帧暂存，避免挤占已满内部RAM。与帧缓冲/0xC0080000前端状态分离，NOLOAD有效元数据由Init建立，不读取未提交区域。

只在网络/PCM边界校验外部输入；内部传递已验证结构。新增实例仅用于CLAP独立权威状态，波形为单前台服务；不建立通用消息框架、异常包装或闪存持久化。校验样本期满、失域、类型混配和真实内存容量属于当前可复现正确性要求；不新增极小概率计数耗尽/并发兜底。

## 软件执行检查点

- [ ] 核对接口和当前受保护工作区；每个模块先记录失败测试，再实现。
- [ ] CLAP模型/wire及SoundPair回归通过。
- [ ] 波形提交/断流/共同窗口/周期及位相差主机回归通过。
- [ ] 四页导航、标定确认/取消、CLAP/WAVE绑定及绘图区裁剪通过。
- [ ] App唯一PCM消费者/锚点扇出、新UDP分派和真实A/B闭环通过，原75项保留。
- [ ] Debug/Release沿现有预设构建；音频、ETH、前端和新SDRAM保留区布局通过。
- [ ] 定向审查差异并分别记录软件提交；保护用户LICENSE删除、nul及既有未跟踪交接/分析文件，不push。

## 后续集中实机验收

软件阶段不自动烧录。另经当前工作流授权后验证：四页触控；标准音只产生标准结果；击掌只产生击掌结果且两板数值/方向一致；静默60秒无误结果；波形周期单音稳定、两板共同窗口/刷新、移动仅改变相对相位；标定500mm、停用/启用/清除及温补规律；全过程同步/音频DROP/主循环耗时无退化。没有对应实测时，文档保持“软件实现，待实机验收”。
