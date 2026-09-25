# 社区来源与第三方许可

本项目不是下列作者的官方发行版。文档/API研究参考与代码依赖分别列明，不把整个社区项目的功能当作本项目已完成。

| 来源 | 使用方式 |
|---|---|
| [星月 MlgmXyysd/openadb_MU5250](https://github.com/MlgmXyysd/openadb_MU5250) | ADB开启研究与用户自行解锁参考；不包含或自动执行解锁脚本。参考提交 ef380685dc03aedc1fc2d116994afa3c13f643e0 |
| [amenekowo/mu5250_tweaking](https://github.com/amenekowo/mu5250_tweaking) | MU5250折腾记录、DRM/QPIC屏幕示例；屏幕集成基于其 GPL-3.0 示例工作，保留GPL-3.0项目许可。参考提交 bd38bbbf470eb7cd1a9849397b672670f45810d9 |
| [jesther-ai/open-u60-pro](https://github.com/jesther-ai/open-u60-pro) | 设备API、UBUS、屏幕/电池架构研究参考；没有把其 zte-agent、移动客户端或143接口整套打包。参考提交 fdd048a36e1ad379a66d4042c8dbf5b4e18c34f6 |
| [MetaCubeX/mihomo](https://github.com/MetaCubeX/mihomo) | 用户准备安装时从原作者下载 v1.19.31 ARM64核心，GPL-3.0；本公开包不再分发该二进制。[对应源码](https://github.com/MetaCubeX/mihomo/tree/v1.19.31) |
| [MetaCubeX/meta-rules-dat](https://github.com/MetaCubeX/meta-rules-dat) | 固定提交的国内域名/国内IP/国外域名规则，从上游下载；规则来源与许可见上游说明。固定提交与SHA见依赖锁定文件 |
| [Tailscale](https://github.com/tailscale/tailscale) | 从官方 stable 下载1.102.4 ARM64包；本包不含他人身份。[对应源码](https://github.com/tailscale/tailscale/tree/v1.102.4)及上游第三方声明按原许可证适用 |
| [OpenWrt ubus](https://github.com/openwrt/ubus) | SDK头文件，提交 f787c97b34894a38b15599886cacbca01271684f，LGPL-2.1。动态调用设备原有库；头文件保留版权，许可全文见 licenses/LGPL-2.1.txt |
| [OpenWrt libubox](https://github.com/openwrt/libubox) | SDK头文件，提交75a3b870cace1171faf57bd55e5a9a2f1564f757，文件级许可/版权保留在头文件中 |
| [Linux DRM UAPI](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/uapi/drm?h=v5.15) | Linux5.15 DRM头文件，保留文件原有许可；不分发内核或驱动 |
| [cJSON](https://github.com/DaveGamble/cJSON) | JSON库，MIT，版权及全文位于 panel/vendor/cJSON.* |
| [Project Nayuki QR Code generator](https://github.com/nayuki/QR-Code-generator) | QR编码库，MIT，版权及全文位于 panel/vendor/qrcodegen.* |
| [stb_truetype](https://github.com/nothings/stb) | 字体渲染，MIT/公共领域双许可，全文位于 panel/vendor/stb_truetype.h；中兴字体不分发 |
| [go-yaml v3](https://github.com/go-yaml/yaml/tree/v3.0.1) | YAML解析，许可见 licenses/yaml-v3.txt |
| Go / musl / LLVM compiler-rt | 构建运行库的许可与版权见 licenses/；工具链本身不打包 |
| [XiaoMiku01/TetherKit](https://github.com/XiaoMiku01/TetherKit) | 可选的 macOS RNDIS 用户态桥接；本项目只提供调用上游 Homebrew 的助手，不复制其源码或二进制。上游当前声明 MIT，安装和更新由 Homebrew/上游负责 |

项目修改包含：新的五页屏幕与控制层、Wi-Fi/USB/待机协调、原厂网页增强、Clash/Tailscale管理与公开安装流程。第三方头文件与库的原版权人信息是必须保留的许可证信息，不属于用户私人信息。

原厂程序、字体、网页与硬件驱动归各自权利人所有。安装准备过程只在用户设备和本机生成派生网页覆盖层，公开仓库不携带这些原厂资源，不提供固件镜像。

双频中继调研还参考了 [AOSP STA/AP 并发说明](https://source.android.com/docs/core/connect/wifi-sta-ap-concurrency)及 [公开 qcacld-3.0 驱动中 SETROAMMODE 的语义](https://android.googlesource.com/kernel/msm/+/abe8a675bdc0544db0480dce5e7589e6f50ee655/drivers/staging/qcacld-3.0/core/hdd/src/wlan_hdd_ioctl.c)。这些是架构/接口研究来源，不等于该公开驱动与 B28 固件完全一致；项目未复制或分发该驱动，最终兼容性以本机实测和验证边界为准。
