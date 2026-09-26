#!/bin/sh
# B31 native ECM maintenance transaction.
#
# The normal UI never starts this transaction until the device has passed the
# real recovery gate.  A test root is accepted only by the host fixture tests;
# it is not a device-side override.  Every restore goes through the factory
# composition owner so FunctionFS/ADB is rebuilt by the vendor service.
set -u
umask 077

G=${U60_ECM_TEST_ROOT:-}/sys/kernel/config/usb_gadget/g1
N=${U60_ECM_TEST_ROOT:-}/sys/class/net
U=${U60_ECM_TEST_ROOT:-}/sys/class/udc
R=${U60_ECM_TEST_ROOT:-}/tmp/u60-ecm-trial
ADB=${U60_ECM_TEST_ROOT:-}/dev/usb-ffs/adb
OWNER=${USB_COMPOSITION:-/sbin/usb_composition}
COMPOSITION=${USB_ECM_COMPOSITION:-9059}
RESTORE_COMPOSITION=${USB_ECM_RESTORE_COMPOSITION:-9059}
INIT=${USB_ECM_INIT:-/etc/init.d/u60-ecm-trial}
IP=${USB_IP:-ip}

# Keep this literal false in device builds.  The host tests replace it in a
# temporary copy, which cannot affect a real target.
ECM_RELEASE_ENABLED=false
TEST_ROOT=${U60_ECM_TEST_ROOT:-}

fail() { printf '{"ok":false,"message":"%s"}\n' "$1"; exit 1; }
valid_name() { case "$1" in ''|*[!a-zA-Z0-9._-]*) return 1;; esac; }
read_attr() { cat "$1" 2>/dev/null || true; }
state() { [ -f "$R/state" ] && cat "$R/state" || printf idle; }
root_path() { printf '%s' "${1#"$G/"}"; }

owner_busy() {
 [ -e /tmp/usb_bind_in_progress ] && return 0
 command -v pidof >/dev/null 2>&1 && pidof zte_ubus_bsp_usb >/dev/null 2>&1 && return 0
 ps 2>/dev/null | grep -E '[ /]zte_usb_switch( |$)' >/dev/null 2>&1 && return 0
 ps 2>/dev/null | grep -E '[ /]usb_composition( |$)' >/dev/null 2>&1 && return 0
 return 1
}

ecm_link() {
 for link in "$G/configs/c."*/f*; do
  case "$(readlink "$link" 2>/dev/null || true)" in
   */gsi.ecm|*/ecm.ecm) printf '%s' "$link"; return 0;;
  esac
 done
 return 1
}

ecm_iface() {
 name=$(read_attr "$G/functions/gsi.ecm/ifname")
 [ -n "$name" ] || name=$(read_attr "$G/functions/ecm.ecm/ifname")
 case "$name" in ''|'(unnamed net_device)'|*[!a-zA-Z0-9_.-]*) return 1;; esac
 [ -e "$N/$name" ] || return 1
 printf '%s' "$name"
}

has_adb_link() {
 for link in "$G/configs/c."*/f*; do
  case "$(readlink "$link" 2>/dev/null || true)" in */functions/ffs.adb) return 0;; esac
 done
 return 1
}

configured() {
 udc=$(read_attr "$G/UDC"); valid_name "$udc" || return 1
 [ "$(read_attr "$U/$udc/state")" = configured ] || return 1
}

