#!/bin/sh
# First installation only. No credentials, identity creation or network config writes.
set -eu
umask 077
cd "$(dirname "$0")"
PKG=$(pwd)
ACTION=${1:-check}
case "$ACTION" in check|--install|--start) ;; *) echo 'Use check, --install or --start' >&2;exit 2;; esac
fail() { echo "$1" >&2;exit 1; }
sha256sum -c SHA256SUMS >/dev/null || fail 'Package checksum failed'
sh ./check-device.sh
if [ "$ACTION" = check ];then
 echo 'PASS: firmware compatibility, package and factory dependencies verified'
 if [ -e /data/u60-panel ] || [ -e /data/u60-clash ] || [ -e /data/tailscale ];then echo 'Existing project files detected; --install will refuse to overwrite them';
 else echo 'Compatible target; --install additionally validates empty directories and the stock boot layout before writing';fi
 exit 0
fi
ID=$(cat RELEASE-ID)
case "$ID" in u60-pro-B28-[0-9]*|u60-pro-B31-[0-9]* ) ;; *) fail 'Invalid release id';; esac
case "$ID" in *[!a-zA-Z0-9-]*) fail 'Invalid release id';; esac
BACKUP="/data/u60-install-backups/$ID"
NCM_SOURCE="payload/data/u60-panel/usb-ncm-composition.sh"
if [ "$ACTION" = --start ];then
 [ "$(cat /data/u60-panel/portable-release 2>/dev/null)" = "$ID" ] || fail 'Install this release first'
 (cd payload && find data -type f | while IFS= read -r p;do
   # Settings may be edited after installation; verify program/immutable files only.
   case "$p" in *.sh|*/u60-panel|*/panel-control|*/panel-ubus|*/panel-wifi-crypto|*/panel-relay|*/panel-standby|*/panel-web|*/panel-web-control|*/mihomo|*/tailscale|*/tailscaled)
    [ "$(sha256sum "$PKG/payload/$p" | cut -d ' ' -f 1)" = "$(sha256sum "/$p" | cut -d ' ' -f 1)" ] || exit 1;;
   esac
  done) || fail 'Installed program differs from this release'
 /etc/init.d/u60-web start
 nohup /data/u60-panel/panel-autostart.sh </dev/null >/dev/null 2>&1 &
 echo 'UI start requested; verify the screen and physical buttons. Network accounts are not initialized.'
 exit 0
fi
for name in u60-panel u60-clash tailscale u60-web;do [ ! -e "/data/$name" ] && [ ! -L "/data/$name" ] || fail 'Existing project files found; use the update workflow';done
for name in u60-usb-isolate u60-usb-role u60-wifi-relay u60-standby u60-web u60-ncm-trial;do
 [ ! -e "/etc/init.d/$name" ] && [ ! -L "/etc/init.d/$name" ] || fail 'Existing project init service found'
 for p in /etc/rc.d/*"$name";do [ ! -e "$p" ] && [ ! -L "$p" ] || fail 'Existing project boot link found';done
done
[ -f "$NCM_SOURCE" ] || fail 'NCM composition is missing from this package'
[ "$(awk '$0 == "exit 0" {n++} END {print n+0}' /etc/rc.local)" = 1 ] || fail 'Expected one exit 0 in stock rc.local'
! grep -q 'u60-panel\|u60-clash\|/data/tailscale' /etc/rc.local || fail 'Existing custom boot entries require review'
[ ! -e "$BACKUP" ] && [ ! -L "$BACKUP" ] || fail 'Recovery directory already exists'
STAGE="/data/.u60-portable-$ID"
[ ! -e "$STAGE" ] && [ ! -L "$STAGE" ] || fail 'Staging directory already exists'
[ ! -L /data/u60-install-backups ] || fail 'Recovery root must not be a symlink'
mkdir -p /data/u60-install-backups
mkdir "$BACKUP" "$STAGE"
chmod 700 /data/u60-install-backups "$BACKUP" "$STAGE"
cp -p /etc/rc.local "$BACKUP/rc.local"
printf '%s\n' "$ID" > "$BACKUP/release-id"
: > "$BACKUP/created-dirs"
CHANGED=0
recover() {
 result=$?;trap - EXIT INT TERM HUP
 if [ "$result" -ne 0 ];then
  if [ "$CHANGED" = 1 ];then
   cp -p "$BACKUP/rc.local" /etc/rc.local
   for name in u60-usb-isolate u60-usb-role u60-wifi-relay u60-standby u60-web u60-ncm-trial;do
    if [ -x "/etc/init.d/$name" ];then "/etc/init.d/$name" disable >/dev/null 2>&1 || true;fi
    rm -f "/etc/init.d/$name" "/etc/init.d/$name.portable-next"
   done
   mkdir -p "$BACKUP/incomplete"
   while IFS= read -r name;do
    case "$name" in u60-panel|u60-clash|tailscale|u60-web) [ ! -e "/data/$name" ] || mv "/data/$name" "$BACKUP/incomplete/$name";; esac
   done < "$BACKUP/created-dirs"
  fi
  echo "Installation failed; stock boot restored. Retained recovery files: $BACKUP" >&2
 fi
 exit "$result"
}
trap recover EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
cp -R payload/data/. "$STAGE/"
cp payload/boot/portable-boot.sh "$STAGE/u60-panel/portable-boot.sh"
chmod 755 "$STAGE/u60-web"
find "$STAGE/u60-web/public" -type d -exec chmod 755 '{}' \;
find "$STAGE/u60-web/public" -type f -exec chmod 644 '{}' \;
printf '%s\n' "$ID" > "$STAGE/u60-panel/portable-release"
chmod 700 "$STAGE/u60-panel/portable-boot.sh"
awk '{if ($0 == "exit 0") print "(/data/u60-panel/portable-boot.sh) &";print}' /etc/rc.local > "$STAGE/rc.local"
sh -n "$STAGE/rc.local"
CHANGED=1
for name in u60-panel u60-clash tailscale u60-web;do
 # Journal intent before rename so a signal cannot leave an untracked directory.
 printf '%s\n' "$name" >> "$BACKUP/created-dirs"
 mv "$STAGE/$name" "/data/$name"
done
for name in u60-usb-isolate u60-usb-role u60-wifi-relay u60-standby u60-web u60-ncm-trial;do
 cp "payload/init/$name" "/etc/init.d/$name.portable-next"
 chmod 700 "/etc/init.d/$name.portable-next"
 mv "/etc/init.d/$name.portable-next" "/etc/init.d/$name"
done
[ -x /data/u60-panel/usb-ncm-composition.sh ] || fail 'Installed NCM composition is not executable'
if [ ! -e /data/u60-panel/compat-mode ];then
 /etc/init.d/u60-usb-isolate enable
 /etc/init.d/u60-usb-role enable
fi
/etc/init.d/u60-web enable
chmod 755 "$STAGE/rc.local"
mv "$STAGE/rc.local" /etc/rc.local
printf 'installed\n' > "$BACKUP/status"
rmdir "$STAGE"
echo "Installed $ID. Stock boot backup: $BACKUP"
echo 'Services were not started. Run --start after reading README.md; configure new accounts separately.'
