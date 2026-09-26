#!/bin/sh /etc/rc.common
# Disabled by default; usb-ncm-trial.sh starts this procd owner only for a
# bounded maintenance transaction. It must never be enabled at boot.
START=99
STOP=01
USE_PROCD=1

start_service() {
 [ "$(cat /tmp/u60-ncm-trial/state 2>/dev/null || true)" = pending ] || return 1
 procd_open_instance
 procd_set_param command /data/u60-panel/usb-ncm-trial.sh supervise
 procd_set_param term_timeout 5
 procd_set_param stdout 1
 procd_set_param stderr 1
 procd_close_instance
}
