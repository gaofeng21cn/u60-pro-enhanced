# 安装与首次配置

本页是完整的安装流程：确认设备固件 → 构建安装包 → 只读准备 → 检查、安装、启动 → 首次配置。准备阶段只读取设备信息；`check` 只上传校验材料；只有 `install` 会写入设备。

## 前提

只支持已核对指纹的国行 B28（`BD_FLYMODEMMU5250V1.0.0B28`，Linux `5.15.185-perf`）与国行 B31（`BD_CNMU5250V1.0.0B31`，Linux `5.15.194-perf`）。准备器按实机固件生成专用安装目录，并绑定准备时读取的设备身份；固件版本或设备身份不匹配时，检查和安装都会拒绝继续。两个固件的安装包不能互换。首次安装器拒绝覆盖已有同名项目目录；本项目已有安装使用下述原地升级入口，不支持其他项目或任意旧布局的迁移。不要为了安装升级或降级固件。

先确认手头设备的固件，输出必须是上面两个版本号之一；其他固件（含 B27、港版/国际版）不支持：

```sh
adb shell "ubus -t 5 call zwrt_zte_mdm.api get_zwrt_common_info '{}' | jsonfilter -e '@.wa_inner_version'"
```

1. 阅读 README 和[验证范围与风险](VALIDATION.md)；自行备份必要配置，确保知道如何通过 USB ADB 恢复。
2. 自行启用 root ADB，安装 Google 官方 Android Platform Tools。解锁方法参考社区来源，兼容性自行核对；本项目不执行解锁，也不下载刷机固件。
3. USB 数据线直接连接电脑，设备保持亮屏；拔下 USB 网卡，电脑保留其他可用网络用于下载依赖。首次安装保持现有 USB/ADB 组合；Mac 的 ECM 直连尚未验证，不运行在线 USB 组合切换。不要远程跨 Tailscale 执行首次安装。
4. `adb devices` 只连接一台待安装设备；`/data` 至少有 400 MB 可用空间。

macOS/Linux 使用终端；Windows 可用 Python 3 与 `adb.exe`，Windows 完整流程尚未实测。若 adb 不在 PATH，后续每条命令加 `--adb /实际路径/adb`；多设备加 `--serial 目标序列号`，不要把它写到公开 issue 里。

## 取得安装包

### 从本仓库源码构建

