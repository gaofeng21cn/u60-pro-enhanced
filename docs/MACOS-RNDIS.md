# Mac RNDIS 直连

U60 B31 出厂 USB gadget 使用 RNDIS。macOS 没有原生 RNDIS 网络接口驱动，因此 Mac 不会像 Windows 一样自动出现网卡。项目保留 U60 原厂 RNDIS、ADB、诊断和其他 USB function 不变，在 Mac 端使用上游 [TetherKit](https://github.com/XiaoMiku01/TetherKit) 以用户态 libusb + `feth` 虚拟网卡接入。

已核对的一台 B31 内核编译了 Linux NCM function，本机 Mac 也加载了原生 NCM 驱动，但原厂 USB composition 脚本没有 NCM 切换入口；切换会同时解绑 ADB 和诊断 function。NCM 目前保持禁用。要做到 Mac 插线即出现网卡，仍需完成 U60 侧 NCM composition、Mac 枚举、DHCP、联网及失败后 ADB 恢复的整条验收链；把 TetherKit 放进 U60 只能提供安装文件，Mac 端仍需主动运行并授权，不能实现免安装直连。

这条路径的安全边界是：助手只在 Mac 上声明 RNDIS 控制/数据接口；不会向 U60 ConfigFS 写入，不会切换 `gsi.rndis`、ECM 或 NCM，不会执行 adb 写入。TetherKit 上游代码也明确避免 `libusb_set_auto_detach_kernel_driver`，以免触发整设备重新枚举。停止助手后，U60 仍保持原厂 USB 组合，ADB 不需要恢复。

## 安装与连接

在 Release 包的 `macos/` 目录执行：

```sh
sh u60-rndis.sh install
sh u60-rndis.sh doctor
sh u60-rndis.sh start
sh u60-rndis.sh status
```

`install` 通过 Homebrew 安装上游 `tetherkit-cli` 和依赖。首次 `start` 会按 macOS 标准流程请求一次 `sudo`，仅用于创建 `feth` 虚拟网卡和打开 BPF；项目助手不读取或保存管理员密码。需要图形界面时可另行安装 `brew install XiaoMiku01/tap/tetherkit`。

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

## NCM 维护试运行（默认关闭）

安装包中的 `installer/payload/data/u60-panel/usb-ncm-trial.sh` 只用于维护人员在设备旁边、已有 Wi-Fi 恢复入口时做候选验证；它不由启动项或普通 UI 调用。脚本只接受已回读的 B31 原厂 RNDIS + ADB 组合，先保存完整 function 链接、UDC 和描述符；发现原厂 `zte_ubus_bsp_usb`/`zte_usb_switch` owner 竞争时直接拒绝，试运行超时自动恢复。不要手工停止 owner、绕过门禁或把它改成开机服务。

```sh
adb shell /data/u60-panel/usb-ncm-trial.sh status
adb shell /data/u60-panel/usb-ncm-trial.sh start
# 仅在 Mac 原生 NCM 枚举、DHCP、HTTPS 和 ADB 恢复均已确认后才可确认
adb shell /data/u60-panel/usb-ncm-trial.sh confirm
adb shell /data/u60-panel/usb-ncm-trial.sh restore
```
