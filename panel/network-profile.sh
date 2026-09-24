#!/bin/sh
# Transactional Internet profile controller for the U60 panel.
# Clash interception is TCP plus DNS only; it does not proxy general UDP.
set -u

ROOT="${PROFILE_ROOT:-/data/u60-panel}"
STATE="$ROOT/network-profile"
LOCK="${PROFILE_LOCK:-/tmp/u60-network-profile.lock}"
IPT="${IPTABLES:-iptables}"
IP6T="${IP6TABLES:-ip6tables}"
IP="${IP_CMD:-ip}"
TS="${TAILSCALE_CLI:-/data/tailscale/bin/tailscale}"
TS_SOCK="${TAILSCALE_SOCK:-/tmp/tailscale/tailscaled.sock}"
JSONFILTER="${JSONFILTER:-jsonfilter}"
NET_CLASS="${NET_CLASS:-/sys/class/net}"
CLASH_ROOT="${CLASH_ROOT:-/data/u60-clash}"
CLASH_BIN="$CLASH_ROOT/mihomo"
CLASH_PID="$CLASH_ROOT/mihomo.pid"
PROC_ROOT="${PROC_ROOT:-/proc}"
REDIR="${REDIR_PORT:-17891}"
DNS="${DNS_PORT:-1053}"

