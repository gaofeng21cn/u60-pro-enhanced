#!/bin/sh
# Use ZTE's coordinated USB owner; never write ConfigFS or terminate adbd.
set -u
umask 077
ROOT=/data/u60-panel
PRIVATE=$ROOT/usb-macnet-private
R=/tmp/u60-macnet
G=/sys/kernel/config/usb_gadget/g1
OP=/sys/class/android_usb/android0/usb_op
VENDOR=/sbin/usb/compositions/usb_switch
HOOK=$ROOT/usb-macnet-hook.sh
ORIGINAL=$PRIVATE/factory-usb-switch.sh
FLAG=$ROOT/usb-macnet-enabled
EXPECTED=a02435921bf6340773967eb7764b685fef78964af7ae4260383ab5514ad9093e

digest() { sha256sum "$1" 2>/dev/null | cut -d ' ' -f1; }
mounted() { grep -F " $VENDOR " /proc/mounts >/dev/null; }
capable() {
 [ "$(uname -r)" = 5.15.194-perf ] || return 1
 [ "$(cat "$ROOT/compat-mode" 2>/dev/null)" = b31-ui-first ] || return 1
 [ -d "$G/functions/gsi.ecm" ] && [ -d "$G/functions/ffs.adb" ] || return 1
 if mounted; then
  [ "$(digest "$VENDOR")" = "$(digest "$HOOK")" ] || return 1
  [ "$(digest "$ORIGINAL")" = "$EXPECTED" ]
 else [ "$(digest "$VENDOR")" = "$EXPECTED" ]; fi
}
now() { cut -d. -f1 /proc/uptime; }
phase() { printf '%s\n' "$1" > "$R/phase.$$" && mv "$R/phase.$$" "$R/phase"; }
save_flag() { printf '%s\n' "$1" > "$FLAG.$$" && mv "$FLAG.$$" "$FLAG"; }
links() { for p in "$G"/configs/c.*/f*; do [ -L "$p" ] && printf '%s %s\n' "$p" "$(readlink "$p")"; done; }
links_ok() {
 case "$(readlink "$G/configs/c.1/f1")" in */"$1") ;; *) return 1;; esac
 adb_found=0
 for p in "$G"/configs/c.1/f*; do
  case "$(readlink "$p")" in */ffs.adb) adb_found=1;; esac
 done
 [ "$adb_found" = 1 ] && pidof adbd >/dev/null && [ -n "$(cat "$G/UDC")" ] &&
 [ "$(cat "$G/idProduct")" = 0x1404 ] && [ ! -s /tmp/usb_bind_in_progress ]
}
exact_links() {
 links | sed 's|/gsi.ecm$|/gsi.rndis|' > "$R/current-links"
 cmp -s "$PRIVATE/factory-links" "$R/current-links"
}
mode_ready() {
 [ "$(cat "$OP")" = "$1" ] && [ ! -s /tmp/usb_bind_in_progress ] && [ -n "$(cat "$G/UDC")" ] || return 1
 if [ "$1" = 0 ]; then
  [ "$(cat "$G/idProduct")" = 0x1403 ]
 else
  links_ok "$2" && exact_links || return 1
  [ "$2" != gsi.ecm ] || [ "$(readlink /sys/class/net/ecm0/master)" = ../../devices/virtual/net/br-lan ] ||
   [ "$(readlink -f /sys/class/net/ecm0/master)" = /sys/devices/virtual/net/br-lan ]
 fi
}
wait_mode() {
 deadline=$(( $(now) + 18 ))
 while [ "$(now)" -lt "$deadline" ]; do
  # Two settled reads prevent accepting usb_op before asynchronous rebind.
  if mode_ready "$1" "$2"; then sleep 1; mode_ready "$1" "$2" && return 0; fi
  sleep 1
 done
 return 1
}
cycle() {
 printf '0\n' > "$OP" || return 1
 wait_mode 0 gsi.rndis || return 1
 printf '1\n' > "$OP" || return 1
 wait_mode 1 "$1"
}
apply() {
 trap 'exit 1' TERM INT
 save_flag 1 && { mounted || mount --bind "$HOOK" "$VENDOR"; } && cycle gsi.ecm
}
restore() {
 trap 'exit 1' TERM INT
 # Clear intent before unmounting so a vendor callback always restores RNDIS.
 save_flag 0 || return 1
 if mounted; then umount "$VENDOR" || return 1; fi
 [ ! -d /sys/class/net/ecm0/brport ] || ip link set ecm0 nomaster || return 1
 cycle gsi.rndis && exact_links && [ "$(digest "$VENDOR")" = "$EXPECTED" ]
}
bounded() {
 operation=$1
 rm -f "$R/result"
 # The supervisor retains the lock and monotonic deadline. The switching child
 # has no client descriptors; a blocked sysfs write cannot block its supervisor.
 ( "$operation"; rc=$?; printf '%s\n' "$rc" > "$R/result" ) 9>&- </dev/null >>"$R/switch.log" 2>&1 &
 child=$!
 end=$(( $(now) + 45 ))
 while [ "$(now)" -lt "$end" ]; do
  if [ -f "$R/result" ]; then wait "$child"; [ "$(cat "$R/result")" = 0 ]; return; fi
  kill -0 "$child" 2>/dev/null || return 1
  sleep 1
 done
 kill -TERM "$child" 2>/dev/null || true
 # Do not wait on a potentially blocked kernel request, or race it with a new
 # writer. Clear transformation intent; report the unresolved recovery boundary.
 save_flag 0
 return 2
}
finish() { printf '{"ok":%s,"message":"%s"}\n' "$1" "$2"; }

