#!/usr/bin/env python3
"""Exercise the supervisor, rollback and confirmation on an isolated fake gadget."""
import os, pathlib, subprocess, tempfile, time, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class EcmTrialTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
  self.root=pathlib.Path(self.tmp.name);self.g=self.root/'sys/kernel/config/usb_gadget/g1';self.n=self.root/'sys/class/net';self.r=self.root/'tmp/u60-ecm-trial'
  for p in ['configs/c.1','functions/ecm.ecm','functions/gsi.rndis','functions/ffs.adb']:(self.g/p).mkdir(parents=True)
  (self.g/'configs/c.1/f1').symlink_to('../../../../usb_gadget/g1/functions/gsi.rndis');(self.g/'configs/c.1/f6').symlink_to('../../../../usb_gadget/g1/functions/ffs.adb')
  (self.g/'UDC').write_text('controller');(self.g/'functions/ecm.ecm/ifname').write_text('usb0')
  for name in ['br-lan','usb0']:(self.n/name).mkdir(parents=True)
  (self.n/'usb0/carrier').write_text('1')
  b=self.root/'bin';b.mkdir()
  for name,body in {'flock':'#!/bin/sh\nexit 0\n','sleep':'#!/bin/sh\n/bin/sleep .01\n','ip':'''#!/usr/bin/env python3
import os,sys,pathlib
r=pathlib.Path(os.environ['U60_ECM_TEST_ROOT']); a=sys.argv[1:]; p=r/'sys/class/net'/a[3]/'master'
if (r/'fail-ip').exists():sys.exit(1)
if a[4]=='master':p.symlink_to('../br-lan')
if a[4]=='nomaster' and p.is_symlink():p.unlink()
'''}.items():
   p=b/name;p.write_text(body);p.chmod(0o700)
  self.script=self.root/'trial.sh';self.script.write_bytes((ROOT/'panel/enable-usb-macnet.sh').read_bytes())
  self.env=dict(os.environ,U60_ECM_TEST_ROOT=str(self.root),PATH=str(b)+':'+os.environ['PATH'])
 def call(self,a):return subprocess.run(['sh',str(self.script),a],env=self.env,capture_output=True,text=True,timeout=5)
 def wait(self,want):
  end=time.monotonic()+4
  while time.monotonic()<end:
   if (self.r/'state').exists() and (self.r/'state').read_text()==want:return
   time.sleep(.01)
  self.fail((self.r/'log').read_text())
 def test_timeout_restores_links_and_adb(self):
  self.assertEqual(self.call('start').returncode,0);self.wait('restored')
  self.assertTrue(str((self.g/'configs/c.1/f1').readlink()).endswith('gsi.rndis'))
  self.assertTrue(str((self.g/'configs/c.1/f6').readlink()).endswith('ffs.adb'))
  self.assertFalse((self.n/'usb0/master').exists());self.assertEqual((self.g/'UDC').read_text().strip(),'controller')
 def test_confirm_then_restore(self):
  self.assertEqual(self.call('start').returncode,0)
  for _ in range(100):
   if (self.n/'usb0/master').exists():break
   time.sleep(.01)
  self.assertEqual(self.call('confirm').returncode,0);self.wait('active')
  self.assertNotEqual(self.call('start').returncode,0)
  self.assertEqual(self.call('restore-trial').returncode,0);self.wait('restored')
 def test_failed_network_setup_rolls_back(self):
  (self.root/'fail-ip').touch();self.call('start');self.wait('restored')
  self.assertTrue(str((self.g/'configs/c.1/f1').readlink()).endswith('gsi.rndis'))
 def test_no_carrier_cannot_confirm(self):
  (self.n/'usb0/carrier').write_text('0');self.call('start')
  time.sleep(.1);self.assertNotEqual(self.call('confirm').returncode,0);self.wait('restored')
 def test_unexpected_composition_is_not_changed(self):
  p=self.g/'configs/c.1/f1';p.unlink();p.symlink_to('../../functions/other')
  self.assertNotEqual(self.call('start').returncode,0);self.assertEqual(str(p.readlink()),'../../functions/other')
if __name__=='__main__':unittest.main()
