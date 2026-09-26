# Mac RNDIS 直连

## 路线决策

路线顺序固定为两层：先实现并验证原生 NCM，只有 NCM 因设备 composition、owner 或实机业务验收无法安全成立时，才使用 TetherKit 降级。B31 的候选 NCM composition 未通过实机恢复验证，目前试运行和底层切换入口均拒绝写入；这不构成原生 NCM 不可实现的结论。原厂 `9059` 是 ECM 候选，但不替代用户要求的 NCM 主线。

原生 NCM 必须同时满足 Mac 原生网卡枚举、DHCP、U60 管理地址、Google/Cloudflare HTTPS、拔插/冷启动和 ADB 保持或恢复；通过其中一项不能发布。实现应接入原厂 `/sbin/usb_composition` 的 composition owner，不能停用 `zte_ubus_bsp_usb`、`zte_usb_switch`，也不能把一次手工 ConfigFS 试验当成产品能力。在这条验收链完成前，普通用户开关保持关闭。

TetherKit GUI 是原生路线被证伪后的降级方案：U60 保持原厂 RNDIS + ADB 组合，Mac 首次安装官方 GUI 并授权一次 privileged helper，之后由 helper 创建和维护 `feth`。它不是原生能力，也不改变原生 ECM/NCM 的验收结论。

