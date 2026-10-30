# 四页触控导航实施计划

**状态：本次A板触控与四页导航已验收；软件、独立审查、烧录、120秒运行和用户实屏反馈均通过。双板声音回归仍为后续验证。**

**当前增量：U3.2，触控与四页导航。** 用户先确认布局与页面切换不停止后台测量，随后单独授权A板Debug镜像f9f8d5caebad烧录并确认实屏结果。

## 目标和边界

将现有只读状态页扩展为可触控的四个逻辑页面；标准测距页面继续绑定原13项真实文本。击掌、波形、标定操作在本增量只呈现清楚标记的未接通状态，不生成模拟值、不修改业务状态。下一个独立增量才接通标定，随后分别接通波形和击掌。

使用已有TouchGFX屏幕的用户View扩展容器，避免手改Designer生成基类、字体、`.touchgfx`或CubeMX配置。保留原配色与英文字体，导航为RANGE / CLAP / WAVE / SETUP；选中项高亮，单次按下松开生效，拖出/取消不切页。所有页面可直接回RANGE。

## 准确文件与职责

- 新建 `App/BSP/bsp_touch.c/.h`：复用已生成的I2C3，FT5336身份与单点坐标读取，限频、忙/失败处理、诊断快照。不初始化或恢复外设。
- 修改 `TouchGFX/target/STM32TouchController.cpp/.hpp`：只在安全用户区接入BSP；失败释放触摸，不使用旧坐标触发操作。
- 修改 `TouchGFX/gui/src/screen1_screen/Screen1View.cpp` 和 `TouchGFX/gui/include/gui/screen1_screen/Screen1View.hpp`：四个逻辑页面、固定导航条、真实状态绑定和功能未接通提示。
- 修改 `cmake/touchgfx.cmake`：在用户扩展点注册触控源文件。
- 修改 `TouchGFX/gui/include/gui/common/FrontendHeap.hpp`，新增 `cmake/linker/stm32f746_touchgfx.ld`：构建发现新增控件使片内BSS触及音频DMA；将完整CPU前端对象放入0xC0080000独立8KiB SDRAM NOLOAD区域，仅显示READY后清零、原位构造。不移动音频/ETH内存或已有帧缓冲，不修改MPU/cache。
- 新建 `Tests/TouchInput/`：真实BSP配HAL边界桩，覆盖寄存器数据、坐标方向、节流和故障。
- 修改 `Tests/TouchGFXService/run_app.py`、`Tests/UI/test_app_lcd.c`：复验发现既有TouchGFX App runner因main排除宏同时排除了共享桩且未接已有Ranging桩而链接失败；仅修测试组织，恢复真实App前后台门控检查。
- 修改 `Tests/TouchGFXView/run_tests.py`，新增该目录导航用例/布局检查：真实用户View与控件边界桩，覆盖手势、页面往返及隐藏期间状态更新。
- 新建本计划及对应 `Docs/Milestones/U3_2_Touch_Navigation.md`：记录实际验证和待验收项。

## 调度与资源

No CubeMX change required。使用已生成I2C3的PH7/PH8；前台与WM8994顺序访问共享总线。以限频轮询代替触控IRQ；不增加GPIO、TIM、DMA、NVIC、时钟、缓存维护或帧缓冲。控制器只由已经受DHT门控的TouchGFX前台调用。I2C错误不能复位音频总线。框架短间隔采样使用缓存；失败/非法或多点输入释放按键。

## 执行与检查

- [x] 先记录主机基线：TouchGFX View/布局、服务及配置检查。
- [x] 先写失败用例：真实View收到导航按下/松开可进入四页，按住不连发，拖出/取消不切页，返回测距显示隐藏期间的新文本，无效数据仍保留原占位语义。
- [x] 在用户View内添加定容控件和逻辑页面，不添加业务控制API。控件区域、字号、字符覆盖需检查，导航不得盖住业务文字。
- [x] BSP先测后实现：核对ST本地驱动寄存器/屏幕方向，HAL只允许有界读；测试忙、错误、多点、越界、缓存/节流、恢复，检查初始化无外设重配。
- [x] 接入STM32TouchController并注册CMake源文件，保留生成区字节不变。
- [x] 独立布局测试核对前端区NOBITS、地址、实际对象大小及帧缓冲边界；原位构造不得依赖启动清零。片内BSS、堆栈和音频DMA原检查仍必须通过。
- [x] 运行TouchInput、TouchGFXView、TouchGFXService/Bsp、App双进程测距和UDP相关回归；Debug/Release及布局检查。
- [x] 独立只读审查功能隔离、坐标方向、按键释放、绘制范围、RAM/ITCM和I2C耗时风险；检查git diff。
- [x] 软件通过后记录ELF SHA256，见[交付记录](../Milestones/U3_2_Touch_Navigation.md)。
- [x] 连接A板，另行取得本镜像烧录授权；Debug f9f8d5caebad校验复位通过。
- [x] 用户实屏确认：“四页切换正常，显示清晰，取消手势正常”；检查提示涵盖四个导航、长按/拖出及禁用设置操作，无需发声。
- [x] 完成新BOOT后120秒串口运行检查：音频/显示错误和PPS故障计数均0，LOOP最大46ms；NET=DOWN，双板共同域/测距不在通过范围。
- [ ] 后续方便发声时复验同一标准声的两板结果及过期撤值；未经实测不宣称不影响测距精度。
- [x] 实屏及运行验证通过，按本清单定向提交本功能，作者/提交者jxq；不提交Logs和受保护用户改动，不push。

## 后续页面契约

标定：A权威操作，500mm参考；启用/停用保留系数，清除才删除；新鲜有效的未施加标定测量才可用于标定，B使用A发布结果。当前模块不掉电保存，不自动承诺Flash持久化。

波形：从原音频所有权窗口中复制有限显示数据；显示冻结不暂停DMA。课程同步波形需共同时间轴和保留真实相位差，普通滚动曲线不能直接记为提高项通过。

击掌：当前App显式过滤非CHIRP。接通时独立验证CLAP事件、跨板配对和类型隔离，不能仅增加UI就宣布完成±20cm验收。
