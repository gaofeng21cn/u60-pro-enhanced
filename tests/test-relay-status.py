#!/usr/bin/env python3
"""Read the production status function with isolated route and WPA observations."""
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class RelayStatusTests(unittest.TestCase):
    def test_driver_stage_and_probe_freshness(self):
        source = (ROOT / 'panel/wifi-relay.sh').read_text()
        source = source[source.index('status() {'):source.index('stop_watch() {')]
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            for name in ('run', 'private', 'bin'):
                (root / name).mkdir()
            (root / 'private/enabled').touch()
            (root / 'run/lease-ready').touch()
            (root / 'run/phase').write_text('CONNECTED\n')
            (root / 'run/health').write_text('ONLINE\n')
            (root / 'uptime').write_text('120.5 30\n')
            for name, body in {
                'ip': '#!/bin/sh\necho "1.1.1.1 dev u60sta"\n',
                'service': '#!/bin/sh\nexit 0\n',
            }.items():
                path = root / 'bin' / name
                path.write_text(body)
                path.chmod(0o700)
            source = source.replace('/proc/uptime', str(root / 'uptime'))
            source = source.replace('/etc/init.d/u60-wifi-relay', str(root / 'bin/service'))
            script = root / 'status.sh'
            script.write_text('''set -u
wpa() { cat "$PRIVATE/driver-status"; }
fallback_policy() { echo wifi-only; }
autostart_policy() { echo 1; }
''' + source + '\nstatus\n')
            env = dict(os.environ, RUN=str(root / 'run'), PRIVATE=str(root / 'private'),
                       PATH=str(root / 'bin') + ':' + os.environ['PATH'])
            for stamp, health, age in [('110', 'ONLINE', 10), ('20', 'UNKNOWN', 100),
                                       ('140', 'UNKNOWN', -1), ('0', 'UNKNOWN', -1),
                                       ('invalid', 'UNKNOWN', -1)]:
                with self.subTest(stamp=stamp):
                    (root / 'run/health-at').write_text(stamp)
                    (root / 'private/driver-status').write_text(
                        'wpa_state=COMPLETED\nfreq=5220\nssid=private-name\n')
                    result = subprocess.run(['sh', str(script)], env=env, check=True,
                                            text=True, capture_output=True, timeout=3)
                    state = json.loads(result.stdout)
                    self.assertEqual(state['health'], health)
                    self.assertEqual(state['health_age_seconds'], age)
                    self.assertEqual(state['link_state'], 'COMPLETED')
                    self.assertEqual(state['frequency'], 5220)
                    self.assertNotIn('private-name', result.stdout)
            (root / 'run/lease-ready').unlink()
            (root / 'private/driver-status').write_text('wpa_state=SCANNING\n')
            result = subprocess.run(['sh', str(script)], env=env, check=True,
                                    text=True, capture_output=True, timeout=3)
            state = json.loads(result.stdout)
            self.assertFalse(state['active'])
            self.assertEqual(state['link_state'], 'SCANNING')
            self.assertEqual(state['health'], 'NO_LINK')
            self.assertEqual(state['health_age_seconds'], -1)


if __name__ == '__main__':
    unittest.main()
