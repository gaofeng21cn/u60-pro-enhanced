#!/bin/sh
# U60 USB role coordinator. The mode is a request; live routes prove activation.
set -u
umask 077
BASE=${U60_USB_TEST_ROOT:-}
ROOT="$BASE/data/u60-panel"
RUN="$BASE/tmp/u60-usb-role"
NET="$BASE/sys/class/net"
UCI=${USB_UCI:-uci}
UBUS=${USB_UBUS:-ubus}
IP=${USB_IP:-ip}
EBT=${USB_EBTABLES:-ebtables}
JSONFILTER=${USB_JSONFILTER:-jsonfilter}
SELF=${USB_SELF:-$ROOT/usb-role.sh}
DHCP_EVENT=${USB_DHCP_EVENT:-$ROOT/usb-dhcp-event.sh}
UDHCPC=${USB_UDHCPC:-udhcpc}
CELLULAR=${USB_CELLULAR_HELPER:-$ROOT/usb-cellular-route.sh}
SERVICE=${USB_SERVICE:-$BASE/etc/init.d/u60-usb-role}
mkdir -p "$RUN"
get() { "$UCI" -q get "$1" 2>/dev/null; }
requested() {
 req=$(cat "$ROOT/usb-role" 2>/dev/null || true)
 case "$req" in AUTO|LAN) ;; *) req=LAN;; esac
 printf '%s' "$req"
}
phase() { printf '%s\n' "$1" > "$RUN/phase.$$"; mv "$RUN/phase.$$" "$RUN/phase"; }
default4() { "$IP" -4 route show default 2>/dev/null; }
cell4_ready() { default4 | grep -q ' dev rmnet_data'; }
cell6_ready() { "$IP" -6 route show default 2>/dev/null | grep -q ' dev rmnet_data'; }
# LAN admission follows the actual bound USB driver. The factory WAN callback
# remains restricted to its qualified eth0 adapter.
usb_discover() {
 USB_DEVICE=; USB_INDEX=; USB_IF=; USB_DRIVER=; USB_VENDOR=; USB_PRODUCT=; USB_COUNT=0; USB_SUPPORTED=false; USB_WAN=false
 for nic in "$NET"/*; do
  [ -e "$nic/device" ] || continue
  dev=$(readlink -f "$nic/device") || continue
  parent=${dev%/*}
  [ -f "$parent/idVendor" ] && [ -f "$parent/idProduct" ] || continue
  name=${nic##*/}
  case "$name" in ''|*[!a-zA-Z0-9_.-]*) continue;; esac
  vendor=$(cat "$parent/idVendor"); product=$(cat "$parent/idProduct")
  case "$vendor$product" in *[!0-9a-fA-F]*|'') continue;; esac
  [ "${#vendor}" = 4 ] && [ "${#product}" = 4 ] || continue
  driver=$(readlink -f "$nic/device/driver" 2>/dev/null || true); driver=${driver##*/}
  case "$driver" in *[!a-zA-Z0-9_-]*) driver=unknown;; esac
  USB_COUNT=$((USB_COUNT+1)); USB_DEVICE=$dev; USB_INDEX=$(cat "$nic/ifindex" 2>/dev/null || true); USB_IF=$name; USB_DRIVER=$driver; USB_VENDOR=$vendor; USB_PRODUCT=$product
 done
 # An unknown eth0 must never silently bypass admission (including old firmware).
 if [ "$USB_COUNT" = 0 ] && [ -e "$NET/eth0" ]; then USB_COUNT=1; USB_IF=eth0; USB_DRIVER=unknown; fi
 [ "$USB_COUNT" = 1 ] || return 0
 case "$USB_DRIVER" in ax_usb_nic|ax88179_178a|asix|r8152|cdc_ether|cdc_ncm|aqc111) USB_SUPPORTED=true;; esac
 if [ "$USB_IF" = eth0 ] && [ "$USB_DRIVER" = ax_usb_nic ] && [ "$USB_VENDOR:$USB_PRODUCT" = 0b95:1790 ]; then USB_WAN=true; fi
}

