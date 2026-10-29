# 诊断证据

- quiet-01：27.887650/34.306938秒分别出现U3 BOOT；用户确认未操作RESET/USB。不能归因为人工复位。
- flash日志文件结束时间与首BOOT接近，第二BOOT约在之后6秒；文件时标仅为近似，缺少原操作逐行精确时标。
- quiet-02：明确主动复位后82.552889秒单BOOT无错误；说明另一个窗口稳定，不解释首窗。
- 用户源无已知显式系统复位调用；看门狗HAL模块未启用，异常处理停留循环。仍不能据此排除所有内存/硬件原因。
- 内嵌OpenOCD program流程先reset init，末一次reset run和shutdown；没有现成证据解释第二BOOT。
- 现有固件未输出或逐启动清除RCC复位标志，因此历史原因不能直接从旧日志恢复。
- 本轮不复位附加成功，UID为A，RCC.CSR=0x0E000000（BOR/PIN/POR）；SFT/IWDG/WWDG均未置位。源码未调用会清标志的HAL_RCC_DeInit；累计标志仍不足以倒推历史事件。
- 系统中不存在本轮自行启动的其它OpenOCD/GDB进程。直接读取没有触发已知复位操作。
- 原固件受控复现flash-01使用统一monotonic时钟：Programming完成8.679323秒、VerifiedOK8.873443秒、reset9秒附近、BOOT9.030818秒、shutdown9.044177秒；70秒内只有一次BOOT。不能将原6.42秒额外启动直接归因为固定OpenOCD流程。
