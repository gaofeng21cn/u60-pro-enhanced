#!/usr/bin/env python3
"""Push this reviewed bundle to exactly one selected ADB device and run its installer."""
import argparse
import hashlib
import pathlib
import re
import shutil
import subprocess

ROOT=pathlib.Path(__file__).resolve().parent
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--adb',default=shutil.which('adb'),help='path to adb; use the existing platform-tools binary')
 p.add_argument('--serial',help='required when more than one ADB device is connected')
 p.add_argument('action',choices=['check','install','start','upgrade-check','upgrade','restore-boot'],nargs='?',default='check')
 a=p.parse_args()
 if not a.adb:p.error('Provide --adb /path/to/adb')
 expected={'SHA256SUMS'}
 for line in (ROOT/'SHA256SUMS').read_text().splitlines():
  h,name=line.split('  ',1);path=ROOT/name
  if name in expected:raise SystemExit('Duplicate manifest entry')
  expected.add(name)
  if path.is_symlink() or '..' in pathlib.PurePosixPath(name).parts or name.startswith('/') or hashlib.sha256(path.read_bytes()).hexdigest()!=h:raise SystemExit('Local bundle checksum failed')
 actual=set()
 for path in ROOT.rglob('*'):
  if path.is_symlink():raise SystemExit('Symlink in bundle; use a clean extraction')
  if path.is_file():actual.add(path.relative_to(ROOT).as_posix())
 if actual!=expected:raise SystemExit('Extra or missing file in bundle; do not place private configs in this directory')
 listing=subprocess.run([a.adb,'devices'],capture_output=True,text=True,check=True,timeout=10).stdout
 devices=[l.split()[0] for l in listing.splitlines()[1:] if len(l.split())==2 and l.split()[1]=='device']
 serial=a.serial
 if not serial:
  if len(devices)!=1:raise SystemExit('Connect exactly one authorized target or specify --serial')
  serial=devices[0]
 if serial not in devices:raise SystemExit('Selected ADB device is not ready')
 ident=(ROOT/'RELEASE-ID').read_text().strip()
 match=re.fullmatch(r'u60-pro-(B28|B31)-[0-9]{8}-[0-9]{6}',ident)
 if not match:raise SystemExit('Invalid release id')
 remote='/data/u60-packages/'+ident
 adb=[a.adb,'-s',serial]
 def shell(code,limit=30):
  # Vendor adbd lacks shell -T and reliable remote exit propagation.
  script="set -e\ntrap 'r=$?; printf \"\\n__U60_RC__=%s\\n\" \"$r\"' EXIT\n"+code+'\nexit 0\n'
  result=subprocess.run(adb+['exec-out','sh','-c',script],text=True,capture_output=True,timeout=limit)
  marker=re.search(r'\n__U60_RC__=(\d+)\s*$',result.stdout)
  if not marker or marker[1]!='0':raise SystemExit('Device action failed; inspect the target over ADB. No further step was run.')
  return result.stdout[:marker.start()]
 # Bind the selected device to the expected firmware before any package upload.
 fw=shell("ubus -t 5 call zwrt_zte_mdm.api get_zwrt_common_info '{}' | jsonfilter -e '@.wa_inner_version'").strip()
 firmware={'B28':'BD_FLYMODEMMU5250V1.0.0B28','B31':'BD_CNMU5250V1.0.0B31'}[match[1]]
 if fw!=firmware:raise SystemExit('Target firmware does not match the prepared package')
 identity=shell("ubus -t 5 call zwrt_web device_info '{}' | jsonfilter -e '@.imei'").strip()
 expected_identity=(ROOT/'TARGET-IDENTITY-SHA256').read_text().strip()
 if not re.fullmatch(r'[0-9]{15}',identity) or not re.fullmatch(r'[0-9a-f]{64}',expected_identity) or hashlib.sha256(b'u60-imei-v1:'+identity.encode()).hexdigest()!=expected_identity:
  raise SystemExit('Target identity does not match the prepared package')
 if a.action in ['install','check','upgrade-check','upgrade']:
  shell('mkdir -p /data/u60-packages\nchmod 700 /data/u60-packages\n[ ! -L '+remote+' ]\nmkdir -p '+remote+'\nchmod 700 '+remote)
  # Directory contents only; no workstation config, credential or Tailscale state is added.
  subprocess.run(adb+['push',str(ROOT)+'/.',remote+'/'],check=True,timeout=180)
 command=('sh ./restore-boot.sh' if a.action=='restore-boot' else
          'sh ./upgrade-installed.sh ' + {'upgrade-check':'--check','upgrade':'upgrade'}[a.action] if a.action in ('upgrade-check','upgrade') else
          'sh ./install-new-device.sh '+{'check':'check','install':'--install','start':'--start'}[a.action])
 # An upgrade restarts the web service and reloads the screen through the
 # power-key path, which needs more than the first-install budget.
 limit=300 if a.action in ('upgrade-check','upgrade') else 120
 print(shell('set -e\ncd '+remote+'\n'+command,limit),end='')
 print('Package directory on target: '+remote)
if __name__=='__main__':main()