U60 B31 出厂 USB gadget 使用 RNDIS。macOS 没有原生 RNDIS 网络接口驱动，因此 Mac 不会像 Windows 一样自动出现网卡。项目保留 U60 原厂 RNDIS、ADB、诊断和其他 USB function 不变，在 Mac 端使用上游 [TetherKit](https://github.com/XiaoMiku01/TetherKit) 以用户态 libusb + `feth` 虚拟网卡接入。

已核对的一台 B31 内核编译了 Linux NCM function，本机 Mac 也加载了原生 NCM 驱动；现有 `9059`/`9057` 只提供 ECM。仓库保留候选 composition 源码，但执行入口已封锁。实机试运行后未观察到 NCM 网卡或 DHCP，ADB 也未自动恢复；现场重启后已回读原厂 RNDIS＋ADB 恢复。没有切换中途的设备日志，不能确定卡在解绑、绑定还是恢复阶段。不能因为 TetherKit 已能枚举 RNDIS 就结束 NCM 工作，也不能因为 composition 文件存在就宣称 Mac 原生网络已可用。

这条路径的安全边界是：助手只在 Mac 上声明 RNDIS 控制/数据接口；不会向 U60 ConfigFS 写入，不会切换 `gsi.rndis`、ECM 或 NCM，不会执行 adb 写入。TetherKit 上游代码也明确避免 `libusb_set_auto_detach_kernel_driver`，以免触发整设备重新枚举。停止助手后，U60 仍保持原厂 USB 组合，ADB 不需要恢复。

## 安装与连接

在 Release 包的 `macos/` 目录执行。正常用户推荐 GUI 路径：

```sh
sh u60-rndis.sh install
sh u60-rndis.sh gui
```

`gui` 打开官方 TetherKit.app。首次运行在窗口中点击“安装特权组件”并输入一次 macOS 管理员密码；之后由上游 `tetherkit-helper` 创建和维护 `feth`/BPF，插拔 U60 时不需要再次运行本项目脚本。连接、DHCP、默认路由和停止操作在 TetherKit 窗口中完成。该一次授权发生在 Mac 上，不会写入 U60，也不会改变 ADB 或 USB composition。

命令行路径仅用于诊断或没有 GUI 的环境：

```sh
sh u60-rndis.sh doctor
sh u60-rndis.sh start
sh u60-rndis.sh status
```

`install` 通过 Homebrew 安装上游 TetherKit GUI、`tetherkit-cli` 和依赖。GUI 首次运行会按上游标准流程安装一次特权 helper；项目助手不读取或保存管理员密码。CLI 的 `start` 仍会在当前终端请求 `sudo`，不适合作为无感连接路径。

`doctor` 只读检查 macOS、TetherKit、`19d2:1404` U60 RNDIS 和 `feth` 创建前置 sysctl。它还会确认 U60 仍以原厂 RNDIS 出现；没有识别到设备时停止，不会改动任何一端。

`start` 启动 `tetherkit-cli`，从日志读出系统侧 `feth` 接口，然后通过 macOS 的 DHCP 客户端获取 U60 地址。默认保留当前 Wi-Fi 默认路由，只建立可访问 U60 管理地址的有线接口。需要把 Mac 的默认出口切到 U60 时使用：

```sh
sh u60-rndis.sh start --route-all
```

这个参数只在 DHCP 返回网关后执行一次 macOS 默认路由切换。若 Mac 仍有其他网络，切换后的实际出口以 `route -n get default` 和真实 HTTPS 探测为准。

停止连接：

```sh
sh u60-rndis.sh stop
```

助手只清理自己记录的 `feth` 地址和 TetherKit 进程。若手工启用了 `--route-all`，停止后按 macOS“网络”设置或重新连接原 Wi-Fi 恢复原默认出口；助手不会猜测并覆盖用户的其他网络服务。

## 排障

- `doctor` 提示未识别设备：确认使用支持数据传输的 USB 线、U60 已开机且仍是原厂 RNDIS；执行 `sh u60-rndis.sh list` 查看 TetherKit 枚举结果。
- 有 `feth` 但没有地址：执行 `sh u60-rndis.sh status`，检查 `macos/u60-rndis` 日志；不要切换 U60 USB 组合。U60 管理地址默认是 `192.168.0.1`，可先从现有 Wi-Fi 访问确认设备仍在线。
- 能访问 U60 但不能访问互联网：先确认 `--route-all` 是否需要，再分别检查 `route -n get default`、`ipconfig getifaddr <feth接口>` 和 `curl --interface <feth接口> https://www.google.com/generate_204`。这类失败属于 Mac 路由或 U60 数据面，不等于 ADB 或 USB gadget 损坏。
- `claiming the RNDIS data interface failed`：停止其他 RNDIS 用户态程序或旧 HoRNDIS；不要安装内核扩展来“抢回”接口。

## 证据范围

本机 macOS 26.5.2 已真实枚举 U60 `19d2:1404` RNDIS，TetherKit `v0.1.5` 的 `--list` 能识别控制接口 0 和数据接口 1。这证明 Mac 端枚举和用户态接管的前置条件成立；DHCP、默认路由和真实 HTTPS 仍需在当前 U60 连接上单独验收。它不证明 ECM/NCM，也不改变 U60 原厂 RNDIS 的设备侧限制。

TetherKit 由上游以 MIT 许可发布；本项目不打包、修改或重新发布 TetherKit 二进制。

## NCM 诊断与恢复边界

当前只开放只读诊断。试运行脚本的所有写动作、直接 composition 调用和普通界面的操作入口均停用；没有环境变量或标记文件可绕过。procd 服务没有开机启动项。

```sh
adb shell /data/u60-panel/usb-ncm-trial.sh status
```

此前的超时循环位于同步切换命令之后，不能覆盖切换命令本身阻塞的情况，不能宣称它提供独立回滚。后续实机切换必须先完成原厂 USB owner 协调、完整快照和独立监督恢复的验证，并实测不依赖 USB 的管理通道。不得停用原厂 USB owner 或绕过当前封锁。保留 ADB function 链接和内核存在 NCM 都不能代替这些证据。

## 原生路线的实现依据

[高通 Linux USB 文档](https://docs.qualcomm.com/bundle/publicresource/topics/80-80022-8/usb.html)列出 `908C NCM + ADB`；[ModalAI 的高通平台实例](https://docs.modalai.com/qgc-via-adb/)也展示了 NCM 与 ADB 共存。这些资料证明有可研究的实现路径，不证明中兴 B31 的驱动、端点和服务可以直接复用。B31 已回读的原厂组合目录没有 `908C`，USB ubus 对象仅公开读取接口；直接调用项目脚本不等于已取得原厂管理服务的协调权。

后续先研究原厂组合服务与驱动的配合、最小 NCM＋ADB 组合和完整恢复过程；验证用的日志须在断开 USB 前由独立进程持久保存。原厂组合的持久化分支涉及闪存写入，不得用于本项目试验。切换阻塞时，独立监督只能提供诊断和有界恢复尝试，不能保证解除内核阻塞；必须另有已实测的非 USB 管理入口。
