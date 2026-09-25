#!/bin/sh
# Observe USB gadget configuration; the ECM candidate remains disabled until
# device rollback and macOS network acceptance are qualified.
ECM_QUALIFIED=false
case "${1:-status}" in start|confirm|restore-trial|supervise) ;; *) U60_CABLE_STATUS=1;; esac
if [ "${U60_CABLE_STATUS:-0}" = 1 ]; then
set -u
GADGET=/sys/kernel/config/usb_gadget/g1
NET=/sys/class/net
UDCS=/sys/class/udc
mode=unknown
adb=false
for link in "$GADGET"/configs/c.1/f*; do
 case "$(readlink "$link" 2>/dev/null || true)" in
  */gsi.ecm|*/ecm.ecm) mode=ecm;;
  */ncm.0) mode=ncm;;
  */gsi.rndis|*/rndis.rndis) mode=rndis;;
  */ffs.adb) adb=true;;
 esac
done
bound=false
configured=false
udc=$(cat "$GADGET/UDC" 2>/dev/null || true)
case "$udc" in
 ''|*[!a-zA-Z0-9._-]*) ;;
 *) bound=true; [ "$(cat "$UDCS/$udc/state" 2>/dev/null || true)" != configured ] || configured=true;;
esac
carrier=false
bridged=false
case "$mode" in ecm) names='ecm0 usb0'; detected=$(cat "$GADGET/functions/ecm.ecm/ifname" 2>/dev/null || true); if [ "$detected" != "(unnamed net_device)" ]; then case "$detected" in ''|*[!a-zA-Z0-9_.-]*) ;; *) names="$names $detected";; esac; fi;; ncm) names='usb0';; rndis) names='rndis0';; *) names='';; esac
for name in $names; do
 if [ "$(cat "$NET/$name/carrier" 2>/dev/null || true)" = 1 ]; then
  carrier=true
  case "$(readlink "$NET/$name/master" 2>/dev/null || true)" in */br-lan) bridged=true;; esac
 fi
done
trial=idle
TRIAL=/tmp/u60-ecm-trial
[ ! -f "$TRIAL/state" ] || trial=$(cat "$TRIAL/state")
case "$trial" in idle|pending|active|restored|failed) ;; *) trial=failed;; esac
available=false
if [ "$ECM_QUALIFIED" = true ] && [ "$(uname -r)" = 5.15.194-perf ] && [ -d "$GADGET/functions/ecm.ecm" ] && [ "$adb" = true ] && [ "$bound" = true ] && [ "$mode" = rndis ]; then available=true; fi
case "${1:-enable}" in
 status)
  ok=false; [ "$mode" = unknown ] || ok=true
  printf '{"ok":%s,"mode":"%s","adb_function":%s,"bound":%s,"configured":%s,"carrier":%s,"bridged":%s,"switch_available":%s,"trial_state":"%s"}\n' "$ok" "$mode" "$adb" "$bound" "$configured" "$carrier" "$bridged" "$available" "$trial"
  ;;
 enable|restore|rndis)
  printf '%s\n' '{"ok":false,"message":"USB 在线切换尚未通过枚举与回退验证，未修改设备；请保持当前模式并通过 Wi-Fi 管理"}'
  exit 1
  ;;
 *) printf '%s\n' '{"ok":false,"message":"用法：status（只读）；在线切换暂不可用"}'; exit 2;;
esac

