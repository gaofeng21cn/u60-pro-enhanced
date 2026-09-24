#!/bin/sh
# Switch the vendor USB gadget between RNDIS and CDC ECM.
# Both compositions retain the ADB function. The operation is reversible and
# reports failure instead of claiming success when the host did not re-enumerate.
set -u
LOG=/data/u60-panel/usb-macnet.log
LOCK=/tmp/u60-usb-macnet.lock
USB_SWITCH=/sbin/usb/compositions/usb_switch
log() { echo "$(date '+%H:%M:%S') $*" >> "$LOG"; }
json() { printf '{"ok":%s,"mode":"%s","carrier":%s,"message":"%s"}\n' "$1" "$2" "$3" "$4"; }
mode() {
	case "$(readlink /sys/kernel/config/usb_gadget/g1/configs/c.1/f1 2>/dev/null || true)" in
		*gsi.ecm) printf ecm;; *gsi.rndis) printf rndis;; *) printf unknown;;
	esac
}
serial() {
	v=$(cat /sys/kernel/config/usb_gadget/g1/strings/0x409/serialnumber 2>/dev/null || true)
	[ -n "$v" ] || v=$(cat /sys/class/android_usb/android0/iSerial 2>/dev/null || true)
	case "$v" in ''|*[!a-zA-Z0-9._-]*) return 1;; esac
	printf '%s' "$v"
}
ensure_carrier() {
	for n in ecm0 usb0 ncm0 rndis0; do
		[ -e "/sys/class/net/$n" ] || continue
		ip link set "$n" up 2>/dev/null || true
		brctl show br-lan 2>/dev/null | grep -qw "$n" || brctl addif br-lan "$n" 2>/dev/null || true
		[ "$(cat "/sys/class/net/$n/carrier" 2>/dev/null || echo 0)" = 1 ] && { printf 1; return; }
	done
}
carrier() {
	for n in ecm0 usb0 ncm0 rndis0; do
		[ "$(cat "/sys/class/net/$n/carrier" 2>/dev/null || echo 0)" = 1 ] && { printf 1; return; }
	done
	printf 0
}
composition() {
	case "$1" in
		ecm) printf 'ecm,diag,serial,modem,mass_storage,ffs,dpl,qdss';;
		rndis) printf 'rndis_gsi,diag,serial,modem,mass_storage,ffs,dpl,qdss';;
		*) return 1;;
	esac
}
switch_mode() {
	want=$1; old=$(mode); [ "$old" = "$want" ] && return 0
	s=$(serial) || { log 'serial unavailable; no change'; return 1; }
	log "switch $old -> $want"
	sh "$USB_SWITCH" 0x19d2 0x1404 "$(composition "$want")" "$s" >>"$LOG" 2>&1 || true
	sleep 3
	[ "$(mode)" = "$want" ] || return 1
	ensure_carrier
}
status() {
	m=$(mode); c=$(carrier)
	[ "$m" != unknown ] && json true "$m" "$c" "USB ${m} 模式；ADB 与网口功能由主机重新枚举"
	[ "$m" = unknown ] && json false unknown "$c" 'USB gadget 模式未知，未执行切换'
}
mkdir "$LOCK" 2>/dev/null || { json false "$(mode)" 0 '另一个 USB 切换正在执行'; exit 1; }
trap 'rmdir "$LOCK" 2>/dev/null || true' EXIT
case "${1:-enable}" in
	status) status; exit 0;;
	enable) want=ecm; label='Mac 兼容 ECM';;
	restore|rndis) want=rndis; label='原厂 RNDIS';;
	*) json false "$(mode)" 0 '用法：enable、status 或 restore'; exit 2;;
esac
old=$(mode)
if switch_mode "$want"; then
	json true "$want" "$(carrier)" "$label 已切换；请等待主机重新枚举"
	exit 0
fi
log "switch failed; attempting rollback to $old"
if [ "$old" = ecm ] || [ "$old" = rndis ]; then switch_mode "$old" || true; fi
json false "$(mode)" "$(carrier)" 'USB 切换未确认，已尝试恢复原模式'
exit 1
