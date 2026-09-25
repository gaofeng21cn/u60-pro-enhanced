#!/usr/bin/env python3
"""Host-only fake filesystem/process/API tests. Never executes a device command."""
import json, os, pathlib, shutil, socket, subprocess, tempfile, textwrap, unittest
SCRIPT = pathlib.Path(__file__).resolve().parents[1] / 'panel/tailscale-mode.sh'
TOOL = r'''#!/usr/bin/env python3
import json,os,pathlib,sys,shutil,socket,time
root=pathlib.Path(os.environ['U60_TS_TEST_ROOT']); name=pathlib.Path(sys.argv[0]).name
cfg=json.loads((root/'mock.json').read_text()); mode=(root/'current-mode').read_text().strip() if (root/'current-mode').exists() else 'none'
if name=='flock':sys.exit(1 if cfg.get('lock_busy') else 0)
if name=='tailscaled':
 time.sleep(cfg.get('start_delay',0))
 mode='tun' if '--tun=tailscale0' in sys.argv else 'userspace'
 with (root/'starts.jsonl').open('a') as f:f.write(json.dumps(sys.argv[1:])+'\n')
 if mode in cfg.get('fail_start_modes',[]):sys.exit(1)
 pid=str(os.getpid()); p=root/'proc'/pid;p.mkdir(parents=True)
 (p/'exe').symlink_to(root/'data/tailscale/bin/tailscaled');(p/'cmdline').write_bytes(b'\0'.join(x.encode() for x in sys.argv)+b'\0')
 (root/'current-mode').write_text(mode)
 sockpath=root/'tmp/tailscale/tailscaled.sock'
 try:sockpath.unlink()
 except FileNotFoundError:pass
 sk=socket.socket(socket.AF_UNIX);sk.bind(str(sockpath));sk.close()
 iface=root/'sys/class/net/tailscale0'
 if mode=='tun' and not cfg.get('missing_iface'):iface.mkdir(parents=True,exist_ok=True)
 else:shutil.rmtree(iface,ignore_errors=True)
 sys.exit(0)
if name=='kill':
 assert sys.argv[1]=='-TERM';pid=sys.argv[2]
 with (root/'kills').open('a') as f:f.write(pid+'\n')
 if cfg.get('refuse_stop')==pid:sys.exit(0)
 shutil.rmtree(root/'proc'/pid,ignore_errors=True)
 try:(root/'tmp/tailscale/tailscaled.sock').unlink()
 except FileNotFoundError:pass
 shutil.rmtree(root/'sys/class/net/tailscale0',ignore_errors=True);sys.exit(0)
if name=='curl':
 endpoint=sys.argv[-1].rsplit('/',1)[-1];prefs=dict(cfg['prefs'])
 if cfg.get('mutate_want_mode')==mode:prefs['WantRunning']=not prefs['WantRunning']
 if endpoint=='prefs':print(json.dumps(prefs));sys.exit(0)
 backend='Running' if prefs['WantRunning'] else 'Stopped'
 if mode in cfg.get('stuck_modes',[]):backend='Starting'
 print(json.dumps({'BackendState':backend}));sys.exit(0)
if name=='jsonfilter':
 try:data=json.load(sys.stdin)
 except Exception:sys.exit(1)
 args=sys.argv[1:];outputs=[];assignments=[]
 def quoted(v):return "'"+str(v).replace("'","'\\''")+"'"
 for i,arg in enumerate(args):
  if arg!='-e':continue
  expr=args[i+1];var=None
  if '=' in expr:var,expr=expr.split('=',1)
  key=expr[2:];array=key.endswith('[*]');key=key.removesuffix('[*]');value=data.get(key)
  if var:
   if value is None:
    if key in data:assignments.append('export '+var+'=; ')
    continue
   if isinstance(value,bool):out='1' if value else '0'
   elif isinstance(value,list):out=' '.join(quoted(x) for x in value)
   else:out=quoted(value)
   assignments.append('export '+var+'='+out+'; ')
  elif value is not None:
   if array:outputs.extend(str(x) for x in value)
   elif isinstance(value,bool):outputs.append('1' if value else '0')
   else:outputs.append(str(value))
 if assignments:print(''.join(assignments),end='')
 if outputs:print('\n'.join(outputs))
 sys.exit(1 if assignments and cfg.get('jsonfilter_empty_exit') else 0)
raise AssertionError(name)
'''

