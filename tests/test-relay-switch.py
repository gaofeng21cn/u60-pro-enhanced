#!/usr/bin/env python3
"""Exercise real relay control sockets and lifecycle with synthetic credentials."""
import json, os, pathlib, socket, subprocess, tempfile, threading, time
ROOT=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='relay-') as td:
 d=pathlib.Path(td);(d/'ctrl').mkdir();(d/'private').mkdir()
 log=d/'calls'; enabled=d/'private/enabled'; helper=d/'helper'
 helper.write_text(f'''#!/bin/sh
printf '%s\\n' "$1" >> '{log}'
case "$1" in
 off) rm -f '{enabled}';;
 on|enable) touch '{enabled}';;
 prepare) [ ! -f '{d}/fail-prepare' ] || exit 1;;
esac
''');helper.chmod(0o700)
 binary=d/'relay'
 subprocess.run(['cc','-O1','-I',str(ROOT/'panel/vendor'),f'-DRUN="{d}"',f'-DPRIVATE="{d}/private"',f'-DRELAY_HELPER="{helper}"','-DCONNECT_TRIES=4',str(ROOT/'panel/panel-relay.c'),str(ROOT/'panel/vendor/cJSON.c'),'-o',str(binary)],check=True,timeout=40)
 sock=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM);sock.bind(str(d/'ctrl/u60sta'));sock.settimeout(.1)
 requests=[]
 state={'stop':False,'completed':True,'scan_done':False,'scan_fail':False,'saved':False,'frequency':5220,'queries_before_done':0}
 def serve():
  while not state['stop']:
   try: raw,addr=sock.recvfrom(8192)
   except socket.timeout:continue
   cmd=raw.decode();requests.append(cmd if 'psk ' not in cmd else 'SET_PSK');response='OK\n'
   if cmd.startswith('GET_NETWORK '):
    k=cmd.split()[-1];response={'ssid':'\"Synthetic upstream\"\n','key_mgmt':'WPA-PSK\n','freq_list':'FAIL\n' if state.get('legacy') else '2437\n','bssid':state.get('pin','00:11:22:33:44:55')+'\n'}.get(k,'FAIL\n')
   elif cmd=='LIST_NETWORKS':response='network id / ssid / bssid / flags\n0\tSynthetic upstream\tany\t[CURRENT]\n'
   elif cmd=='SCAN':
    if state['scan_fail']:response='FAIL\n'
    else:
     state['scan_done']=False
     sock.sendto(b'<3>CTRL-EVENT-SCAN-RESULTS\n',addr)  # stale event before acknowledgement
     sock.sendto(b'OK\n',addr)
     def finish(target=addr):
      state['scan_done']=True
      try:sock.sendto(b'<3>CTRL-EVENT-SCAN-RESULTS\n',target)
      except OSError:pass
     threading.Timer(.2,finish).start()
     continue
   elif cmd=='SCAN_RESULTS':
    if not state['scan_done']:state['queries_before_done']+=1
    response='bssid / frequency / signal level / flags / ssid\n00:11:22:33:44:55\t2412\t-40\t[WPA2-PSK-CCMP][ESS]\tFresh upstream\n'+state.get('candidates','')
   elif cmd=='ADD_NETWORK':response='0\n'
   elif cmd.startswith('SET_NETWORK ') and ' freq_list ' in cmd:state['frequency']=int(cmd.split()[-1])
   elif cmd=='STATUS':
    pending=state.get('association_pending',0);state['association_pending']=max(0,pending-1)
    response='wpa_state='+('COMPLETED' if state['completed'] and not pending else 'ASSOCIATING')+'\nfreq='+str(state['frequency'])+'\n'
   elif cmd=='SAVE_CONFIG':state['saved']=True
   try:sock.sendto(response.encode(),addr)
   except OSError:pass
 thread=threading.Thread(target=serve);thread.start()
 def run(action,args=None):
  p=subprocess.run([str(binary),action],input=json.dumps(args or {}),capture_output=True,text=True,timeout=18)
  return json.loads(p.stdout)
 def reset():
  enabled.touch();log.write_text('');state.update(saved=False,completed=True,scan_done=False,scan_fail=False,queries_before_done=0)
 def calls():return log.read_text().splitlines()
 try:
  reset();r=run('scan');assert r['ok'] and r['networks'][0]['ssid']=='Fresh upstream'
  assert calls()==['prepare','scan-stop'] and enabled.exists() and state['queries_before_done']==0 and not state['saved']
  reset();state['scan_fail']=True;assert not run('scan')['ok'];assert enabled.exists() and 'off' not in calls()
  args={'ssid':'Synthetic upstream','password':'synthetic-test-only','security':'WPA2','bssid':'00:11:22:33:44:55','frequency':5220}
  reset();assert not run('connect',dict(args,password='short'))['ok'];assert not calls() and enabled.exists()
  reset();assert run('connect',args)['ok'];assert calls()==['pause','prepare','align','enable'] and state['saved'] and enabled.exists()
  reset();state['completed']=False;r=run('connect',args);assert not r['ok'];assert calls()==['pause','prepare','align','pause','on'] and not state['saved'] and enabled.exists()
  reset();(d/'fail-prepare').touch();assert not run('connect',args)['ok'];assert calls()==['pause','prepare','pause','on'] and not state['saved'] and enabled.exists()
  (d/'fail-prepare').unlink();reset();requests.clear()
  state['candidates']='00:11:22:33:44:56\t5220\t-25\t[WPA2-PSK-CCMP][ESS]\tSynthetic upstream\n'
  assert not run('coordinate')['ok'];assert not calls() and not state['saved']
  state['candidates']+='00:11:22:33:44:57\t2412\t-45\t[WPA2-PSK-CCMP][ESS]\tSynthetic upstream\n00:11:22:33:44:58\t2437\t-65\t[WPA2-PSK-CCMP][ESS]\tSynthetic upstream\n'
  state['association_pending']=2;requests.clear()
  assert run('coordinate')['ok'];assert requests.count('STATUS')>=3 and requests.count('SELECT_NETWORK 0')==1;assert (d/'private/upstream-band').read_text().strip()=='2';assert calls()==['align'] and 'SET_NETWORK 0 bssid 00:11:22:33:44:57' in requests and 'SET_NETWORK 0 freq_list 2412' in requests and not state['saved']
  assert not any(x.startswith('GET_NETWORK') and x.endswith('psk') for x in requests)
  state['legacy']=True;state['pin']='00:11:22:33:44:58';(d/'private/upstream-band').unlink();requests.clear()
  assert run('coordinate')['ok'];assert (d/'private/upstream-band').read_text().strip()=='2'
  state['pin']='any';requests.clear();assert run('coordinate')['ok'];assert 'SET_NETWORK 0 freq_list 2412' in requests
  enabled.unlink();assert not run('coordinate')['ok']
  print('PASS: fresh scan completion, active relay retained, invalid input inert, switching and failed-switch recovery')
 finally:
  state['stop']=True;thread.join(timeout=2);sock.close()
