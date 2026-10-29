# M2.1.3 TX Hardware Timestamp Implementation Plan

<!-- doc-status: archived-plan; reviewed: 2026-10-23 -->

> **已完成的历史方案，请勿重复实施。** 仅保留当时范围、授权和生成检查；其中“当前/下一步/待验”均按原日期理解，不构成新授权。验收见[对应报告](../Milestones/M2_1_PTP_TX.md)，现行入口见[文档导航](../README.md)。

> 使用superpowers:executing-plans在现有jxq功能分支逐项执行，不新建工作树；功能提交需实物通过。

**Goal:** 为30个原UDP应答取得唯一匹配的TX硬件时间，保持既有RX和网络行为。

**Architecture:** 用户linkoutput包装只观察单pbuf合法应答；BSP保存一次性发送令牌，申请TTSE、验证完成描述符并复制DESC6/7。4条CPU队列在主循环限速输出，不改生成代码或HAL。

**Tech Stack:** STM32F746、CubeF7 1.17.4、NO_SYS LwIP 2.1.2、C11、GCC/CMake和原生测试。

依据：[用户已确认设计](2026-10-20_PTP_TX_Design.md)。接口边界如下，命名供测试与实现共用：

```c
BSP_PtpStatus BSP_PTP_TxClean(void);
BSP_PtpStatus BSP_PTP_TxBegin(const uint8_t *buffer, uint16_t length);
BSP_PtpStatus BSP_PTP_TxEnd(BSP_PtpTime *time);
typedef void (*PtpTx_Log)(const char *line, int length);
void PtpTx_Init(uint32_t now_ms, const uint32_t uid_words[3], PtpTx_Log log);
void PtpTx_Process(uint32_t now_ms);
```

当前：步骤1～3已完成，109项检查及构建/布局/再生成通过；步骤4本固件授权烧录、30组RX/TX/ACK、70包零缺口及91条PTP/心跳联合验收通过；按jxq提交并停止本增量。

## 1. 描述符边界

- [x] 新建App/BSP/bsp_ptp_tx.h/.c（初始空桩）、Tests/Ptp/test_bsp_ptp_tx.c；修改Tests/Ptp/run_tests.py、stubs/main.h以使用真实HAL类型和替代屏障。运行python -B Tests/Ptp/run_tests.py，观察合法申请断言失败。
- [x] 测试合法DESC6/7来源、索引回绕和残留TTSS清除；完成最小BSP实现后要求通过。
- [x] 测试空/超长缓冲、状态/增强格式/映射错误、DMA拥有、已有PacketAddress、HAL申请失败；所有前置失败不得破坏描述符或创建有效令牌。
- [x] 测试OWN/ES/TTSS/FS/LS/索引/地址/长度不符，过早/未来/越界时间、模式丢失、空输出；失败输出保持不变，一次性令牌不能重复领取。
- [x] 测试DMA仍拥有时不写描述符，归还后可清理TTSE，环映射异常不盲写；BSP_Read的前后时间及屏障只用于所有权顺序，不替代DMA捕获。

## 2. linkoutput观察与队列

- [x] 新建App/Sync/ptp_tx.h/.c（初始空桩）和Tests/Ptp/test_ptp_tx.c；扩展run_tests.py。先观察原发送入口未被包装时的失败。
- [x] 实现仅本板UID、源5006/目标6006、目标电脑IP、未分片IPv4/type=3/28字节长度正确的应答键解析。到达linkoutput时才申请；非目标帧原样发送，目标链式pbuf记UNSUPPORTED后原样发送。
- [x] 原入口返回后立即结束一次性令牌，保存硬件快照/键到4条固定队列，保留原返回值，不持有或释放pbuf；主循环每秒至多输出一条记录和一条统计。
- [x] 测试正常键、真实UID、ARP延迟到达、非法/非目标/分片/链式帧、申请或快照失败、原返回错误、队列溢出/排空、tick回绕和重复初始化；不得生成软件替代时间。

## 3. 应用接入与构建

- [x] 修改Tests/App/test_app_main.c：先要求RX后初始化/处理TX并使用SBCG-M2.1-TX，观察接入缺失失败。
- [x] 修改App/app_main.c接入PtpTx_Init/Process，CMakeLists.txt既有LwIP分支注册两个新源码。生成文件不改。
- [x] 运行Tests/Ptp、App、UdpProbe、Udp、Network的run_tests.py及python -B -m unittest discover -s Tools/Monitor/tests -v，逐项记录退出码及实际检查数。
- [x] python -B Tools/Monitor/autodebug.py build及Release构建；分别运行check_eth_layout.py，要求无新未解释诊断、布局和公共宏一致。
- [x] 请求只读实现复核；先解决阻塞问题，再做源码快照与实际CubeMX Generate Code回归，保留许可证并核对源码/配置/ELF哈希，复跑构建/布局。

## 4. 实物及交付

- [x] 可审阅固件准备好后取得对应烧录/自动验收授权。使用原ST-LINK配置program verify reset，不改选项字节。
- [x] COM3 115200/8N1同步记录新BOOT、至少60条PTP/心跳、70秒5005接收及30个每秒请求；结束后留至少5秒排空日志。
- [x] 核对30个请求各有唯一RX、TX和ACK，UID/序号/tick匹配，事件单调且TX≥同请求RX，全部错误/溢出0，5005观察区间零缺口。保存原文、哈希、实际RTT，明确LOCAL/SYNC=0和未测边界。
- [x] 更新本计划、设计状态、Docs/Milestones/M2_1_PTP_TX.md、Project_State、Docs入口和根README；核对最终差异，仅暂存本功能文件，以jxq提交并按现有协作授权推送。停止，不开始同步算法。

测试与实现必须按失败→最小实现→通过推进；硬件结果不能预填。最后提交/推送结果以Git历史及远端核对。
