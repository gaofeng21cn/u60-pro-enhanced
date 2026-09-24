#!/usr/bin/env python3
"""Host-only black-box validation; no adb and no device writes."""
import json, pathlib, subprocess, tempfile, copy
ROOT=pathlib.Path(__file__).resolve().parents[1]
BASE={
 'tun_ready':False,
 'wifi_idle_minutes':'10',
 'zwrt_wlan.status':{'app_status':'idle','driver_status':'idle','pending':0},
 'wifi.zte_mbb':{'wifi_onoff':'1','lbd':'0'},
 'wifi.main_2g':{'ssid':'Demo','disabled':'0','encryption':'sae-mixed','secret':'SHOULD_NEVER_APPEAR'},
 'wifi.main_5g':{'ssid':'Demo_5G','disabled':'0','encryption':'sae-mixed','key':'SHOULD_NEVER_APPEAR'},
 'wifi.wifi0':{'channel':'0','channellist':'1,6,11'},
 'wifi.wifi1':{'channel':'36','channellist':'36,40,44,48'},
 'ts.prefs':{'WantRunning':True,'RouteAll':False,'CorpDNS':False,'ExitNodeAllowLANAccess':False,'ExitNodeID':'','ExitNodeIP':'','AdvertiseRoutes':['10.10.0.0/16'],'Persist':{'PrivateNodeKey':'SHOULD_NEVER_APPEAR'}},
 'ts.status':{'BackendState':'Running','TailscaleIPs':['100.100.1.1'],'Peer':{'opaque-key':{'ID':'peer-exit','HostName':'Exit Demo','Online':True,'ExitNodeOption':True,'TailscaleIPs':['100.100.2.2'],'PublicKey':'SHOULD_NEVER_APPEAR'}}},
 'zwrt_router.api.router_get_dhcp_router':{'lan_addr':'192.168.8.1','lan_netmask':'255.255.255.0'},
 'zwrt_router.api.router_get_wan_mode_para':{'opms_wan_mode':'PPP','password':'SHOULD_NEVER_APPEAR'},
 'usb.role.status':{'ok':True,'requested':'AUTO','state':'WAIT_ADAPTER','badge':'','message':'未接网卡'},
 'zwrt_bsp.usb.list':{'mode':'debug','connect':1,'usb2rj45':0},
 'zwrt_bsp.battery.list':{'battery_capacity':100},
 'zwrt_bsp.thermal.get_cpu_temp':{'cpuss_temp':43},
 'zte_nwinfo_api.nwinfo_get_netinfo':{'network_type':'SA','network_provider_fullname':'Demo','wan_active_band':'n78','signalbar':'5'},
 'zwrt_router.api.router_get_user_list_num':{'access_total_num':2},
 'zwrt_data.get_wwandst':{'day_rx_bytes':100,'day_tx_bytes':20,'month_rx_bytes':500,'month_tx_bytes':60,'real_rx_speed':150,'real_tx_speed':20},
}
count=0
with tempfile.TemporaryDirectory(prefix='u60-control-test-') as tmp:
 tmp=pathlib.Path(tmp);binary=tmp/'control'
 subprocess.run(['cc','-O1','-g','-Wall','-Wextra','-I',str(ROOT/'panel/vendor'),str(ROOT/'panel/panel-control.c'),str(ROOT/'panel/vendor/cJSON.c'),'-lm','-o',str(binary)],check=True)
 def call(action=None,args=None,patch=None,raw=None):
  global count
  fixture=copy.deepcopy(BASE)
  if patch:fixture.update(patch)
  f=tmp/'fixture.json';f.write_text(json.dumps(fixture))
  p=subprocess.run([str(binary),'--fixture',str(f)],input=raw if raw is not None else json.dumps({'action':action,'args':args or {}}),text=True,capture_output=True,timeout=8)
  assert p.returncode==0 and p.stderr=='',p.stderr
  assert 'SHOULD_NEVER_APPEAR' not in p.stdout
  count+=1
  return json.loads(p.stdout)
 r=call('state',patch={'zwrt_data.get_wwandst':{'day_rx_bytes':1250000000,'day_tx_bytes':0,'month_rx_bytes':72500000000,'month_tx_bytes':0}})
 cell=next(s['items'] for s in r['sections'] if s['id']=='cell')
 assert next(i['value'] for i in cell if i['id']=='today')=='1.25 GB'
 assert next(i['value'] for i in cell if i['id']=='month')=='72.50 GB'
 # Peer tests are bounded to current online peers and never mutate prefs.
 for tun,ip_ok in [(True,True),(True,False),(False,False)]:
  r=call('tailscale.peer_test',{'ip':'100.100.2.2'},{'tun_ready':tun,'peer_tunnel_ok':True,'peer_ip_ok':ip_ok})
  assert r['tunnel_ok'] and r['ip_ok']==ip_ok and r['fixture_write_count']==0
 for ip in ['8.8.8.8','100.65.0.1','100.100.2.2;reboot','::1']:
  r=call('tailscale.peer_test',{'ip':ip});assert not r['ok'] and r['fixture_write_count']==0
 for tun in [False,True]:
  r=call('tailscale.lan_gateway',{'enabled':True},{'tun_ready':tun});assert r['ok']==tun and r['fixture_write_count']==int(tun)
 r=call('network.tailscale_mode',{'mode':'userspace'},{'tun_ready':True,'lan_gateway':True});assert not r['ok'] and r['fixture_write_count']==0
 # Password viewing is explicit, read-only and independent of setter validation.
 for band in ['main_2g','main_5g']:
  for password in ['example-pass', 'x'*63]:
   r=call('wifi.show_password',{'section':band},{'wifi_password_read':{'ok':True,'password':password}})
   assert r['ok'] and r['fixture_write_count']==0 and r['report']['lines'][-1]==password
 r=call('wifi.show_password',{'section':'invalid'});assert not r['ok'] and r['fixture_write_count']==0
 r=call('wifi.show_password',{'section':'main_2g'});assert not r['ok'] and r['fixture_write_count']==0
 r=call('state',patch={'wifi_password_read':{'ok':True,'password':'PRIVATE-VIEW-ONLY'}});assert 'PRIVATE-VIEW-ONLY' not in json.dumps(r)
 s=call('state');assert s['ok'];assert s['data']['today_bytes']==120 and s['data']['month_bytes']==560
 wifi=next(sec['items'] for sec in s['sections'] if sec['id']=='wifi')
 assert [i['id'] for i in wifi[:4]]==['power','main_2g.enabled','main_5g.enabled','sleep']
 assert wifi[1]['action']=='wifi.ap' and wifi[1]['args']=={'enabled':False,'section':'main_2g'}
 runtime={'ok':True,'available':True,'enabled':False,'enabled_5g':False,'active':False,'power_configured':True,'busy':False,'managed_off':True,'managed_off_5g':False,'state_2g':'MISSING','state_5g':'MISSING'}
 missing=call('state',patch={'wifi_runtime':runtime});mi=next(s['items'] for s in missing['sections'] if s['id']=='wifi')
 assert missing['data']['wifi']['enabled']=='0' and '未运行' in missing['data']['wifi_status']
 for it in mi[:3]:assert it['enabled'] and it['args']['enabled'] is True,(it['id'],it)
 for action,args,command in [('wifi.power',{'enabled':True},'power-on'),('wifi.power',{'enabled':False},'power-off'),('wifi.ap',{'section':'main_2g','enabled':True},'on'),('wifi.ap',{'section':'main_2g','enabled':False},'off'),('wifi.ap',{'section':'main_5g','enabled':True},'on-5g'),('wifi.ap',{'section':'main_5g','enabled':False},'off-5g')]:
  r=call(action,args,{'wifi_runtime':runtime});assert r['ok'] and r['fixture_command']==command and r['fixture_write_count']==1
  r=call(action,args,{'wifi_runtime':runtime,'wifi_helper_reply':{'ok':False,'message':'actual AP never started'}});assert not r['ok']
 for args in [{'section':'main_2g','enabled':'false'},{'section':'other','enabled':True}]:
  r=call('wifi.ap',args,{'wifi_runtime':runtime});assert not r['ok'] and r['fixture_write_count']==0
 running=dict(runtime,enabled_5g=True,active=True,state_5g='ENABLED')
 active=call('state',patch={'wifi_runtime':running});ai=next(s['items'] for s in active['sections'] if s['id']=='wifi')
 assert active['data']['wifi_status']=='仅 5G' and ai[0]['args']['enabled'] is False and ai[1]['args']['enabled'] is True and ai[2]['args']['enabled'] is False
 checking=call('state',patch={'wifi_runtime':dict(running,enabled_5g=False,state_5g='DFS',busy=True)})
 ci=next(s['items'] for s in checking['sections'] if s['id']=='wifi');assert checking['data']['wifi_status']=='切换中' and ci[2]['value']=='正在启动'
 assert call('wifi.ap',{'section':'main_2g','enabled':False})['ok']
 assert call('wifi.ap',{'section':'main_2g','enabled':True},{'wifi.main_2g':{'disabled':'1'}})['ok']
 for status in [{'app_status':'reconfig','driver_status':'idle','pending':0},{'app_status':'idle','driver_status':'idle','pending':1},{}]:
  r=call('wifi.ap',{'section':'main_2g','enabled':False},{'zwrt_wlan.status':status});assert not r['ok'] and r['fixture_write_count']==0
 for role in ['AUTO','LAN']:
  r=call('usb.role',{'role':role});assert r['ok'] and r['fixture_write_count']==1
  r=call('usb.role',{'role':role},{'reject_writes':True});assert not r['ok']
 for action in ['usb.macnet.enable','usb.macnet.restore']:
  r=call(action,{});assert not r['ok'] and r['fixture_write_count']==0
 for host in [
  {'ok':True,'mode':'rndis','bound':True,'configured':True,'carrier':False},
  {'ok':True,'mode':'ecm','bound':True,'configured':True,'carrier':True,'bridged':True},
  {'ok':True,'mode':'ecm','bound':False},
  {'ok':False,'mode':'unknown'}]:
  r=call('state',patch={'usb.macnet.status':host})
  items=next(x['items'] for x in r['sections'] if x['id']=='usb')
  assert r['fixture_write_count']==0
  assert all(not x['enabled'] for x in items if x['id'].startswith('macnet.'))
  if host.get('bridged'):assert r['data']['usb_status']=='USB 内网链路已连接'
  if not host['ok']:assert r['data']['usb_status']=='读取失败'
 for badge,state in [('WAN','WAN'),('LAN','LAN'),('WAIT','RESTORING'),('ERROR','CONFLICT'),('','WAIT_ADAPTER')]:
  r=call('state',patch={'usb.role.status':{'ok':True,'requested':'AUTO','state':state,'badge':badge,'message':'状态样例'}})
  assert r['data']['usb']['badge']==badge and r['data']['usb']['state']==state
  sec=next(x for x in r['sections'] if x['id']=='usb')
  assert sec['items'][0]['enabled'] and all(not i['enabled'] for i in sec['items'][1:])
 assert {x['id'] for x in s['sections']}=={'wifi','usb','tailscale','cell','system','router'}
 for sec in s['sections']:
  for item in sec['items']:
   for f in item.get('fields',[]):
    if f['kind']=='password':assert f['value']==''
 (tmp/'control-state-fixture.json').write_text(json.dumps(s,ensure_ascii=False,indent=2)+'\n')
 for a,args in [('wifi.power',{'enabled':False}),('wifi.ap',{'section':'main_5g','enabled':False}),('wifi.ssid',{'section':'main_2g','ssid':'New Name'}),('wifi.channel',{'section':'wifi0','channel':'6'}),('tailscale.connected',{'enabled':False}),('tailscale.accept_dns',{'enabled':True}),('tailscale.accept_routes',{'enabled':True}),('tailscale.allow_lan',{'enabled':True})]:
  assert call(a,args,{'tun_ready':True})['ok'],a
  r=call(a,args,{'stale_readback':True,'tun_ready':True});assert not r['ok'] and r['fixture_write_count']==1,(a,r)
  r=call(a,args,{'reject_writes':True,'tun_ready':True});assert not r['ok'] and r['fixture_write_count']==1,(a,r)
 for a,args in [('wifi.power',{'enabled':'false'}),('wifi.ssid',{'section':'main_2g','ssid':'a'*33}),('wifi.ssid',{'section':'main_2g','ssid':'x\ny'}),('wifi.ssid',{'section':'../../etc','ssid':'x'}),('wifi.channel',{'section':'wifi0','channel':'44'}),('wifi.channel',{'section':'wifi0','channel':'6;reboot'}),('tailscale.exit',{'id':'unknown'}),('tailscale.offer_exit',{'enabled':True}),('tailscale.advertise_lan',{'enabled':True}),('usb.role',{'role':'WAN'}),('wifi.password',{'section':'main_2g','password':'not-retained'}),('internet.profile',{'profile':'clash'}),('arbitrary',{}),('tailscale.accept_routes',{'enabled':True})]:
  r=call(a,args);assert not r['ok'] and r['fixture_write_count']==0,(a,r)
 for a,args in [('tailscale.offer_exit',{'enabled':True}),('tailscale.advertise_lan',{'enabled':True}),('tailscale.exit',{'id':'peer-exit'})]:
  r=call(a,args,{'tun_ready':True});assert r['ok'],(a,r)
 for patch,success,writes in [({'crypto_ready':True},True,1),({'crypto_ready':True,'stale_readback':True},False,1),({'crypto_ready':True,'reject_writes':True},False,1),({'crypto_ready':False},False,0)]:
  r=call('wifi.password',{'section':'main_2g','password':'demo-valid-pass'},patch);assert r['ok']==success and r['fixture_write_count']==writes,r
 for password in ['short','x'*64,'bad\npassword','中文密码内容测试']:
  r=call('wifi.password',{'section':'main_2g','password':password},{'crypto_ready':True});assert not r['ok'] and r['fixture_write_count']==0
 r=call('tailscale.exit',{'id':'peer-exit'},{'tun_ready':True,'profile_failure':True});assert not r['ok'] and r['fixture_write_count']==3 and '已恢复' in r['message'],r
 for target in ['clash','direct']:
  r=call('network.profile',{'profile':target});assert r['ok'] and r['fixture_write_count']==1
 for profile in ['clash','direct']:
  r=call('tailscale.exit',{'id':''},{'profile':profile});assert r['ok'] and r['profile']==profile,r
 r=call('state');router=next(s for s in r['sections'] if s['id']=='router');assert next(i for i in router['items'] if i['id']=='profile')['type']=='info'
 ts=next(s for s in r['sections'] if s['id']=='tailscale');assert not next(i for i in ts['items'] if i['id']=='exit')['enabled']
 r=call('network.profile',{'profile':'bad;command'});assert not r['ok'] and r['fixture_write_count']==0
 for mode in ['tun','userspace']:
  r=call('network.tailscale_mode',{'mode':mode});assert r['ok'] and r['fixture_write_count']==1
 r=call('network.tailscale_mode',{'mode':'bad;command'});assert not r['ok'] and r['fixture_write_count']==0
 r=call('network.tailscale_mode',{'mode':'tun'},{'mode_failure':True});assert not r['ok'] and r['rolled_back']
 r=call('state',patch={'zwrt_data.get_wwandst':{}});assert r['data']['today_bytes'] is None
 # Sleep timer is independent from radio reconfiguration and proxy profiles.
 for timeout in ['-1','5','10','20','30','60','120']:
  r=call('wifi.sleep',{'minutes':timeout});assert r['ok'],(timeout,r)
  assert r['fixture_write_count']==(0 if timeout=='10' else 1),r
 for value in ['', '0','1','1440','-2','10;reboot',10,True]:
  r=call('wifi.sleep',{'minutes':value});assert not r['ok'] and r['fixture_write_count']==0,r
 for patch in [{'stale_readback':True},{'reject_writes':True}]:
  r=call('wifi.sleep',{'minutes':'-1'},patch);assert not r['ok'] and r['fixture_write_count']==1,r
 r=call('wifi.sleep',{'minutes':'-1'},{'wifi_idle_minutes':''});assert not r['ok'] and r['fixture_write_count']==0
 s=call('state',patch={'wifi_idle_minutes':'-1'});assert s['data']['wifi']['idle_minutes']=='-1'
 sleep_item=next(x for sec in s['sections'] if sec['id']=='wifi' for x in sec['items'] if x['id']=='sleep')
 assert sleep_item['value']=='永不休眠' and sleep_item['enabled']
 assert sleep_item['choices'][0]['args']=={'minutes':'-1'}
 for raw in ['{}','{','[]','{"action":"state"} trailing','x'*16385]:assert not call(raw=raw)['ok']
 print(f'PASS {count} host-only backend cases; sanitized schema fixture generated')
