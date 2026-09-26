#!/usr/bin/env python3
"""Run the real first-install scripts in isolated target directories, with real hashes."""
import hashlib
import os
import pathlib
import re
import subprocess
import tempfile
import time
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
SERVICES=['u60-usb-isolate','u60-usb-role','u60-wifi-relay','u60-standby','u60-web','u60-ncm-trial','u60-ecm-trial']
class PortableInstall(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='u60-portable-');self.root=pathlib.Path(self.tmp.name)
  self.target=self.root/'device';self.pkg=self.root/'package';self.tools=self.root/'tools'
  for p in [self.target,self.pkg,self.tools]:p.mkdir()
  for d in ['data','etc/init.d','etc/rc.d','sys/class/input/event0/device','sys/class/input/event3/device','sys/class/power_supply/usb','sys/class/net','dev/dri','dev/net']:(self.target/d).mkdir(parents=True,exist_ok=True)
  self.put(self.target/'sys/class/input/event0/device/name','pmic_pwrkey\n')
  self.put(self.target/'sys/class/input/event3/device/name','sitronix_ts_i2c\n')
  self.put(self.target/'sys/class/power_supply/usb/online','1\n')
  for p in ['dev/dri/card0','dev/net/tun']:(self.target/p).symlink_to('/dev/null')
  self.put(self.target/'etc/init.d/zte_topsw_devui','#!/bin/sh\nexit 0\n')
  self.oldrc='#!/bin/sh\n# factory preserved\necho factory\nexit 0\n'
  self.put(self.target/'etc/rc.local',self.oldrc)
  self.put(self.target/'factory-file','factory ABI fixture')
  self.put(self.pkg/'FACTORY-SHA256SUMS',self.sum(self.target/'factory-file')+'  '+str(self.target/'factory-file')+'\n')
  self.put(self.pkg/'RELEASE-ID','u60-pro-B31-20260921-150000\n')
  self.put(self.pkg/'TARGET-IDENTITY-SHA256',hashlib.sha256(b'u60-imei-v1:123456789012345').hexdigest()+'\n')
  for f in ['install-new-device.sh','restore-boot.sh','check-device.sh']:
   code=(ROOT/'scripts/portable'/f).read_text()
   for prefix in ['/data/','/etc/','/sys/','/dev/dri/','/dev/net/']:
    code=re.sub(r'(?<![a-zA-Z0-9_.-])'+re.escape(prefix),str(self.target)+prefix,code)
   # Runtime comparison /$p also addresses the isolated device.
   code=code.replace('"/$p"','"'+str(self.target)+'/$p"')
   self.put(self.pkg/f,code)
  payload=self.pkg/'payload'
  for d in ['data/u60-panel','data/u60-clash','data/tailscale/bin','data/u60-web','init','boot']:(payload/d).mkdir(parents=True,exist_ok=True)
  self.put(payload/'data/u60-panel/u60-panel','#!/bin/sh\nexit 0\n')
  for name in ['usb-ncm-composition.sh','usb-ncm-trial.sh','usb-ecm-trial.sh']:
   self.put(payload/'data/u60-panel'/name,'#!/bin/sh\nexit 1\n')
  self.put(payload/'data/u60-panel/panel-autostart.sh','#!/bin/sh\ntouch "'+str(self.target/'started')+'"\n')
  self.put(payload/'data/u60-panel/compat-mode','b31-ui-first\n')
  (payload/'data/u60-web/public').mkdir(parents=True)
  self.put(payload/'data/u60-web/public/index.html','<html>fixture</html>\n')
  self.put(payload/'data/u60-panel/network-profile','direct\n')
  self.put(payload/'data/u60-panel/tailscale-lan','0\n')
  self.put(payload/'data/u60-clash/mihomo','#!/bin/sh\nexit 0\n')
  self.put(payload/'data/tailscale/bin/tailscaled','#!/bin/sh\nexit 0\n')
  self.put(payload/'boot/portable-boot.sh','#!/bin/sh\nexit 0\n')
  for name in SERVICES:
   self.put(payload/'init'/name,'''#!/bin/sh
case "$1" in
 enable) [ -z "${INITFAIL:-}" ] || exit 8;ln -s "../init.d/'''+name+'''" "'''+str(self.target/'etc/rc.d/S99')+name+'''";;
 disable) rm -f "'''+str(self.target/'etc/rc.d/S99')+name+'''";;
esac
''')
  for name in ['id','uname','df','ubus','jsonfilter','uci','curl','flock','ip','iptables','ip6tables','ebtables','hostapd_cli','start-stop-daemon']:
   code='#!/bin/sh\nexit 0\n'
   if name=='id':code='#!/bin/sh\necho 0\n'
   if name=='uname':code='#!/bin/sh\n[ "$1" != -m ] || { echo aarch64;exit; };echo 5.15.194-perf\n'
   if name=='df':code='#!/bin/sh\nif [ -n "${WRAPPED_DF:-}" ];then echo long-device-name;echo "2000000 200000 1800000 10% /data";else echo "dev 2000000 200000 1800000 10% /data";fi\n'
   if name=='jsonfilter':code='#!/bin/sh\ncat >/dev/null\ncase "$*" in *@.imei*) echo "${FAKE_IMEI:-123456789012345}";; *) echo "${FAKE_FW:-BD_CNMU5250V1.0.0B31}";; esac\n'
   self.put(self.tools/name,code)
  self.put(self.tools/'sha256sum','''#!/usr/bin/env python3
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
  self.put(self.tools/'mv','''#!/bin/sh
case "$1" in */u60-clash) if [ -f "$FAULT" ];then rm "$FAULT";exit 9;fi;; esac
exec /bin/mv "$@"
''')
  self.env=dict(os.environ,PATH=str(self.tools)+':'+os.environ['PATH'],FAULT=str(self.root/'fault'))
  self.hash_package()
 def tearDown(self):self.tmp.cleanup()
 def put(self,p,body):p.write_text(body);p.chmod(0o700)
 def sum(self,p):return hashlib.sha256(p.read_bytes()).hexdigest()
 def hash_package(self):
  files=sorted(p for p in self.pkg.rglob('*') if p.is_file() and p.name!='SHA256SUMS')
  self.put(self.pkg/'SHA256SUMS',''.join(self.sum(p)+'  '+str(p.relative_to(self.pkg))+'\n' for p in files))
 def run_install(self,*args,env=None):return subprocess.run(['sh',str(self.pkg/'install-new-device.sh'),*args],env=env or self.env,text=True,capture_output=True,timeout=20)
 def assert_stock(self):
  self.assertEqual((self.target/'etc/rc.local').read_text(),self.oldrc)
  self.assertFalse((self.target/'data/u60-panel').exists())
 def test_check_is_readonly(self):
  before=sorted(str(p) for p in self.target.rglob('*'));r=self.run_install();self.assertEqual(r.returncode,0,r.stderr)
  self.assertEqual(before,sorted(str(p) for p in self.target.rglob('*')));self.assert_stock()
 def test_install_preserves_vendor_boot_and_does_not_start_or_create_identity(self):
  r=self.run_install('--install');self.assertEqual(r.returncode,0,r.stdout+r.stderr)
  rc=(self.target/'etc/rc.local').read_text();self.assertIn('# factory preserved',rc);self.assertEqual(rc.count('portable-boot.sh'),1)
  self.assertEqual((self.target/'data/u60-panel/network-profile').read_text(),'direct\n')
  self.assertEqual((self.target/'data/u60-panel/compat-mode').read_text(),'b31-ui-first\n')
  self.assertEqual([p.name for p in (self.target/'etc/rc.d').iterdir()],['S99u60-web'])
  self.assertEqual((self.target/'data/u60-web').stat().st_mode & 0o777,0o755)
  self.assertEqual((self.target/'data/u60-web/public').stat().st_mode & 0o777,0o755)
  self.assertEqual((self.target/'data/u60-web/public/index.html').stat().st_mode & 0o777,0o644)
  self.assertFalse((self.target/'started').exists());self.assertFalse((self.target/'data/tailscale/tailscaled.state').exists());self.assertFalse((self.target/'data/u60-clash/config.yaml').exists())
  r=self.run_install('--install');self.assertNotEqual(r.returncode,0);self.assertEqual(rc,(self.target/'etc/rc.local').read_text())
 def test_b28_keeps_original_service_startup(self):
  self.put(self.pkg/'RELEASE-ID','u60-pro-B28-20260921-150000\n')
  (self.pkg/'payload/data/u60-panel/compat-mode').unlink()
  self.put(self.tools/'uname','#!/bin/sh\n[ "$1" != -m ] || { echo aarch64;exit; };echo 5.15.185-perf\n')
  self.hash_package()
  r=self.run_install('--install',env=dict(self.env,FAKE_FW='BD_FLYMODEMMU5250V1.0.0B28'))
  self.assertEqual(r.returncode,0,r.stdout+r.stderr)
  self.assertEqual(sorted(p.name for p in (self.target/'etc/rc.d').iterdir()),['S99u60-usb-isolate','S99u60-usb-role','S99u60-web'])
 def test_cross_firmware_package_refused_before_write(self):
  r=self.run_install('--install',env=dict(self.env,FAKE_FW='BD_FLYMODEMMU5250V1.0.0B28'))
  self.assertNotEqual(r.returncode,0);self.assert_stock()
 def test_other_device_refused_before_write(self):
  r=self.run_install('--install',env=dict(self.env,FAKE_IMEI='999999999999999'))
  self.assertNotEqual(r.returncode,0);self.assert_stock()
 def test_mismatched_firmware_rejected_before_write(self):
  r=self.run_install('--install',env=dict(self.env,FAKE_FW='B27'));self.assertNotEqual(r.returncode,0);self.assert_stock()
 def test_usb_disconnected_rejected_before_write(self):
  (self.target/'sys/class/power_supply/usb/online').write_text('0\n');r=self.run_install('--install');self.assertNotEqual(r.returncode,0);self.assert_stock()
 def test_existing_identity_directory_refuses_overwrite(self):
  (self.target/'data/tailscale').mkdir();(self.target/'data/tailscale/tailscaled.state').write_text('SYNTHETIC-IDENTITY')
  r=self.run_install('--install');self.assertNotEqual(r.returncode,0);self.assert_stock();self.assertEqual((self.target/'data/tailscale/tailscaled.state').read_text(),'SYNTHETIC-IDENTITY')
 def test_tampered_package_rejected(self):
  (self.pkg/'payload/data/u60-panel/u60-panel').write_text('changed');r=self.run_install('--install');self.assertNotEqual(r.returncode,0);self.assert_stock()
 def test_mid_install_failure_restores_boot_and_retains_incomplete_copy(self):
  (self.root/'fault').touch();r=self.run_install('--install');self.assertNotEqual(r.returncode,0);self.assert_stock()
  self.assertTrue(list((self.target/'data/u60-install-backups').glob('*/incomplete/u60-panel/u60-panel')))
 def test_init_enable_failure_restores_all_created_dirs(self):
  r=self.run_install('--install',env=dict(self.env,INITFAIL='1'));self.assertNotEqual(r.returncode,0);self.assert_stock()
  for name in SERVICES:self.assertFalse((self.target/'etc/init.d'/name).exists())
  self.assertFalse((self.target/'data/tailscale').exists())
 def test_boot_restore_keeps_new_device_private_data(self):
  r=self.run_install('--install');self.assertEqual(r.returncode,0,r.stderr)
  identity=self.target/'data/tailscale/tailscaled.state';identity.write_text('SYNTHETIC-NEW-IDENTITY')
  r=subprocess.run(['sh',str(self.pkg/'restore-boot.sh')],env=self.env,text=True,capture_output=True,timeout=10)
  self.assertEqual(r.returncode,0,r.stderr);self.assertEqual((self.target/'etc/rc.local').read_text(),self.oldrc)
  self.assertEqual(identity.read_text(),'SYNTHETIC-NEW-IDENTITY');self.assertTrue((self.target/'data/u60-panel/u60-panel').exists())
 def test_abi_changed_rejected(self):
  (self.target/'factory-file').write_text('different firmware binary');r=self.run_install('--install');self.assertNotEqual(r.returncode,0);self.assert_stock()
 def test_busybox_wrapped_disk_output_supported(self):
  r=self.run_install('--install',env=dict(self.env,WRAPPED_DF='1'));self.assertEqual(r.returncode,0,r.stderr)
 def test_start_requires_install_and_verifies_programs(self):
  r=self.run_install('--start');self.assertNotEqual(r.returncode,0);self.assertFalse((self.target/'started').exists())
  r=self.run_install('--install');self.assertEqual(r.returncode,0,r.stderr)
  program=self.target/'data/u60-panel/u60-panel';old=program.read_text();program.write_text('tampered')
  r=self.run_install('--start');self.assertNotEqual(r.returncode,0);self.assertFalse((self.target/'started').exists())
  program.write_text(old)
  r=self.run_install('--start');self.assertEqual(r.returncode,0,r.stderr)
  for _ in range(20):
   if (self.target/'started').exists():break
   time.sleep(.05)
  self.assertTrue((self.target/'started').exists())
if __name__=='__main__':unittest.main()
