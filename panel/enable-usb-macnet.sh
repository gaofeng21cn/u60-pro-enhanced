#!/bin/sh
# Read-only USB gadget diagnostics. Native NCM switching is quarantined
# until independent recovery has been qualified.
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
case "$mode" in
  ecm) names='ecm0 usb0'; detected=$(cat "$GADGET/functions/ecm.ecm/ifname" 2>/dev/null || true);;
  ncm) names='usb0'; detected=$(cat "$GADGET/functions/ncm.0/ifname" 2>/dev/null || true);;
  rndis) names='rndis0'; detected=$(cat "$GADGET/functions/gsi.rndis/ifname" 2>/dev/null || true);;
  *) names=''; detected='';;
esac
case "$detected" in ''|'(unnamed net_device)'|*[!a-zA-Z0-9_.-]*) ;; *) names="$names $detected";; esac
for name in $names; do
  if [ "$(cat "$NET/$name/carrier" 2>/dev/null || true)" = 1 ]; then
    carrier=true
    case "$(readlink "$NET/$name/master" 2>/dev/null || true)" in */br-lan) bridged=true;; esac
  fi
done

trial=idle
TRIAL=/tmp/u60-ncm-trial
[ ! -f "$TRIAL/state" ] || trial=$(cat "$TRIAL/state")
case "$trial" in idle|pending|active|restored|failed) ;; *) trial=failed;; esac
ncm_present=false
[ ! -d "$GADGET/functions/ncm.0" ] || ncm_present=true
ncm_composition=false
for composition in /data/u60-panel/usb-ncm-composition.sh /sbin/usb/compositions/908C /sbin/usb/compositions/908c; do
  [ ! -x "$composition" ] || { ncm_composition=true; break; }
done

case "${1:-status}" in
  status)
    ok=false
    [ "$mode" = unknown ] || ok=true
    printf '{"ok":%s,"mode":"%s","adb_function":%s,"bound":%s,"configured":%s,"carrier":%s,"bridged":%s,"switch_available":false,"ncm_present":%s,"ncm_composition":%s,"trial_state":"%s"}\n' \
      "$ok" "$mode" "$adb" "$bound" "$configured" "$carrier" "$bridged" "$ncm_present" "$ncm_composition" "$trial"
    ;;
  enable|start|confirm|restore|restore-trial|rndis|invalid)
    printf '%s\n' '{"ok":false,"message":"USB 在线切换已停用；NCM 的 ADB 恢复保护尚未验证"}'
    exit 1
    ;;
  *)
    printf '%s\n' '{"ok":false,"message":"用法：status（只读）；所有 USB 切换入口均已停用"}'
    exit 2
    ;;
esac
