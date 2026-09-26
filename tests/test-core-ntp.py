#!/usr/bin/env python3
"""Existing private configs migrate atomically, rollback on reload failure."""
import pathlib, tempfile, subprocess, os, hashlib
R=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as td:
 p=pathlib.Path(td);(p/'bin').mkdir()
 source=(R/'scripts/portable/setup-clash.sh').read_text().replace('/data/u60-clash',td).replace('/tmp/u60-control.lock',td+'/control.lock').replace('/tmp/u60-ntp-reply.',td+'/reply.')
 (p/'setup.sh').write_text(source)
 (p/'mihomo').write_text('#!/bin/sh\n[ ! -f "'+td+'/invalid" ]\n');(p/'mihomo').chmod(0o700)
 curl=p/'bin/curl';curl.write_text('''#!/bin/sh
cat >/dev/null
printf x >> "$TEST_ROOT/reloads"
[ -f "$TEST_ROOT/reject" ] && printf 500 || printf 204
''');curl.chmod(0o700)
 (p/'bin/id').write_text('#!/bin/sh\necho 0\n');(p/'bin/id').chmod(0o700)
 (p/'bin/flock').write_text('#!/bin/sh\nexit 0\n');(p/'bin/flock').chmod(0o700)
 # SHA tool for macOS hosts without sha256sum.
 q=p/'bin/sha256sum';q.write_text('#!/usr/bin/env python3\nimport sys,hashlib\np=sys.argv[1];print(hashlib.sha256(open(p,"rb").read()).hexdigest()+"  "+p)\n');q.chmod(0o700)
 old='secret: "synthetic-test-only"\nmode: rule\nproxy-providers: {}\n'
 def run(action='--enable-ntp'):return subprocess.run(['sh',str(p/'setup.sh'),action],env=dict(os.environ,PATH=str(p/'bin')+':'+os.environ['PATH'],TEST_ROOT=td),capture_output=True,text=True)
 (p/'config.yaml').write_text(old);r=run();assert r.returncode==0,r.stderr
 cfg=(p/'config.yaml').read_text();assert cfg.startswith(old) and 'write-to-system: false' in cfg and 'dialer-proxy: DIRECT' in cfg and 'server: 162.159.200.1' in cfg
 assert 'synthetic-test-only' not in r.stdout+r.stderr
 n=(p/'reloads').read_text();assert run().returncode==0 and (p/'reloads').read_text()==n
 (p/'config.yaml').write_text(old);(p/'reject').touch();r=run();assert r.returncode!=0 and (p/'config.yaml').read_text()==old
 (p/'reject').unlink();(p/'invalid').touch();n=(p/'reloads').read_text();assert run().returncode!=0 and (p/'config.yaml').read_text()==old and (p/'reloads').read_text()==n
 (p/'invalid').unlink()
 historical=old+'ntp:\n  enable: true\n  server: time.apple.com\n  write-to-system: false\nother:\n  server: time.apple.com\n'
 repaired=historical.replace('  server: time.apple.com', '  server: 162.159.200.1', 1)
 (p/'config.yaml').write_text(historical);r=run('--repair-ntp');assert r.returncode==0 and (p/'config.yaml').read_text()==repaired
 n=(p/'reloads').read_text();assert run('--repair-ntp').returncode==0 and (p/'reloads').read_text()==n
 custom=historical.replace('server: time.apple.com','server: private.example',1)
 (p/'config.yaml').write_text(custom);assert run('--repair-ntp').returncode==0 and (p/'config.yaml').read_text()==custom and (p/'reloads').read_text()==n
 (p/'config.yaml').write_text(historical);(p/'reject').touch();assert run('--repair-ntp').returncode!=0 and (p/'config.yaml').read_text()==historical
 print('PASS: core-only NTP migration, existing config retention, secret isolation, validation and failed reload rollback')
