#!/bin/sh
# B31 native NCM composition for the factory usb_composition owner.
# The caller must pass from_adb=y so FunctionFS/ADB remains mounted.
# Quarantine the direct entry as well as usb-ncm-trial.sh. Keeping an ADB
# function link did not keep the transport recoverable on the real device.
printf '%s\n' 'NCM composition disabled: ADB recovery is not qualified' >&2
exit 1

set -eu
umask 077

G=${USB_NCM_GADGET:-/sys/kernel/config/usb_gadget/g1}
UDCS=${USB_NCM_UDCS:-/sys/class/udc}
MARKER=${USB_NCM_BIND_MARKER:-/tmp/usb_bind_in_progress}
udc=''
from_adb=${3:-n}

fail() { echo "NCM composition refused: $1" >&2; exit 1; }
valid_name() { case "$1" in ''|*[!a-zA-Z0-9._-]*) return 1;; esac; }

[ "$from_adb" = y ] || fail 'from_adb=y is required to protect ADB'
[ -d "$G" ] || fail 'ConfigFS gadget is missing'
udc=$(ls -1 "$UDCS" 2>/dev/null | head -n 1)
valid_name "$udc" || fail 'UDC name is invalid'
[ -e "$UDCS/$udc" ] || fail 'UDC is not present'
[ -d "$G/functions/ncm.0" ] || fail 'NCM function is missing from the kernel'

# This is deliberately the current B31 1404 function set with only the
# network function replaced. Refuse a partial or foreign gadget before the
# existing UDC is detached.
for fn in ncm.0 ffs.diag cser.nmea.1 cser.dun.0 mass_storage.0 ffs.adb gsi.dpl qdss.qdss_mdm; do
 [ -d "$G/functions/$fn" ] || fail "required function is missing: $fn"
done
[ -d "$G/configs/c.1" ] || fail 'factory configuration is missing'

serial=$(cat "$G/strings/0x409/serialnumber" 2>/dev/null || true)
[ -n "$serial" ] || serial=0123456789ABCDEF

echo start > "$MARKER"
trap 'rm -f "$MARKER"' EXIT
cd "$G"
printf '\n' > UDC
rm -f os_desc/c.* configs/c.*/f*
rmdir configs/c.2 configs/c.3 2>/dev/null || true

echo 0x19d2 > idVendor
echo 0x908C > idProduct
echo 0x00 > bDeviceClass
echo "$serial" > strings/0x409/serialnumber
echo 'ZTE Mobile Broadband' > strings/0x409/product
echo 'ZTE,Incorporated' > strings/0x409/manufacturer
echo 'NCM + DIAG + NMEA + DUN + MASS + ADB + DPL + QDSS' > configs/c.1/strings/0x409/configuration

ln -s ../../../../usb_gadget/g1/functions/ncm.0 configs/c.1/f1
ln -s ../../../../usb_gadget/g1/functions/ffs.diag configs/c.1/f2
ln -s ../../../../usb_gadget/g1/functions/cser.nmea.1 configs/c.1/f3
ln -s ../../../../usb_gadget/g1/functions/cser.dun.0 configs/c.1/f4
ln -s ../../../../usb_gadget/g1/functions/mass_storage.0 configs/c.1/f5
ln -s ../../../../usb_gadget/g1/functions/ffs.adb configs/c.1/f6
ln -s ../../../../usb_gadget/g1/functions/gsi.dpl configs/c.1/f7
ln -s ../../../../usb_gadget/g1/functions/qdss.qdss_mdm configs/c.1/f8

rm -f "$MARKER"
echo "$udc" > UDC
cd /