say() { printf '%s\n' "$1"; }
valid_profile() { case "$1" in clash|direct|tailscale|error) return 0;; *) return 1;; esac; }
read_state() {
	p=direct
	[ -r "$STATE" ] && IFS= read -r p < "$STATE" || true
	valid_profile "$p" || p=direct
	printf '%s' "$p"
}
write_state() {
	mkdir -p "$ROOT" || return 1
	tmp="$STATE.tmp.$$"
	(umask 022; printf '%s\n' "$1" > "$tmp") || { rm -f "$tmp"; return 1; }
	mv -f "$tmp" "$STATE"
}
unlock() { rm -f "$LOCK/owner"; rmdir "$LOCK" 2>/dev/null || true; }
lock() {
	if mkdir "$LOCK" 2>/dev/null; then
		echo $$ > "$LOCK/owner"; trap unlock EXIT INT TERM HUP; return 0
	fi
	owner=$(cat "$LOCK/owner" 2>/dev/null || true)
	if [ -n "$owner" ] && kill -0 "$owner" 2>/dev/null; then return 1; fi
	rm -f "$LOCK/owner"; rmdir "$LOCK" 2>/dev/null || true
	mkdir "$LOCK" 2>/dev/null || return 1
	echo $$ > "$LOCK/owner"; trap unlock EXIT INT TERM HUP
}
del4_nat() { while "$IPT" -t nat -D PREROUTING "$@" >/dev/null 2>&1; do :; done; }
del4_fwd() { while "$IPT" -D FORWARD "$@" >/dev/null 2>&1; do :; done; }
del6_nat() {
	command -v "$IP6T" >/dev/null 2>&1 || return 0
	while "$IP6T" -t nat -D PREROUTING "$@" >/dev/null 2>&1; do :; done
}
del6_fwd() {
	command -v "$IP6T" >/dev/null 2>&1 || return 0
	while "$IP6T" -D FORWARD "$@" >/dev/null 2>&1; do :; done
}
absent4_nat() { ! "$IPT" -t nat -C PREROUTING "$@" >/dev/null 2>&1; }
absent4_fwd() { ! "$IPT" -C FORWARD "$@" >/dev/null 2>&1; }
remove_project_rules() {
 del4_nat -i br-lan -p tcp --dport 53 -j U60_CLASH_DNS
 del4_nat -i br-lan -p udp --dport 53 -j U60_CLASH_DNS
 "$IPT" -t nat -F U60_CLASH_DNS >/dev/null 2>&1 || true
 "$IPT" -t nat -X U60_CLASH_DNS >/dev/null 2>&1 || true
 del4_fwd -i br-lan ! -o tailscale0 -p udp --dport 443 -j REJECT --reject-with icmp-port-unreachable
	del4_nat -i br-lan -p tcp -j U60_CLASH
	del4_nat -i br-lan -p tcp -m tcp --dport 53 -j REDIRECT --to-ports "$DNS"
	del4_nat -i br-lan -p udp -m udp --dport 53 -j REDIRECT --to-ports "$DNS"
	del4_nat -i br-lan -p tcp --dport 53 -j REDIRECT --to-ports "$DNS"
	del4_nat -i br-lan -p udp --dport 53 -j REDIRECT --to-ports "$DNS"
	del4_fwd -i br-lan -p udp -m udp --dport 443 -j REJECT --reject-with icmp-port-unreachable
	del4_fwd -i br-lan -p udp --dport 443 -j REJECT --reject-with icmp-port-unreachable
	del6_nat -i br-lan -p udp --dport 53 -j REDIRECT --to-ports "$DNS"
	del6_fwd -i br-lan -p udp --dport 443 -j REJECT
	"$IPT" -t nat -F U60_CLASH >/dev/null 2>&1 || true
	"$IPT" -t nat -X U60_CLASH >/dev/null 2>&1 || true
 absent4_nat -i br-lan -p tcp --dport 53 -j U60_CLASH_DNS || return 1
 absent4_nat -i br-lan -p udp --dport 53 -j U60_CLASH_DNS || return 1
 absent4_fwd -i br-lan ! -o tailscale0 -p udp --dport 443 -j REJECT --reject-with icmp-port-unreachable || return 1
	absent4_nat -i br-lan -p tcp -j U60_CLASH || return 1
	absent4_nat -i br-lan -p tcp --dport 53 -j REDIRECT --to-ports "$DNS" || return 1
	absent4_nat -i br-lan -p udp --dport 53 -j REDIRECT --to-ports "$DNS" || return 1
	absent4_fwd -i br-lan -p udp --dport 443 -j REJECT --reject-with icmp-port-unreachable || return 1
}
add_return() { "$IPT" -t nat -A U60_CLASH -d "$1" -j RETURN >/dev/null; }
add_dns_return() { "$IPT" -t nat -A U60_CLASH_DNS -d "$1" -j RETURN >/dev/null; }
add_ts_route_returns() {
	[ -d "$NET_CLASS/tailscale0" ] || return 0
	"$IP" -o route show table all dev tailscale0 2>/dev/null |
	while IFS=' ' read -r destination rest; do
		case "$destination" in
			default|100.64.0.0/10) continue ;;
			*.*.*.*/*) add_return "$destination" && add_dns_return "$destination" || exit 1 ;;
		esac
	done
}
remove_guard() {
	while "$IPT" -D FORWARD -i br-lan -j U60_PANEL_GUARD >/dev/null 2>&1; do :; done
	"$IPT" -F U60_PANEL_GUARD >/dev/null 2>&1 || true
	"$IPT" -X U60_PANEL_GUARD >/dev/null 2>&1 || true
	! "$IPT" -C FORWARD -i br-lan -j U60_PANEL_GUARD >/dev/null 2>&1 || return 1
	command -v "$IP6T" >/dev/null 2>&1 || return 0
	while "$IP6T" -D FORWARD -i br-lan -j U60_PANEL_GUARD6 >/dev/null 2>&1; do :; done
	"$IP6T" -F U60_PANEL_GUARD6 >/dev/null 2>&1 || true
	"$IP6T" -X U60_PANEL_GUARD6 >/dev/null 2>&1 || true
	! "$IP6T" -C FORWARD -i br-lan -j U60_PANEL_GUARD6 >/dev/null 2>&1
}
guard_return() { "$IPT" -A U60_PANEL_GUARD -d "$1" -j RETURN >/dev/null; }
install_guard() {
	remove_guard || return 1
	"$IPT" -N U60_PANEL_GUARD >/dev/null || return 1
	for net in 192.168.0.0/16 10.0.0.0/8 172.16.0.0/12 100.64.0.0/10 127.0.0.0/8 169.254.0.0/16 224.0.0.0/4; do
		guard_return "$net" || return 1
	done
	if [ -d "$NET_CLASS/tailscale0" ]; then
		"$IP" -o route show table all dev tailscale0 2>/dev/null |
		while IFS=' ' read -r destination rest; do
			case "$destination" in
				default|100.64.0.0/10) continue ;;
				*.*.*.*/*) guard_return "$destination" || exit 1 ;;
			esac
		done || return 1
	fi
	"$IPT" -A U60_PANEL_GUARD -j REJECT --reject-with icmp-port-unreachable >/dev/null || return 1
	"$IPT" -I FORWARD 1 -i br-lan -j U60_PANEL_GUARD >/dev/null || return 1
	command -v "$IP6T" >/dev/null 2>&1 || return 1
	"$IP6T" -N U60_PANEL_GUARD6 >/dev/null || return 1
	for net6 in fc00::/7 fe80::/10 ff00::/8; do
		"$IP6T" -A U60_PANEL_GUARD6 -d "$net6" -j RETURN >/dev/null || return 1
	done
	"$IP6T" -A U60_PANEL_GUARD6 -j REJECT --reject-with icmp6-port-unreachable >/dev/null || return 1
	"$IP6T" -I FORWARD 1 -i br-lan -j U60_PANEL_GUARD6 >/dev/null || return 1
}
apply_clash() {
	[ -r "$CLASH_PID" ] || return 1
	pid=$(cat "$CLASH_PID" 2>/dev/null || true)
	case "$pid" in ''|*[!0-9]*) return 1;; esac
	pid_owned "$pid" || return 1
	kill -0 "$pid" 2>/dev/null || return 1
	remove_project_rules || return 1
	"$IPT" -t nat -N U60_CLASH >/dev/null || return 1
 "$IPT" -t nat -N U60_CLASH_DNS >/dev/null || return 1
 add_dns_return 100.64.0.0/10 || return 1
	for net in 192.168.0.0/16 10.0.0.0/8 172.16.0.0/12 100.64.0.0/10 127.0.0.0/8 169.254.0.0/16 224.0.0.0/4; do
		add_return "$net" || return 1
	done
	add_ts_route_returns || return 1
	"$IPT" -t nat -A U60_CLASH -p tcp -j REDIRECT --to-ports "$REDIR" >/dev/null || return 1
	"$IPT" -t nat -I PREROUTING 1 -i br-lan -p tcp -j U60_CLASH >/dev/null || return 1
 # REDIRECT with a port requires a protocol in this rule, not only its caller.
 "$IPT" -t nat -A U60_CLASH_DNS -p tcp -j REDIRECT --to-ports "$DNS" >/dev/null || return 1
 "$IPT" -t nat -A U60_CLASH_DNS -p udp -j REDIRECT --to-ports "$DNS" >/dev/null || return 1
 "$IPT" -t nat -I PREROUTING 1 -i br-lan -p udp --dport 53 -j U60_CLASH_DNS >/dev/null || return 1
 "$IPT" -t nat -I PREROUTING 1 -i br-lan -p tcp --dport 53 -j U60_CLASH_DNS >/dev/null || return 1
 "$IPT" -I FORWARD 1 -i br-lan ! -o tailscale0 -p udp --dport 443 -j REJECT --reject-with icmp-port-unreachable >/dev/null || return 1
}
ts_field() { "$TS" --socket="$TS_SOCK" status --json 2>/dev/null | "$JSONFILTER" -e "$1" 2>/dev/null; }
validate_tailscale() {
	[ -d "$NET_CLASS/tailscale0" ] || return 1
	[ "$(ts_field '@.BackendState')" = "Running" ] || return 1
	case "$(ts_field '@.ExitNodeStatus.Online')" in 1|true) ;; *) return 1;; esac
	[ -n "$(ts_field '@.ExitNodeStatus.ID')" ] || return 1
	"$IP" -4 route show table 52 2>/dev/null |
		grep -Eq '^(default|0[.]0[.]0[.]0/0).* dev tailscale0([[:space:]]|$)' || return 1
}
clear_exit() {
 # Starting only describes control connectivity, not the selected Internet exit.
 # Read both explicit preference fields; missing data must never mean no exit.
 if exit_prefs_empty; then return 0; fi
 backend=$(ts_field '@.BackendState')
 case "$backend" in Running|Stopped|Starting) ;; *) [ ! -d "$NET_CLASS/tailscale0" ]; return $?;; esac
 "$TS" --socket="$TS_SOCK" set --exit-node= >/dev/null 2>&1 || return 1
 exit_prefs_empty
}
exit_prefs_empty() {
 exit_fields=$(curl --noproxy '*' -sf --max-time 2 --unix-socket "$TS_SOCK" http://local-tailscaled.sock/localapi/v0/prefs 2>/dev/null |
  "$JSONFILTER" -e 'exit_id=@.ExitNodeID' -e 'exit_ip=@.ExitNodeIP' 2>/dev/null)
 case "$exit_fields" in *"export exit_id='';"*) ;; *) return 1;; esac
 case "$exit_fields" in *"export exit_ip='';"*) ;; *) return 1;; esac
}
restore_exit() {
 [ -n "${previous_exit:-}" ] || return 1
 case "$previous_exit" in *[!0-9a-fA-F:.]*) return 1;; esac
 "$TS" --socket="$TS_SOCK" set --exit-node="$previous_exit" >/dev/null 2>&1
}
apply_profile() {
	case "$1" in
	clash) clear_exit || return 1; apply_clash || return 1 ;;
	direct) clear_exit || return 1; remove_project_rules || return 1 ;;
	tailscale) validate_tailscale || return 1; remove_project_rules || return 1 ;;
	error) install_guard; return ;;
	esac
	remove_guard
}
rollback() {
 if [ "$1" = tailscale ]; then restore_exit || return 1; fi
 apply_profile "$1"
}
fail_closed() {
 if install_guard; then
  write_state error >/dev/null 2>&1 || true
  say '{"ok":false,"error":"rollback_failed","profile":"error","fail_closed":true}'
 else
  write_state error >/dev/null 2>&1 || true
  say '{"ok":false,"error":"fail_closed_failed","profile":"error","fail_closed":false}'
 fi
}
pid_owned() {
	pid="$1"
	[ -L "$PROC_ROOT/$pid/exe" ] || return 1
	[ "$(readlink "$PROC_ROOT/$pid/exe" 2>/dev/null)" = "$CLASH_BIN" ]
}
start_core() {
	[ -x "$CLASH_BIN" ] || return 1
	if [ -r "$CLASH_PID" ]; then
		old=$(cat "$CLASH_PID" 2>/dev/null || true)
		case "$old" in *[!0-9]*|'') old=;; esac
		[ -n "$old" ] && pid_owned "$old" && kill "$old" 2>/dev/null || true
	fi
	if [ -r "$CLASH_ROOT/mode" ]; then
		mode=$(tr -d ' \n\r\t' < "$CLASH_ROOT/mode")
		case "$mode" in rule|global|direct) sed -i "s/^mode: .*/mode: $mode/" "$CLASH_ROOT/config.yaml" || return 1;; esac
	fi
	: > "$CLASH_ROOT/mihomo.log" || return 1
	# BusyBox -b closes stdio. Reopen the private startup log after daemonizing.
	chmod 600 "$CLASH_ROOT/mihomo.log"
	start-stop-daemon -S -b -m -p "$CLASH_PID" -x /bin/sh -- -c 'exec "$1" -d "$2" -f "$2/config.yaml" >>"$2/mihomo.log" 2>&1' sh "$CLASH_BIN" "$CLASH_ROOT" >/dev/null || return 1
	# Boot is CPU/IO heavy; a PID after one second is not service readiness.
	n=0
	while [ "$n" -lt 15 ]; do
		pid=$(cat "$CLASH_PID" 2>/dev/null || true)
		case "$pid" in ''|*[!0-9]*) ;; *)
			if pid_owned "$pid" && kill -0 "$pid" 2>/dev/null; then
				code=$(curl --noproxy '*' -s --connect-timeout 1 --max-time 1 -o /dev/null -w '%{http_code}' http://127.0.0.1:19090/version 2>/dev/null || true)
				case "$code" in 200|401) return 0;; esac
			fi;;
		esac
		sleep 0.5
		n=$((n + 1))
	done
	stop_core >/dev/null 2>&1 || true
	return 1
}
stop_core() {
	[ -r "$CLASH_PID" ] || return 0
	pid=$(cat "$CLASH_PID" 2>/dev/null || true)
	case "$pid" in ''|*[!0-9]*) return 1;; esac
	pid_owned "$pid" || return 1
	kill "$pid" 2>/dev/null || return 1
}

