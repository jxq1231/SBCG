# 高级功能七项审查问题的软件修复计划

基线：5fd8bfa；用户已授权修复七项P2，实机验收另行进行。本轮只形成软件检查点，不烧录、不播放、不修改硬件资源或生成配置。

## 唯一目标

闭合本轮审查的七项可复现软件问题，保持标准音、同步闭环、温度、标定及四页导航的既有合同。各组先建立失败回归，再实现、主机验证和构建，定向提交为jxq，最后集中复核。

## 改动范围与方案

1. `App/Detector/event_classifier.c/.h`、`Tests/Detector/test_event_classifier.c`及必要的分类/服务测试：保持CHIRP判据，CLAP只接收具有独立瞬态/宽带支持的脉冲；纯音、截断标准音和不满足条件的声音归UNKNOWN。用真实模板和合成脉冲做正反例，不靠下游重复判断。
2. `App/UI/waveform_service.c/.h`、`Tests/Waveform/test_waveform.c`：分数周期估计与跨计划连续的共同相位参考；首次可以用A过零建立参考，后续不随A声学相位重置。覆盖只移动A、只移动B、1882.35Hz半帧周期及原997Hz/2kHz、换域和调频失效。保持环形PCM所有权、定容内存、5Hz计划和共同deadline。
3. `App/Measure/clap_ranging.c/.h`、`Tests/Clap/test_clap_ranging.c`：B对RST2和CLP1使用同一有序标定合同，旧合同不恢复、旧状态不撤掉新合同结果；沿现有会话隔离。已发布结果冻结温补，即使后续温度失效，两板仍按原声音源期限共同保留并到期；A温度无效拒绝新发布。
4. `App/app_main.c`、`App/Network/udp_sound_event.c`、必要头文件、`Tests/App/test_app_udp_integration.c`、`Tests/App/run_app_udp_integration.py`、`Tests/Udp/test_udp_sound_event.c`：将音频index_faults纳入既有健康条件；连接有序标定合同接口；粗配对使用足够覆盖有效温度范围的时间窗，最终10m边界由权威温补结果处理，不能以固定343m/s提前拒绝。补真实A/B应用旧包两种次序、温度失效、索引故障及低温10m/超量程回归。
5. 本计划及对应`Docs/Milestones/`修复记录：记录软件证据、提交与实机未验项，不将软件测试写成实物精度通过。

## 资源与约束

**No CubeMX change required。** 无新GPIO/TIM/NVIC/DMA/SAI/ETH/LTDC/I2C/cache关系。只使用已有CPU PCM副本、前台协议和应用状态；不改.ioc、HAL、生成区、协议包长度、原始PCM或显示平滑掩盖错误。保留既有LICENSE删除、Analysis/Docs未跟踪文件和nul。不push、不改写历史。

## 检查点

- [x] 七项旧行为的定向失败证据与最小修复。
- [x] 分类、波形、拍掌、测距、UDP和真实双进程应用回归。
- [x] Debug/Release现有预设构建及内存布局检查。
- [x] 交叉审查新逻辑、必要的边界单次校验和资源变化。
- [x] 核对diff，定向提交软件检查点（jxq/jxq），保留所有用户文件。

完成证据见 `Docs/Milestones/2026-11-02_Advanced_UI_Review_Fixes.md`。本轮软件测试通过不等于物理精度通过，实机验收仍未执行。

此前全套测试有四组既存失败（ChirpMatcher资产期望、Sdram应用stub、SoundDetector初始化器、SoundDetectorService日志断言），另有旧UI服务stub失败。本轮若受影响用例必须修复；其他既存失败单独如实记录，不放宽断言掩盖功能问题。

## 后续实机验证

另行统一验证真实拍掌/纯音/截断标准音隔离，移动A和B时各自相位变化，非整数周期单音稳定性，两板标定变更与温度失效一致性，以及低温参数下长距离输出。物理精度、误触发和两屏刷新时差仍须现场证据；本轮不宣称通过。
