# M4.3.19 app_main结果交换接线测试

<!-- doc-status: m4.3.19-app-udp-boundary-test-software-complete; reviewed: 2026-10-27 -->

## 结论

新增主机测试：

```text
Tests/App/test_app_udp_integration.c
Tests/App/run_app_udp_integration.py
```

编译真实`app_main.c`+真实结果状态/测量/域/门控/标定模块；UDP发送边界用测试桩捕获`UDPSoundEvent_SendResult`，通过`SBCG_HOST_TEST`钩子注入MACK/DOMN ACK。

覆盖：

1. App首次发送当前结果；
2. MACK后停止同ID刷新；
3. B的ACK序号回退→恢复重发；
4. 发送失败后500ms内不重试，500ms后重试；
5. 域失效→结果撤销；
6. 域恢复→不重发旧结果。

命令：

```powershell
python -B Tests/App/run_app_udp_integration.py
```

PASS。

## 互证关系

- `Tests/Udp`：真实`udp_sound_event`模块的报文分发/本端提交配对/DOMN/MACK路径（lwip IO为桩）；
- 本测试：真实`app_main`接线，UDP发送边界为桩；
- 两者组合覆盖App→结果发送→ACK/域状态，但**仍不是实际lwip/UDP双板收发**。

## 边界

- 不是真实UDP/交换机/双板；
- 未验证真实丢包、乱序、链路恢复；
- 未烧录；
- 测试钩子只在`SBCG_HOST_TEST`下存在，不进入固件默认构建。
