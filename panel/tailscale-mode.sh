#!/bin/sh
# Staged U60 Pro Tailscale mode migration. No up/set/reset and no identity copies.
# Local tests may prefix the filesystem; production workers run with clean env.
set -u
umask 077
TEST_ROOT=${U60_TS_TEST_ROOT:-}
case "$TEST_ROOT" in '') ROOT=;; /*) ROOT=$TEST_ROOT;; *) exit 2;; esac
BIN="$ROOT/data/tailscale/bin/tailscaled"
STATE="$ROOT/data/tailscale/tailscaled.state"
SOCKET="$ROOT/tmp/tailscale/tailscaled.sock"
STATEDIR="$ROOT/tmp/tailscale"
PIDFILE="$ROOT/data/tailscale/tailscaled.pid"
MODEFILE="$ROOT/data/u60-panel/tailscale-mode"
PROC="$ROOT/proc"
TUN="$ROOT/dev/net/tun"
IFACE="$ROOT/sys/class/net/tailscale0"
CURL="$ROOT/usr/bin/curl"
JSONFILTER="$ROOT/usr/bin/jsonfilter"
FLOCK="$ROOT/usr/bin/flock"
LOCK="$ROOT/tmp/u60-tailscale-mode.lock"
PAUSE=0.25
[ -z "$TEST_ROOT" ] || PAUSE=0.01
command=${1:-status}
case "$command" in status|start|tun|userspace|standby-resume) ;; *) printf '%s\n' '{"ok":false,"message":"Unsupported mode command"}'; exit 2;; esac
if [ -f "$ROOT/tmp/u60-standby/active" ] && [ "$command" != status ] && [ "$command" != standby-resume ];then
 printf '%s\n' '{"ok":false,"message":"Standby service transition in progress"}';exit 75
fi
[ "$command" != standby-resume ] || command=start
saved_mode() { SAVED=userspace; if [ -f "$MODEFILE" ]; then IFS= read -r mode < "$MODEFILE" || :; [ "${mode:-}" != tun ] || SAVED=tun; fi; }
owned() {
 case "${1:-}" in ''|*[!0-9]*|0|1) return 1;; esac
 link=$(readlink "$PROC/$1/exe" 2>/dev/null)
 if [ "$link" != "$BIN" ];then
  [ "$link" = "$BIN (deleted)" ] || return 1
  running_sha=$(sha256sum "$PROC/$1/exe" 2>/dev/null | cut -d ' ' -f 1)
  installed_sha=$(sha256sum "$BIN" 2>/dev/null | cut -d ' ' -f 1)
  [ -n "$running_sha" ] && [ "$running_sha" = "$installed_sha" ] || return 1
 fi
 [ -r "$PROC/$1/cmdline" ] || return 1
 # Binary identity plus exact state/socket scope, not a process-name match.
 tr '\000' '\n' < "$PROC/$1/cmdline" | grep -Fx -- "--state=$STATE" >/dev/null 2>&1 || return 1
 tr '\000' '\n' < "$PROC/$1/cmdline" | grep -Fx -- "--socket=$SOCKET" >/dev/null 2>&1 || return 1
}
find_owner() {
 OWNER=; MULTIPLE=0
 if [ -r "$PIDFILE" ]; then IFS= read -r p < "$PIDFILE" || :; if owned "${p:-}"; then OWNER=$p; fi; fi
 for path in "$PROC"/[0-9]*; do [ -d "$path" ] || continue; p=${path##*/}; [ "$p" != "$OWNER" ] || continue; if owned "$p"; then if [ -n "$OWNER" ]; then MULTIPLE=1; else OWNER=$p; fi; fi; done
}
process_mode() {
 ACTIVE=unknown
 if [ -n "$OWNER" ] && owned "$OWNER"; then
  if tr '\000' '\n' < "$PROC/$OWNER/cmdline" | grep -Fx -- '--tun=tailscale0' >/dev/null 2>&1; then ACTIVE=tun
  elif tr '\000' '\n' < "$PROC/$OWNER/cmdline" | grep -Fx -- '--tun=userspace-networking' >/dev/null 2>&1; then ACTIVE=userspace; fi
 fi
}
api() { "$CURL" --silent --fail --max-time 1 --unix-socket "$SOCKET" "http://local-tailscaled.sock/localapi/v0/$1" 2>/dev/null; }
backend() {
 BACKEND=$(api status | "$JSONFILTER" -e '@.BackendState' 2>/dev/null) || BACKEND=Unknown
 case "$BACKEND" in Running|Stopped|Starting|NeedsLogin|NeedsMachineAuth|NoState) ;; *) BACKEND=Unknown;; esac
}
public_prefs() {
 # Filter immediately. Persist/private keys/auth material never enter a variable,
 # file, message or log. These shell assignments are compared, NEVER eval'ed.
 # OpenWrt jsonfilter emits explicit empty assignments for null/empty arrays
 # but exits 1. Require every public field from the same parsed snapshot
 # instead of treating that exit code alone as an unreadable preferences API.
 filtered=$(api prefs | "$JSONFILTER" -e 'want=@.WantRunning' -e 'routes=@.AdvertiseRoutes' -e 'exit_id=@.ExitNodeID' -e 'exit_ip=@.ExitNodeIP' -e 'accept_routes=@.RouteAll' -e 'accept_dns=@.CorpDNS' -e 'allow_lan=@.ExitNodeAllowLANAccess' -e 'shields=@.ShieldsUp' -e 'hostname=@.Hostname' -e 'ssh=@.RunSSH' -e 'tags=@.AdvertiseTags' 2>/dev/null)
 for field in want routes exit_id exit_ip accept_routes accept_dns allow_lan shields hostname ssh tags; do
  case "$filtered" in *"export $field="*) ;; *) return 1;; esac
 done
 printf '%s' "$filtered"
}
emit() {
 # All interpolated tokens below come from closed enums; message is a constant.
 ok=$1; message=$2; rolled=${3:-false}; running=false; ready=false
 [ "$BACKEND" != Running ] || running=true
 [ ! -e "$IFACE" ] || ready=true
 saved_mode
 mode=$ACTIVE; [ "$mode" != unknown ] || mode=$SAVED
 printf '{"ok":%s,"mode":"%s","configured_mode":"%s","backend":"%s","running":%s,"tun_ready":%s,"rolled_back":%s,"message":"%s"}\n' "$ok" "$mode" "$SAVED" "$BACKEND" "$running" "$ready" "$rolled" "$message"
}
finish() { emit "$1" "$2" "${3:-false}"; [ "$1" = true ]; exit $?; }
saved_mode; find_owner; process_mode; BACKEND=Unknown
[ -z "$OWNER" ] || backend
if [ "$command" = status ]; then
 if [ "$MULTIPLE" -eq 1 ]; then finish false 'Multiple matching daemons; manual review required'; fi
 finish true 'Mode status read; interface presence alone is not forwarding validation'
