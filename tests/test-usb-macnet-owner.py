#!/usr/bin/env python3
"""Exercise native USB owner with an asynchronous vendor model, never a device."""
import hashlib,json,os,pathlib,shutil,subprocess,tempfile,threading,time,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class NativeOwner(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.base=pathlib.Path(self.tmp.name)
  self.root=self.base/'data/u60-panel';self.root.mkdir(parents=True)
  self.private=self.root/'usb-macnet-private';self.private.mkdir()
  self.run=self.base/'tmp/u60-macnet';self.run.mkdir(parents=True)
  self.g=self.base/'sys/kernel/config/usb_gadget/g1';(self.g/'configs/c.1').mkdir(parents=True)
  for name in ['gsi.rndis','gsi.ecm','ffs.adb','ffs.diag']:(self.g/'functions'/name).mkdir(parents=True)
  self.op=self.base/'sys/class/android_usb/android0/usb_op';self.op.parent.mkdir(parents=True);self.op.write_text('1\n')
  self.vendor=self.base/'sbin/usb/compositions/usb_switch';self.vendor.parent.mkdir(parents=True)
  self.original=b'#!/bin/sh\nexit 0\n';self.vendor.write_bytes(self.original)
  (self.private/'factory-usb-switch.sh').write_bytes(self.original)
  self.mounts=self.base/'proc/mounts';self.mounts.parent.mkdir();self.mounts.write_text('')
  (self.root/'compat-mode').write_text('b31-ui-first\n')
  mode=self.base/'sys/bus/platform/devices/a600000.ssusb/mode';mode.parent.mkdir(parents=True);mode.write_text('peripheral\n')
  (self.g/'UDC').write_text('controller\n');self.configure('gsi.rndis')
  (self.private/'factory-links').write_text(self.links())
  src=(ROOT/'panel/usb-macnet-owner.sh').read_text()
  for prefix in ['/data/','/tmp/','/sys/','/sbin/']:src=src.replace(prefix,str(self.base)+prefix)
  src=src.replace('cut -d. -f1 /proc/uptime','date +%s')
  src=src.replace('/proc/mounts',str(self.mounts)).replace('/proc/$$/fd/9','/dev/fd/9')
  src=src.replace('a02435921bf6340773967eb7764b685fef78964af7ae4260383ab5514ad9093e',hashlib.sha256(self.original).hexdigest())
  self.owner=self.root/'usb-macnet-owner.sh';self.owner.write_text(src)
  self.bin=self.base/'bin';self.bin.mkdir();self.env=dict(os.environ,PATH=str(self.bin)+':'+os.environ['PATH'])
  self.command('uname','echo 5.15.194-perf')
  self.command('sha256sum','shasum -a 256 "$@"')
  self.command('pidof','exit 0')
  self.command('readlink','if [ "$1" = -f ];then shift;fi\n/usr/bin/readlink "$@"')
  self.command('mount',f'cp "$2" "$3"\nprintf "mock {self.vendor} bind rw 0 0\\n" > "{self.mounts}"')
  self.command('umount',f'cp "{self.private}/factory-usb-switch.sh" "$1"\n: > "{self.mounts}"')
  self.command('ip',f'rm -f "{self.base}/sys/class/net/ecm0/master"\nrmdir "{self.base}/sys/class/net/ecm0/brport" 2>/dev/null || true')
  (self.root/'usb-macnet-hook.sh').write_text('#!/bin/sh\n# hook\n')
  self.stop=threading.Event();self.fail_ecm=False;self.transitions=[]
  self.thread=threading.Thread(target=self.vendor_loop,daemon=True);self.thread.start()
 def command(self,name,body):
  p=self.bin/name;p.write_text('#!/bin/sh\n'+body+'\n');p.chmod(0o700)
 def links(self):
  return ''.join(str(p)+' '+os.readlink(p)+'\n' for p in sorted((self.g/'configs/c.1').glob('f*')))
 def configure(self,network,pid='0x1404'):
  for p in (self.g/'configs/c.1').glob('f*'):p.unlink()
  names=[network,'ffs.diag']+(['ffs.adb'] if pid=='0x1404' else [])
  for i,name in enumerate(names,1):(self.g/f'configs/c.1/f{i}').symlink_to(self.g/'functions'/name)
  (self.g/'idProduct').write_text(pid+'\n')
  if network=='gsi.ecm':
   nic=self.base/'sys/class/net/ecm0';nic.mkdir(parents=True,exist_ok=True);(nic/'brport').mkdir(exist_ok=True)
   (nic/'master').symlink_to(self.base/'sys/devices/virtual/net/br-lan')
 def vendor_loop(self):
  previous='1'
  while not self.stop.wait(.02):
   mode=self.op.read_text().strip()
   if mode not in ('0','1') or mode==previous:continue
   previous=mode;self.transitions.append(mode)
   # usb_op changes before the asynchronous vendor finishes configuring links.
   time.sleep(.25)
   enabled=(self.root/'usb-macnet-enabled').exists() and (self.root/'usb-macnet-enabled').read_text().strip()=='1'
   network='gsi.ecm' if mode=='1' and enabled and not self.fail_ecm else 'gsi.rndis'
   self.configure(network,'0x1403' if mode=='0' else '0x1404')
 def worker(self,action):
  (self.run/'action').write_text(action)
  return subprocess.run(['sh','-c','exec 9>"$1/lock"; sh "$2" worker','test',str(self.run),str(self.owner)],env=self.env,capture_output=True,text=True,timeout=55)
 def tearDown(self):
  self.stop.set();self.thread.join(2);self.tmp.cleanup()
 def test_async_enable_and_restore_preserve_all_functions(self):
  before=self.links();p=self.worker('enable');self.assertEqual(p.returncode,0,p.stderr)
  self.assertEqual((self.run/'phase').read_text().strip(),'enabled')
  self.assertIn('gsi.ecm',self.links());self.assertIn('ffs.adb',self.links())
  status=json.loads(subprocess.check_output(['sh',str(self.owner),'status'],env=self.env,text=True))
  self.assertEqual(status['phase'],'enabled');self.assertTrue(status['active'])
  p=self.worker('disable');self.assertEqual(p.returncode,0,p.stderr)
  self.assertEqual((self.run/'phase').read_text().strip(),'disabled')
  self.assertEqual(self.links(),before);self.assertEqual(self.vendor.read_bytes(),self.original)
  self.assertEqual(self.transitions,['0','1','0','1'])
 def test_failed_enable_restores_factory_and_clears_intent(self):
  self.fail_ecm=True;self.worker('enable')
  self.assertEqual((self.run/'phase').read_text().strip(),'failed')
  self.assertEqual((self.root/'usb-macnet-enabled').read_text().strip(),'0')
  self.assertEqual(self.links(),(self.private/'factory-links').read_text())
  self.assertEqual(self.vendor.read_bytes(),self.original)
 def test_boot_retains_intent_until_a_host_is_attached(self):
  self.command('flock','exit 0')
  mode=self.base/'sys/bus/platform/devices/a600000.ssusb/mode'
  mode.write_text('none\n');(self.root/'usb-macnet-enabled').write_text('1\n')
  proc=subprocess.Popen(['sh',str(self.owner),'boot'],env=self.env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
  time.sleep(.3)
  self.assertEqual(self.op.read_text().strip(),'1');self.assertEqual(self.transitions,[])
  mode.write_text('peripheral\n')
  out,err=proc.communicate(timeout=12);self.assertEqual(proc.returncode,0,err)
  self.assertTrue(json.loads(out)['ok'])
  for _ in range(100):
   if (self.run/'phase').read_text().strip()=='enabled':break
   time.sleep(.1)
  self.assertEqual((self.run/'phase').read_text().strip(),'enabled')
  self.assertTrue((self.root/'usb-macnet-enabled').read_text().strip()=='1')

 def test_unknown_vendor_is_rejected_without_writes(self):
  self.vendor.write_text('unknown firmware script')
  before=self.op.read_text()
  p=subprocess.run(['sh',str(self.owner),'enable'],env=self.env,capture_output=True,text=True)
  self.assertFalse(json.loads(p.stdout)['ok']);self.assertEqual(self.op.read_text(),before)
  self.assertFalse((self.root/'usb-macnet-enabled').exists())
 def test_hook_preserves_every_argument_and_only_replaces_network(self):
  capture=self.base/'args'
  (self.private/'factory-usb-switch.sh').write_text('#!/bin/sh\nprintf "%s\\n" "$@" > '+str(capture)+'\n')
  hook=(ROOT/'panel/usb-macnet-hook.sh').read_text().replace('/data/',str(self.base)+'/data/').replace('/sys/',str(self.base)+'/sys/')
  p=self.root/'hook-test';p.write_text(hook);(self.root/'usb-macnet-enabled').write_text('1')
  for pid,want in [('0x1404','ecm_gsi,diag,ffs,dpl'),('0x1403','rndis_gsi,diag,ffs,dpl')]:
   args=['0x19d2',pid,'rndis_gsi,diag,ffs,dpl','serial with spaces','y','extra']
   subprocess.run(['sh',str(p),*args],env=self.env,check=True)
   args[2]=want;self.assertEqual(capture.read_text().splitlines(),args)
if __name__=='__main__':unittest.main()
