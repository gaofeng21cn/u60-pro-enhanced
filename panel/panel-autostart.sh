#!/bin/sh
# Preserve the factory boot barrier, then hand the display to the custom UI.
ROOT="${ROOT:-/data/u60-panel}"
UI_ONLY=$(cat "$ROOT/compat-mode" 2>/dev/null)
if [ "$UI_ONLY" != b31-ui-first ] && [ -x "$ROOT/usb-role.sh" ]; then "$ROOT/usb-role.sh" prepare >/dev/null 2>&1 || true; fi
n=0
while [ "$n" -lt 120 ]; do
	if [ -e /tmp/zte_boot_done ] && pidof zte_topsw_devui >/dev/null 2>&1; then
		break
	fi
	sleep 0.25
	n=$((n + 1))
done
[ -e /tmp/zte_boot_done ] && pidof zte_topsw_devui >/dev/null 2>&1 || exit 1
sleep 1
# B31 stays quiet until the user opts into each network owner. Saved intent is
# independent of a historical rc.d enable bit and survives program upgrades.
if [ "$UI_ONLY" = b31-ui-first ]; then
 if [ -f "$ROOT/usb-managed" ];then /etc/init.d/u60-usb-role start >/dev/null 2>&1 || true;fi
 if [ -f "$ROOT/relay-private/enabled" ];then "$ROOT/wifi-relay.sh" boot >/dev/null 2>&1 || true;fi
	"$ROOT/panel-watch.sh" >/dev/null 2>&1 &
	exec "$ROOT/panel-run.sh"
fi
[ ! -x /etc/init.d/u60-standby ] || /etc/init.d/u60-standby start >/dev/null 2>&1
if [ -x "$ROOT/usb-role.sh" ] && [ -x /etc/init.d/u60-usb-role ]; then /etc/init.d/u60-usb-role start >/dev/null 2>&1 || true; fi
if [ -x "$ROOT/wifi-band.sh" ]; then "$ROOT/wifi-band.sh" watch >/dev/null 2>&1 & fi
if [ -x "$ROOT/panel-maintenance.sh" ]; then "$ROOT/panel-maintenance.sh" watch >/dev/null 2>&1 & fi
if [ -x "$ROOT/tailscale-lan.sh" ]; then "$ROOT/tailscale-lan.sh" watch >/dev/null 2>&1 & fi
[ ! -x "$ROOT/wifi-relay.sh" ] || "$ROOT/wifi-relay.sh" boot >/dev/null 2>&1
"$ROOT/panel-watch.sh" >/dev/null 2>&1 &
exec "$ROOT/panel-run.sh"