action="${1:-status}"
case "$action" in
status)
	p=$(read_state)
	if [ -d "$NET_CLASS/tailscale0" ]; then tun=true; else tun=false; fi
	printf '{"ok":true,"profile":"%s","clash_scope":"ipv4_tcp_and_dns_limited","udp_proxy":false,"ipv6_proxy":false,"tailscale_tun":%s}\n' "$p" "$tun"
	exit 0
	;;
verify)
	# Read-only coverage receipt. Never writes state, never takes the profile lock.
	p=$(read_state)
	tcp=false; dns=false; reject=false; guard=false; v6rules=false; v6route=false
	"$IPT" -t nat -C PREROUTING -i br-lan -p tcp -j U60_CLASH >/dev/null 2>&1 && tcp=true
	if "$IPT" -t nat -C PREROUTING -i br-lan -p udp --dport 53 -j U60_CLASH_DNS >/dev/null 2>&1 &&
		"$IPT" -t nat -C PREROUTING -i br-lan -p tcp --dport 53 -j U60_CLASH_DNS >/dev/null 2>&1; then dns=true; fi
	"$IPT" -C FORWARD -i br-lan ! -o tailscale0 -p udp --dport 443 -j REJECT --reject-with icmp-port-unreachable >/dev/null 2>&1 && reject=true
	"$IPT" -C FORWARD -i br-lan -j U60_PANEL_GUARD >/dev/null 2>&1 && guard=true
	if command -v "$IP6T" >/dev/null 2>&1 && "$IP6T" -t nat -C PREROUTING -i br-lan -j U60_CLASH6 >/dev/null 2>&1; then v6rules=true; fi
	"$IP" -6 route show default 2>/dev/null | grep -Eq '^default' && v6route=true
	printf '{"ok":true,"profile":"%s","clash_scope":"ipv4_tcp_and_dns_limited","udp_proxy":false,"ipv6_proxy":false,"redirect":{"prerouting_tcp":%s,"dns":%s,"udp443_reject":%s,"guard":%s,"ipv6_redirect":%s},"ipv6_default_route":%s}\n' \
		"$p" "$tcp" "$dns" "$reject" "$guard" "$v6rules" "$v6route"
	exit 0
	;;