可下载本仓库 [v0.1.5-experimental 安装包](https://github.com/gaofeng21cn/u60-pro-enhanced/releases/tag/v0.1.5-experimental)，下载用户不需要编译器。以下是开发者构建方式。构建依赖 Zig 0.14.1、Go、Python 3 和 Git，主机测试还需 C 编译器和 Node.js。**请显式使用 Zig 0.14.1**：0.16 会构建失败。

```sh
sh scripts/build.sh
sh scripts/test.sh
python3 scripts/package.py
```

工具链不在 PATH 时用 `ZIG=/path/to/zig GO=/path/to/go sh scripts/build.sh` 指定。`scripts/package.py` 在 `dist/` 下生成安装包目录和同名 `.tar.gz`；目录已存在时会拒绝重建，需先自行检查并移走旧目录。`test.sh` 只证明主机回归，不代替实机验收，可以按需跳过。

### 下载预编译安装包

下载本仓库 Release 附件中的 tar.gz 与 SHA256SUMS.txt，校验后解压。不要下载自动生成的 Source code 作为安装材料；上游 B28 安装包不能用于 B31。

## 准备（只读设备）

先进入安装包目录。本仓库构建产物位于 `dist/u60-pro-enhanced-<版本>/`（构建后可用 `ls dist/` 查看实际目录名），使用上游 Release 时就是解压出来的同名目录。

```sh
cd dist/u60-pro-enhanced-v0.1.5-experimental
python3 prepare.py
```

`prepare.py` 不读取用户配置，也不写设备：它只读取目标固件号、设备标识摘要和三份固定网页资源，然后联网下载固定版本并校验的依赖，在与安装包同级的 `u60-prepared-private/` 目录生成仅绑定本机的安装材料。设备标识只以摘要保存，用于后续拒绝错误设备。该目录含目标机派生的原厂网页资源，不是可再分发的公开发布物。准备失败时删除这一次未完成的输出目录后重试，不要跳过校验。

## 检查、安装与启动

### 已经装过本项目的设备

不要重复执行首次安装：安装器会拒绝覆盖已有目录。已有安装改用升级入口，先上传材料并检查，再执行升级：

```sh
python3 deploy-from-computer.py upgrade-check
python3 deploy-from-computer.py upgrade
```

`upgrade-check` 会把经过校验的安装材料上传到 `/data/u60-packages/`，随后检查固件、设备身份、已有安装记录、当前版本对应的首次安装备份或上一轮升级备份，以及可用空间；它不替换已安装程序、不重启服务，也不构成升级授权。`upgrade` 只替换程序与网页资源，逐个文件先备份哈希再替换、替换后逐字节核对，然后分别重启网页和屏幕并回读；代理配置、订阅、节点收藏与最近使用、Tailscale 身份和各项本机选择都不改动。任何一步失败会自动恢复上一版本程序，备份保留在 `/data/u60-upgrade-backups/<release>`。

B31 开机后 USB 需要等待原厂 ConfigFS gadget 完成重新枚举；ADB 在这段时间暂时消失属于预期现象。当前版本不执行 USB 组合切换，启动钩子会延迟检查网页服务并在失败时有限重试；不要用旧版 `usb_switch` 或向 `/sys/class/android_usb` 写入开关来“恢复”连接。

准备升级包时如果设备已经挂载增强网页，`prepare.py` 会因原厂页面指纹不符而拒绝：先运行 `/etc/init.d/u60-web stop` 让原厂页面重新可见，准备完成后再 `/etc/init.d/u60-web start`。

以下三条命令依次上传校验材料、写入程序和启动项、启动屏幕与网页扩展：

```sh
cd ../u60-prepared-private
python3 deploy-from-computer.py check
python3 deploy-from-computer.py install
python3 deploy-from-computer.py start
```

`check` 把本地经过校验的安装材料上传到 `/data/u60-packages/` 并只读检查兼容性；`install` 写入新程序与启动项、保存原厂 `/etc/rc.local`，但此时不启动服务；`start` 才启动屏幕与网页扩展。B31 首装仅启动屏幕与网页；用户主动选择 USB 角色后启动协调服务并保存启用意图，接力按独立的开机连接选项恢复。默认仍不启用深度待机；B28 保持原有基础启动行为。校验失败不要强制继续。

先实际测试屏幕、电源键、原厂网页和原有热点，再分别验证网络功能。

初始不含运行中的 Clash 配置或 Tailscale 身份。充电能力与 Wi-Fi 密码加密写入能力没有移植开发机验收标记；相应按钮可能显示“待验证”并拒绝操作，这是公开版本明确保留的限制。不要从他人设备复制验证标记。

升级时字节相同的网络核心保留运行 inode；若新版需要更换正在运行的网络核心，检查会拒绝，需先通过界面停止相应核心再升级，升级后按原有方式启用。

## Clash 首次使用

以下命令明确授权在**这台目标设备本机**生成新的随机 Clash 控制密钥并创建配置，不使用他人凭据：

```sh
adb shell sh /data/u60-panel/setup-clash.sh
```

配置仅写入目标 `/data/u60-clash/config.yaml`，权限 0600，控制 API 只监听本机。初始组选择 DIRECT，先保持直连。

登录原厂网页 → 增强功能 → Clash，添加自己的 **Mihomo proxy-provider 格式**订阅，选择目标策略组、保存/更新。普通完整配置、Base64 分享链接或任意机场格式不保证直接兼容；不提供第三方在线转换服务。先选择节点，确认测速可用，再启用代理路由与规则模式。新增订阅不会自行切走当前节点。

分别测试国内站点和需要代理的站点，并核查连接命中的策略；仅“网页打开”不足以证明命中预期规则。不要将管理端口映射到公网。初始 LAN 为原厂 `192.168.0.0/24`，自定义 LAN 地址需同时审核 Clash 绑定、DNS 和子网设置，当前没有自动迁移保证。

### 原厂时间与代理校时

正确的通用模型是系统 epoch 表示 UTC 时间，显示层按 `Asia/Shanghai` 转换为北京时间；时区不应改变 epoch。B31 实测可能将东八区本地时间写入系统 epoch，同时保持 `TZ=UTC`。小屏显示看似正确，但 VMess 握手会因约 8 小时偏差失败。仅修改 `TZ` 或只减去 8 小时都不能解决原厂 NITZ/NTP 后续写回的问题。

首次配置默认启用 Mihomo 自带 NTP，使用 DIRECT 获取时间、只校正核心内部协议时间，`write-to-system: false`。这是与原厂固件兼容的协议修复，并未修正整机 epoch；保留原厂时钟和定时功能，不固定减去某个时区偏移，也不关闭 TLS 验证。需要能访问所配置 NTP 服务的 UDP 123；同步前或 NTP 不可达时，时间敏感节点仍可能失败。

已有配置的用户可显式执行（升级程序本身不重写私人配置）：

```sh
adb shell sh /data/u60-panel/setup-clash.sh --enable-ntp
```

该命令只在尚无顶层 `ntp` 配置时追加设置，先备份和校验，再热加载；失败恢复原配置。已有 NTP 设置保持原样。待同步后重新请求目标站点；不以配置存在代替联网成功。

## Tailscale 首次使用

```sh
adb shell sh /data/u60-panel/setup-tailscale.sh
```

按终端给出的登录链接，在自己的浏览器完成登录。这会在设备创建自己的持久身份，链接和 state 文件不得公开。首次初始化没有在第二台设备实测；若中途失败，保留状态，先排查，不能通过复制别人的 state 绕过。

默认不接管 DNS，不自动发布子网或使用出口节点。登录后用屏幕/网页启用下属设备转发；需要外部访问热点设备时，发布实际 LAN CIDR，再自行到 Tailscale 后台批准并授权对应访问策略。参考 [Tailscale 子网路由说明](https://tailscale.com/kb/1019/subnets)。外部测试设备应断开 U60 Wi-Fi，使用其他网络。

## 安装后验收

按[验证范围与风险](VALIDATION.md)完成自己的验收。遇到原厂 UI 能恢复而新 UI 不正常，先双击电源切回原厂，再按[回退与保留数据](RECOVERY.md)处理。未确认远程通道和恢复方法前，不做断线/整卡拔出或远程重启实验。
