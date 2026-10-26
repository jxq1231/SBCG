# 单板TOA可信边界修复（软件验证）

<!-- doc-status: software-boundaries-verified-not-flashed; reviewed: 2026-10-26 -->

## 本次推进的目标

为最终两板时间相减提供可信输入，先修复当前TOA窗口/输出内存边界；不实现新的直达波算法，不声明±100μs、方向或测距达标。原任务书与当前单板/双板顺序见[目标路线](../Goal_Aligned_Roadmap_2026_10_26.md)。

## 修改文件

- `App/Detector/sound_detector_service.c`：所需左边界不在当前借用半块、或右侧不足完整256帧时拒绝TOA估算，保留能量候选、输出NA；完整窗行为不变。TOA文本缓冲从16改为21字节，转换前拒绝负数、非有限值及舍入后达到2^64的微秒值。
- `Tests/SoundDetectorService/test_toa_boundaries.c`：新增真实服务/检测器配调用边界替身，核对完整窗提取/双声道、缺左沿/192点尾部拒绝、End失败/NA后恢复、17位文本和非法时间数值。
- `Tests/SoundDetectorService/run_tests.py`：O0/O2严格编译、原真实估算器服务/批量测试及新增边界测试；每次唯一输出目录，原断言未改。

用户未提交的toa_estimator.c/.h、Tests/Detector及PPS/CubeMX文件保持；本提交不包含这些改动。本轮构建基于含上述用户改动的工作区，不能把它当作裸提交或新实物镜像验收。

## 调用和硬件关系

`App_Process → AudioCapture_Process → 原唯一Begin → SoundDetectorService_Stage → EstimateToa资格检查 → 原ToaEstimator → 原End/Complete → Process范围检查/输出`。

No CubeMX change required；没有新IRQ或DMA关系，没有引脚/时钟/缓存变化。仍使用当前SAI DMA半块，TOA只读取本次已借用的PCM，并在真实End成功后发布。当前没有跨块PCM重组；不完整事件会明确NA，这可能减少边界附近的有效TOA覆盖率，不能为凑10/10补零或裁掉前沿。

## 验证结果

1. 原真实估算器与服务/批量测试先在当前用户算法上通过，正常斜坡TOA_US=188保留。
2. 窗口RED：192点尾窗、跨块缺左沿在旧代码O0/O2下均错误调用估算器，失败于调用次数断言；完整256点控制组通过。
3. 序列化RED：17位TOA使旧O0的CONFIRMED、旧O2的FIRST被覆盖；合法极大块首还会错误输出0。新增断言在旧代码均失败。
4. 最终GREEN：`GREEN_1793002403621049500`六编译、二十运行全exit0；覆盖原窗口、双声道、冷却不变、End/NA恢复、长文本、负数/小负数/NaN/±Inf/超范围。局部toa_ok仍是估算成功次数，不冒充可表示微秒或声学有效性。
5. 主代理独立重跑SDS运行器exit0；TOA、Audio、ChirpService、App、SoundEventTime相关回归亦退出0。最终序列化修改后重新跑SDS及重建/布局；没有以初次较早结果替代最终检查。
6. Debug/Release及音频/ETH四项布局最终全部通过。初次编译的21/16字节告警已消除，仅保留原RWX LOAD链接告警。
7. 独立只读窗口审查和新增序列化增量审查均PASS，无必须修正项；58个范围外文件散列保持，包括用户正在完善的估算器。

证据：`Logs/Detector/TOA_Boundary_Fix/BASELINE_1793001467333721800`、`RED_1793001698912349700`、`RED_1793002330572941600`、`GREEN_1793002403621049500`；主代理结果在`Logs/Goal_Aligned_Review_20261026/`，包括`final-SoundDetectorService.txt`、`tests.json`、`final-builds.json`及布局输出。早期恢复测试夹具遗漏原冷却导致的失败保留，修正只涉及夹具。

最终工作区ELF SHA256：Debug `45bc8c82e342a033f4d2ec09379f5c0aa0b6330d96434ad465c8b426a6fe265c`，Release `d12af676358350fc8e8ae95ba1413dac2cc939504fc0be29f55a8ecc5d06b75a`。

**本轮未烧录、未播放或操作硬件。** 不重复已经由后续修改完成的旧c219af诊断。下一独立目标优先用现有PCM做全事件对账/前沿与身份验证；跨块完整窗恢复、PTP子帧映射及最终两板精度仍需后续逐项实现和实物补验。