case "${1:-status}" in
 capability) capable && printf '1\n' || printf '0\n'; exit 0;;
 status)
  enabled=false; [ "$(cat "$FLAG" 2>/dev/null)" != 1 ] || enabled=true
  state_phase=$(cat "$R/phase" 2>/dev/null || echo idle)
  case "$state_phase" in queued|switching|enabled|disabled|restoring|failed|recovery_failed|idle|waiting) ;; *) state_phase=failed;; esac
  available=false; capable && available=true
  active=false; links_ok gsi.ecm && [ -d /sys/class/net/ecm0/brport ] && active=true
  printf '{"ok":true,"enabled":%s,"active":%s,"available":%s,"phase":"%s"}\n' "$enabled" "$active" "$available" "$state_phase"
  exit 0;;
 enable|disable|boot|worker) action=$1;;
 *) finish false '无效 USB 模式操作'; exit 2;;
esac

if [ "$action" = boot ]; then
 [ "$(cat "$FLAG" 2>/dev/null)" = 1 ] || exit 0
 capable || exit 1
 mkdir -p "$R" || exit 1
 exec 8>"$R/boot-lock"
 flock -n 8 || exit 0
 phase waiting
 # A battery-powered boot may have no host. Retain intent and wait cheaply for
 # the first cable attachment instead of discarding the user's setting.
 while [ "$(cat "$FLAG" 2>/dev/null)" = 1 ]; do
  if [ "$(cat /sys/bus/platform/devices/a600000.ssusb/mode 2>/dev/null)" = peripheral ] && links_ok gsi.rndis; then break; fi
  if links_ok gsi.ecm; then phase enabled; exit 0; fi
  sleep 5
 done
 [ "$(cat "$FLAG" 2>/dev/null)" = 1 ] || exit 0
 action=enable
fi

if [ "$action" != worker ]; then
 capable || { finish false '此固件或原厂 USB 脚本未经兼容验证'; exit 1; }
 [ "$(cat /sys/bus/platform/devices/a600000.ssusb/mode 2>/dev/null)" = peripheral ] || { finish false '请先拔下 USB 网卡，使用数据线连接电脑'; exit 1; }
 [ "$(cat "$OP")" = 1 ] && [ ! -s /tmp/usb_bind_in_progress ] || { finish false '原厂 USB 调试模式尚未就绪，请稍后重试'; exit 1; }
 mkdir -p "$R" "$PRIVATE" || exit 1
 exec 9>"$R/lock"
 flock -n 9 || { finish false 'USB 模式正在应用，请等待结果'; exit 1; }
 # Never blindly retry after an unresolved kernel writer.
 [ "$(cat "$R/phase" 2>/dev/null)" != recovery_failed ] || { finish false 'USB 恢复未完成，请先通过 Wi-Fi 管理检查设备'; exit 1; }
 if [ "$action" = enable ] && [ "$(cat "$FLAG" 2>/dev/null)" = 1 ] && links_ok gsi.ecm && exact_links && [ -d /sys/class/net/ecm0/brport ]; then
  phase enabled; finish true 'Mac USB 联网已启用'; exit 0
 fi
 if [ "$action" = disable ] && ! mounted && links_ok gsi.rndis; then
  save_flag 0; phase disabled; finish true '已使用原厂 USB 模式'; exit 0
 fi
 if ! mounted; then
  links_ok gsi.rndis || { finish false '当前 USB 组合与原厂调试模式不一致'; exit 1; }
  cp "$VENDOR" "$ORIGINAL.next" && chmod 700 "$ORIGINAL.next" && mv "$ORIGINAL.next" "$ORIGINAL" || exit 1
  links > "$PRIVATE/factory-links"
 fi
 [ "$(digest "$ORIGINAL")" = "$EXPECTED" ] && exact_links || { finish false '原厂 USB 恢复快照校验失败'; exit 1; }
 printf '%s\n' "$action" > "$R/action"
 phase queued
 # Keep fd 9 in the detached supervisor: no request/startup race or stale job.
 nohup sh "$0" worker </dev/null >"$R/operation.log" 2>&1 &
 finish true '正在应用 USB 模式，连接会短暂重建；完成后自动保存开机选择'
 exit 0
fi

# Only the launcher holding the inherited lock may start a worker.
[ -e /proc/$$/fd/9 ] || exit 1
requested=$(cat "$R/action" 2>/dev/null)
case "$requested" in enable|disable) ;; *) phase failed; exit 1;; esac
capable || { phase failed; exit 1; }
if [ "$requested" = enable ]; then
 phase switching
 bounded apply; result=$?
 if [ "$result" = 0 ]; then phase enabled; exit 0; fi
 if [ "$result" = 2 ]; then phase recovery_failed; exit 1; fi
fi
phase restoring
if bounded restore; then
 if [ "$requested" = disable ]; then phase disabled; else phase failed; fi
else phase recovery_failed; fi
