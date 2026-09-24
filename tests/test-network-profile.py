#!/usr/bin/env python3
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).parents[1] / "panel" / "network-profile.sh"


class Harness:
    def __init__(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        self.state = self.root / "state"
        self.state.mkdir()
        self.net = self.root / "net"
        self.net.mkdir()
        self.log = self.root / "iptables.log"
        self.fail = self.root / "fail"
        clash = self.root / "clash"
        clash.mkdir()
        (clash / "mihomo").write_text("")
        (clash / "mihomo").chmod(0o755)
        (clash / "mihomo.pid").write_text(f"{os.getpid()}\n")
        proc = self.root / "proc" / str(os.getpid())
        proc.mkdir(parents=True)
        (proc / "exe").symlink_to(clash / "mihomo")
        self._cmd("iptables", r'''#!/bin/sh
echo "$*" >> "$MOCK_LOG"
# Real iptables rejects REDIRECT --to-ports without an explicit protocol,
# even when all jumps into the user chain already match TCP or UDP.
case " $* " in
 *" --to-ports "*)
  case " $* " in *" -p tcp "*|*" -p udp "*) ;; *) exit 2;; esac;;
esac
if [ -s "$MOCK_FAIL" ] && grep -F -q -- "$(cat "$MOCK_FAIL")" <<EOF
$*
EOF
then exit 1; fi
case " $* " in
 *" -C "*)
  # Answer presence checks from MOCK_RULES so verify receipts can be exercised.
  line="$*"
  if [ -s "$MOCK_RULES" ] && grep -F -q -- "${line#*-C }" "$MOCK_RULES"; then exit 0; fi
  exit 1;;
 *" -D "*|*" -F "*|*" -X "*|*" -L "*) exit 1;;
esac
exit 0
''')
        self._cmd("ip6tables", r'''#!/bin/sh
echo "ip6 $*" >> "$MOCK_LOG"
case " $* " in *" -D "*|*" -C "*|*" -F "*|*" -X "*) exit 1;; esac
exit 0
''')
        self.routes = self.root / "routes"
        self.routes.write_text("")
        self._cmd("ip", '#!/bin/sh\ncat "$MOCK_ROUTES"\n')
        self.exit_file=self.root / "exit"
        self.exit_file.write_text("")
        self._cmd("tailscale", r'''#!/bin/sh
case "$*" in
 *" set --exit-node="*)
  case "$*" in *"--exit-node=100."*) echo '100.64.0.5' > "$MOCK_EXIT";; *) : > "$MOCK_EXIT";; esac
  exit 0;;
esac
if [ -s "$MOCK_EXIT" ]; then
 printf '%s\n' '{"BackendState":"Running","ExitNodeStatus":{"Online":true,"ID":"node","TailscaleIPs":["100.64.0.5"]}}'
else printf '{"BackendState":"%s"}\n' "${MOCK_BACKEND:-Running}"; fi
''')
        self._cmd("jsonfilter", r'''#!/usr/bin/env python3
import json,sys,shlex
try:
 data=json.load(sys.stdin)
 for expr in sys.argv[2::2]:
  name=None
  if '=' in expr:name,expr=expr.split('=',1)
  d=data
  for part in expr.removeprefix('@.').replace('[0]','.0').split('.'):
   d=d[int(part)] if part.isdigit() else d[part]
  if name:print('export '+name+'='+shlex.quote(str(d))+';')
  else:print((1 if d else 0) if isinstance(d,bool) else d)
except (KeyError,IndexError,ValueError):pass
''')
        self._cmd("start-stop-daemon", r'''#!/bin/sh
pidfile=
while [ "$#" -gt 0 ]; do
  [ "$1" = "-p" ] && { shift; pidfile="$1"; }
  shift
done
sleep 60 >/dev/null 2>&1 &
pid=$!
echo "$pid" > "$pidfile"
mkdir -p "$MOCK_PROC/$pid"
ln -sf "$MOCK_CLASH" "$MOCK_PROC/$pid/exe"
''')
        self._cmd("curl", r'''#!/bin/sh
case "$*" in *localapi/v0/prefs*)
 if [ -n "$MOCK_PREFS_MISSING" ]; then printf '{}';
 elif [ -s "$MOCK_EXIT" ]; then printf '{"ExitNodeID":"node","ExitNodeIP":"100.64.0.5"}';
 else printf '{"ExitNodeID":"","ExitNodeIP":""}'; fi
 exit 0;;
esac
if [ -n "$MOCK_READY_DELAY" ]; then
 n=0; [ ! -f "$MOCK_READY_DELAY" ] || n=$(cat "$MOCK_READY_DELAY")
 n=$((n+1)); echo "$n" > "$MOCK_READY_DELAY"
 [ "$n" -ge 4 ] || { printf 000; exit 7; }
fi
printf 401
''')
        self.env = dict(os.environ,
            PROFILE_ROOT=str(self.state),
            PROFILE_LOCK=str(self.root / "lock"),
            CLASH_ROOT=str(clash),
            PROC_ROOT=str(self.root / "proc"),
            NET_CLASS=str(self.net),
            IPTABLES=str(self.bin / "iptables"),
            IP6TABLES=str(self.bin / "ip6tables"),
            IP_CMD=str(self.bin / "ip"),
            TAILSCALE_CLI=str(self.bin / "tailscale"),
            JSONFILTER=str(self.bin / "jsonfilter"),
            MOCK_LOG=str(self.log), MOCK_FAIL=str(self.fail),
            MOCK_RULES=str(self.root / "rules"),
            MOCK_ROUTES=str(self.routes), MOCK_EXIT=str(self.exit_file),
            MOCK_PROC=str(self.root / "proc"), MOCK_CLASH=str(clash / "mihomo"),
            PATH=f"{self.bin}:{os.environ['PATH']}")

    def _cmd(self, name, text):
        p = self.bin / name
        p.write_text(text)
        p.chmod(0o755)

    def run(self, action):
        return subprocess.run(["sh", str(SCRIPT), action], env=self.env,
                              text=True, capture_output=True)

    def close(self):
        self.tmp.cleanup()


class NetworkProfileTests(unittest.TestCase):
    def test_standby_guards_background_start_but_allows_saved_resume(self):
        marker=self.h.root/'standby-active';marker.touch();self.h.env['PROFILE_STANDBY_ACTIVE']=str(marker)
        r=self.h.run('clash-start');self.assertEqual(r.returncode,75,r.stderr)
        self.assertFalse(self.h.log.exists())
        (self.h.state/'network-profile').write_text('clash\n')
        r=self.h.run('standby-resume');self.assertEqual(r.returncode,0,r.stderr)
        self.assertEqual((self.h.state/'network-profile').read_text().strip(),'clash')
    def setUp(self):
        self.h = Harness()

    def tearDown(self):
        self.h.close()

    def test_direct_removes_only_named_project_and_legacy_rules(self):
        r = self.h.run("direct")
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        commands = self.h.log.read_text()
        self.assertIn("-D PREROUTING -i br-lan -p tcp -j U60_CLASH", commands)
        self.assertIn("-F U60_CLASH", commands)
        self.assertNotIn("-t nat -F\n", commands)
        self.assertNotIn("-F PREROUTING", commands)
        self.assertEqual((self.h.state / "network-profile").read_text().strip(), "direct")
        again = self.h.run("direct")
        self.assertEqual(again.returncode, 0, again.stderr + again.stdout)
        self.assertEqual((self.h.state / "network-profile").read_text().strip(), "direct")

    def test_status_receipt_matches_controller_contract(self):
        r=self.h.run('status');self.assertEqual(r.returncode,0);self.assertTrue(json.loads(r.stdout)['ok'])

    def test_verify_is_read_only_and_reports_actual_coverage(self):
        rules = self.h.root / "rules"
        (self.h.state / "network-profile").write_text("clash\n")
        r = self.h.run("verify")
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        body = json.loads(r.stdout)
        self.assertEqual(body["profile"], "clash")
        self.assertEqual(body["clash_scope"], "ipv4_tcp_and_dns_limited")
        self.assertFalse(body["udp_proxy"]); self.assertFalse(body["ipv6_proxy"])
        self.assertFalse(body["redirect"]["prerouting_tcp"])
        self.assertFalse(body["redirect"]["dns"])
        self.assertFalse(body["redirect"]["udp443_reject"])
        self.assertFalse(body["redirect"]["ipv6_redirect"])
        # A read-only receipt must not mutate state, take the lock or rewrite rules.
        self.assertEqual((self.h.state / "network-profile").read_text().strip(), "clash")
        self.assertFalse((self.h.root / "lock").exists())
        self.assertFalse(rules.exists())
        commands = self.h.log.read_text()
        self.assertNotIn("-A ", commands)
        self.assertNotIn("-I ", commands)
        self.assertNotIn("-D ", commands)
        rules.write_text("PREROUTING -i br-lan -p tcp -j U60_CLASH\n"
                         "PREROUTING -i br-lan -p udp --dport 53 -j U60_CLASH_DNS\n"
                         "PREROUTING -i br-lan -p tcp --dport 53 -j U60_CLASH_DNS\n"
                         "FORWARD -i br-lan ! -o tailscale0 -p udp --dport 443 -j REJECT --reject-with icmp-port-unreachable\n")
        r = self.h.run("verify")
        body = json.loads(r.stdout)
        self.assertTrue(body["redirect"]["prerouting_tcp"])
        self.assertTrue(body["redirect"]["dns"])
        self.assertTrue(body["redirect"]["udp443_reject"])
        self.assertFalse(body["redirect"]["guard"])
        self.assertFalse(body["ipv6_default_route"])
        self.h.routes.write_text("default via fe80::1 dev wwan0 proto ra\n")
        body = json.loads(self.h.run("verify").stdout)
        self.assertTrue(body["ipv6_default_route"])
        (self.h.state / "network-profile").write_text("direct\n")
        body = json.loads(self.h.run("verify").stdout)
        self.assertEqual(body["profile"], "direct")
        self.assertTrue(body["redirect"]["prerouting_tcp"])

    def test_clash_does_not_depend_on_tailnet_control_connection(self):
        (self.h.net / 'tailscale0').mkdir()
        (self.h.state / 'network-profile').write_text('clash')
        self.h.env['MOCK_BACKEND']='Starting'
        r=self.h.run('clash')
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        self.assertEqual(json.loads(r.stdout)['profile'],'clash')

    def test_starting_tailnet_missing_exit_preferences_is_not_safe(self):
        (self.h.net / 'tailscale0').mkdir()
        (self.h.state / 'network-profile').write_text('clash')
        self.h.env.update(MOCK_BACKEND='Starting',MOCK_PREFS_MISSING='1')
        r=self.h.run('clash')
        self.assertNotEqual(r.returncode,0)
        self.assertTrue(json.loads(r.stdout)['fail_closed'])

    def test_tailscale_requires_tun_and_keeps_previous(self):
        (self.h.state / "network-profile").write_text("direct\n")
        r = self.h.run("tailscale")
        self.assertEqual(r.returncode, 7)
        self.assertEqual(json.loads(r.stdout)["kept"], "direct")
        self.assertEqual((self.h.state / "network-profile").read_text().strip(), "direct")

    def test_tailscale_ready_removes_interception(self):
        (self.h.net / "tailscale0").mkdir(); self.h.exit_file.write_text("100.64.0.5")
        self.h.routes.write_text("default dev tailscale0 table 52\n")
        r = self.h.run("tailscale")
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        self.assertEqual(json.loads(r.stdout)["profile"], "tailscale")

    def test_tailscale_rejects_online_exit_without_kernel_route(self):
        (self.h.net / "tailscale0").mkdir(); self.h.exit_file.write_text("100.64.0.5")
        r = self.h.run("tailscale")
        self.assertEqual(r.returncode, 7)
        self.assertEqual(json.loads(r.stdout)["kept"], "direct")

    def test_direct_and_clash_clear_tailscale_exit(self):
        for profile in ['direct','clash']:
            self.h.exit_file.write_text('100.64.0.5')
            r=self.h.run(profile)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertFalse(self.h.exit_file.read_text())

    def test_stop_stale_pid_enters_guard_not_false_rollback(self):
        (self.h.state/'network-profile').write_text('clash')
        (pathlib.Path(self.h.env['PROC_ROOT'])/str(os.getpid())/'exe').unlink()
        r=self.h.run('clash-stop')
        self.assertNotEqual(r.returncode,0)
        self.assertEqual(json.loads(r.stdout)['profile'],'error')
        self.assertTrue(json.loads(r.stdout)['fail_closed'])
        self.assertIn('-I FORWARD 1 -i br-lan -j U60_PANEL_GUARD',self.h.log.read_text())
        self.assertIn('-I FORWARD 1 -i br-lan -j U60_PANEL_GUARD6',self.h.log.read_text())

    def test_enable_selects_clash_and_reuses_existing_core(self):
        (self.h.state/'network-profile').write_text('direct')
        r=self.h.run('clash-enable')
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
        self.assertEqual(json.loads(r.stdout)['profile'],'clash')
        self.assertIn('-N U60_CLASH',self.h.log.read_text())
        self.assertEqual((pathlib.Path(self.h.env['CLASH_ROOT'])/'mihomo.pid').read_text().strip(),str(os.getpid()))

    def test_enable_from_stopped_core_selects_clash(self):
        (self.h.state/'network-profile').write_text('direct')
        pid=pathlib.Path(self.h.env['CLASH_ROOT'])/'mihomo.pid';pid.unlink()
        r=self.h.run('clash-enable')
        try:
            self.assertEqual(r.returncode,0,r.stdout+r.stderr)
            self.assertEqual(json.loads(r.stdout)['profile'],'clash')
            self.assertIn('-N U60_CLASH',self.h.log.read_text())
        finally:
            if pid.exists():os.kill(int(pid.read_text()),15)

    def test_enable_failed_switch_preserves_existing_core(self):
        (self.h.state/'network-profile').write_text('direct');self.h.fail.write_text('-N U60_CLASH')
        r=self.h.run('clash-enable')
        self.assertNotEqual(r.returncode,0)
        self.assertEqual(json.loads(r.stdout)['kept'],'direct')
        os.kill(os.getpid(),0)

    def test_clash_start_preserves_saved_direct_profile(self):
        (self.h.state / "network-profile").write_text("direct\n")
        clash_pid = pathlib.Path(self.h.env["CLASH_ROOT"]) / "mihomo.pid"
        clash_pid.unlink()
        r = self.h.run("clash-start")
        try:
            self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
            self.assertEqual(json.loads(r.stdout)["profile"], "direct")
            self.assertEqual((self.h.state / "network-profile").read_text().strip(), "direct")
            self.assertNotIn("-N U60_CLASH", self.h.log.read_text())
        finally:
            if clash_pid.exists():
                os.kill(int(clash_pid.read_text()), 15)

    def test_boot_waits_for_controller_after_process_exists(self):
        (self.h.state / "network-profile").write_text("clash\n")
        pidfile = pathlib.Path(self.h.env["CLASH_ROOT"]) / "mihomo.pid"
        pidfile.unlink()
        ready = self.h.root / "ready-attempts"
        self.h.env["MOCK_READY_DELAY"] = str(ready)
        r = self.h.run("clash-start")
        try:
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertEqual(ready.read_text().strip(), "4")
            self.assertEqual(json.loads(r.stdout)["profile"], "clash")
        finally:
            if pidfile.exists(): os.kill(int(pidfile.read_text()), 15)

    def test_clash_failure_rolls_back_previous_state(self):
        (self.h.state / "network-profile").write_text("direct\n")
        self.h.fail.write_text("-A U60_CLASH -p tcp")
        r = self.h.run("clash")
        self.assertEqual(r.returncode, 7)
        self.assertEqual(json.loads(r.stdout)["kept"], "direct")
        self.assertEqual((self.h.state / "network-profile").read_text().strip(), "direct")
        self.assertIn("-X U60_CLASH", self.h.log.read_text())

    def test_clash_is_explicitly_tcp_only_and_bypasses_tailnet(self):
        (self.h.net / "tailscale0").mkdir(); self.h.exit_file.write_text("100.64.0.5")
        self.h.routes.write_text("10.23.0.0/16 dev tailscale0 proto static\n")
        r = self.h.run("clash")
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        body = json.loads(r.stdout)
        self.assertEqual(body["clash_scope"], "ipv4_tcp_and_dns_limited")
        self.assertFalse(body["udp_proxy"])
        self.assertFalse(body["ipv6_proxy"])
        commands = self.h.log.read_text()
        self.assertIn("-d 100.64.0.0/10 -j RETURN", commands)
        self.assertIn("-d 10.23.0.0/16 -j RETURN", commands)
        self.assertIn("-A U60_CLASH -p tcp -j REDIRECT", commands)
        self.assertIn("-A U60_CLASH_DNS -d 100.64.0.0/10 -j RETURN", commands)
        self.assertIn("-A U60_CLASH_DNS -d 10.23.0.0/16 -j RETURN", commands)
        self.assertLess(commands.index("-A U60_CLASH_DNS -d 100.64.0.0/10"),commands.index("-A U60_CLASH_DNS -p tcp -j REDIRECT"))
        self.assertIn("-A U60_CLASH_DNS -p tcp -j REDIRECT --to-ports 1053", commands)
        self.assertIn("-A U60_CLASH_DNS -p udp -j REDIRECT --to-ports 1053", commands)
        self.assertIn("-I FORWARD 1 -i br-lan ! -o tailscale0 -p udp --dport 443", commands)
        self.assertNotIn("-A U60_CLASH -p udp -j REDIRECT", commands)

    def test_failed_rollback_installs_fail_closed_guard(self):
        (self.h.state / "network-profile").write_text("clash\n")
        self.h.fail.write_text("-A U60_CLASH -p tcp")
        r = self.h.run("clash")
        self.assertEqual(r.returncode, 8)
        self.assertEqual(json.loads(r.stdout)["error"], "rollback_failed")
        self.assertTrue(json.loads(r.stdout)["fail_closed"])
        self.assertEqual((self.h.state / "network-profile").read_text().strip(), "error")
        commands = self.h.log.read_text()
        self.assertIn("-I FORWARD 1 -i br-lan -j U60_PANEL_GUARD", commands)
        self.assertIn("-A U60_PANEL_GUARD -d 100.64.0.0/10 -j RETURN", commands)
        self.assertIn("-A U60_PANEL_GUARD -j REJECT", commands)
        self.assertIn("ip6 -A U60_PANEL_GUARD6 -d fc00::/7 -j RETURN", commands)
        self.assertIn("ip6 -A U60_PANEL_GUARD6 -d fe80::/10 -j RETURN", commands)
        self.assertIn("ip6 -A U60_PANEL_GUARD6 -d ff00::/8 -j RETURN", commands)
        self.assertIn("ip6 -A U60_PANEL_GUARD6 -j REJECT", commands)
        self.assertIn("ip6 -I FORWARD 1 -i br-lan -j U60_PANEL_GUARD6", commands)
        self.assertNotIn("INPUT", commands)

    def test_guard_removed_only_after_successful_clash_graph(self):
        (self.h.state / "network-profile").write_text("error\n")
        r = self.h.run("clash")
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        commands = self.h.log.read_text().splitlines()
        redirect = next(i for i, c in enumerate(commands)
                        if "-I PREROUTING 1 -i br-lan -p tcp -j U60_CLASH" in c)
        remove4 = max(i for i, c in enumerate(commands)
                      if "-D FORWARD -i br-lan -j U60_PANEL_GUARD" in c)
        remove6 = max(i for i, c in enumerate(commands)
                      if "ip6 -D FORWARD -i br-lan -j U60_PANEL_GUARD6" in c)
        self.assertLess(redirect, remove4)
        self.assertLess(redirect, remove6)


if __name__ == "__main__":
    unittest.main()
