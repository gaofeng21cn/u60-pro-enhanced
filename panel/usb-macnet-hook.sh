#!/bin/sh
# Invoked by the unchanged vendor USB owner, after its coordinated detach.
# Change only the network function; retain DIAG, serial, storage and ADB.
FLAG=/data/u60-panel/usb-macnet-enabled
if [ "$(cat "$FLAG" 2>/dev/null)" = 1 ] && [ "${2:-}" = 0x1404 ]; then
 case "${3:-}" in
  rndis_gsi,*,ffs*)
   vid=$1; pid=$2; functions="ecm_gsi,${3#rndis_gsi,}"
   shift 3
   set -- "$vid" "$pid" "$functions" "$@";;
 esac
fi
sh /data/u60-panel/usb-macnet-private/factory-usb-switch.sh "$@"
result=$?
if [ "$result" = 0 ] && [ "$(cat "$FLAG" 2>/dev/null)" = 1 ]; then
 case "$(readlink /sys/kernel/config/usb_gadget/g1/configs/c.1/f1)" in
  */gsi.ecm)
   ip link set ecm0 master br-lan && ip link set ecm0 up || exit 1
   ;;
 esac
fi
exit "$result"
