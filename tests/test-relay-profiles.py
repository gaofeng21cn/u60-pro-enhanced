#!/usr/bin/env python3
"""Real native profile store, atomic edits and multi-network selection sockets."""
import json,os,pathlib,socket,subprocess,tempfile,threading,time,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class Profiles(unittest.TestCase):
 def test_store_and_selection(self):
  with tempfile.TemporaryDirectory(prefix='profiles-') as td:
   d=pathlib.Path(td);private=d/'private';private.mkdir();(d/'ctrl').mkdir()
   helper=d/'helper';helper.write_text('#!/bin/sh\nexit 0\n');helper.chmod(0o700)
   binary=d/'relay'
   subprocess.run(['cc','-O1','-I',str(ROOT/'panel/vendor'),f'-DRUN="{d}"',f'-DPRIVATE="{private}"',f'-DRELAY_HELPER="{helper}"','-DCONNECT_TRIES=1',str(ROOT/'panel/panel-relay.c'),str(ROOT/'panel/vendor/cJSON.c'),'-o',str(binary)],check=True)
   def config():return 'ctrl_interface='+str(d/'ctrl')+'\nupdate_config=1\nnetwork={\n ssid="Home"\n psk="synthetic-secret-home"\n key_mgmt=WPA-PSK\n freq_list=2412\n priority=2\n}\nnetwork={\n ssid="Office"\n psk="synthetic-secret-office"\n key_mgmt=WPA-PSK\n freq_list=5220\n priority=1\n}\n'
   conf=private/'wpa.conf';conf.write_text(config());conf.chmod(0o600)
   def run(action,args=None):
    p=subprocess.run([str(binary),action],input=json.dumps(args or {}),text=True,capture_output=True,timeout=8)
    self.assertNotIn('synthetic-secret',p.stdout+p.stderr)
    return json.loads(p.stdout)
   networks=run('profiles')['networks'];home,office=networks;self.assertEqual(len(networks),2)
   before=conf.read_bytes();self.assertFalse(run('profile-prefer',{'id':'../../bad'})['ok']);self.assertEqual(conf.read_bytes(),before)
   self.assertTrue(run('profile-prefer',{'id':office['id']})['ok']);self.assertIn('priority=1000',conf.read_text())
   self.assertIn('synthetic-secret-home',conf.read_text());self.assertEqual(conf.stat().st_mode&0o777,0o600)
   (private/'enabled').touch();self.assertFalse(run('profile-forget',{'id':home['id']})['ok'])
   sock=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM);sock.bind(str(d/'ctrl/u60sta'));sock.settimeout(.1)
   state={'stop':False,'id':-1,'connected':False,'freq':0,'fail':set(),'selects':[]}
   def serve():
    while not state['stop']:
     try:raw,addr=sock.recvfrom(8192)
     except socket.timeout:continue
     cmd=raw.decode();response='OK\n'
     if cmd=='STATUS':response=f"wpa_state={'COMPLETED' if state['connected'] else 'DISCONNECTED'}\nfreq={state['freq']}\nid={state['id']}\n"
     elif cmd=='LIST_NETWORKS':response='network id / ssid / bssid / flags\n0\tHome\tany\t\n1\tOffice\tany\t\n'
     elif cmd.startswith('GET_NETWORK '):
      _,n,key=cmd.split();n=int(n);response={'ssid':['"Home"','"Office"'][n],'key_mgmt':'WPA-PSK','freq_list':['2412','5220'][n],'priority':'0'}.get(key,'FAIL')+'\n'
     elif cmd=='SCAN':
      sock.sendto(b'OK\n',addr)
      sock.sendto(b'<3>CTRL-EVENT-SCAN-RESULTS\n',addr);continue
     elif cmd=='SCAN_RESULTS':response='bssid / frequency / signal level / flags / ssid\n00:11:22:33:44:55\t2412\t-30\t[WPA2-PSK-CCMP][ESS]\tHome\n00:11:22:33:44:56\t5220\t-70\t[WPA2-PSK-CCMP][ESS]\tOffice\n'
     elif cmd.startswith('SELECT_NETWORK '):
      state['id']=int(cmd.split()[1]);state['selects'].append(state['id']);state['connected']=state['id'] not in state['fail'];state['freq']=[2412,5220][state['id']]
     elif cmd=='DISCONNECT':state['connected']=False
     try:sock.sendto(response.encode(),addr)
     except OSError:pass
   thread=threading.Thread(target=serve);thread.start()
   try:
    self.assertTrue(run('coordinate')['ok']);self.assertEqual(state['selects'],[1])
    self.assertTrue(run('coordinate')['ok']);self.assertEqual(state['selects'],[1],'healthy network must not be preempted')
    state['connected']=False;state['fail']={1}
    self.assertFalse(run('coordinate')['ok']);self.assertEqual(state['selects'][-1],1)
    retry=next(d.glob('retry-*'));retry.write_text(str(int(time.monotonic())-120))
    self.assertTrue(run('coordinate')['ok']);self.assertEqual(state['selects'][-1],0,'even an old failure stays excluded until every visible candidate has a turn')
    self.assertEqual(len(run('profiles')['networks']),2)
   finally:state['stop']=True;thread.join(2);sock.close()
   (private/'enabled').unlink();self.assertTrue(run('profile-forget',{'id':home['id']})['ok']);left=run('profiles')['networks'];self.assertEqual(len(left),1);self.assertEqual(left[0]['ssid'],'Office');self.assertNotIn('synthetic-secret-home',conf.read_text())
   self.assertTrue(run('profile-forget',{'id':office['id']})['ok']);self.assertEqual(run('profiles')['networks'],[])
if __name__=='__main__':unittest.main()
