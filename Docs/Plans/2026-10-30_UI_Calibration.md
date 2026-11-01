# U3.3 500mm标定按钮实施计划

状态：软件实现、23项验证命令及独立审查通过；A板974ed3cd9ec8已烧录，120秒无声运行及灰色按钮保护已获串口/用户确认；500mm正向标定与启停/清除物理数值变化仍待验收，未提交功能完成版本。基线057dcb1。

## 唯一目标

A板通过SETUP操作500mm单点标定；停用保留保存系数，清除删除；B板只读展示A的有效偏移/版本。默认未标定时，原测距数值及发布规则不变。不新增掉电保存、远程控制协议或其它提高项。

## 文件与实现

- `App/Calibration/calibration.c/.h`、`Tests/Calibration/test_calibration.c`：新增enabled，保存偏移与应用偏移分开；实际启停/清除推进版本，Init才清零版本；测试原有边界和幂等操作。
- `App/Measure/measurement_result.c/.h`、`Tests/Measure/test_ranging_presentation.c`：新增只读标定样本接口；当前健康A/TRACKING/新鲜且未到共同时间截止才可获取。原始距离复用filtered_delta_ns和当前声速的统一舍入，避免重复施加标定。现有formal一直false，不能据此禁用标定。
- 新增 `App/Calibration/calibration_control.c/.h`、`Tests/Calibration/test_control.c`：纯前台单请求交接；业务层独立核对A权限、确认时样本身份及执行时新鲜资格，拒绝确认期间样本变化。对外输出按钮资格和明确结果。
- `App/app_main.c`：真实RefreshRanging先更新健康/新鲜度，再处理UI请求；实际系数变化后沿已有SetCalibration合同撤值/清tracker，等待新独立声音。使用enabled选择有效偏移；不改原测量时间契约。
- `TouchGFX/gui/src/screen1_screen/Screen1View.cpp`、`TouchGFX/gui/include/gui/screen1_screen/Screen1View.hpp`：接通SETUP三按钮，先显示确认/取消，再入队；按住/拖出/触控错误不能提交；B及无样本时对应按钮禁用，显示保存/应用偏移、版本和反馈。
- `cmake/touchgfx.cmake`：仅用户扩展注册control源。
- `Tests/Calibration/run_tests.py`、`Tests/TouchGFXView/run_tests.py`/布局检查、相关App回归和测试桩：覆盖上述动作，并复验结果同步、过期撤值及原导航。
- `Docs/Milestones/U3_3_UI_Calibration.md`：记录验证、镜像与待实机项。

## 请求与状态合同

CALIBRATE保存确认页所展示样本的id/revision/domain/authority_epoch/source_ns。执行时从已刷新业务获取当前标定样本，身份变化或无新鲜TRACKING则拒绝且不改原系数。原始raw不使用UI文本或最近单个pair诊断。标定成功自动启用新系数。

ENABLE/DISABLE为明确目标状态，重复设置不推进版本；无保存系数则拒绝。CLEAR删除保存系数，已空时幂等。不会把旧声音立刻重发布为500mm；版本变化通过既有RST2反映，B不自行重算。B协议没有保存/启用标志，只展示A当前应用偏移和版本，不猜测其保存状态。

## 资源与验证

No CubeMX change required。无GPIO/TIM/DMA/NVIC/SAI/ETH/LTDC/时钟/cache配置变更，无新IRQ/DMA关系；原触控I2C轮询与SDRAM前端存储沿用U3.2。

- [x] 先测后改模型、受检样本、确认按钮和请求处理；O0/O2测试。
- [x] 模型涵盖启停保留系数、零偏移、幂等清除、重新标定及失败保持；样本涵盖TRACKING、温补、已标定、HOLD/过期/失域/候选/B拒绝。
- [x] 请求涵盖权限、样本更换、重复点击、有效变更后撤值；UI涵盖确认/取消、长按/拖出/错误、B只读以及不操作时原导航/文本不变。
- [x] 实际App A/B回归75项通过：原47项保留，新增28项，验证校正仅一次、两侧载荷一致、撤值且新声音恢复。
- [x] Debug/Release构建及音频/ETH/前端布局、字体字符/边界；独立审查、git diff。证据与硬件步骤见[交付记录](../Milestones/U3_3_UI_Calibration.md)。
- [x] 已取得本版烧录授权，A板写入974ed3cd9ec8校验复位成功；120秒无声运行及无样本灰色保护通过，用户确认四页切换正常。
- [ ] 500mm正向标定、启停/清除数值变化和两板一致，需要交换机、两板和用户播放；尚未实测。
- [ ] 实机观察真实通过后定向提交jxq/jxq；未实测不声称0.2–5m ±5mm标定精度通过，Logs及原用户改动不提交，不push。