snapshot() {
 mkdir -p "$R/snapshot" || return 1
 : > "$R/snapshot/links" || return 1
 for cfg in "$G"/configs/c.*; do
  [ -d "$cfg" ] || continue
  cn=${cfg##*/}; valid_name "$cn" || return 1
  for link in "$cfg"/f*; do
   [ -L "$link" ] || continue
   fn=${link##*/}; valid_name "$fn" || return 1
   target=$(readlink "$link")
   case "$target" in ../../../../usb_gadget/g1/functions/*) ;; *) return 1;; esac
   printf '%s\t%s\t%s\n' "$cn" "$fn" "$target" >> "$R/snapshot/links" || return 1
  done
 done
 udc=$(read_attr "$G/UDC"); valid_name "$udc" || return 1
 printf '%s\n' "$udc" > "$R/snapshot/udc" || return 1
}

run_owner() {
 # Host fixtures execute a deterministic fake owner synchronously. On the
 # device, BusyBox/OpenWrt provides timeout; never let a vendor owner block
 # the supervisor indefinitely.
 if [ -n "$TEST_ROOT" ] || ! command -v timeout >/dev/null 2>&1; then
  "$OWNER" "$1" n n y >"$R/owner.log" 2>&1
 else
  timeout 20 "$OWNER" "$1" n n y >"$R/owner.log" 2>&1
 fi
}

restore() {
 [ -s "$R/snapshot/udc" ] || return 1
 # The vendor owner recreates FunctionFS and starts the ADB-facing parts. A
 # raw ConfigFS relink is only a last diagnostic fallback and is never used as
 # a success condition.
 run_owner "$RESTORE_COMPOSITION" || return 1
 n=0
 while [ "$n" -lt 20 ]; do
  if configured && has_adb_link && [ -e "$ADB/ep0" ] && { ! command -v pidof >/dev/null 2>&1 || pidof adbd >/dev/null 2>&1; }; then
   printf restored > "$R/state"
   return 0
  fi
  sleep 1; n=$((n + 1))
 done
 return 1
}

supervise() {
 exec 9>"$R/lock" || exit 1
 flock -n 9 || exit 1
 [ "$(state)" = pending ] || exit 1
 trap 'restore || printf failed > "$R/state"' EXIT
 trap 'exit 1' HUP INT TERM
 sleep 2
 [ ! -e "$R/cancel" ] || exit 0
 owner_busy && fail '原厂 USB owner 正在切换'
 run_owner "$COMPOSITION" || fail '原厂 ECM composition 未完成'
 iface=$(ecm_iface) || fail 'ECM 网口未出现'
 "$IP" link set dev "$iface" master br-lan || fail 'ECM 未加入内网'
 "$IP" link set dev "$iface" up || fail 'ECM 网口无法启动'
 n=0
 while [ "$n" -lt 90 ]; do
  [ ! -e "$R/cancel" ] || exit 0
  if [ -f "$R/confirmed" ] && [ "$(read_attr "$N/$iface/carrier")" = 1 ]; then
   master=$(readlink -f "$N/$iface/master" 2>/dev/null || true)
   [ "${master##*/}" = br-lan ] && { printf active > "$R/state"; trap - EXIT HUP INT TERM; exit 0; }
  fi
  sleep 1; n=$((n + 1))
 done
}

start_trial() {
 [ "$ECM_RELEASE_ENABLED" = true ] || [ -n "$TEST_ROOT" ] || fail 'ECM 恢复验收尚未通过，未修改 USB'
 [ -x "$OWNER" ] || fail '原厂 USB owner 不可用'
 [ -d "$G/functions/gsi.ecm" ] || [ -d "$G/functions/ecm.ecm" ] || fail '设备没有 ECM function'
 [ "$(state)" = idle ] || [ "$(state)" = restored ] || fail '已有 ECM 试运行，请先恢复'
 configured || fail 'USB 尚未完成原厂枚举'
 has_adb_link || fail '当前组合没有 ADB'
 owner_busy && fail '原厂 USB owner 正在运行，拒绝切换'
 mkdir -p "$R" || fail '无法建立 ECM 事务目录'
 exec 9>"$R/lock"; flock -n 9 || fail 'USB 操作正在执行'
 snapshot || fail '无法保存完整 USB 组合'
 rm -f "$R/confirmed" "$R/cancel"
 printf pending > "$R/state"
 exec 9>&-
 if [ -x "$INIT" ]; then "$INIT" start || fail '无法启动 ECM 监督服务'; else nohup sh "$0" supervise </dev/null >"$R/log" 2>&1 & fi
 printf '%s\n' '{"ok":true,"message":"ECM 试运行已启动；验证完成前保持恢复窗口"}'
}

confirm_trial() {
 [ "$(state)" = pending ] || fail '没有待确认的 ECM 试运行'
 iface=$(ecm_iface) || fail 'ECM 网口未知'
 [ "$(read_attr "$N/$iface/carrier")" = 1 ] || fail 'Mac ECM 链路未建立'
 master=$(readlink -f "$N/$iface/master" 2>/dev/null || true)
 [ "${master##*/}" = br-lan ] || fail 'ECM 未接入内网'
 touch "$R/confirmed"
 n=0; while [ "$n" -lt 8 ] && [ "$(state)" = pending ]; do sleep 1; n=$((n + 1)); done
 [ "$(state)" = active ] || fail 'ECM 业务确认未通过，已进入恢复'
 printf '%s\n' '{"ok":true,"message":"ECM 已确认；拔插或冷启动恢复仍需验收"}'
}

restore_trial() {
 [ -d "$R" ] || fail '没有 ECM 试运行事务'
 touch "$R/cancel"
 [ ! -x "$INIT" ] || "$INIT" stop >/dev/null 2>&1 || true
 n=0
 while [ "$n" -lt 10 ]; do
  exec 8>"$R/lock"; if flock -n 8; then break; fi
  sleep 1; n=$((n + 1))
 done
 flock -n 8 || fail 'ECM 试运行尚未结束'
 restore || fail '原厂 USB owner 恢复失败；需要物理恢复'
 rm -f "$R/confirmed" "$R/cancel"
 printf '%s\n' '{"ok":true,"message":"已由原厂 USB owner 恢复 ADB 与 USB 组合"}'
}

case "${1:-status}" in
 status)
  trial=$(state); case "$trial" in idle|pending|active|restored|failed) ;; *) trial=failed;; esac
  busy=false; owner_busy && busy=true
  printf '{"ok":true,"state":"%s","owner_busy":%s,"adb_function":%s,"configured":%s}\n' "$trial" "$busy" "$(has_adb_link && echo true || echo false)" "$(configured && echo true || echo false)"
  ;;
 start) start_trial;;
 supervise) supervise;;
 confirm) confirm_trial;;
 restore|restore-trial) restore_trial;;
 *) fail '用法：status | start | confirm | restore';;
esac