usb_discover
adapter_present() { [ "$USB_COUNT" -gt 0 ]; }
usb_current() {
 [ -n "$USB_IF" ] && [ -n "$USB_INDEX" ] &&
 [ "$(readlink -f "$NET/$USB_IF/device" 2>/dev/null)" = "$USB_DEVICE" ] &&
 [ "$(cat "$NET/$USB_IF/ifindex" 2>/dev/null)" = "$USB_INDEX" ]
}
supported() { [ "$USB_COUNT" = 1 ] && [ "$USB_SUPPORTED" = true ] && usb_current; }
wan_supported() { [ "$USB_WAN" = true ]; }
link_up() { [ -n "$USB_IF" ] && [ "$(cat "$NET/$USB_IF/carrier" 2>/dev/null)" = 1 ]; }
bridge_member() { [ -n "$USB_IF" ] && [ "$(basename "$(readlink -f "$NET/$USB_IF/master" 2>/dev/null)")" = br-lan ]; }
lan_attach() {
 adapter_present || return 0
 supported || return 1
 # Do not steal an interface from another bridge or a routed network.
 master=$(readlink -f "$NET/$USB_IF/master" 2>/dev/null || true)
 case "$master" in ''|*/br-lan) ;; *) return 1;; esac
 if ! bridge_member; then
  [ -z "$("$IP" -4 -o addr show dev "$USB_IF" scope global)" ] || return 1
  [ -z "$("$IP" -6 -o addr show dev "$USB_IF" scope global)" ] || return 1
  usb_current || return 1
  "$IP" link set dev "$USB_IF" master br-lan || return 1
 fi
 usb_current || return 1
 "$IP" link set dev "$USB_IF" up || return 1
 bridge_member
}
ebt_rule() {
 chain=$1; shift
 "$EBT" -L "$chain" 2>/dev/null | grep -F -q -- "$*" || "$EBT" -A "$chain" "$@"
}
isolate_bridge() {
 guard_if=${USB_IF:-eth0}
 "$EBT" -L U60_USB_GUARD >/dev/null 2>&1 || "$EBT" -N U60_USB_GUARD || return 1
 ebt_rule U60_USB_GUARD -j DROP || return 1
 ebt_rule INPUT -i "$guard_if" -j U60_USB_GUARD || return 1
 ebt_rule OUTPUT -o "$guard_if" -j U60_USB_GUARD || return 1
 ebt_rule FORWARD -i "$guard_if" -j U60_USB_GUARD || return 1
 ebt_rule FORWARD -o "$guard_if" -j U60_USB_GUARD
}
release_bridge() {
 guard_if=${USB_IF:-eth0}
 while "$EBT" -D INPUT -i "$guard_if" -j U60_USB_GUARD >/dev/null 2>&1; do :; done
 while "$EBT" -D OUTPUT -o "$guard_if" -j U60_USB_GUARD >/dev/null 2>&1; do :; done
 while "$EBT" -D FORWARD -i "$guard_if" -j U60_USB_GUARD >/dev/null 2>&1; do :; done
 while "$EBT" -D FORWARD -o "$guard_if" -j U60_USB_GUARD >/dev/null 2>&1; do :; done
 "$EBT" -F U60_USB_GUARD >/dev/null 2>&1 || true
 "$EBT" -X U60_USB_GUARD >/dev/null 2>&1 || true
}
activate_rmnet() {
 cfg=$1
 runtime=$("$UBUS" -t 5 call "network.interface.$cfg" status 2>/dev/null) || return 0
 auto=$(printf '%s' "$runtime" | "$JSONFILTER" -e '@.autostart' 2>/dev/null)
 [ ! -e "$BASE/tmp/rmnet_script_$cfg.lock" ] || return 0
 pending=$(printf '%s' "$runtime" | "$JSONFILTER" -e '@.pending' 2>/dev/null)
 [ "$pending" != true ] || return 0
 # A factory `down` persists as autostart=false across a network reload.
 # Re-enable via netifd, rather than calling the vendor protocol shell manually.
 if [ "$auto" = false ]; then
  "$UBUS" -t 5 call "network.interface.$cfg" up >/dev/null || return 1
  write_run services_at 0; write_run settle_until "$(( $(now)+100 ))"
 fi
}
cellular_config() {
 # Stay in AUTO while restoring LTE, so a new adapter cannot become a LAN port.
 mode=$(get zwrt_router.network.opms_wan_mode)
 case "$mode" in PPP|AUTO) ;; *) return 1;; esac
 dirty=0
 [ "$(get network.zte_wan.proto)" = rmnet ] || dirty=1
 [ -z "$(get network.zte_wan.ifname)" ] || dirty=1
 [ "$(get network.zte_wan.ipv6)" = 0 ] || dirty=1
 [ "$(get network.zte_wan6.proto)" = rmnet ] || dirty=1
 [ "$(get network.zte_wan6.ipv6)" = 1 ] || dirty=1
 [ -z "$(get network.zte_wan6.ifname)" ] || dirty=1
 # B27's native pull-out handler updates modem/DNS/cutoff bookkeeping.
 # Route-only repair leaves stale upstream DNS and can provoke reboot 1155.
 # The handler itself leaves eth0 bindings behind: remove those BEFORE it runs.
 if [ "$mode" = AUTO ] && { [ "$(get zwrt_router.network.opms_wan_auto_mode)" != AUTO_LTE_GATEWAY ] || [ "$(get network.zte_wan.proto)" != rmnet ] || [ "$(get network.zte_wan6.proto)" != rmnet ] || [ "$(get zwrt_router.tmp_router.current_wan_interface)" = eth0 ]; }; then
  stamp=$(now); last=$(cat "$RUN/native_at" 2>/dev/null || echo 0)
  if [ "$last" -gt 0 ] && [ "$((stamp-last))" -lt 10 ]; then return 1; fi
  phase RESTORING
  write_run native_at "$stamp"
  "$UCI" -q delete network.zte_wan.ifname || true
  "$UCI" -q delete network.zte_wan6.ifname || true
  # B27 preserves DHCP ipv6=1 on the IPv4 section; clear it before native restore.
  "$UCI" set network.zte_wan.ipv6=0 || return 1
  "$UCI" set network.zte_wan6.ipv6=1 || return 1
  "$UCI" commit network || return 1
  # It can time out after applying changes; trust the read-back, not exit alone.
  "$UBUS" -t 15 call zwrt_router.api router_smart_wan_event '{"notify_event":"notify_rj45_pull_out"}' >/dev/null || true
  [ "$(get zwrt_router.network.opms_wan_auto_mode)" = AUTO_LTE_GATEWAY ] || return 1
  [ "$(get network.zte_wan.proto)" = rmnet ] && [ "$(get network.zte_wan6.proto)" = rmnet ] || return 1
  dirty=0
  [ "$(get network.zte_wan.ipv6)" = 0 ] && [ "$(get network.zte_wan6.ipv6)" = 1 ] && [ -z "$(get network.zte_wan.ifname)$(get network.zte_wan6.ifname)" ] || dirty=1
  write_run services_at 0; write_run settle_until "$((stamp+100))"
 fi
 if [ "$dirty" = 1 ]; then
  phase RESTORING
  "$UCI" set network.zte_wan.proto=rmnet || return 1
  "$UCI" set network.zte_wan.profile=1 || return 1
  "$UCI" set network.zte_wan.ipv6=0 || return 1
  "$UCI" -q delete network.zte_wan.ifname || true
  "$UCI" set network.zte_wan6.proto=rmnet || return 1
  "$UCI" set network.zte_wan6.profile=1 || return 1
  "$UCI" set network.zte_wan6.ipv6=1 || return 1
  "$UCI" -q delete network.zte_wan6.ifname || true
  "$UCI" commit network || return 1
  "$UBUS" -t 5 call network reload || return 1
 fi
 stock_sleeping && return 0
 if ! cell4_ready; then activate_rmnet zte_wan || true; stock_sleeping || "$CELLULAR" 4 || true; fi
 stock_sleeping && return 0
 if ! cell6_ready; then activate_rmnet zte_wan6 || true; stock_sleeping || "$CELLULAR" 6 || true; fi
 cell4_ready
}
now() { cut -d . -f 1 "$BASE/proc/uptime"; }
write_run() { printf '%s\n' "$2" > "$RUN/$1.$$"; mv "$RUN/$1.$$" "$RUN/$1"; }
wan_ready() {
 wan_supported && link_up && ! bridge_member &&
 [ "$(get zwrt_router.network.opms_wan_mode)" = AUTO ] &&
 [ "$(get network.zte_wan.proto)" = dhcp ] &&
 default4 | grep -q " dev $USB_IF" || return 1
 "$IP" -4 -o addr show dev "$USB_IF" | grep -q ' inet '
}
clear_eth_addresses() {
 adapter_present || return 0
 supported || return 1
 "$IP" -4 addr flush dev "$USB_IF" scope global || return 1
 "$IP" -6 addr flush dev "$USB_IF" scope global || return 1
}
set_mode() {
 [ "$(get zwrt_router.network.opms_wan_mode)" = "$1" ] && return 0
 phase SWITCHING
 "$UBUS" -t 5 call zwrt_router.api router_set_wan_mode "{\"opms_wan_mode\":\"$1\"}" >/dev/null || return 1
 [ "$(get zwrt_router.network.opms_wan_mode)" = "$1" ]
}
reconcile_services() (
 # Serialize with screen mutations, and read the profile only after locking.
 exec 7>"$BASE/tmp/u60-control.lock"
 flock -n 7 || exit 1
 profile=$(cat "$ROOT/network-profile" 2>/dev/null) || exit 1
 case "$profile" in clash|direct|tailscale) ;; *) exit 1;; esac
 "$ROOT/network-profile.sh" "$profile" >/dev/null || exit 1
 "$ROOT/tailscale-lan.sh" reconcile >/dev/null || exit 1
)
services_after_change() {
 route=$(default4 | awk '/^default/{print $3 ":" $5;exit}')
 [ -n "$route" ] || return 0
 previous=$(cat "$RUN/exit" 2>/dev/null || true)
 stamp=$(now)
 if [ "$route" != "$previous" ]; then
  write_run exit "$route"; write_run settle_until "$((stamp+100))"; write_run services_at 0
  if [ -x "$BASE/data/tailscale/bin/tailscale" ]; then
   "$BASE/data/tailscale/bin/tailscale" --socket=/tmp/tailscale/tailscaled.sock debug rebind >/dev/null 2>&1 || true
  fi
 fi
 last=$(cat "$RUN/services_at" 2>/dev/null || echo 0)
 until=$(cat "$RUN/settle_until" 2>/dev/null || echo 0)
 if [ "$last" = 0 ] || { [ "$stamp" -le "$until" ] && [ "$((stamp-last))" -ge 20 ]; }; then
  reconcile_services && write_run services_at "$stamp"
 fi
}
# Pure-shell fast path for an empty adapter port. Never defers attachment,
# role changes, route loss or the post-switch settling window.
idle_ready() {
 usb_discover
 [ "$USB_COUNT" = 0 ] && [ ! -e "$RUN/suspended" ] || return 1
 idle_req=''; IFS= read -r idle_req < "$ROOT/usb-role" || [ -n "$idle_req" ] || return 1
 [ "$idle_req" = "${1:-}" ] || return 1
 idle_phase=''; IFS= read -r idle_phase < "$RUN/phase" || [ -n "$idle_phase" ] || return 1
 case "$idle_req:$idle_phase" in LAN:LAN|AUTO:CELLULAR) ;; *) return 1;; esac
 idle_until=0; read -r idle_until < "$RUN/settle_until" 2>/dev/null || true
 idle_uptime=""; read -r idle_uptime idle_rest < "$BASE/proc/uptime" || [ -n "$idle_uptime" ] || return 1
 idle_uptime=${idle_uptime%%.*}
 case "$idle_until:$idle_uptime" in *[!0-9:]*|:*) return 1;; esac
 [ "$idle_uptime" -gt "$idle_until" ] || return 1
 idle_v4=0
 while read -r idle_if idle_dest idle_gw idle_flags idle_rest; do
  case "$idle_if:$idle_dest" in rmnet_data*:00000000) ;; *) continue;; esac
  case "$idle_flags" in ''|*[!0-9a-fA-F]*) continue;; esac
  [ "$((0x$idle_flags & 513))" = 1 ] && idle_v4=1 && break
 done < "$BASE/proc/net/route"
 [ "$idle_v4" = 1 ] || return 1
 while read -r idle_dest idle_prefix idle_src idle_srcprefix idle_gw idle_metric idle_ref idle_use idle_flags idle_if; do
  [ "$idle_dest:$idle_prefix" = 00000000000000000000000000000000:00 ] || continue
  case "$idle_if" in rmnet_data*) ;; *) continue;; esac
  case "$idle_flags" in ''|*[!0-9a-fA-F]*) continue;; esac
  [ "$((0x$idle_flags & 513))" = 1 ] && return 0
 done < "$BASE/proc/net/ipv6_route"
 return 1
}