class ModeTests(unittest.TestCase):
 def test_standby_blocks_background_start_without_modifying_prefs(self):
  (self.root/'tmp/u60-standby').mkdir(parents=True);(self.root/'tmp/u60-standby/active').touch()
  r=self.run_mode('start');self.assertFalse(r['ok']);self.assertEqual(self.kills(),[]);self.assertEqual(self.starts(),[])
  r=self.run_mode('standby-resume');self.assertTrue(r['ok']);self.assertEqual(self.kills(),[])
 def setUp(self):
  # Keep unix socket path under macOS's short sockaddr_un limit.
  self.tmp=tempfile.TemporaryDirectory(prefix='tsm-',dir='/tmp');self.root=pathlib.Path(self.tmp.name)
  for path in ['data/tailscale/bin','data/u60-panel','tmp/tailscale','proc','usr/bin','bin','sys/class/net','dev/net']:(self.root/path).mkdir(parents=True,exist_ok=True)
  for path in ['data/tailscale/bin/tailscaled','usr/bin/curl','usr/bin/jsonfilter','usr/bin/flock','bin/kill']:
   p=self.root/path;p.write_text(TOOL);p.chmod(0o755)
  # The script's TEST_ROOT fast path polls at 10 ms (12 attempts ~= 120 ms).
  # A cold Python fixture can take longer just to publish fake /proc + socket,
  # causing a false rollback and a second late fixture. Use production's 250 ms
  # pacing through a test-only PATH shim; keep every readiness/rollback check.
  sleeper=self.root/'bin/sleep'
  sleeper.write_text('#!/bin/sh\n[ "$1" != 0.01 ] || set -- 0.25\nexec /bin/sleep "$@"\n');sleeper.chmod(0o755)
  self.state=self.root/'data/tailscale/tailscaled.state';self.state.write_text('FAKE EXISTING IDENTITY\n')
  (self.root/'dev/net/tun').touch()
  self.config={'prefs':{'WantRunning':True,'ExitNodeID':'','ExitNodeIP':'','AdvertiseRoutes':[],'RouteAll':False,'CorpDNS':True,'ExitNodeAllowLANAccess':True,'ShieldsUp':True,'Hostname':'preserve-me','RunSSH':False,'AdvertiseTags':[],'Persist':{'PrivateNodeKey':'fixture-secret-must-not-emit'}}}
  self.write_config();self.owner('userspace')
 def tearDown(self):self.tmp.cleanup()
 def write_config(self):(self.root/'mock.json').write_text(json.dumps(self.config))
 def owner(self,mode,pid='80001',binary=None):
  p=self.root/'proc'/pid;p.mkdir(exist_ok=True);(p/'exe').symlink_to(binary or self.root/'data/tailscale/bin/tailscaled')
  argv=[str(binary or self.root/'data/tailscale/bin/tailscaled'),'--tun='+('tailscale0' if mode=='tun' else 'userspace-networking'),'--state='+str(self.state),'--socket='+str(self.root/'tmp/tailscale/tailscaled.sock')]
  (p/'cmdline').write_bytes(b'\0'.join(v.encode() for v in argv)+b'\0');(self.root/'data/tailscale/tailscaled.pid').write_text(pid+'\n');(self.root/'current-mode').write_text(mode)
  sp=self.root/'tmp/tailscale/tailscaled.sock'
  if not sp.exists():s=socket.socket(socket.AF_UNIX);s.bind(str(sp));s.close()
  if mode=='tun':(self.root/'sys/class/net/tailscale0').mkdir(exist_ok=True)
 def run_mode(self,command):
  self.write_config();env=dict(os.environ,U60_TS_TEST_ROOT=str(self.root),PATH=str(self.root/'bin')+os.pathsep+os.environ['PATH'])
  p=subprocess.run(['sh',str(SCRIPT),command],env=env,text=True,capture_output=True,timeout=25)
  self.assertEqual(p.stderr,'');self.assertNotIn('fixture-secret',p.stdout);self.assertNotIn('preserve-me',p.stdout)
  out=json.loads(p.stdout);self.assertEqual(out['ok'],p.returncode==0);self.assertEqual(self.state.read_text(),'FAKE EXISTING IDENTITY\n');return out
 def kills(self):return (self.root/'kills').read_text().splitlines() if (self.root/'kills').exists() else []
 def starts(self):return [json.loads(x) for x in (self.root/'starts.jsonl').read_text().splitlines()] if (self.root/'starts.jsonl').exists() else []
 def evidence(self,r):return json.dumps({'reply':r,'starts':self.starts(),'kills':self.kills(),'proc':sorted(p.name for p in (self.root/'proc').iterdir())})
 def test_identical_deleted_core_retains_exact_scope(self):
  core=self.root/'data/tailscale/bin/tailscaled';old=core.with_name('tailscaled (deleted)');old.write_bytes(core.read_bytes())
  exe=self.root/'proc/80001/exe';exe.unlink();exe.symlink_to(old)
  r=self.run_mode('status');self.assertTrue(r['running']);self.assertEqual(self.starts(),[])
  old.write_text('different old binary');r=self.run_mode('status');self.assertFalse(r['running'])

 def test_migrate_tun_preserves_prefs(self):
  original=json.dumps(self.config['prefs'],sort_keys=True);r=self.run_mode('tun');self.assertTrue(r['ok']);self.assertEqual(r['mode'],'tun');self.assertTrue(r['running']);self.assertTrue(r['tun_ready']);self.assertEqual(r['configured_mode'],'tun');self.assertEqual(self.kills(),['80001']);self.assertEqual(json.dumps(self.config['prefs'],sort_keys=True),original)
  argv=self.starts()[0];self.assertIn('--port=41641',argv);self.assertIn('--no-logs-no-support',argv);self.assertFalse(any('auth' in x for x in argv));self.assertEqual(len(self.starts()),1)
 def test_real_jsonfilter_empty_arrays_exit_one(self):
  self.config['jsonfilter_empty_exit']=True;self.config['prefs']['AdvertiseRoutes']=None;self.config['prefs']['AdvertiseTags']=None
  r=self.run_mode('tun');self.assertTrue(r['ok'],self.evidence(r));self.assertEqual(r['mode'],'tun')
 def test_start_failure_restores_previous(self):
  self.config['fail_start_modes']=['tun'];r=self.run_mode('tun');self.assertFalse(r['ok']);self.assertTrue(r['rolled_back']);self.assertEqual(r['mode'],'userspace');self.assertTrue(r['running']);self.assertEqual(len(self.starts()),2);self.assertEqual(r['configured_mode'],'userspace')
 def test_stale_pid_never_killed(self):
  (self.root/'data/tailscale/tailscaled.pid').write_text('90000\n');self.owner('userspace','90000',binary='/unrelated/program');r=self.run_mode('tun');self.assertTrue(r['ok']);self.assertNotIn('90000',self.kills());self.assertTrue((self.root/'proc/90000').exists());self.assertIn('80001',self.kills())
 def test_missing_iface_rolls_back(self):
  self.config['missing_iface']=True;r=self.run_mode('tun');self.assertFalse(r['ok']);self.assertTrue(r['rolled_back']);self.assertEqual(r['mode'],'userspace')
 def test_stopped_can_configure_without_running(self):
  self.config['prefs']['WantRunning']=False;self.config['missing_iface']=True;r=self.run_mode('tun');self.assertTrue(r['ok'],self.evidence(r));self.assertFalse(r['running']);self.assertFalse(r['tun_ready']);self.assertEqual(r['backend'],'Stopped')
 def test_slow_host_fixture_start_still_uses_single_daemon(self):
  self.config['start_delay']=0.30;self.config['prefs']['WantRunning']=False;self.config['missing_iface']=True
  for command in ['tun','userspace']:
   with self.subTest(command=command):
    r=self.run_mode(command);self.assertTrue(r['ok'],self.evidence(r));self.assertFalse(r['rolled_back']);self.assertEqual(r['mode'],command);self.assertEqual(r['backend'],'Stopped');self.assertEqual(len(list((self.root/'proc').iterdir())),1)
  self.assertEqual(len(self.starts()),2)
 def test_preferences_changed_roll_back(self):
  self.config['mutate_want_mode']='tun';r=self.run_mode('tun');self.assertFalse(r['ok']);self.assertTrue(r['rolled_back']);self.assertEqual(r['mode'],'userspace')
 def test_exit_downgrade_rejected(self):
  shutil.rmtree(self.root/'proc/80001');self.owner('tun');self.config['prefs']['ExitNodeID']='chosen-exit';r=self.run_mode('userspace');self.assertFalse(r['ok']);self.assertEqual(self.kills(),[]);self.assertEqual(self.starts(),[])
 def test_advertised_subnet_downgrade_rejected(self):
  shutil.rmtree(self.root/'proc/80001');self.owner('tun');self.config['prefs']['AdvertiseRoutes']=['192.168.0.0/24'];r=self.run_mode('userspace');self.assertFalse(r['ok']);self.assertEqual(self.kills(),[])
 def test_accepted_subnet_downgrade_rejected(self):
  shutil.rmtree(self.root/'proc/80001');self.owner('tun');self.config['prefs']['RouteAll']=True;r=self.run_mode('userspace');self.assertFalse(r['ok']);self.assertEqual(self.kills(),[])
 def test_missing_route_field_rejects_downgrade(self):
  shutil.rmtree(self.root/'proc/80001');self.owner('tun');del self.config['prefs']['AdvertiseRoutes'];r=self.run_mode('userspace');self.assertFalse(r['ok']);self.assertEqual(self.kills(),[])
 def test_userspace_downgrade_when_routes_explicitly_clear(self):
  shutil.rmtree(self.root/'proc/80001');self.owner('tun');r=self.run_mode('userspace');self.assertTrue(r['ok'],self.evidence(r));self.assertEqual(r['mode'],'userspace')
 def test_graceful_stop_timeout_does_not_duplicate(self):
  self.config['refuse_stop']='80001';r=self.run_mode('tun');self.assertFalse(r['ok']);self.assertEqual(self.starts(),[]);self.assertEqual(self.kills(),['80001'])
 def test_start_uses_saved_tun(self):
  shutil.rmtree(self.root/'proc/80001');(self.root/'data/u60-panel/tailscale-mode').write_text('tun\n');r=self.run_mode('start');self.assertTrue(r['ok']);self.assertEqual(r['mode'],'tun')
 def test_status_does_not_write(self):
  r=self.run_mode('status');self.assertTrue(r['ok']);self.assertEqual(r['mode'],'userspace');self.assertEqual(self.starts(),[]);self.assertEqual(self.kills(),[]);self.assertFalse((self.root/'data/u60-panel/tailscale-mode').exists());self.assertFalse((self.root/'tmp/u60-tailscale-mode.lock').exists())
 def test_missing_tun_is_not_migrated(self):
  (self.root/'dev/net/tun').unlink();r=self.run_mode('tun');self.assertFalse(r['ok']);self.assertEqual(self.kills(),[])
 def test_missing_identity_never_created(self):
  self.state.unlink();self.write_config();p=subprocess.run(['sh',str(SCRIPT),'tun'],env=dict(os.environ,U60_TS_TEST_ROOT=str(self.root)),capture_output=True,text=True,timeout=25);r=json.loads(p.stdout);self.assertFalse(r['ok']);self.assertFalse(self.state.exists());self.assertEqual(self.kills(),[]);self.assertEqual(self.starts(),[])
 def test_lock_busy_rejects(self):
  self.config['lock_busy']=True;r=self.run_mode('tun');self.assertFalse(r['ok']);self.assertEqual(self.kills(),[])

if __name__=='__main__':unittest.main()