exit $?
fi
set -eu
umask 077
BASE=${U60_ECM_TEST_ROOT:-}
G="$BASE/sys/kernel/config/usb_gadget/g1"
N="$BASE/sys/class/net"
R="$BASE/tmp/u60-ecm-trial"
IP=${USB_IP:-ip}
SELF=$(readlink -f "$0")
fail() { printf '{"ok":false,"message":"%s"}\n' "$1"; exit 1; }
valid_name() { case "$1" in ''|*[!a-zA-Z0-9_.-]*) return 1;; esac; }
iface_name() {
 name=$(cat "$G/functions/ecm.ecm/ifname" 2>/dev/null || true)
 valid_name "$name" && [ -e "$N/$name" ] || return 1
 printf '%s' "$name"
}
restore() {
 [ -f "$R/original" ] || return 0
 udc=$(cat "$R/udc"); valid_name "$udc" || return 1
 target=$(cat "$R/original"); [ "$target" = ../../../../usb_gadget/g1/functions/gsi.rndis ] || return 1
 current=$(readlink "$G/configs/c.1/f1" 2>/dev/null || true)
 case "$current" in ''|../../../../usb_gadget/g1/functions/ecm.ecm|../../../../usb_gadget/g1/functions/gsi.rndis) ;; *) return 1;; esac
 iface=$(iface_name 2>/dev/null || true)
 if valid_name "$iface" && [ -e "$N/$iface" ]; then
  master=$(readlink -f "$N/$iface/master" 2>/dev/null || true)
  if [ "${master##*/}" = br-lan ]; then "$IP" link set dev "$iface" nomaster || return 1; fi
 fi
 printf '\n' > "$G/UDC" || return 1
 if [ -L "$G/configs/c.1/f1" ]; then rm "$G/configs/c.1/f1" || return 1; fi
 ln -s "$target" "$G/configs/c.1/f1" || return 1
 printf '%s\n' "$udc" > "$G/UDC" || return 1
 [ "$(cat "$G/UDC")" = "$udc" ] || return 1
 printf restored > "$R/state"
}
case "${1:-status}" in
 status)
  state=idle; [ ! -f "$R/state" ] || state=$(cat "$R/state")
  case "$state" in idle|pending|active|restored|failed) ;; *) state=failed;; esac
  printf '{"ok":true,"state":"%s"}\n' "$state";;
 start)
  [ "$ECM_QUALIFIED" = true ] || [ -n "$BASE" ] || fail 'ECM 实机恢复尚未通过验证，未修改 USB'
  mkdir -p "$R"
  exec 9>"$R/lock"; flock -n 9 || fail 'USB 操作正在执行'
  [ ! -e "$R/state" ] || { [ "$(cat "$R/state")" = restored ] || fail '已有 USB 试运行，请先恢复'; }
  [ "$(uname -r)" = 5.15.194-perf ] || [ -n "$BASE" ] || fail '仅支持已核对的 B31 内核'
  [ -n "$BASE" ] || [ "$(ubus -t 5 call zwrt_zte_mdm.api get_zwrt_common_info '{}' | jsonfilter -e '@.wa_inner_version')" = BD_CNMU5250V1.0.0B31 ] || fail '固件不匹配'
  [ -d "$G/functions/ecm.ecm" ] && [ -e "$N/br-lan" ] || fail 'ECM 或内网未就绪'
  original=$(readlink "$G/configs/c.1/f1")
  [ "$original" = ../../../../usb_gadget/g1/functions/gsi.rndis ] || fail '当前 USB 组合不在支持范围'
  adb=false
  for f in "$G"/configs/c.1/f*; do case "$(readlink "$f")" in */functions/ffs.adb) adb=true;; esac; done
  [ "$adb" = true ] || fail '当前组合没有 ADB'
  udc=$(cat "$G/UDC"); valid_name "$udc" || fail 'USB 未绑定'
  mkdir -p "$R"
  printf '%s\n' "$original" > "$R/original"; printf '%s\n' "$udc" > "$R/udc"
  rm -f "$R/confirmed" "$R/cancel"
  printf pending > "$R/state"
  # The detached supervisor survives USB transport loss and owns rollback.
  nohup sh "$SELF" supervise </dev/null >"$R/log" 2>&1 &
  printf '{"ok":true,"message":"ECM 试运行已启动；90 秒内验证并确认，否则自动恢复 RNDIS"}\n';;
 supervise)
  # FD 9 is the operation lock inherited from start.
  flock -n 9 || exit 1
  [ "$(cat "$R/state")" = pending ] || exit 1
  trap 'restore || printf failed > "$R/state"' EXIT
  trap 'exit 1' HUP INT TERM
  sleep 2
  [ ! -e "$R/cancel" ] || exit 0
  printf '\n' > "$G/UDC"
  rm "$G/configs/c.1/f1"
  ln -s ../../../../usb_gadget/g1/functions/ecm.ecm "$G/configs/c.1/f1"
  printf '%s\n' "$(cat "$R/udc")" > "$G/UDC"
  iface=$(iface_name) || fail 'ECM 接口尚未出现'
  "$IP" link set dev "$iface" master br-lan
  "$IP" link set dev "$iface" up
  n=0
  while [ "$n" -lt 90 ]; do
   [ ! -e "$R/cancel" ] || exit 0
   if [ -f "$R/confirmed" ] && [ "$(cat "$N/$iface/carrier")" = 1 ]; then
    master=$(readlink -f "$N/$iface/master" 2>/dev/null || true)
    if [ "${master##*/}" = br-lan ]; then printf active > "$R/state"; trap - EXIT HUP INT TERM; exit 0; fi
   fi
   sleep 1; n=$((n+1))
  done;;
 confirm)
  [ "$(cat "$R/state" 2>/dev/null)" = pending ] || fail '没有待确认的试运行'
  [ "$(readlink "$G/configs/c.1/f1")" = ../../../../usb_gadget/g1/functions/ecm.ecm ] || fail 'ECM 尚未生效'
  iface=$(iface_name) || fail 'ECM 接口未知'
  [ "$(cat "$N/$iface/carrier")" = 1 ] || fail 'Mac 网络链路未建立'
  master=$(readlink -f "$N/$iface/master"); [ "${master##*/}" = br-lan ] || fail 'ECM 未接入内网'
  touch "$R/confirmed"
  n=0
  while [ "$n" -lt 4 ] && [ "$(cat "$R/state")" = pending ]; do sleep 1; n=$((n+1)); done
  [ "$(cat "$R/state")" = active ] || fail '试运行已结束，未保留 ECM'
  printf '{"ok":true,"message":"已保留本次 ECM 连接；重启恢复原厂 USB 组合"}\n';;
 restore-trial)
  [ -d "$R" ] || fail '没有本次 ECM 配置可恢复'
  touch "$R/cancel"
  exec 8>"$R/lock"; flock -w 5 8 || fail '试运行尚未结束'
  restore || fail 'USB 恢复未完成'
  rm -f "$R/confirmed"
  printf '{"ok":true,"message":"已恢复原厂 RNDIS 组合"}\n';;
 *) fail '用法：status | start | confirm | restore-trial';;
esac