stock_sleeping() { [ -x "$ROOT/panel-standby" ] && "$ROOT/panel-standby" blocked; }
reconcile() {
 [ ! -e "$RUN/suspended" ] || return 0
 [ ! -e "$BASE/tmp/u60-standby/asleep" ] || return 0
 # Stock sleep intentionally withdraws PDP/default routes. Never redial it as
 # a missing-route fault. The observer is native and has no network side effects.
 stock_sleeping && return 0
 req=$(requested)
 usb_discover
 if [ "$req" = AUTO ] && adapter_present && ! wan_supported; then phase UNSUPPORTED_WAN; return 1; fi
 if adapter_present && ! supported; then phase ERROR; return 1; fi
 if [ "$req" = LAN ]; then
  # A live upstream may never be bridged by a delayed/stale request.
  if [ "$(get zwrt_router.network.opms_wan_mode)" != PPP ] && link_up; then phase UNPLUG; return 1; fi
  if [ "$(get zwrt_router.network.opms_wan_mode)" != PPP ]; then clear_eth_addresses || return 1; fi
  set_mode PPP || { phase ERROR; return 1; }
  cellular_config || { phase RESTORING; return 1; }
  lan_attach || { phase ERROR; return 1; }
  release_bridge
  phase LAN
 else
  isolate_bridge || { phase ERROR; return 1; }
  set_mode AUTO || { phase ERROR; return 1; }
  # Reassert bridge separation if the vendor hotplug path adds eth0 back.
  if bridge_member; then "$IP" link set dev "$USB_IF" nomaster || return 1; fi
  if ! adapter_present || ! link_up; then
   clear_eth_addresses || return 1
   rm -f "$RUN/attachment" "$RUN/probe_at" "$RUN/accepted"
   cellular_config || { phase RESTORING; return 1; }
   phase CELLULAR
  elif wan_ready; then
   phase WAN
  else
   # Retain the modem until a validated upstream lease is actually adopted.
   if [ "$(get network.zte_wan.proto)" != dhcp ]; then cellular_config || true; fi
   ident=$(cat "$NET/$USB_IF/ifindex")
   attachment=$(cat "$RUN/attachment" 2>/dev/null || true)
   if [ "$attachment" != "$ident" ]; then
    write_run attachment "$ident"; write_run probe_at "$(now)"; phase DETECTING; return 0
   fi
   stamp=$(now); last=$(cat "$RUN/probe_at" 2>/dev/null || echo 0)
   [ "$((stamp-last))" -ge 3 ] || return 0
   # Give native netifd DHCP setup a bounded window after callback adoption.
   accepted=$(cat "$RUN/accepted" 2>/dev/null || echo 0)
   if [ "$accepted" -gt 0 ] && [ "$((stamp-accepted))" -lt 20 ]; then phase DETECTING; return 0; fi
   if [ "$accepted" -gt 0 ]; then cellular_config || true; rm -f "$RUN/accepted"; fi
   phase DETECTING
   "$IP" link set dev "$USB_IF" up || return 1
   export U60_USB_ATTACHMENT="$ident"
   "$UDHCPC" -f -q -n -i "$USB_IF" -t 3 -T 2 -s "$DHCP_EVENT" -p "$RUN/dhcp.pid" >/dev/null 2>&1 || true
   write_run probe_at "$(now)"
   if [ ! -e "$RUN/accepted" ]; then
    failure=$(cat "$RUN/phase")
    # Timeout is never evidence that a cable is a LAN client.
    if cellular_config; then
     if [ "$failure" = CONFLICT ]; then phase CONFLICT; else phase NO_UPSTREAM; fi
    else phase RESTORING; fi
   fi
  fi
 fi
 services_after_change
}
set_request() {
 if [ "$1" = AUTO ] && [ -f "$ROOT/relay-private/enabled" ];then echo '{"ok":false,"message":"请先断开Wi-Fi中继，再切换USB AUTO"}';return 2;fi
 case "${1:-}" in AUTO|LAN) ;; *) echo '{"ok":false,"message":"请选择 AUTO 或 LAN"}'; return 2;; esac
 if [ "$1" = AUTO ] && adapter_present && ! wan_supported; then echo '{"ok":false,"message":"此网卡仅开放 LAN；AUTO 尚未完成适配"}'; return 2; fi
 if adapter_present && ! supported; then echo '{"ok":false,"message":"当前网卡尚未适配"}'; return 2; fi
 if [ "$1" = LAN ] && [ "$(get zwrt_router.network.opms_wan_mode)" != PPP ] && link_up; then
  echo '{"ok":false,"message":"请先拔网线，再切换 LAN 接电脑"}'; return 2
 fi
 # Persist intent only when its procd owner can actually run. The controller
 # keeps this request serialized with other user settings.
 previous=$(requested);managed=0;[ ! -f "$ROOT/usb-managed" ] || managed=1
 running=0;"$SERVICE" running >/dev/null 2>&1 && running=1
 if [ "$1" = AUTO ]; then isolate_bridge || { echo '{"ok":false,"message":"网口隔离未就绪"}'; return 1; }; fi
 printf '%s\n' "$1" > "$ROOT/usb-role.$$" && mv "$ROOT/usb-role.$$" "$ROOT/usb-role" || return 1
 touch "$ROOT/usb-managed";rm -f "$RUN/suspended";phase SWITCHING
 if [ "$running" = 0 ];then "$SERVICE" start >/dev/null 2>&1 || :;fi
 ready=0;n=0
 while [ "$n" -lt 10 ];do
  if "$SERVICE" running >/dev/null 2>&1;then ready=1;break;fi
  n=$((n+1));sleep .2
 done
 if [ "$ready" = 0 ];then
  # Stop a late starting owner before undoing its requested state.
  [ "$running" = 1 ] || "$SERVICE" stop >/dev/null 2>&1 || :
  printf '%s\n' "$previous" > "$ROOT/usb-role.$$" && mv "$ROOT/usb-role.$$" "$ROOT/usb-role"
  [ "$managed" = 1 ] || rm -f "$ROOT/usb-managed"
  if [ "$previous" = LAN ] && [ "$(get zwrt_router.network.opms_wan_mode)" = PPP ];then release_bridge;fi
  phase SERVICE_DOWN
  echo '{"ok":false,"message":"网口协调服务未启动，已恢复原选择；请重试"}';return 1
 fi
 echo '{"ok":true,"message":"协调服务已运行，选择已保存；实际接线与角色请看网口状态"}'

}
status() {
 req=$(requested); current=$(cat "$RUN/phase" 2>/dev/null || true)
 adapter=false; link=false; ipv4=''; gateway=''; badge=''; state=WAIT_ADAPTER
 message='未接网卡'
 if adapter_present; then
  adapter=true
  if link_up; then link=true; fi
  if [ "$USB_COUNT" -gt 1 ]; then state=UNSUPPORTED; badge=ERROR; message='请只接一张 USB 网卡';
  elif ! supported; then state=UNSUPPORTED; badge=ERROR; message='网卡驱动尚未适配';
  elif [ "$req" = AUTO ] && ! wan_supported; then state=UNSUPPORTED_WAN; badge=ERROR; message='此网卡仅开放 LAN；请拔网线后切换 LAN';
  elif [ "$link" = false ]; then state=WAIT_CABLE; badge=WAIT; message='网卡已接入，等待网线';
  elif [ "$req" = LAN ] && bridge_member && [ "$(get zwrt_router.network.opms_wan_mode)" = PPP ]; then
   state=LAN; badge=LAN; message='LAN：向电脑或其他设备供网'
  elif [ "$req" = AUTO ] && wan_ready; then
   state=WAN; badge=WAN; message='WAN：有线上网'
   ipv4=$("$IP" -4 -o addr show dev "$USB_IF" | awk '$3=="inet" && $4 !~ /^169[.]254[.]/ {split($4,a,"/");print a[1];exit}')
   gateway=$(default4 | awk -v dev="$USB_IF" '$0 ~ " dev " dev {print $3;exit}')
  else state=DETECTING; badge=WAIT; message='正在识别上级网络'; fi
 fi
 service=false;"$SERVICE" running >/dev/null 2>&1 && service=true
 if [ -f "$ROOT/usb-managed" ] && [ "$service" = false ];then current=SERVICE_DOWN;fi
 case "$current" in UNSUPPORTED_WAN) state=UNSUPPORTED_WAN;badge=ERROR;message="此网卡仅开放 LAN；AUTO 尚未适配";; SERVICE_DOWN) state=SERVICE_DOWN;badge=ERROR;message="协调服务未运行，已保存选择尚未应用";; RESTORING) state=RESTORING;badge=WAIT;message='正在恢复蜂窝网络';; SWITCHING) state=SWITCHING;badge=WAIT;message='正在切换网口角色';; UNPLUG) state=UNPLUG;badge=ERROR;message='请先拔网线，再切换 LAN';; NO_UPSTREAM) state=NO_UPSTREAM;badge=WAIT;message='未获取上级地址，继续使用蜂窝';; CONFLICT) state=CONFLICT;badge=ERROR;message='上级与 U60 内网地址冲突';; ERROR) state=ERROR;badge=ERROR;message='切换未完成，请查看网线或改用 LAN';; esac
 if [ "$adapter" = false ] && [ "$req" = AUTO ] && [ "$state" != SERVICE_DOWN ]; then message='未接网卡 · 使用蜂窝'; fi
 case "$ipv4$gateway" in *[!0-9.]*) ipv4='';gateway='';state=ERROR;badge=ERROR;; esac
 printf '{"ok":true,"requested":"%s","service_running":%s,"state":"%s","adapter":%s,"link":%s,"interface":"%s","driver":"%s","vendor":"%s","product":"%s","lan_supported":%s,"wan_supported":%s,"ipv4":"%s","gateway":"%s","badge":"%s","message":"%s"}\n' "$req" "$service" "$state" "$adapter" "$link" "$USB_IF" "$USB_DRIVER" "$USB_VENDOR" "$USB_PRODUCT" "$USB_SUPPORTED" "$USB_WAN" "$ipv4" "$gateway" "$badge" "$message"
}
case "${1:-status}" in
 status) status;;
 set) set_request "${2:-}";;
 prepare) [ "$(requested)" != AUTO ] || isolate_bridge;;
 reconcile)
  exec 8>"$RUN/lock"; flock -n 8 || exit 3
  reconcile;;
 boot-watch)
  while [ ! -e "$BASE/tmp/zte_boot_done" ]; do sleep 2; done
  exec "$SELF" watch;;
 watch)
  exec 9>"$RUN/watch.lock"; flock -n 9 || exit 0
  echo $$ > "$RUN/watch.pid"
 idle_count=0; idle_last_req=""
 while :; do
  if [ -f "$BASE/tmp/u60-standby/asleep" ];then sleep 2;continue;fi
  if [ "$idle_count" -lt 14 ] && idle_ready "$idle_last_req"; then
   idle_count=$((idle_count+1))
  else
   # Children must not retain the singleton lock across a watcher restart.
   (exec 9>&-; "$SELF" reconcile) >/dev/null 2>&1
   idle_count=0; idle_last_req=""
   IFS= read -r idle_last_req < "$ROOT/usb-role" || true
  fi
  sleep 2
 done;;
 idle-ready) idle_ready "${2:-}";;
 to-cellular)
  exec 9>"$RUN/lock"; flock -n 9 || exit 3
  cellular_config
  ;;
 isolate) isolate_bridge;;
 release) release_bridge;;
 *) printf '%s\n' '{"ok":false,"message":"此操作尚未开放"}'; exit 2;;
esac
