#!/bin/sh
# This machine starts its own configured services; no identity or node is cloned.
umask 077
(
 sleep 20
 web_log=/data/u60-panel/web-startup.log
 attempt=1
 while [ "$attempt" -le 3 ];do
  if pidof panel-web >/dev/null 2>&1;then break;fi
  if /etc/init.d/u60-web start >/dev/null 2>&1 && pidof panel-web >/dev/null 2>&1;then
   printf '%s web-start-recovered attempt=%s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$attempt" >> "$web_log"
   break
  fi
  printf '%s web-start-failed attempt=%s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$attempt" >> "$web_log"
  [ "$attempt" -eq 3 ] || sleep 5
  attempt=$((attempt + 1))
 done
 [ ! -s /data/tailscale/tailscaled.state ] || sh /data/tailscale/tailscale-start.sh
 [ ! -s /data/u60-clash/config.yaml ] || sh /data/u60-clash/start.sh
) </dev/null >/dev/null 2>&1 &
exec /data/u60-panel/panel-autostart.sh
