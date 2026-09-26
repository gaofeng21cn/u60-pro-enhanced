#!/usr/bin/env python3
import pathlib,subprocess,tempfile,json
R=pathlib.Path(__file__).parents[1]
with tempfile.TemporaryDirectory() as td:
 d=pathlib.Path(td);binary=d/'control'
 subprocess.run(['cc','-O1','-Wno-deprecated-declarations','-I',str(R/'panel/vendor'),str(R/'panel/panel-control.c'),str(R/'panel/vendor/cJSON.c'),'-lm','-o',str(binary)],check=True)
 # Synthetic public fixture; never read a captured device snapshot.
 data={'zwrt_wlan.status':{'app_status':'idle','driver_status':'idle','pending':0},
       'wifi.zte_mbb':{'wifi_onoff':'1','lbd':'0'},
       'wifi.main_2g':{'ssid':'Demo','disabled':'0'},
       'wifi.main_5g':{'ssid':'Demo_5G','disabled':'0'},
       'usb.role.status':{'ok':True,'requested':'LAN','state':'NO_ADAPTER','badge':''}}
 data['relay.status']={'ok':True,'enabled':False,'active':False,'saved':False,'state':'OFF','frequency':0}
 data['wifi_runtime']={'ok':True,'enabled':False,'enabled_5g':True,'available':True,'active':True,'power_configured':True,'state_2g':'DISABLED','state_5g':'ENABLED'}
 data['relay.scan']={'ok':True,'networks':[{'ssid':'Test WiFi','bssid':'aa:bb:cc:dd:ee:ff','security':'WPA2','signal':-40,'frequency':5220},{'ssid':'Test WiFi','bssid':'aa:bb:cc:dd:ee:fd','security':'WPA2','signal':-80,'frequency':5180},{'ssid':'Enterprise','bssid':'aa:bb:cc:dd:ee:fe','security':'unsupported','frequency':2412}]}
 f=d/'fixture.json'
 def call(action,args={}):
  f.write_text(json.dumps(data));p=subprocess.run([str(binary),'--fixture',str(f)],input=json.dumps({'action':action,'args':args}),text=True,capture_output=True,check=True);return json.loads(p.stdout)
 r=call('wifi.relay.scan');assert len(r['picker']['choices'])==1;args=r['picker']['choices'][0]['args'];assert args['bssid']=='aa:bb:cc:dd:ee:ff' and args['frequency']==5220;r=call('wifi.relay.select',args)
 assert r['picker']['confirm'] is False and r['picker']['fields'][0]['minLength']==8 and r['picker']['fields'][0]['maxLength']==63
 assert r['picker']['action']=='wifi.relay.connect' and r['picker']['fields'][0]['kind']=='password' and r['picker']['fields'][0]['value']==''
 assert not call('wifi.relay.select',dict(args,security='unsupported'))['ok']
 data['relay.scan']['networks'].reverse();r=call('wifi.relay.scan');assert len(r['picker']['choices'])==1 and r['picker']['choices'][0]['args']['bssid']=='aa:bb:cc:dd:ee:ff'
 data['relay.scan']['networks'].append({'ssid':'Test WiFi','bssid':'aa:bb:cc:dd:ee:fc','security':'WPA2','signal':-30,'frequency':2412})
 r=call('wifi.relay.scan');assert len(r['picker']['choices'])==2 and r['picker']['choices'][0]['label'].startswith('2.4G') and r['picker']['choices'][1]['label'].startswith('5G')
 assert not call('wifi.relay.select',dict(args,frequency=5260))['ok']
 assert not call('wifi.relay.select',dict(args,frequency=2413))['ok']
 assert '信道' in r['picker']['choices'][0]['description']
 assert '5G 1' in r['picker']['description']
 r=call('state');s=next(x for x in r['sections']if x['id']=='wifi');assert s['items'][0]['id']=='relay' and s['items'][1]['id']=='relay-scan';assert s['items'][0]['type']=='info'
 data['relay.status'].update(enabled=True,active=True,saved=True,state='CONNECTED',frequency=2437);r=call('state');s=next(x for x in r['sections']if x['id']=='wifi');assert s['items'][0]['action']=='wifi.relay.off' and s['items'][0]['label']=='停止 Wi-Fi 中继' and s['items'][0]['confirm'];assert s['items'][1]['id']=='relay-scan' and s['items'][1]['enabled'] and s['items'][1]['confirm'] is False;assert call('wifi.relay.scan')['ok'];assert r['data']['wifi_status']=='2.4G 上游中继'
 assert 'password' not in json.dumps(r['data']['wifi_relay'])
 assert call('wifi.ap',{'section':'main_2g','enabled':True})['ok']
 assert not call('usb.role',{'role':'AUTO'})['ok']
 assert not next(x for x in next(z for z in r['sections']if z['id']=='usb')['items']if x['id']=='role')['enabled']
 for key,value in [('fallback','wifi-only'),('fallback','cellular'),('autostart','0'),('autostart','1')]:assert call('wifi.relay.policy',{'key':key,'value':value})['ok']
 assert not call('wifi.relay.policy',{'key':'fallback','value':'anything'})['ok']
 assert not call('wifi.relay.forget')['ok']
 data['relay.status'].update(enabled=False,saved=True,health='NO_LINK',autostart=0,fallback='wifi-only');r=call('state');rows=next(x for x in r['sections'] if x['id']=='wifi')['items']
 assert next(x for x in rows if x['id']=='relay-autostart')['value']=='关闭'
 assert next(x for x in rows if x['id']=='relay-fallback')['value']=='禁止蜂窝回退'
 assert next(x for x in rows if x['id']=='relay-forget')['enabled']
 assert call('wifi.relay.forget')['ok']
 # Saved fallback intent must not be reported as proven cellular connectivity.
 for link,label in [('SCANNING','正在寻找上游'),('4WAY_HANDSHAKE','正在验证上游密码'),('ASSOCIATING','正在连接上游'),('COMPLETED','Wi-Fi 已连接 · 等待路由')]:
  data['relay.status'].update(enabled=True,active=False,state='CONNECTING',link_state=link)
  r=call('state');row=next(x for x in next(s for s in r['sections'] if s['id']=='wifi')['items'] if x['id']=='relay')
  assert row['value']==label and row['action']=='wifi.relay.off'
 for policy in ['wifi-only','cellular']:
  data['relay.status'].update(link_state='DISCONNECTED',fallback=policy)
  r=call('state');rows=next(s['items'] for s in r['sections'] if s['id']=='wifi')
  assert next(x for x in rows if x['id']=='relay')['value']==('等待上游 · 禁止蜂窝回退' if policy=='wifi-only' else '等待上游 · 允许蜂窝回退')
 data['relay.status'].update(active=True,state='CONNECTED',health='ONLINE',health_age_seconds=12)
 r=call('state');rows=next(s['items'] for s in r['sections'] if s['id']=='wifi')
 health=next(x for x in rows if x['id']=='relay-health')
 assert 'HTTP 探测通过' in health['value'] and '12 秒前' in health['value']
 assert '不证明 HTTPS' in health['reason']
 data['relay.profiles']={'ok':True,'networks':[{'id':'44656d6f-WPA2-2','ssid':'Demo','security':'WPA2','band':2,'priority':3}]}
 r=call('wifi.relay.profiles');assert r['picker']['choices'][0]['args']['id']=='44656d6f-WPA2-2'
 r=call('wifi.relay.profile',{'id':'44656d6f-WPA2-2'});assert len(r['picker']['choices'])==2 and r['picker']['confirm']
 data['relay.status']['enabled']=False
 r=call('wifi.relay.profile',{'id':'44656d6f-WPA2-2'});assert len(r['picker']['choices'])==3
 assert not call('wifi.relay.profile',{'id':'missing'})['ok']
 for op in ['connect','prefer','forget']:assert call('wifi.relay.manage',{'id':'44656d6f-WPA2-2','operation':op})['ok']
 assert not call('wifi.relay.manage',{'id':'44656d6f-WPA2-2','operation':'exec'})['ok']
 data['relay.scan']['networks']=[dict(args,frequency=5260,signal=-40)]
 r=call('wifi.relay.scan');assert not r['ok'] and 'DFS' in r['message'] and '36' in r['message']
 data['relay.scan']['networks']=[]
 assert '未发现网络' in call('wifi.relay.scan')['message']
 print('PASS: relay scan-picker-password flow, unsupported networks excluded, active/off entry, home status and no secret readback')
