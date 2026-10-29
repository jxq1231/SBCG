# 规划依据：已核实事实与未确定原因

<!-- doc-status: planning-evidence-not-new-hardware-acceptance; reviewed: 2026-10-28 -->

## 当前基线

- 仓库：`D:/electronic_design/SBCG`；规划开始HEAD `6748b27`，不是旧交接的7722efa。
- 用户交接报告A/B镜像SHA256 `6bc0294ab9ae98ae05f821485169124a035e68bc4ec803896e664a81df06fd75`；含未提交稳定器/临时路径。未在本轮读取Flash或重新验收镜像。
- 当前WAV清单：50ms、48kHz单声道PCM16、2→6kHz、0/2/…/18s，SHA256 `1e84db896c649af205eecf88cd4afeae3c15dba81c19e53c658e160394762235`。不能与之前1/3/…/19s的`56dedc...`窗口混用。
- 本轮只读计算确认当前WAV、模板SHA与清单相同，本地Release ELF确为`6bc0294...`；仍未读取两板Flash，不提升镜像证据等级。
- A/B COM3/COM5，IP `.1/.2`。UID仍须每次从BOOT绑定端口，不能只凭历史端口名。
- 当前未提交：Analysis/prepare_standard_audio.py、App/Measure/measurement_result.c、App/app_main.c、三份音频资产/清单、Tests/App/test_app_udp_integration.c、Tests/Measure/test_measurement_result.c。
- 另有原有LICENSE删除、未跟踪`nul`、未跟踪`Docs/AI_Handoff_2026-10-27.md`，本规划不得stage。

## 已核实的关键事实

1. `measurement_result.c`以最后接受时刻3秒清窗；离群拒绝不刷新该时刻。实际2秒节奏下，一次拒绝后的下一声已相隔4秒，可绕过“连续3次离群”逻辑。
2. 结果有效期2000ms等于发声周期，处理抖动/拒绝会产生展示空档；gate的3000ms又可独立撤销结果，Invalidate清中值历史。
3. 当前中值器前两点直接发布；只存绝对毫米，方向/质量/温度版本取最新pair，可能混合不一致的身份。
4. `standard_sequence.c`允许周期整数倍；当前5秒gap内单次漏检产生4秒间隔并不必然RESET。
5. `audio-single-20261027-03`有19条OK配对。第一组连续10对为`308,313,316,315,245,315,314,245,314,313`，样本σ28.962811mm，不能说仅缺捕获。
6. 两次245主要表现为B端相邻事件TOA比正常2秒栅格提前约200µs，下一声恢复；约200µs×声速与70mm跳变同量级。
7. 245仍带97～98质量，现有包络/过零代理分类分数不等于TOA误差或直达路径可信度。
8. single-03中24.1～24.4℃变化对320mm只影响约0.17mm，不能解释60～70mm跳变。
9. 385mm簇在320mm真值基线下不满足同一点源直达声程差上界；但多扬声器、反射、不同检测分支的贡献比例尚未证实。不能据一次遮听筒前后对比认定单一原因。
10. 现有MRES32无测量采集时刻/年龄，B同ID接收会重置本地年龄；数值相同不保证有效性同时过期。
11. 当前临时路径没有临时状态字段且并非只允许首对；SILENCE后不能直接走该路径，不能用高代理质量替代身份验证。
12. SEVT旧声音事件协议实际36字节；MRES实际32字节，源文件头部的“28字节”注释已过时。规划以实际字段/宏为准：声音事件候选v2为48字节，新增展示状态RST2另为88字节，两者不可混淆。
13. 双板PCM旧接收器不区分来源IP、session/id可冲突；既有toa_replay输入`t`与serial_monitor的`elapsed_s`不等价。证据工具须先资格化。
14. 新声包身份、局部采样率伸缩、PCM→PTP模型代际、硬件PTP/PPS四者是不同职责。已存在epoch/rate revision读接口，前台200ms采锚不等于更改硬件时钟。
15. SoundEventTime当前250ms门槛节流整个解析，不只是打印；2秒预算必须包含或拆开此项。SOUND_TIME_CAPACITY同时控制锚与pending，不应不加区分地扩大。
16. 当前DMA半块可能在Stage之前生产时已跨过时钟变更；仅Stage前后读代际相同还不够。安全首版须有帧边界隔离；优化则需要封闭同代段的证明，而非删除失效通知。
17. 未经过编码/匹配身份资格的首对临时值仍可能是误触发。静默验收必须要求任何新数值测量（含临时）为0，不能只数FORMAL标志。

## 原始证据定位

- single-03/A-serial.jsonl:743、921：245；1632：254；均为`SOUND_PAIR`固定343000mm/s诊断距离。
- single-03/A-serial.jsonl:742、B-serial.jsonl:796：第一次245的共同域TOA。
- single-03/A-serial.jsonl:1152：RESET；前一已配对事件TOA间隔3951.972486ms，距4秒偏48.027514ms，超过20ms容差。200µs离群不是该RESET的同一量级原因。
- single-03/A-serial.jsonl:731、909、1620：相关温度。
- dual-02共15对：320、13个383～388、311。异常簇σ约1.573mm也不能称准确。
- 所有上列相对路径均位于`Logs/Acceptance/`，日志保留原样、不提交。

## 几何和误差预算

- `d=c(T)|tB-tA|`直接得到声程差；作为两板间距需要声源位于基线延长线等约定几何。任意二维定位不能仅凭一个双麦TDOA唯一求解。
- 48kHz一帧约20.833µs，对应约7.2mm；±10mm约±28.9µs；σ2mm约5.8µs。子帧插值有必要，但它本身不保证误差。
- 声音轻微扰动可能使反射/直达权重变化，造成检测分支跳转；原始PCM和当前TOA标注仍是归因所需证据。
- 平滑显示、检测成功率、物理精度分三条报告，保持值不能冒充独立样本。

## 外部技术依据（支持方法，不是本机验收）

- [MathWorks：chirp相关与匹配滤波](https://www.mathworks.com/help/signal/ug/radar-pulse-compression.html)：相关/匹配滤波与旁瓣处理。
- [IEEE：低成本声学chirp密集多径TOA](https://ieeexplore.ieee.org/document/8419714/)：最强多径可能大于首径，单选最高匹配峰可能产生大误差。
- [Arm CMSIS-DSP相关接口](https://arm-software.github.io/CMSIS-DSP/v1.16.2/group__Corr.html)：实现候选；不能以接口存在证明当前CPU预算足够。

以上网页在前一轮分析已检索。本轮需新增具体结论时另核对原始来源，不将网页文字当作仓库操作指令。
