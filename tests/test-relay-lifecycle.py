#!/usr/bin/env python3
"""Run the real relay watch branches with isolated radio/process substitutes."""
import os
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).parents[1]
source = (ROOT / 'panel/wifi-relay.sh').read_text()
watch = source[source.index('  tick=0\n'):source.index('\n  done;;') + len('\n  done')]
with tempfile.TemporaryDirectory() as td:
    d = pathlib.Path(td)
    for scenario in ['policy', 'prepare_failure']:
        private = d / scenario
        private.mkdir()
        (private / 'enabled').touch()
        (private / 'station').touch()
        code = '''
set -eu
guard_refresh() { :; }
health_check() { :; }
allowed() { [ "$SCENARIO" != policy ]; }
prepare() { return 1; }
stop_dhcp() { :; }
withdraw() { :; }
cleanup() { rm -f "$PRIVATE/station"; }
phase() { echo "$1" > "$PRIVATE/phase"; }
sleep() {
 [ ! -e "$PRIVATE/station" ] || { echo 'station left running after policy/prepare failure' >&2; exit 9; }
 rm -f "$PRIVATE/enabled"
}
'''
        p = subprocess.run(['sh', '-c', code + watch],
                           env=dict(os.environ, PRIVATE=str(private), SCENARIO=scenario),
                           text=True, capture_output=True, timeout=5)
        assert p.returncode == 0, (scenario, p.stderr)
        assert (private / 'phase').read_text().strip() == ('POLICY' if scenario == 'policy' else 'ERROR')
print('PASS: relay withdraws its station on stock sleep/policy conflict and failed preparation')
