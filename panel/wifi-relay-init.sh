#!/bin/sh /etc/rc.common
START=99
STOP=01
USE_PROCD=1
boot() { /data/u60-panel/wifi-relay.sh boot; }
start_service() {
 [ -f /data/u60-panel/relay-private/enabled ] || return 0
 procd_open_instance
 procd_set_param command /data/u60-panel/wifi-relay.sh watch
 procd_set_param respawn 3600 5 5
 procd_set_param term_timeout 20
 procd_set_param stdout 0
 procd_set_param stderr 0
 procd_close_instance
}
