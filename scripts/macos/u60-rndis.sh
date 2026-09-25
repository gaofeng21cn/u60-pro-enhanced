#!/bin/bash
# Connect an U60 Pro's factory RNDIS gadget to macOS through TetherKit.
# This script never changes the U60 USB gadget composition or runs adb writes.
set -euo pipefail

VID=${U60_USB_VID:-19d2}
PID=${U60_USB_PID:-1404}
RUN_DIR=${U60_RNDIS_RUN_DIR:-${TMPDIR:-/tmp}/u60-rndis}
LOG="$RUN_DIR/tetherkit.log"
PID_FILE="$RUN_DIR/tetherkit.pid"
IF_FILE="$RUN_DIR/interface"

die() { printf '错误：%s\n' "$*" >&2; exit 1; }
note() { printf '%s\n' "$*"; }
need_darwin() { [ "$(uname -s)" = Darwin ] || die '此助手只支持 macOS'; }
need_cli() {
  command -v tetherkit-cli >/dev/null 2>&1 || die '未找到 tetherkit-cli。先运行：brew install XiaoMiku01/tap/tetherkit-cli';
}
safe_iface() { case "$1" in feth[0-9]|feth[0-9][0-9]|feth[0-9][0-9][0-9]) return 0;; esac; return 1; }
running_pid() {
  [ -s "$PID_FILE" ] || return 1
  pid=$(cat "$PID_FILE")
  case "$pid" in *[!0-9]*|'') return 1;; esac
  kill -0 "$pid" 2>/dev/null
}
current_iface() {
  [ -s "$IF_FILE" ] || return 1
  iface=$(cat "$IF_FILE")
  safe_iface "$iface" || return 1
  ifconfig "$iface" >/dev/null 2>&1 || return 1
  printf '%s\n' "$iface"
}
status() {
  need_darwin
  iface=$(current_iface 2>/dev/null || true)
  if running_pid; then
    state=running
  else
    state=stopped
  fi
  if [ -n "$iface" ]; then
    ip=$(ipconfig getifaddr "$iface" 2>/dev/null || true)
    note "状态：$state"
    note "接口：$iface${ip:+ · $ip}"
  else
    note "状态：$state"
    note '接口：未发现'
  fi
  if [ -f "$LOG" ]; then note "日志：$LOG"; fi
  [ "$state" = running ]
}
doctor() {
  need_darwin
  need_cli
  note "macOS：$(sw_vers -productVersion)"
  note "TetherKit：$(tetherkit-cli --version | head -n 1)"
  if tetherkit-cli --lang en --no-color --vid "$VID" --pid "$PID" --list | grep -q " ${VID}:${PID} "; then
    note "U60 RNDIS：已识别（${VID}:${PID}）"
  else
    die "未识别 U60 RNDIS（${VID}:${PID}）；请确认数据线已连接且设备保持原厂 RNDIS"
  fi
  for key in hwcsum fcs tso_support lro; do
    value=$(/usr/sbin/sysctl -n "net.link.fake.$key" 2>/dev/null || true)
    [ "$value" = 0 ] || die "net.link.fake.$key=$value；请先设为 0，再启动 TetherKit"
  done
  note '安全检查：只使用 RNDIS 用户态桥接，不修改 U60 USB 组合'
}
install_tools() {
  need_darwin
  command -v brew >/dev/null 2>&1 || die '未找到 Homebrew；请先安装 Homebrew'
  brew install XiaoMiku01/tap/tetherkit-cli
  note '已安装 tetherkit-cli。首次 start 会请求一次 sudo，用于创建 macOS feth 网卡和打开 BPF。'
  note '如需图形界面，可另行安装：brew install XiaoMiku01/tap/tetherkit'
}
list_devices() {
  need_darwin; need_cli
  tetherkit-cli --lang en --no-color --vid "$VID" --pid "$PID" --list
}
start() {
  route_all=false
  if [ "${1:-}" = --route-all ]; then route_all=true; shift; fi
  [ "$#" = 0 ] || die '用法：start [--route-all]'
  need_darwin; need_cli; doctor >/dev/null
  if running_pid; then
    note 'TetherKit 已在运行'; status || true; exit 0
  fi
  mkdir -p "$RUN_DIR"; chmod 700 "$RUN_DIR"
  : > "$LOG"; rm -f "$IF_FILE"
  sudo -v
  sudo tetherkit-cli --lang en --no-color --vid "$VID" --pid "$PID" --stats 0 >"$LOG" 2>&1 &
  printf '%s\n' "$!" > "$PID_FILE"
  iface=''
  for _ in $(seq 1 30); do
    iface=$(sed -nE 's/.*system side (feth[0-9]+) .*/\1/p' "$LOG" | tail -n 1)
    if [ -n "$iface" ] && safe_iface "$iface" && ifconfig "$iface" >/dev/null 2>&1; then break; fi
    sleep 1
  done
  [ -n "$iface" ] || { tail -n 30 "$LOG" >&2; die 'TetherKit 未创建 macOS 虚拟网卡'; }
  printf '%s\n' "$iface" > "$IF_FILE"
  sudo /sbin/ipconfig set "$iface" DHCP
  ip=''
  for _ in $(seq 1 20); do ip=$(ipconfig getifaddr "$iface" 2>/dev/null || true); [ -n "$ip" ] && break; sleep 1; done
  [ -n "$ip" ] || { note '已创建接口但 DHCP 尚未取得地址；查看日志后运行 status'; exit 1; }
  note "U60 RNDIS 已连接：$iface · $ip"
  if [ "$route_all" = true ]; then
    gateway=$(ipconfig getoption "$iface" router 2>/dev/null || true)
    [ -n "$gateway" ] || die 'DHCP 未提供网关，未修改默认路由'
    sudo /sbin/route -n change default "$gateway"
    note "默认路由已切到 U60（$gateway）；停止时请按需恢复原网络服务"
  fi
}
stop() {
  need_darwin
  if running_pid; then
    pid=$(cat "$PID_FILE"); sudo kill -TERM "$pid" 2>/dev/null || true
    for _ in $(seq 1 20); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
  fi
  iface=$(current_iface 2>/dev/null || true)
  [ -z "$iface" ] || sudo /sbin/ipconfig set "$iface" NONE 2>/dev/null || true
  rm -f "$PID_FILE" "$IF_FILE"
  note 'U60 RNDIS 已停止；U60 USB 组合和 ADB 未被修改'
}
case "${1:-}" in
  install) shift; [ "$#" = 0 ] || die 'install 不接受参数'; install_tools ;;
  doctor) shift; [ "$#" = 0 ] || die 'doctor 不接受参数'; doctor ;;
  list) shift; [ "$#" = 0 ] || die 'list 不接受参数'; list_devices ;;
  start) shift; start "$@" ;;
  status) shift; [ "$#" = 0 ] || die 'status 不接受参数'; status || true ;;
  stop) shift; [ "$#" = 0 ] || die 'stop 不接受参数'; stop ;;
  *)
    cat <<'EOF'
用法：u60-rndis.sh <install|doctor|list|start|status|stop>

  install             安装上游 TetherKit GUI（Homebrew）
  doctor              检查 macOS、TetherKit、U60 RNDIS 和 feth 前置条件
  list                只读列出已识别的 U60 RNDIS
  start [--route-all] 启动用户态 RNDIS，并自动配置 DHCP
  status              查看进程、feth 接口和地址
  stop                停止 TetherKit 并清理临时接口配置

此脚本不会切换 U60 USB gadget，不执行 adb 写入，也不会改变 U60 的 ADB 组合。
EOF
    exit 2
    ;;
esac
