#!/usr/bin/env python3
"""Run the real upgrade script against an isolated target with real hashes.

The safety properties that matter are: refusing anything that does not match
this device or this installation, never touching user state, and restoring the
previous program bytes when verification fails.
"""
import hashlib
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SERVICES = ['u60-usb-isolate', 'u60-usb-role', 'u60-wifi-relay', 'u60-standby', 'u60-web']
STATE_FILES = {
    'data/u60-panel/network-profile': 'clash\n',
    'data/u60-panel/tailscale-lan': '1\n',
    'data/u60-panel/tailscale-mode': 'tun\n',
    'data/u60-panel/standby-mode': 'normal\n',
    'data/u60-panel/usb-role': 'LAN\n',
    'data/u60-panel/compat-mode': 'b31-ui-first\n',
    'data/u60-clash/config.yaml': 'secret: "not-a-real-secret"\nmode: rule\n',
    'data/u60-clash/panel-prefs.json': '{"favorites":["示例"],"recents":[],"delays":{}}\n',
    'data/u60-clash/mode': 'rule\n',
    'data/u60-panel/portable-release': 'u60-pro-B31-20260920-090000\n',
}


class PortableUpgrade(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='u60-upgrade-')
        self.root = pathlib.Path(self.tmp.name)
        self.target = self.root / 'device'
        self.pkg = self.root / 'package'
        self.tools = self.root / 'tools'
        for p in (self.target, self.pkg, self.tools):
            p.mkdir()
        for d in ['data', 'etc/init.d', 'etc/rc.d', 'sys/class/input/event0/device',
                  'sys/class/input/event3/device', 'sys/class/power_supply/usb',
                  'sys/class/net', 'dev/dri', 'dev/net']:
            (self.target / d).mkdir(parents=True, exist_ok=True)
        self.put(self.target / 'sys/class/input/event0/device/name', 'pmic_pwrkey\n')
        self.put(self.target / 'sys/class/input/event3/device/name', 'sitronix_ts_i2c\n')
        self.put(self.target / 'sys/class/power_supply/usb/online', '1\n')
        for p in ['dev/dri/card0', 'dev/net/tun']:
            (self.target / p).symlink_to('/dev/null')
        self.put(self.target / 'etc/init.d/zte_topsw_devui', '#!/bin/sh\nexit 0\n')
        self.put(self.target / 'etc/rc.local',
                 '#!/bin/sh\n(/data/u60-panel/portable-boot.sh) &\nexit 0\n')
        self.put(self.target / 'factory-file', 'factory ABI fixture')
        self.put(self.pkg / 'FACTORY-SHA256SUMS',
                 self.sum(self.target / 'factory-file') + '  ' + str(self.target / 'factory-file') + '\n')
        self.put(self.pkg / 'RELEASE-ID', 'u60-pro-B31-20260924-120000\n')
        self.put(self.pkg / 'TARGET-IDENTITY-SHA256',
                 hashlib.sha256(b'u60-imei-v1:123456789012345').hexdigest() + '\n')
        for f in ['check-device.sh', 'upgrade-installed.sh']:
            code = (ROOT / 'scripts/portable' / f).read_text()
            for prefix in ['/data/', '/etc/', '/sys/', '/dev/dri/', '/dev/net/', '/tmp/']:
                code = re.sub(r'(?<![a-zA-Z0-9_.-])' + re.escape(prefix), str(self.target) + prefix, code)
            self.put(self.pkg / f, code)
        # A realistic installed tree: programs from an older release plus user state.
        payload = self.pkg / 'payload'
        for d in ['data/u60-panel', 'data/u60-clash', 'data/u60-web/public',
                  'data/tailscale/bin', 'init', 'boot']:
            (payload / d).mkdir(parents=True, exist_ok=True)
        self.programs = {
            'data/u60-panel/u60-panel': '#!/bin/sh\nexit 0\n',
            'data/u60-panel/panel-control': '#!/bin/sh\necho "{\\"ok\\":true}"\n',
            'data/u60-panel/panel-web': '#!/bin/sh\nexit 0\n',
            'data/u60-panel/network-profile.sh': '#!/bin/sh\necho status\n',
            'data/u60-clash/mihomo': '#!/bin/sh\nexit 0\n',
            'data/u60-web/public/index.html': '<html>old</html>\n',
            'data/u60-web/mount.sh': '#!/bin/sh\nexit 0\n',
            'data/tailscale/bin/tailscaled': '#!/bin/sh\nexit 0\n',
        }
        for rel, body in self.programs.items():
            self.put(payload / rel, body)
        self.put(payload / 'data/u60-panel/compat-mode', 'b31-ui-first\n')
        self.put(payload / 'boot/portable-boot.sh', '#!/bin/sh\nexit 0\n')
        for name in SERVICES:
            self.put(payload / 'init' / name, '#!/bin/sh\ncase "$1" in start) exit 0;; restart) exit 0;;esac\n')
        # Installed device: same programs, plus the state and the first-install backup.
        for rel, body in self.programs.items():
            self.put(self.target / rel, body)
        for rel, body in STATE_FILES.items():
            self.put(self.target / rel, body)
        self.put(self.target / 'data/u60-panel/portable-boot.sh', '#!/bin/sh\nexit 0\n')
        self.put(self.target / 'data/u60-user-notes.txt', 'keep me\n')
        for name in SERVICES:
            self.put(self.target / 'etc/init.d' / name, '#!/bin/sh\ncase "$1" in start) exit 0;; restart) exit 0;;esac\n')
        self.installed_id = STATE_FILES['data/u60-panel/portable-release'].strip()
        (self.target / 'data/u60-install-backups' / self.installed_id).mkdir(parents=True)
        self.put(self.target / 'data/u60-install-backups' / self.installed_id / 'rc.local', self.oldrc())
        # A running screen and web service, expressed as marker files for the fakes.
        (self.root / 'watcher').touch()
        (self.root / 'panel-web').touch()
        self.screen_log = self.target / 'data/u60-panel/panel.log'
        self.screen_log.write_text('old run\n')
        # The running screen holds the panel lock, exactly like the real device.
        self.panel_lock = self.target / 'tmp/u60-panel.lock'
        self.panel_lock.parent.mkdir(parents=True, exist_ok=True)
        self.panel_lock.write_text('999999\n')
        for name in ['id', 'uname', 'df', 'ubus', 'jsonfilter', 'uci', 'curl', 'flock', 'ip',
                     'iptables', 'ip6tables', 'ebtables', 'hostapd_cli', 'start-stop-daemon']:
            code = '#!/bin/sh\nexit 0\n'
            if name == 'id':
                code = '#!/bin/sh\necho 0\n'
            if name == 'uname':
                code = '#!/bin/sh\n[ "$1" != -m ] || { echo aarch64;exit; };echo 5.15.194-perf\n'
            if name == 'df':
                code = ('#!/bin/sh\necho "Filesystem 1K-blocks Used Available Use% Mounted on"\n'
                        'echo "/dev/block/by-name/userdata 8000000 100000 7000000 3% /data"\n')
            if name == 'jsonfilter':
                code = ('#!/bin/sh\ncat >/dev/null\ncase "$*" in *@.imei*) echo "${FAKE_IMEI:-123456789012345}";; '
                        '*) echo "${FAKE_FW:-BD_CNMU5250V1.0.0B31}";; esac\n')
            self.put(self.tools / name, code)
        self.tools.joinpath('sha256sum').write_text('''#!/usr/bin/env python3
import hashlib,pathlib,sys
if len(sys.argv)==1:
 print(hashlib.sha256(sys.stdin.buffer.read()).hexdigest()+'  -')
elif sys.argv[1]=='-c':
 for line in pathlib.Path(sys.argv[2]).read_text().splitlines():
  h,p=line.split('  ',1)
  if not pathlib.Path(p).is_file() or hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()!=h:sys.exit(1)
else:
 p=sys.argv[1];print(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()+'  '+p)
''')
        checker = ('#!/bin/sh\ncat >/dev/null\n[ "${UPGRADE_VERIFY_FAIL:-0}" = 1 ] && exit 3\n'
                   'exit 0\n')
        # panel-control is invoked with a JSON request on stdin after the upgrade.
        self.put(self.tools / 'panel-control', checker)
        self.tools.joinpath('pidof').write_text(
            '#!/bin/sh\n[ -f "' + str(self.root) + '/panel-web" ] || exit 1\necho 4242\n')
        # kill is a shell builtin, so the watcher must be a real process that
        # reacts to SIGUSR1 exactly like the on-device watcher does.
        self.watcher = self.root / 'screen-watcher.py'
        self.watcher.write_text(
            'import pathlib, signal, sys, time\n'
            'log = pathlib.Path(sys.argv[1])\n'
            'lock = pathlib.Path(sys.argv[2])\n'
            'seen = [0]\n'
            'def handler(signum, frame):\n'
            '    seen[0] += 1\n'
            '    if seen[0] == 1:\n'
            '        lock.write_text("")\n'
            '    elif seen[0] >= 2:\n'
            '        with log.open("a") as f:\n'
            '            f.write("06:00:00 switch event=first-snapshot startup_ms=2000\\n")\n'
            '        lock.write_text("4242\\n")\n'
            'signal.signal(signal.SIGUSR1, handler)\n'
            'print("ready", flush=True)\n'
            'while True:\n'
            '    time.sleep(0.05)\n')
        self.watcher_proc = subprocess.Popen(
            [sys.executable, str(self.watcher), str(self.screen_log), str(self.panel_lock)],
            stdout=subprocess.PIPE, text=True)
        self.watcher_proc.stdout.readline()
        self.watcher_proc.stdout.close()
        self.tools.joinpath('ps').write_text(
            '#!/bin/sh\n[ -f "' + str(self.root) + '/watcher" ] || exit 0\n'
            '[ -f "' + str(self.root) + '/watcher-down" ] && exit 0\n'
            'echo "' + str(self.watcher_proc.pid) + ' root /data/u60-panel/u60-panel watch"\n')
        for name in ['pidof', 'ps', 'panel-control']:
            (self.tools / name).chmod(0o700)
        self.env = dict(os.environ, PATH=str(self.tools) + ':' + os.environ['PATH'])
        self.hash_package()

    def oldrc(self):
        return '#!/bin/sh\n(/data/u60-panel/portable-boot.sh) &\nexit 0\n'

    def tearDown(self):
        if getattr(self, 'watcher_proc', None):
            self.watcher_proc.terminate()
            self.watcher_proc.wait(timeout=10)
        self.tmp.cleanup()

    def put(self, p, body):
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(body)
        p.chmod(0o700)

    def sum(self, p):
        return hashlib.sha256(p.read_bytes()).hexdigest()

    def hash_package(self):
        files = sorted(p for p in self.pkg.rglob('*') if p.is_file() and p.name != 'SHA256SUMS')
        self.put(self.pkg / 'SHA256SUMS',
                 ''.join(self.sum(p) + '  ' + str(p.relative_to(self.pkg)) + '\n' for p in files))

    def run_upgrade(self, *args, env=None):
        return subprocess.run(['sh', str(self.pkg / 'upgrade-installed.sh'), *args],
                              env=env or self.env, text=True, capture_output=True, timeout=90)

    def test_check_requires_an_existing_installation(self):
        (self.target / 'data/u60-panel/portable-release').unlink()
        r = self.run_upgrade('--check')
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('No existing installation', r.stderr)

    def test_refuses_the_already_installed_release(self):
        self.put(self.pkg / 'RELEASE-ID', self.installed_id + '\n')
        self.put(self.target / 'data/u60-panel/portable-release', self.installed_id + '\n')
        self.hash_package()
        r = self.run_upgrade('--check')
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('already installed', r.stderr)

    def test_refuses_a_package_for_another_firmware_variant(self):
        # A cross-variant package is refused by the shared device gate before the
        # upgrade script runs, so nothing may be written either way.
        self.put(self.pkg / 'RELEASE-ID', 'u60-pro-B28-20260924-120000\n')
        self.hash_package()
        r = self.run_upgrade('--check', env=dict(self.env, FAKE_FW='BD_FLYMODEMMU5250V1.0.0B28'))
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual((self.target / 'data/u60-panel/network-profile').read_text(), 'clash\n')
        self.assertEqual((self.target / 'data/u60-panel/portable-release').read_text(),
                         self.installed_id + '\n')

    def test_refuses_another_device_before_writing(self):
        r = self.run_upgrade('upgrade', env=dict(self.env, FAKE_IMEI='999999999999999'))
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('another device', r.stderr)
        self.assertEqual((self.target / 'data/u60-panel/network-profile').read_text(), 'clash\n')

    def test_refuses_a_package_that_adds_an_unexpected_file(self):
        self.put(self.pkg / 'payload/data/u60-panel/panel-new-daemon', '#!/bin/sh\nexit 0\n')
        self.hash_package()
        r = self.run_upgrade('--check')
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('Unexpected new file', r.stderr)

    def test_check_is_readonly_and_lists_the_plan(self):
        before = sorted(str(p) for p in self.target.rglob('*'))
        r = self.run_upgrade('--check')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('user configuration is preserved', r.stdout)
        self.assertEqual(before, sorted(str(p) for p in self.target.rglob('*')))
        plan = self.run_upgrade('--rollback-list')
        self.assertEqual(plan.returncode, 0, plan.stderr)
        listed = plan.stdout.split()
        for state in ['network-profile', 'compat-mode', 'panel-prefs.json', 'config.yaml', 'portable-release']:
            self.assertFalse(any(line.endswith('/' + state) for line in listed),
                             f'{state} must not be part of the replace plan')
        self.assertTrue(any(line.endswith('/data/u60-panel/u60-panel') for line in listed))

    def test_accepts_a_prior_upgrade_backup_when_install_backup_is_older(self):
        install_backup = self.target / 'data/u60-install-backups' / self.installed_id
        for path in install_backup.iterdir():
            path.unlink()
        install_backup.rmdir()
        previous = self.target / 'data/u60-upgrade-backups' / 'prior-release'
        previous.mkdir(parents=True)
        (previous / 'to-release').write_text(self.installed_id + '\n')
        (previous / 'BACKUP-SHA256SUMS').write_text('verified\n')
        r = self.run_upgrade('--check')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('user configuration is preserved', r.stdout)

    def test_upgrade_replaces_programs_and_preserves_user_state(self):
        upgraded = dict(self.programs)
        upgraded['data/u60-panel/u60-panel'] = '#!/bin/sh\necho new-version\n'
        upgraded['data/u60-web/public/index.html'] = '<html>new</html>\n'
        for rel, body in upgraded.items():
            self.put(self.pkg / 'payload' / rel, body)
        self.hash_package()
        r = self.run_upgrade('upgrade')
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('Upgraded', r.stdout)
        for rel, body in upgraded.items():
            self.assertEqual((self.target / rel).read_text(), body, rel)
        for rel, body in STATE_FILES.items():
            # portable-release is the installed-version marker and is the one
            # state file an upgrade updates on purpose.
            if rel == 'data/u60-panel/portable-release':
                continue
            self.assertEqual((self.target / rel).read_text(), body, f'state changed: {rel}')
        self.assertEqual((self.target / 'data/u60-user-notes.txt').read_text(), 'keep me\n')
        self.assertEqual((self.target / 'data/u60-panel/portable-release').read_text(),
                         'u60-pro-B31-20260924-120000\n')
        backup = self.target / 'data/u60-upgrade-backups/u60-pro-B31-20260924-120000'
        self.assertEqual((backup / 'status').read_text(), 'upgraded\n')
        # The old bytes stay available for a manual rollback.
        self.assertEqual((backup / 'data/u60-panel/u60-panel').read_text(),
                         self.programs['data/u60-panel/u60-panel'])

    def test_failed_verification_restores_the_previous_programs(self):
        # The window between "files replaced" and "verified" is exactly where an
        # upgrade must not leave the device half-updated. Fail the screen reload
        # there and require the previous programs back.
        self.put(self.pkg / 'payload/data/u60-panel/u60-panel', '#!/bin/sh\necho new\n')
        self.hash_package()
        (self.root / 'watcher-down').touch()
        r = self.run_upgrade('upgrade')
        self.assertNotEqual(r.returncode, 0)
        self.assertIn('restoring the previous programs', r.stderr)
        self.assertEqual((self.target / 'data/u60-panel/u60-panel').read_text(),
                         self.programs['data/u60-panel/u60-panel'])
        self.assertEqual((self.target / 'data/u60-panel/portable-release').read_text(),
                         self.installed_id + '\n')
        for rel, body in STATE_FILES.items():
            self.assertEqual((self.target / rel).read_text(), body, f'state changed: {rel}')


if __name__ == '__main__':
    unittest.main()
