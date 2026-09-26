#!/bin/sh
# Upgrade an existing installation in place. Programs and assets are replaced;
# user configuration, credentials and identities are never touched.
#
# The firmware, package and device identity gates are the same ones the first
# install uses, so a package prepared for another device or firmware cannot be
# applied here. Every replaced file is backed up with hashes first, and a failed
# verification restores the previous bytes.
set -eu
umask 077
cd "$(dirname "$0")"
PKG=$(pwd)
ACTION=${1:-upgrade}
case "$ACTION" in upgrade|--check|--rollback-list) ;; *) echo 'Use upgrade, --check or --rollback-list' >&2;exit 2;; esac
fail() { echo "$1" >&2;exit 1; }

sha256sum -c SHA256SUMS >/dev/null || fail 'Package checksum failed'
sh ./check-device.sh
ID=$(cat RELEASE-ID)
case "$ID" in u60-pro-B28-[0-9]*|u60-pro-B31-[0-9]*) ;; *) fail 'Invalid release id';; esac
case "$ID" in *[!a-zA-Z0-9-]*) fail 'Invalid release id';; esac

INSTALLED=$(cat /data/u60-panel/portable-release 2>/dev/null || true)
case "$INSTALLED" in u60-pro-B28-[0-9]*|u60-pro-B31-[0-9]*) ;; *) fail 'No existing installation to upgrade; use the first-install path';; esac
[ "$INSTALLED" != "$ID" ] || fail 'This exact release is already installed'
VARIANT=${ID#u60-pro-};VARIANT=${VARIANT%%-*}
INSTALLED_VARIANT=${INSTALLED#u60-pro-};INSTALLED_VARIANT=${INSTALLED_VARIANT%%-*}
[ "$VARIANT" = "$INSTALLED_VARIANT" ] || fail 'Installed and candidate releases target different firmware variants'
# A later release may already be installed through the in-place upgrade path.
# In that case the prior upgrade backup (whose to-release is the current
# installed release) is the authoritative rollback point; requiring the
# original first-install directory to have the same name would reject a valid
# upgrade chain.
RECOVERY_FOUND=0
if [ -d "/data/u60-install-backups/$INSTALLED" ]; then
 RECOVERY_FOUND=1
else
 for candidate in /data/u60-upgrade-backups/*; do
  [ -d "$candidate" ] || continue
  [ "$(cat "$candidate/to-release" 2>/dev/null || true)" = "$INSTALLED" ] || continue
  [ -s "$candidate/BACKUP-SHA256SUMS" ] || continue
  RECOVERY_FOUND=1
  break
 done
fi
[ "$RECOVERY_FOUND" = 1 ] || fail 'Missing a verified recovery backup; use the documented recovery path instead'

# Paths owned by the user or by the running device. These are never replaced:
# credentials, the chosen profile, node preferences, sleep/role choices and the
# screen boot mode all survive an upgrade.
is_state() {
 case "$1" in
  u60-panel/network-profile|u60-panel/tailscale-lan|u60-panel/tailscale-mode) return 0;;
  u60-panel/standby-mode|u60-panel/usb-role|u60-panel/compat-mode) return 0;;
  u60-panel/portable-release|u60-clash/panel-prefs.json|u60-clash/mode) return 0;;
  u60-clash/config.yaml|u60-clash/config.yaml.*|u60-clash/panel-prefs.json.lock) return 0;;
  u60-clash/mihomo.log|u60-clash/web-backups/*|u60-clash/proxy_provider/*) return 0;;
  u60-panel/*.log|u60-panel/*.hb) return 0;;
 esac
 return 1
}

BACKUP="/data/u60-upgrade-backups/$ID"
[ ! -e "$BACKUP" ] || fail 'Upgrade backup already exists; inspect it before retrying'
STAGE="/data/u60-packages/upgrade-$ID"
[ ! -e "$STAGE" ] || fail 'Staging directory already exists'
NCM_SOURCE="payload/data/u60-panel/usb-ncm-composition.sh"
[ -f "$NCM_SOURCE" ] || fail 'NCM composition is missing from this package'

# Plan entries are relative to /data so one list drives backup, install and
# verification. The boot hook is listed separately as /data/u60-panel/portable-boot.sh.
PLAN=$(cd payload/data && find . -type f | sed 's|^\./||' | sort | while IFS= read -r rel; do
 is_state "$rel" && continue
 if [ ! -e "/data/$rel" ]; then
  [ "$rel" = u60-panel/usb-ncm-composition.sh ] || [ "$rel" = u60-panel/usb-ncm-trial.sh ] || fail "Unexpected new file in package: $rel"
 fi
 printf '%s\n' "$rel"
done)
INIT_PLAN=$(cd payload/init && find . -type f | sed 's|^\./||' | sort | while IFS= read -r rel; do
 if [ ! -e "/etc/init.d/$rel" ]; then
  [ "$rel" = u60-ncm-trial ] || fail "Unexpected new init service in package: $rel"
 fi
 printf '%s\n' "$rel"
done)
BOOT_SRC=payload/boot/portable-boot.sh
[ -f "$BOOT_SRC" ] || fail 'Package boot hook missing'
[ -e /data/u60-panel/portable-boot.sh ] || fail 'Installed boot hook missing; use the documented recovery path'
[ -n "$PLAN" ] || fail 'Nothing to upgrade'

if [ "$ACTION" = --rollback-list ]; then
 printf '%s\n' "$PLAN" | sed 's|^|/data/|'
 printf '%s\n' "$INIT_PLAN" | sed 's|^|/etc/init.d/|'
 printf '%s\n' '/data/u60-panel/portable-boot.sh'
 exit 0
fi

# Space: the backup copy plus the staged copy roughly doubles the payload.
NEED=$(du -sk payload | awk '{print $1}')
# BusyBox may wrap the device name, so take the available column by position
# from the end rather than assuming one line and a fixed field.
AVAIL=$(df -k /data | awk 'END {print $(NF-2)}')
case "$AVAIL" in ''|*[!0-9]*) fail 'Cannot read available data space';; esac
[ "$AVAIL" -gt $((NEED * 2 + 8192)) ] || fail 'Not enough free space in /data for the upgrade backup'

# No core restart is hidden inside a UI/program upgrade. A different live core
# needs its own controlled stop/start; refuse it before creating a backup.
for core in u60-clash/mihomo tailscale/bin/tailscaled;do
 if [ "$(sha256sum "/data/$core" | cut -d ' ' -f 1)" != "$(sha256sum "payload/data/$core" | cut -d ' ' -f 1)" ];then
  for exe in /proc/[0-9]*/exe;do
   link=$(readlink "$exe" 2>/dev/null || true)
   case "$link" in "/data/$core"|"/data/$core (deleted)") fail 'A different network core is running; stop it through its UI before upgrading';; esac
  done
 fi
done

if [ "$ACTION" = --check ]; then
 echo 'PASS: firmware, package, existing installation and device identity verified'
 echo "Would replace $(printf '%s\n%s\n' "$PLAN" "$INIT_PLAN" | grep -c .) files plus the boot hook; user configuration is preserved"
 exit 0
fi

PROFILE_BEFORE=$(cat /data/u60-panel/network-profile 2>/dev/null || echo direct)
CONFIG_SHA=$(sha256sum /data/u60-clash/config.yaml 2>/dev/null | cut -d ' ' -f 1 || echo none)
LOG="/data/u60-panel/panel.log"
LOG_BEFORE=$(wc -l < "$LOG" 2>/dev/null || echo 0)

mkdir -p /data/u60-upgrade-backups "$BACKUP" "$STAGE"
chmod 700 /data/u60-upgrade-backups "$BACKUP" "$STAGE"

restore_tree() {
 while IFS= read -r rel; do
  [ -n "$rel" ] || continue
  if [ -f "$BACKUP/data/$rel" ]; then
   if [ ! -f "/data/$rel" ] || [ "$(sha256sum "$BACKUP/data/$rel" | cut -d ' ' -f 1)" != "$(sha256sum "/data/$rel" | cut -d ' ' -f 1)" ];then
    cp -p "$BACKUP/data/$rel" "/data/$rel.restore" && mv "/data/$rel.restore" "/data/$rel" || return 1
   fi
  elif [ -f "$BACKUP/data-missing/$rel" ]; then
   rm -f "/data/$rel" || return 1
  else
   return 1
  fi
 done <<EOF
$PLAN
EOF
 while IFS= read -r rel; do
  [ -n "$rel" ] || continue
  if [ -f "$BACKUP/init/$rel" ]; then
   cp -p "$BACKUP/init/$rel" "/etc/init.d/$rel" || return 1
  elif [ -f "$BACKUP/init-missing/$rel" ]; then
   rm -f "/etc/init.d/$rel" || return 1
  else
   return 1
  fi
 done <<EOF
$INIT_PLAN
EOF
 cp -p "$BACKUP/portable-boot.sh" /data/u60-panel/portable-boot.sh || return 1
 return 0
}

CHANGED=0
recover() {
 result=$?
 trap - EXIT INT TERM HUP
 if [ "$result" -ne 0 ] && [ "$CHANGED" = 1 ]; then
  echo 'Upgrade failed; restoring the previous programs' >&2
  if (cd "$BACKUP" && sha256sum -c BACKUP-SHA256SUMS >/dev/null 2>&1) && restore_tree; then
   printf '%s\n' "$INSTALLED" > /data/u60-panel/portable-release
   /etc/init.d/u60-web restart >/dev/null 2>&1 || true
   echo "Previous programs restored from $BACKUP" >&2
  else
   echo "Automatic rollback did not complete; retained files in $BACKUP for manual review" >&2
  fi
 fi
 rm -rf "$STAGE"
 exit "$result"
}
trap recover EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

cp -R payload/data/. "$STAGE/"
mkdir -p "$STAGE/init"
cp -R payload/init/. "$STAGE/init/"

# Backup with hashes so a rollback can prove it restored exactly the prior bytes.
mkdir -p "$BACKUP/data" "$BACKUP/init"
: > "$BACKUP/BACKUP-SHA256SUMS"
backup_one() {
 mkdir -p "$(dirname "$BACKUP/$1")"
 cp -p "$2" "$BACKUP/$1"
 printf '%s  %s\n' "$(sha256sum "$BACKUP/$1" | cut -d ' ' -f 1)" "$1" >> "$BACKUP/BACKUP-SHA256SUMS"
}
backup_missing() {
 mkdir -p "$(dirname "$BACKUP/$1")"
 : > "$BACKUP/$1"
 printf '%s  %s\n' "$(sha256sum "$BACKUP/$1" | cut -d ' ' -f 1)" "$1" >> "$BACKUP/BACKUP-SHA256SUMS"
}
while IFS= read -r rel; do
 [ -n "$rel" ] || continue
 if [ -f "/data/$rel" ]; then
  backup_one "data/$rel" "/data/$rel"
 else
  backup_missing "data-missing/$rel"
 fi
done <<EOF
$PLAN
EOF
while IFS= read -r rel; do
 [ -n "$rel" ] || continue
 if [ -f "/etc/init.d/$rel" ]; then
  backup_one "init/$rel" "/etc/init.d/$rel"
 else
  backup_missing "init-missing/$rel"
 fi
done <<EOF
$INIT_PLAN
EOF
backup_one portable-boot.sh /data/u60-panel/portable-boot.sh
(cd "$BACKUP" && sha256sum -c BACKUP-SHA256SUMS >/dev/null) || fail 'Upgrade backup verification failed'
printf '%s\n' "$INSTALLED" > "$BACKUP/from-release"
printf '%s\n' "$ID" > "$BACKUP/to-release"
CHANGED=1

install_one() {
 dst=$1; src=$2; mode=$3
 # Preserve the inode of unchanged live executables, sockets' owners and assets.
 if [ -f "$dst" ] && [ "$(sha256sum "$dst" | cut -d ' ' -f 1)" = "$(sha256sum "$src" | cut -d ' ' -f 1)" ];then
  chmod "$mode" "$dst";return 0
 fi
 cp "$src" "$dst.next"
 chmod "$mode" "$dst.next"
 mv "$dst.next" "$dst"
}
while IFS= read -r rel; do
 [ -n "$rel" ] || continue
 case "$rel" in
  u60-panel/*) install_one "/data/$rel" "$STAGE/$rel" 700;;
  u60-clash/mihomo|u60-clash/start.sh|u60-clash/tailscale-start.sh) install_one "/data/$rel" "$STAGE/$rel" 700;;
  tailscale/bin/*) install_one "/data/$rel" "$STAGE/$rel" 700;;
  u60-web/mount.sh) install_one "/data/$rel" "$STAGE/$rel" 700;;
  u60-web/public/*) install_one "/data/$rel" "$STAGE/$rel" 644;;
  *) install_one "/data/$rel" "$STAGE/$rel" 600;;
 esac
done <<EOF
$PLAN
EOF
while IFS= read -r rel; do
 [ -n "$rel" ] || continue
 install_one "/etc/init.d/$rel" "$STAGE/init/$rel" 700
done <<EOF
$INIT_PLAN
EOF
install_one /data/u60-panel/portable-boot.sh "$BOOT_SRC" 700
printf '%s\n' "$ID" > /data/u60-panel/portable-release

# Verify the new bytes before restarting anything.
verify_one() { [ "$(sha256sum "$1" | cut -d ' ' -f 1)" = "$(sha256sum "$2" | cut -d ' ' -f 1)" ] || fail "Installed bytes differ: $1"; }
while IFS= read -r rel; do
 [ -n "$rel" ] || continue
 verify_one "/data/$rel" "$STAGE/$rel"
done <<EOF
$PLAN
EOF
while IFS= read -r rel; do
 [ -n "$rel" ] || continue
 verify_one "/etc/init.d/$rel" "$STAGE/init/$rel"
done <<EOF
$INIT_PLAN
EOF
verify_one /data/u60-panel/portable-boot.sh "$BOOT_SRC"

# The web page and the screen are separate surfaces and are verified separately.
/etc/init.d/u60-web restart >/dev/null 2>&1 || fail 'Web service restart failed'
n=0;while [ "$n" -lt 20 ]; do pidof panel-web >/dev/null 2>&1 && break;sleep 0.5;n=$((n + 1));done
pidof panel-web >/dev/null 2>&1 || fail 'Web service did not come back after the upgrade'

# Reload the screen through the same power-key path a user would use, then wait
# for evidence of a fresh start instead of assuming it came up.
toggle_screen() {
 watcher=$(ps -ef | grep 'u60-panel watch' | grep -v grep | awk '{print $1}' | head -1)
 [ -n "$watcher" ] || return 1
 kill -USR1 "$watcher"
}
toggle_screen || fail 'Screen watcher is not running; programs were updated but the screen was left as it is'
# Wait for the display handoff to finish instead of assuming a fixed delay, so a
# slow device is not raced and a fast one is not delayed.
panel_released() { [ -z "$(cat /tmp/u60-panel.lock 2>/dev/null || true)" ]; }
n=0
while [ "$n" -lt 60 ]; do
 panel_released && break
 sleep 0.5;n=$((n + 1))
done
panel_released || fail 'The screen did not return to the factory UI; the display was left as it is'
toggle_screen || fail 'Screen watcher stopped responding during the upgrade'
n=0
while [ "$n" -lt 45 ]; do
 [ "$(wc -l < "$LOG" 2>/dev/null || echo 0)" -gt "$LOG_BEFORE" ] && tail -25 "$LOG" | grep -q 'first-snapshot' && break
 sleep 1;n=$((n + 1))
done
tail -25 "$LOG" | grep -q 'first-snapshot' || fail 'Screen did not report a new start after the upgrade'

# Owner readback: the chosen profile, the proxy configuration and the control
# program must all still be intact.
[ "$(cat /data/u60-panel/network-profile 2>/dev/null || echo direct)" = "$PROFILE_BEFORE" ] || fail 'Network profile changed during the upgrade'
[ "$(sha256sum /data/u60-clash/config.yaml 2>/dev/null | cut -d ' ' -f 1 || echo none)" = "$CONFIG_SHA" ] || fail 'Proxy configuration changed during the upgrade'
printf '%s\n' '{"action":"state","args":{}}' | /data/u60-panel/panel-control >/dev/null 2>&1 || fail 'Control program did not answer after the upgrade'

printf 'upgraded\n' > "$BACKUP/status"
rm -rf "$STAGE"
trap - EXIT INT TERM HUP
echo "Upgraded $INSTALLED -> $ID"
echo "Program backup (kept for rollback): $BACKUP"
echo 'User configuration, subscriptions, node preferences and identities were preserved.'
