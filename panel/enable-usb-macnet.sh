#!/bin/sh
# Observe USB gadget configuration only. Live composition changes are disabled
# until host re-enumeration and an independent recovery path are qualified.
set -u
GADGET=/sys/kernel/config/usb_gadget/g1
NET=/sys/class/net
UDCS=/sys/class/udc
mode=unknown
adb=false
for link in "$GADGET"/configs/c.1/f*; do
 case "$(readlink "$link" 2>/dev/null || true)" in
  */gsi.ecm|*/ecm.ecm) mode=ecm;;
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
case "$mode" in ecm) names='ecm0 usb0';; rndis) names='rndis0';; *) names='';; esac
for name in $names; do
 if [ "$(cat "$NET/$name/carrier" 2>/dev/null || true)" = 1 ]; then
  carrier=true
  case "$(readlink "$NET/$name/master" 2>/dev/null || true)" in */br-lan) bridged=true;; esac
 fi
done
case "${1:-enable}" in
 status)
  ok=false; [ "$mode" = unknown ] || ok=true
  printf '{"ok":%s,"mode":"%s","adb_function":%s,"bound":%s,"configured":%s,"carrier":%s,"bridged":%s,"switch_available":false}\n' "$ok" "$mode" "$adb" "$bound" "$configured" "$carrier" "$bridged"
  ;;
 enable|restore|rndis)
  printf '%s\n' '{"ok":false,"message":"USB 在线切换尚未通过枚举与回退验证，未修改设备；请保持当前模式并通过 Wi-Fi 管理"}'
  exit 1
  ;;
 *) printf '%s\n' '{"ok":false,"message":"用法：status（只读）；在线切换暂不可用"}'; exit 2;;
esac
