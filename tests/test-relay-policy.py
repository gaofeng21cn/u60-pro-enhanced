#!/usr/bin/env python3
"""Exercise production policy rules and bounded, STA-bound health probes."""
import json, os, pathlib, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
s=(ROOT/'panel/wifi-relay.sh').read_text()
f=s[s.index('fallback_policy()'):s.index('service_start()')]
with tempfile.TemporaryDirectory() as td:
 d=pathlib.Path(td);(d/'private').mkdir();(d/'run').mkdir();(d/'bin').mkdir()
 def command(name,body):
  p=d/'bin'/name;p.write_text(body);p.chmod(0o700)
 command('cut','#!/bin/sh\necho 123456\n')
 command('flock','#!/bin/sh\nexit 0\n')
 command('iptables','''#!/usr/bin/env python3
import os,pathlib,json,sys
p=pathlib.Path(os.environ['TEST_ROOT']);rules=p/'rules.json';data=json.loads(rules.read_text()) if rules.exists() else []
a=sys.argv[1:];verb=a[2];key=[pathlib.Path(sys.argv[0]).name]+a[3:]
if verb=='-I':key.pop(2)
if verb=='-C':sys.exit(0 if key in data else 1)
if verb=='-I':
 if (p/'fail-v6').exists() and key[0]=='ip6tables':sys.exit(1)
 if key not in data:data.append(key)
if verb=='-D':
 if key not in data:sys.exit(1)
 data.remove(key)
rules.write_text(json.dumps(data))
''')
 (d/'bin/ip6tables').write_bytes((d/'bin/iptables').read_bytes());(d/'bin/ip6tables').chmod(0o700)
 command('ip','#!/bin/sh\n[ ! -f "$TEST_ROOT/no-route" ] && echo "1.1.1.1 via 10.1.2.1 dev u60sta"\n')
 command('curl','''#!/usr/bin/env python3
import os,pathlib,sys
p=pathlib.Path(os.environ['TEST_ROOT']);a=sys.argv[1:]
assert a[a.index('--interface')+1]=='u60sta' and a[a.index('--noproxy')+1]=='*'
assert '--max-time' in a and '--connect-timeout' in a and '-L' not in a
with (p/'probes').open('a') as f:f.write(a[-1]+'\\n')
print((p/'probe-code').read_text().strip())
''')
 env=dict(os.environ,PRIVATE=str(d/'private'),RUN=str(d/'run'),TEST_ROOT=td,PATH=str(d/'bin')+':'+os.environ['PATH'])
 def run(code):return subprocess.run(['sh','-c','set -u\n'+f+'\n'+code],env=env,text=True,capture_output=True,timeout=12)
 def rules():return json.loads((d/'rules.json').read_text()) if (d/'rules.json').exists() else []
 assert run('policy_set fallback wifi-only').returncode==0;assert not rules(),'disabled relay must not cut off current cellular service'
 (d/'private/enabled').touch();assert run('guard_refresh').returncode==0
 assert len(rules())==4 and {x[0] for x in rules()}=={'iptables','ip6tables'} and {x[1] for x in rules()}=={'OUTPUT','FORWARD'}
 assert run('guard_refresh').returncode==0 and len(rules())==4
 assert run('policy_set fallback cellular').returncode==0 and not rules()
 (d/'fail-v6').touch();assert run('policy_set fallback wifi-only').returncode!=0
 assert not rules() and (d/'private/fallback').read_text().strip()=='cellular','partial protection cannot become saved success'
 (d/'fail-v6').unlink();assert run('policy_set autostart 0').returncode==0 and run('autostart_policy').stdout.strip()=='0'
 assert run('policy_set fallback invalid').returncode!=0
 assert run('health_check').returncode==0 and (d/'run/health').read_text().strip()=='NO_LINK' and not (d/'probes').exists()
 (d/'run/lease-ready').touch()
 for code,expected,count in [('204','ONLINE',1),('302','PORTAL',2),('000','UNREACHABLE',2)]:
  (d/'probe-code').write_text(code);(d/'run/health-at').unlink(missing_ok=True);(d/'probes').unlink(missing_ok=True)
  result=run('health_check');assert result.returncode==0,result.stderr
  assert (d/'run/health').read_text().strip()==expected,(code,(d/'run/health').read_text(),result.stderr)
  assert len((d/'probes').read_text().splitlines())==count
  assert run('health_check').returncode==0 and len((d/'probes').read_text().splitlines())==count,'rate limit'
 print('PASS: no-cell rules cover proxy OUTPUT and client FORWARD in both families; partial failure restores policy; STA probes distinguish online/portal/unreachable and rate limit')