fi
[ -x "$BIN" ] && [ -x "$CURL" ] && [ -x "$JSONFILTER" ] && [ -x "$FLOCK" ] || finish false 'Required binary or local API utility missing'
[ -s "$STATE" ] || finish false 'Existing Tailscale identity state missing; no new identity created'
[ -d "$ROOT/data/u60-panel" ] && [ -d "$ROOT/data/tailscale" ] || finish false 'Installation directories missing'
mkdir -p "$STATEDIR" || finish false 'Cannot create runtime directory'
exec 9>"$LOCK"
"$FLOCK" -n 9 || finish false 'Another Tailscale mode operation is active'
# Re-read after lock: status above was only informational.
find_owner; process_mode; [ "$MULTIPLE" -eq 0 ] || finish false 'Multiple matching daemons; no process stopped'
DESIRED=$command; [ "$command" != start ] || DESIRED=$SAVED
OLD_MODE=$ACTIVE
if [ "$DESIRED" = tun ]; then
 if [ -z "$TEST_ROOT" ]; then [ -c "$TUN" ] || finish false 'Kernel TUN device unavailable; userspace retained'; else [ -e "$TUN" ] || finish false 'Kernel TUN device unavailable; userspace retained'; fi
fi
OLD_OWNER=$OWNER
if [ "$DESIRED" = userspace ] && [ -z "$OWNER" ] && [ "$SAVED" = tun ]; then finish false 'Start the saved TUN daemon and disable active routing before downgrade'; fi
BEFORE=; OLD_BACKEND=Unknown; REQUIRED_WANT=
if [ -n "$OWNER" ]; then
 [ "$OLD_MODE" != unknown ] || finish false 'Existing daemon mode cannot be verified'
 backend; OLD_BACKEND=$BACKEND
 BEFORE=$(public_prefs) || finish false 'Cannot read existing preferences; no process stopped'
 case "$BEFORE" in *'export want=1;'*) REQUIRED_WANT=1;; *'export want=0;'*) REQUIRED_WANT=0;; *) finish false 'WantRunning unavailable; no process stopped';; esac
 case "$OLD_BACKEND" in Running|Stopped|NeedsLogin|NeedsMachineAuth|NoState) ;; *) finish false 'Daemon state is not stable enough for migration';; esac
 if [ "$DESIRED" = userspace ] && [ "$OLD_MODE" != userspace ]; then
  # Require explicit empty values from the SAME successfully parsed snapshot.
  # Missing fields and transient API errors must never mean "no active routes".
  case "$BEFORE" in *"export exit_id='';"*) ;; *) finish false 'Disable selected exit before userspace mode';; esac
  case "$BEFORE" in *"export exit_ip='';"*) ;; *) finish false 'Disable selected exit IP before userspace mode';; esac
  case "$BEFORE" in *'export routes=;'*) ;; *) finish false 'Disable advertised routes before userspace mode';; esac
  case "$BEFORE" in *'export accept_routes=0;'*) ;; *) finish false 'Disable accepted subnet routes before userspace mode';; esac
 fi