clash|direct|tailscale|clash-start|clash-enable|clash-stop|standby-resume) ;;
*) say '{"ok":false,"error":"invalid_action"}'; exit 2;;
esac
if [ -f "${PROFILE_STANDBY_ACTIVE:-/tmp/u60-standby/active}" ] && [ "$action" != standby-resume ]; then
 say '{"ok":false,"error":"standby_transition"}';exit 75
fi
lock || { say '{"ok":false,"error":"busy"}'; exit 3; }
if [ "$action" = standby-resume ];then
 action=clash-start
 current_pid=$(cat "$CLASH_PID" 2>/dev/null || true)
 if pid_owned "$current_pid" && kill -0 "$current_pid" 2>/dev/null;then action=$(read_state);fi
fi
previous=$(read_state)
previous_exit=$(ts_field '@.ExitNodeStatus.TailscaleIPs[0]')
target="$action"
[ "$action" = clash-start ] && target="$previous"
[ "$action" = clash-enable ] && target=clash
[ "$target" = error ] && target=clash
[ "$action" = clash-stop ] && target=direct

enable_reuse=0
if [ "$action" = clash-enable ] && [ -r "$CLASH_PID" ]; then
 existing=$(cat "$CLASH_PID" 2>/dev/null || true)
 case "$existing" in ''|*[!0-9]*) ;; *) if pid_owned "$existing" && kill -0 "$existing" 2>/dev/null; then enable_reuse=1; fi;; esac
