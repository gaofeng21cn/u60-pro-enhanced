#!/usr/bin/env python3
"""A procd stop acknowledgement must not race the old watcher's cleanup."""
import pathlib,subprocess,tempfile,os
R=pathlib.Path(__file__).resolve().parents[1];s=(R/'panel/wifi-relay.sh').read_text()
fn=s[s.index('stop_watch()'):s.index('case "${1:-}" in')].replace('/etc/init.d/u60-wifi-relay stop','stop_service')
branch=s[s.index(' off)\n')+len(' off)\n'):s.index('\n watch)')].removesuffix(';;')
with tempfile.TemporaryDirectory() as td:
 p=pathlib.Path(td)
 pre='''set -u
calls=0
owned=0
stop_service() { [ ! -e "$PRIVATE/enabled" ]; }
flock() { calls=$((calls+1));[ "$SCENARIO" != timeout ] && [ "$calls" -ge 3 ] && owned=1; }
sleep() { :; }
cell_guard() { :; }
cleanup() { [ "$owned" = 1 ] || exit 9;touch "$RUN/cleaned"; }
phase() { echo "$1" > "$RUN/phase"; }
'''
 for case in ['release','timeout']:
  d=p/case;d.mkdir();(d/'enabled').touch()
  result=subprocess.run(['sh','-c',pre+fn+'\n'+branch],env=dict(os.environ,RUN=str(d),PRIVATE=str(d),SCENARIO=case),text=True,capture_output=True,timeout=4)
  if case=='release':assert result.returncode==0 and (d/'cleaned').exists() and (d/'phase').read_text().strip()=='OFF',result.stderr
  else:assert result.returncode!=0 and not (d/'cleaned').exists()
print('PASS: off waits for real watch ownership; lock timeout never races cleanup')
