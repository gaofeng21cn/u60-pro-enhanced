# 当前产品基线

版本：`v0.1.16-experimental`。目标设备：国行 B31（`BD_CNMU5250V1.0.0B31`）。

## 已交付能力

- 原厂固件、热点、管理页、ADB 与增强小屏并存；小屏和网页使用一致的网络、代理、组网和 USB 术语。
- Clash/Mihomo：订阅、节点偏好、规则/全局/直连、实际接管诊断；关闭代理会保存开机意图。当前代理覆盖为 IPv4 TCP 与 DNS，普通 UDP 和 IPv6 全量代理不在合同内。
- Tailscale：连接、设备访问、子网转发和出口相关控制按实际状态呈现；身份保留在设备本机。
- Wi-Fi 接力：2.4GHz 与非 DFS 5GHz 上游入口、保存网络、优先级、失败重试、蜂窝回退策略和停止入口。当前实机完整业务证据以 2.4GHz 为主。
- USB 网卡：LAN/AUTO 角色协调与已适配驱动识别；具体网卡、供电和多网卡组合需要分别验收。
- Mac USB 联网：B31 通过中兴原厂 USB owner 提供 ECM＋ADB；无需 Mac 驱动。正式开关位于“网络 → USB 数据线直连”，保存开机意图；启用、关闭、HTTPS、ADB 共存、实体拔插与冷启动自动恢复已通过。
- 安装与维护：设备/固件绑定、升级备份、失败回退、只读诊断；开发设备另有私有局域网密钥 SSH，不随公开包安装；升级前会拒绝仍挂载 Mac USB wrapper 的状态。

## 尚未闭合的证据

这些项目没有被模拟结果冒充完成：

- 5GHz 上游连接后的手机热点完整业务；第二台设备首装；Windows 安装；
- 所有常见 USB 网卡、扩展坞、供电方向和多网卡组合；
- 长时间运行、整夜待机、完整充放电周期和长期功耗；
- 普通 UDP、IPv6 全量代理、企业 Wi-Fi、DFS/6GHz、门户认证等未承诺能力。

## 验收与维护入口

- 安装：[INSTALL.md](INSTALL.md)
- 使用：[USAGE.md](USAGE.md)
- 设备证据：[VALIDATION.md](VALIDATION.md)
- Mac USB：[MACOS-RNDIS.md](MACOS-RNDIS.md)
- 恢复：[RECOVERY.md](RECOVERY.md)
- 目标与范围：[PLAN.md](PLAN.md)

README 只保留用户安装入口；当前事实以本页和主题文档为准。