fi
if { [ "$action" = clash-start ] || { [ "$action" = clash-enable ] && [ "$enable_reuse" = 0 ]; }; } && ! start_core; then
	if ! rollback "$previous"; then fail_closed; else say '{"ok":false,"error":"clash_start_failed"}'; fi; exit 4
fi
if apply_profile "$target"; then
	if ! write_state "$target"; then
		if ! rollback "$previous"; then
			install_guard >/dev/null 2>&1 || true
			write_state error >/dev/null 2>&1 || true
		fi
		say '{"ok":false,"error":"state_write_failed"}'; exit 5
	fi
	if [ "$action" = clash-stop ] && ! stop_core; then
        if rollback "$previous"; then
          write_state "$previous" >/dev/null 2>&1 || true
          printf '{"ok":false,"error":"clash_stop_failed","kept":"%s"}\n' "$previous"
        else fail_closed; fi
        exit 6
	fi
	printf '{"ok":true,"profile":"%s","clash_scope":"ipv4_tcp_and_dns_limited","udp_proxy":false,"ipv6_proxy":false}\n' "$target"
	exit 0
fi
if [ "$action" = clash-start ] || { [ "$action" = clash-enable ] && [ "$enable_reuse" = 0 ]; }; then stop_core >/dev/null 2>&1 || true; fi
if rollback "$previous"; then
	write_state "$previous" >/dev/null 2>&1 || true
	printf '{"ok":false,"error":"profile_failed","kept":"%s"}\n' "$previous"
	exit 7
fi
if install_guard; then
	write_state error >/dev/null 2>&1 || true
	say '{"ok":false,"error":"rollback_failed","profile":"error","fail_closed":true}'
	exit 8
fi
write_state error >/dev/null 2>&1 || true
say '{"ok":false,"error":"fail_closed_failed","profile":"error","fail_closed":false}'
exit 9
