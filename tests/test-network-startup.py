#!/usr/bin/env python3
"""Run the actual B31 startup branch with only service/UI commands substituted."""
import pathlib,tempfile,subprocess,os
ROOT=pathlib.Path(__file__).resolve().parents[1]
s=(ROOT/'panel/panel-autostart.sh').read_text();branch=s[s.index('if [ "$UI_ONLY" = b31-ui-first ]; then'):s.index('\n[ ! -x /etc/init.d/u60-standby')]
with tempfile.TemporaryDirectory() as td:
 d=pathlib.Path(td);(d/'relay-private').mkdir()
 for name in ['usb-service','wifi-relay.sh','panel-watch.sh','panel-run.sh']:
  p=d/name;p.write_text('#!/bin/sh\nprintf "%s %s\\n" "'+name+'" "$*" >> "'+str(d/'calls')+'"\n');p.chmod(0o700)
 script=branch.replace('/etc/init.d/u60-usb-role',str(d/'usb-service'))
 for usb,relay in [(False,False),(True,False),(False,True),(True,True)]:
  for flag,on in [('usb-managed',usb),('relay-private/enabled',relay)]:
   if on:(d/flag).touch()
   else:(d/flag).unlink(missing_ok=True)
  (d/'calls').write_text('')
  p=subprocess.run(['sh','-c',script],env=dict(os.environ,ROOT=td,UI_ONLY='b31-ui-first'),capture_output=True,text=True,timeout=4);assert p.returncode==0,p.stderr
  calls=(d/'calls').read_text();assert ('usb-service start' in calls)==usb;assert ('wifi-relay.sh boot' in calls)==relay;assert 'panel-run.sh' in calls
# Boot policy is separate from runtime start: opting out stops next-boot resume,
# not the user's current association.
s=(ROOT/'panel/wifi-relay.sh').read_text();boot=s[s.index(' boot)\n')+len(' boot)\n'):s.index('\n status)')].removesuffix(';;')
with tempfile.TemporaryDirectory() as td:
 d=pathlib.Path(td)
 for auto in [0,1]:
  (d/'enabled').touch();(d/'calls').write_text('')
  pre='autostart_policy() { echo "$AUTO"; }; cell_guard() { echo "guard $1" >> "$PRIVATE/calls"; }; guard_refresh() { echo protection >> "$PRIVATE/calls"; }; service_start() { echo start >> "$PRIVATE/calls"; }; '
  p=subprocess.run(['sh','-c',pre+boot],env=dict(os.environ,PRIVATE=td,AUTO=str(auto)),capture_output=True,text=True,timeout=3);assert p.returncode==0,p.stderr
  assert (d/'enabled').exists()==bool(auto);assert ('start' in (d/'calls').read_text())==bool(auto)
print('PASS: B31 restores only opted-in owners; autostart off preserves saved credentials but not boot intent')
