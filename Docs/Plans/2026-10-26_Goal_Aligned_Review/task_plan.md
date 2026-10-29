# 任务书复核与单板可信验证

<!-- doc-status: review-and-software-boundary-fix-complete; reviewed: 2026-10-26 -->

目标：核对c79bf25之后的实际代码/测试与原任务书，给出通向提高项的单板/双板分层路径；当前只实施一个最小修复：TOA不完整窗口拒绝，不实现新的前沿算法或双板框架。

- [x] 重新读取原PDF并核对SHA256/第2–4页图示与指标。
- [x] 独立复核其他修改的TOA代码、真实PCM/串口与统计报告。
- [x] 对不完整TOA窗口及时间序列化补失败测试并修复，保持正常完整窗口行为；不完整/不可表示的输出NA，不读取未初始化尾部、不溢出格式化缓冲，不把跨块裁掉的前沿当有效。
- [x] 复跑相关主机测试、两配置构建/布局，保留用户既有改动；只声明软件边界修复，不烧录旧镜像。
- [x] 更新现行进度与原报告的验收口径，记录每步对最终距离/方向/提高项的作用。

交付：[目标路线](../../Goal_Aligned_Roadmap_2026_10_26.md)与[软件边界修复](../../Milestones/TOA_Trusted_Boundaries_2026_10_26.md)。未开始新前沿算法/双板框架。

修改范围：`App/Detector/sound_detector_service.c`修复窗口/序列化有效性；`Tests/SoundDetectorService/`新增/扩展对应测试与运行器。用户后来修改的toa_estimator.c/.h及Tests/Detector保持原样，不归并到本次提交。No CubeMX change required，不改GPIO/RCC/DMA/NVIC/Cache/门限/搜索。所有跨块PCM拼接和新前沿策略留作后续单独增量。