fi
signal_term() {
 owned "$1" || return 0
 if [ -n "$TEST_ROOT" ]; then "$ROOT/bin/kill" -TERM "$1"; else kill -TERM "$1"; fi
}
stop_owned() {
 target=$1; owned "$target" || return 0
 signal_term "$target" 2>/dev/null || return 1
 for attempt in 1 2 3 4 5 6 7 8; do owned "$target" || return 0; sleep "$PAUSE"; done
 # Never SIGKILL a daemon that may still be flushing its existing state.
 return 1
}
start_mode() {
 newmode=$1; tunarg=userspace-networking; [ "$newmode" != tun ] || tunarg=tailscale0
 export GOGC=25 GOMEMLIMIT=192MiB
 "$BIN" "--tun=$tunarg" "--state=$STATE" "--statedir=$STATEDIR" "--socket=$SOCKET" --port=41641 --no-logs-no-support </dev/null >/dev/null 2>&1 9>&- &
 NEW_PID=$!
 OWNER=$NEW_PID; ACTIVE=$newmode
 printf '%s\n' "$NEW_PID" > "$PIDFILE" || return 1
}
ready_mode() {
 readiness_seconds=20; [ -z "$TEST_ROOT" ] || readiness_seconds=6
 readiness_deadline=$(($(date +%s) + readiness_seconds))
 while [ "$(date +%s)" -lt "$readiness_deadline" ]; do
  [ "$(date +%s)" -lt "$readiness_deadline" ] || return 1
  if owned "$OWNER" && [ -S "$SOCKET" ]; then
   backend
   AFTER=$(public_prefs) || AFTER=
   prefs_ok=0
   if [ -n "$BEFORE" ]; then [ "$AFTER" != "$BEFORE" ] || prefs_ok=1
   else case "$AFTER" in *'export want=1;'*|*'export want=0;'*) prefs_ok=1;; esac; fi
   if [ "$prefs_ok" -eq 1 ]; then
    if [ "$OLD_BACKEND" = Running ]; then [ "$BACKEND" = Running ] || { sleep "$PAUSE"; continue; }; fi
    if [ "$OLD_BACKEND" = Stopped ]; then [ "$BACKEND" = Stopped ] || { sleep "$PAUSE"; continue; }; fi
    case "$BACKEND" in Running|Stopped|NeedsLogin|NeedsMachineAuth|NoState) ;; *) sleep "$PAUSE"; continue;; esac
    if [ "$ACTIVE" = tun ] && [ "$BACKEND" = Running ] && [ ! -e "$IFACE" ]; then sleep "$PAUSE"; continue; fi
    return 0
   fi
  fi
  sleep "$PAUSE"
 done
 return 1
}
save_mode() { temp="$MODEFILE.tmp.$$"; printf '%s\n' "$DESIRED" > "$temp" && mv "$temp" "$MODEFILE"; }
if [ -n "$OWNER" ] && [ "$ACTIVE" = "$DESIRED" ]; then
 if ready_mode; then save_mode || finish false 'Mode active but preference file could not be saved'; finish true 'Requested mode already active; existing preferences preserved'; fi
 finish false 'Existing daemon has not passed readiness checks; no process stopped'
fi
if [ -n "$OLD_OWNER" ]; then stop_owned "$OLD_OWNER" || finish false 'Daemon did not stop gracefully; no replacement started'; fi
started=0
start_mode "$DESIRED" && started=1
if [ "$started" -eq 1 ] && ready_mode; then
 if save_mode; then
  if [ "$BACKEND" = Running ]; then finish true 'Mode changed; Running and preferences verified; test forwarded traffic separately';
  else finish true 'Mode configured; daemon is not Running; existing login and preferences preserved'; fi
 fi
fi
# Failed migration: stop only the new exact-scope daemon, then restore old mode.
if ! stop_owned "$OWNER"; then finish false 'New daemon did not stop gracefully; rollback blocked to avoid duplicate daemons'; fi
OWNER=; ACTIVE=unknown; BACKEND=Unknown
if [ -n "$OLD_OWNER" ]; then
 if start_mode "$OLD_MODE" && ready_mode; then finish false 'Mode change failed; previous daemon mode and preferences restored' true; fi
 finish false 'Mode change failed and prior daemon did not regain readiness; existing state retained'
fi
finish false 'Daemon did not reach readiness; existing state retained; no previous daemon to restore'
