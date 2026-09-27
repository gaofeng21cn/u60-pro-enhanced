#!/bin/sh
# Read-only USB gadget diagnostics. Native mode has a separate coordinated
# owner; status reads never mutate ConfigFS.
set -u

GADGET=/sys/kernel/config/usb_gadget/g1
NET=/sys/class/net
UDCS=/sys/class/udc

# Inspect all configurations and keep each network function separate. A 9059
# gadget contains both RNDIS and ECM; symlink order cannot select its protocol.
mode=unknown
adb=false
rndis=false
ecm=false
ncm=false
carrier=false
bridged=false
for link in "$GADGET"/configs/c.*/f*; do
  function=$(readlink "$link" 2>/dev/null || true)
  function=${function##*/}
  case "$function" in
    gsi.ecm|ecm.ecm) ecm=true; names='ecm0 usb0';;
    ncm.0) ncm=true; names='usb0';;
    gsi.rndis|rndis.rndis) rndis=true; names='rndis0';;
    ffs.adb) adb=true; continue;;
    *) continue;;
  esac
  detected=$(cat "$GADGET/functions/$function/ifname" 2>/dev/null || true)
  case "$detected" in
    ''|'(unnamed net_device)'|*[!a-zA-Z0-9_.-]*) ;;
    *) names="$detected";;
  esac
  for name in $names; do
    if [ "$(cat "$NET/$name/carrier" 2>/dev/null || true)" = 1 ]; then
      carrier=true
      case "$(readlink "$NET/$name/master" 2>/dev/null || true)" in */br-lan) bridged=true;; esac
    fi
  done
done
count=0
for protocol in rndis ecm ncm; do
 case "$protocol" in rndis) present=$rndis;; ecm) present=$ecm;; ncm) present=$ncm;; esac
 if [ "$present" = true ]; then mode=$protocol; count=$((count + 1)); fi
done
[ "$count" -le 1 ] || mode=mixed

bound=false
configured=false
udc=$(cat "$GADGET/UDC" 2>/dev/null || true)
case "$udc" in
  ''|*[!a-zA-Z0-9._-]*) ;;
  *) bound=true; [ "$(cat "$UDCS/$udc/state" 2>/dev/null || true)" != configured ] || configured=true;;
esac
# Stale interface carrier is not an enumerated USB link.
if [ "$configured" != true ]; then carrier=false; bridged=false; fi

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
ecm_present=false
[ ! -d "$GADGET/functions/gsi.ecm" ] && [ ! -d "$GADGET/functions/ecm.ecm" ] || ecm_present=true
ecm_trial=idle
ECM_TRIAL=/tmp/u60-ecm-trial
[ ! -f "$ECM_TRIAL/state" ] || ecm_trial=$(cat "$ECM_TRIAL/state")
case "$ecm_trial" in idle|pending|active|restored|failed) ;; *) ecm_trial=failed;; esac

case "${1:-status}" in
  status)
    ok=false
    [ "$mode" = unknown ] || ok=true
    printf '{"ok":%s,"mode":"%s","rndis_function":%s,"ecm_function":%s,"ncm_function":%s,"adb_function":%s,"bound":%s,"configured":%s,"carrier":%s,"bridged":%s,"switch_available":false,"ncm_present":%s,"ncm_composition":%s,"trial_state":"%s","ecm_present":%s,"ecm_trial_state":"%s"}\n' \
      "$ok" "$mode" "$rndis" "$ecm" "$ncm" "$adb" "$bound" "$configured" "$carrier" "$bridged" "$ncm_present" "$ncm_composition" "$trial" "$ecm_present" "$ecm_trial"
    ;;
  enable|start|confirm|restore|restore-trial|rndis|invalid)
    printf '%s\n' '{"ok":false,"message":"旧 USB 试验入口已停用；请使用 Mac USB 联网开关"}'
    exit 1
    ;;
  *)
    printf '%s\n' '{"ok":false,"message":"用法：status（只读）；此诊断脚本不执行 USB 切换"}'
    exit 2
    ;;
esac
