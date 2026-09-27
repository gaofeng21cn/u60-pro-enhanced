#!/bin/sh
# Revert this first-install boot hook. Keep all app data and new credentials.
set -eu
umask 077
cd "$(dirname "$0")"
sha256sum -c SHA256SUMS >/dev/null
ID=$(cat RELEASE-ID)
case "$ID" in u60-pro-B28-[0-9]*|u60-pro-B31-[0-9]*) ;; *) exit 2;; esac
case "$ID" in *[!a-zA-Z0-9-]*) exit 2;; esac
BACKUP="/data/u60-install-backups/$ID"
[ "$(cat "$BACKUP/release-id")" = "$ID" ]
[ "$(cat /data/u60-panel/portable-release)" = "$ID" ]
[ -s "$BACKUP/rc.local" ]
[ "$(grep -c '^(/data/u60-panel/portable-boot.sh) &$' /etc/rc.local)" = 1 ]
sh -n "$BACKUP/rc.local"
if grep -F ' /sbin/usb/compositions/usb_switch ' /proc/mounts >/dev/null || [ "$(cat /data/u60-panel/usb-macnet-enabled 2>/dev/null || true)" = 1 ];then
 echo 'Turn off Mac USB networking through its UI before restoring boot' >&2
 exit 1
fi
for name in u60-usb-isolate u60-usb-role u60-wifi-relay u60-standby u60-web;do
 [ ! -x "/etc/init.d/$name" ] || "/etc/init.d/$name" disable
done
/etc/init.d/u60-web stop
cp -p /etc/rc.local "$BACKUP/rc.local.before-restore"
cp -p "$BACKUP/rc.local" /etc/rc.local.portable-restore
mv /etc/rc.local.portable-restore /etc/rc.local
printf 'boot-restored\n' > "$BACKUP/status"
echo 'Stock boot restored; application data was retained. Current processes remain until power-off.'
echo 'Power off and turn on manually, then verify the stock UI and Wi-Fi. Do not erase the data directories.'
