# 代码证据

<!-- doc-status: completed-work-notes; reviewed: 2026-10-23 -->

- 构建登记58个App C/H，源码实际是否进入ELF还需结合target_sources/compile_commands。
- Core/Src/main.c先MPU_Config，HAL_Init，再系统/外设时钟及GPIO/DMA/UART/I2C/LwIP/SAI初始化，之后App_Init和无限App_Process。
- App_Process每轮先MX_LWIP_Process；DHT起始低电平期间codec/audio/anchor/map及串口排空延后，网络/PTP继续处理。
- 当前FW SBCG-M3.2-MAP，最近实物验收40组三锚点估算通过；本轮不新增实物结论。

- 实际编译数据库137项；初次统计/App/子串会误计LWIP/App，最终统计必须使用相对路径首段判定。CubeMX列表包含PPP/TCP源码，不能据此说当前启用了PPP/TCP；lwipopts中TCP=0、NO_SYS=1。
- 启动顺序证据：Reset_Handler先SystemInit，再.data复制/.bss清零、__libc_init_array、main；不能套用其它启动文件顺序。
- SysTick仅HAL_IncTick；USART1/DMA2_Stream7/SAI2均调用HAL处理；以太网前台轮询。

- 编译单元分类最终137=App28/Core6/LWIP2/Drivers20/Middlewares80/启动1；App总58=28C+30H。唯一资产为.github/assets/sbcg-banner.svg，不进入LCD。
- 网络复核指出既有low_level_output忽略HAL_ETH_Transmit返回，因此STACK_OK/ACK_OK不能独立证明硬件发出；以电脑实际收包联合验收为证，本轮不修复生成代码。
