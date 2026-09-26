# U60 Pro Enhanced

为中兴 U60 Pro（MU5250）国行 **B28 / B31** 提供原生小屏界面、原厂网页增强、Clash/Mihomo、Tailscale、Wi-Fi 接力与 USB 网口管理。保留原厂固件和管理页，双击电源键可切换界面。

**当前版本：[v0.1.13-experimental](https://github.com/gaofeng21cn/u60-pro-enhanced/releases/tag/v0.1.13-experimental)**。这是实验版：支持固件检查和设备身份绑定，不代表全部网络与硬件组合已经验收。安装前请阅读[验证范围](docs/VALIDATION.md)。Mac 数据线直连按“原生 NCM → TetherKit 降级”的顺序推进；原生 USB 网络尚未完成实机发布验收，当前用户能力仍保持关闭。

## 先确认你的设备

| 项目 | 要求 |
|---|---|
| 国行 B28 | `BD_FLYMODEMMU5250V1.0.0B28`，内核 `5.15.185-perf` |
| 国行 B31 | `BD_CNMU5250V1.0.0B31`，内核 `5.15.194-perf` |
| 电脑 | Python 3、Google Android Platform Tools（`adb`），可访问 GitHub 下载依赖 |
| 连接 | 已启用 root ADB，USB 数据线直连；准备与安装时拔下外接 USB 网卡 |
| 空间 | 设备 `/data` 至少 400 MB 可用，升级可能需要更多；程序会检查 |

不支持 B27、港版/国际版及其他固件。**不要为安装本项目刷机、修改 USB 组合或安装不匹配的内核模块。** 项目不提供 ADB 解锁、固件或救砖镜像。Windows 安装全流程尚未实测；已验证的电脑交付路径为 macOS。

## 下载与准备

下载本仓库 Release 中的 `u60-pro-enhanced-v0.1.13-experimental.tar.gz` 和 `SHA256SUMS.txt`，不要使用 GitHub 自动生成的 Source code 压缩包作为安装包。上游 B28 包不能用于 B31。

macOS 示例（Linux 将 `shasum -a 256` 换成 `sha256sum`）：

```sh
shasum -a 256 -c SHA256SUMS.txt
tar -xzf u60-pro-enhanced-v0.1.13-experimental.tar.gz
cd u60-pro-enhanced-v0.1.13-experimental
adb devices
python3 prepare.py
```

只连接一台待安装设备。`prepare.py` 读取固件、设备身份摘要和原厂网页资源，下载固定版本并校验依赖，生成旁边的 `u60-prepared-private/`；不会安装程序或改动路由。该目录只适用于这台设备，包含设备派生材料，**不要上传、分享或用于其他设备**。多设备、依赖下载失败和原厂网页指纹问题见[安装说明](docs/INSTALL.md)。

## 首次安装

准备完成后进入私有安装目录，按顺序执行：

```sh
cd ../u60-prepared-private
python3 deploy-from-computer.py check
python3 deploy-from-computer.py install
python3 deploy-from-computer.py start
```

`check` 上传材料并检查；`install` 安装程序、保存原厂启动备份；`start` 启动。已有项目目录时首装会拒绝覆盖，请走升级流程。

安装后先检查小屏、原厂网页和原有热点上网，再逐项启用网络扩展。B31 首装不主动开启 USB 协调、Wi-Fi 接力或深度待机；用户选择 USB 角色后才启动协调服务，开启接力后才按其策略运行。没有预置代理订阅或 Tailscale 身份。

## 已安装用户升级

下载新包，重新为同一设备准备一个**新的私有目录**。设备已挂载增强网页时，先在准备期间临时停止网页覆盖层，结束后恢复：

```sh
adb shell /etc/init.d/u60-web stop
python3 prepare.py --output ../u60-prepared-private-v012
adb shell /etc/init.d/u60-web start
cd ../u60-prepared-private-v012
python3 deploy-from-computer.py upgrade-check
python3 deploy-from-computer.py upgrade
```

**即使准备失败，也要执行上面的网页 `start`。** 不要修改已经生成的安装目录以绕过校验。升级逐文件备份、核对安装字节并重新加载网页和屏幕；保留订阅、节点偏好、Tailscale 身份及用户设置，失败时尝试恢复前一版本。升级前程序备份留在设备；[清理与恢复说明](docs/RECOVERY.md)解释哪些材料可以归档。

升级时字节相同的网络核心保留运行 inode；若新版需要更换正在运行的网络核心，检查会拒绝，需先通过界面停止相应核心再升级，升级后按原有方式启用。

## 日常使用

- **小屏**：总览看上游、实际出口、速率与用量；代理/组网页提供常用操作，订阅、规则、出口与诊断归入管理子页。单击电源亮灭屏，双击切换原厂/增强界面。长按电源菜单受原厂固件影响，不保证无人值守重启。
- **网页**：连接 U60 热点，访问原厂管理地址（默认 `http://192.168.0.1/`），使用自己的原厂管理密码登录，选择“增强功能 → 总览 / 网络 / 代理 / 组网 / 电源 / 工具”；“代理”对应 Clash / Mihomo，“组网”对应 Tailscale，与小屏一致。没有额外默认密码。APN、SIM、DHCP 等原厂设置继续在原厂菜单操作。
- **Clash/Mihomo**：先执行 `adb shell sh /data/u60-panel/setup-clash.sh` 初始化本机配置，再在网页添加自己的 Mihomo proxy-provider 订阅、选择节点并开启代理。以“实际代理状态”为准；核心运行不等于流量已接管。当前代理覆盖 IPv4 TCP 与 DNS，UDP 443 拒绝以促使 TCP 回退，其他 UDP 与 IPv6 不宣称代理。
- **Tailscale**：执行 `adb shell sh /data/u60-panel/setup-tailscale.sh`，在自己的浏览器完成登录。内网访问还需实际路由发布、后台批准及 ACL/Grant，不能只看本机在线。

若节点握手失败且设备时间异常，参考[原厂时间与代理校时](docs/INSTALL.md#原厂时间与代理校时)。新配置默认使用直接 IPv4 校时；旧默认 `time.apple.com` 可用 `setup-clash.sh --repair-ntp` 修复，自定义配置不会被升级自动覆盖，系统时钟不改。

详细步骤见[使用说明](docs/USAGE.md)。不要把管理端口映射到公网，不在 Issue 中发送密码、订阅、设备标识或私有安装目录。

## USB 与 Wi-Fi 接力分别能做什么

| 场景 | 操作与边界 |
|---|---|
| U60 → USB 网卡 → 网线 → 电脑 | 选择 **LAN**，给下游供网；LAN 增加以下驱动的实验性适配： `r8152`、`cdc_ether`、`cdc_ncm`、`aqc111` 与 ASIX 驱动；AUTO 上游目前只对已验证的 AX88179（`0b95:1790`、`ax_usb_nic`）开放 |
| 上级路由器 → 网线 → USB 网卡 → U60 | 选择 **AUTO**，获取有线上游地址；断线可回蜂窝。AUTO 不会把“没有 DHCP”猜成 LAN |
| U60 → USB 数据线 → Mac | 优先补齐并验证原生 NCM composition；在 NCM 完成实机验收前保持默认 RNDIS。只有原生 NCM 被设备验收证明无法安全成立后，才使用 `macos/u60-rndis.sh` 的 TetherKit 降级方案 |
| 上游 Wi-Fi → U60 → 自身热点或 LAN | 同时连接一个 2.4G／非 DFS 5G 上游，最多保存 8 个网络，按首选顺序重连；不是 Mesh，不聚合两条 Wi-Fi 带宽 |

Wi-Fi 接力支持 **2.4GHz 和非 DFS 的 5GHz 上游**，并非仅限 2.4GHz。5GHz 支持信道 36/40/44/48、149/153/157/161/165；找不到 5GHz 网络时先检查上游信道。

### Mac 数据线直连

路线顺序固定为：先实现并验证原生 NCM，再考虑 TetherKit 降级。候选试运行尚未出现 Mac 原生网卡，且 ADB 未能自动恢复；设备经现场重启恢复原厂 RNDIS＋ADB。当前已封锁试运行与底层切换入口，不能把代码存在当成可用能力，也不能据此判定原生 NCM 不可实现。恢复保护和独立管理通道验证通过之前，不再进行设备 USB 切换。

macOS 原生支持 CDC NCM，但不支持 U60 当前的 RNDIS。NCM composition 验收完成前，Release 包只把上游 [TetherKit](https://github.com/XiaoMiku01/TetherKit) 作为保留的降级材料，不把它当作当前主路线；它不安装内核扩展、不降低 SIP，也不修改 U60 的 USB gadget。只有 NCM 被设备验收证明无法安全成立后，才按下列方式启用降级方案：

```sh
cd u60-pro-enhanced-v0.1.13-experimental
sh macos/u60-rndis.sh install
sh macos/u60-rndis.sh gui
```

首次打开 TetherKit 时，在窗口中安装一次特权组件并输入 macOS 管理员密码；之后由官方 helper 维护 USB 网卡，用户只需在 GUI 中连接 U60 并选择 DHCP。该路径不需要每次重复输入密码，也不会触碰 U60 的 USB 组合或 ADB。命令行仅用于诊断：

```sh
sh macos/u60-rndis.sh doctor
sh macos/u60-rndis.sh start
sh macos/u60-rndis.sh status
```

需要让 Mac 将默认路由切到 U60 时显式执行 `sh macos/u60-rndis.sh start --route-all`；不带该参数只建立 DHCP 网卡，不覆盖现有 Wi-Fi 默认路由。停止使用 `sh macos/u60-rndis.sh stop`。TetherKit 由其上游以 MIT 许可提供，本项目不重新分发其二进制；Homebrew 会从上游构建并安装。完整限制与排障见[Mac RNDIS 直连](docs/MACOS-RNDIS.md)。

Wi-Fi 接力入口为“小屏：网络 → Wi-Fi → 连接上游 Wi-Fi；网页：增强功能 → 网络”。密码可在小屏或已登录的网页输入；小屏提供独立字符页，连接失败后保留内存草稿供修改重试。要求 5G 主热点开启、访客热点关闭、USB 为 LAN；同频热点可能短暂断开重连。界面分别显示关联/出口和互联网探测，探测失败不自动改变出口。

停止中继：总览点“中继管理 / 停止”，或“网络 → Wi-Fi”顶部点“停止 Wi-Fi 中继”；不会忘记上游密码。

接力可选择开机连接、允许/禁止蜂窝回退，并在关闭后忘记保存的网络。禁止回退会在接力开启期间阻断本机代理和下游经蜂窝的 IPv4/IPv6 数据包，也可能使蜂窝远程管理断开；它不关闭基带，不保证整机零流量。关闭接力恢复原出口。最多保存 8 个上游，可立即连接、设为首选或单独忘记；断线后按优先级和信号尝试可用网络，健康连接不抢切。企业认证、DFS 与完整认证门户登录仍未提供。详见[Wi-Fi 接力说明](docs/WIFI-RELAY.md)。小屏“网络 → 网络诊断”与网页“工具与诊断”提供[一键网络自检](docs/NETWORK-DIAGNOSTICS.md)，分开检查路由、地址、DNS、默认出口和 Clash 的 HTTPS。

## 恢复与问题反馈

屏幕异常时先双击电源切回原厂，保留 USB ADB。完整启动项回退按[恢复说明](docs/RECOVERY.md)执行；不要删除 `/data` 项目目录或恢复出厂来替代回退。切回原厂界面本身不会停止代理或接力。

报告问题请提供固件版本、项目版本、连接方式、可复现步骤及脱敏错误信息。明确区分“已连接”“拿到地址”“能够访问互联网”，并说明是否开启代理/组网。

## 从源码构建

仅开发者需要 Zig **0.14.1**、Go、C 编译器和 Node.js；下载 Release 的用户无需安装这些工具。

```sh
ZIG=/path/to/zig-0.14.1 sh scripts/build.sh
sh scripts/test.sh
python3 scripts/package.py
```

产物位于 `dist/`。主机测试不能替代真实硬件、下游业务、长待机和恢复验证。

## 来源与许可

屏幕、网页增强、Wi-Fi 接力、USB 网口等基础来自 [defilippisprafka-netizen/u60-pro-enhanced](https://github.com/defilippisprafka-netizen/u60-pro-enhanced)。本仓库独立维护 B31 支持、设备绑定交付、控制与状态改进。许可与第三方材料见 [LICENSE](LICENSE)、[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
