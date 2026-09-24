#!/usr/bin/env python3
"""Local loopback API + disposable configs. Never invokes device commands."""
import http.server,json,pathlib,subprocess,tempfile,threading,unittest,urllib.parse,os
ROOT=pathlib.Path(__file__).resolve().parents[1]
class API(http.server.BaseHTTPRequestHandler):
 mode='rule';selected='香港 01';reject=False;calls=[];proxies=None;rules=None;provider_nodes=[]
 def log_message(self,*a):pass
 def respond(self,data,status=200):
  b=json.dumps(data).encode();self.send_response(status);self.send_header('Content-Length',str(len(b)));self.end_headers();self.wfile.write(b)
 def do_GET(self):
  path=urllib.parse.unquote(self.path);API.calls.append(('GET',path))
  p={'type':'Selector','now':API.selected,'all':['香港 01','日本 02','DIRECT']}
  if path=='/configs':self.respond({'mode':API.mode})
  elif path=='/proxies':self.respond({'proxies':API.proxies if API.proxies is not None else {'默认代理':p,'香港 01':{'type':'Shadowsocks'}}})
  elif path=='/rules':self.respond({'rules':API.rules}) if API.rules is not None else self.respond({},503)
  elif path=='/proxies/默认代理':self.respond(p)
  elif '/delay?' in path:self.respond({'delay':76})
  elif path=='/providers/proxies':self.respond({'providers':{'测试订阅':{'vehicleType':'HTTP','subscriptionInfo':{'Total':1000,'Upload':100,'Download':200},'proxies':API.provider_nodes,'secretURL':'must-not-leak'}}})
  elif path=='/providers/rules':self.respond({'providers':{k:{'ruleCount':10} for k in ['u60-cn-domain','u60-cn-ip','u60-foreign-domain']}})
  elif path=='/connections':self.respond({'uploadTotal':100,'downloadTotal':200,'connections':[]})
  else:self.respond({},404)
 def write_response(self):
  n=int(self.headers.get('Content-Length','0'));raw=self.rfile.read(n);data=json.loads(raw) if raw else {}
  path=urllib.parse.unquote(self.path);API.calls.append((self.command,path))
  if API.reject:self.respond({'message':'rejected'},500);return
  if path=='/configs':API.mode=data['mode']
  if path=='/proxies/默认代理':API.selected=data['name']
  self.respond({})
 do_PUT=write_response;do_PATCH=write_response;do_DELETE=write_response;do_POST=write_response
class Test(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory();cls.p=pathlib.Path(cls.temp.name)
  cls.server=http.server.HTTPServer(('127.0.0.1',0),API)
  (cls.p/'mihomo').write_text('#!/bin/sh\n[ ! -e "'+str(cls.p/'reject')+'" ]\n');(cls.p/'mihomo').chmod(0o700)
  wrapper=cls.p/'wrapper.c';wrapper.write_text('#define main backend_main\n#include "'+str(ROOT/'panel/panel-control.c')+'"\n#undef main\nint main(void){char in[8192];size_t n=fread(in,1,sizeof(in)-1,stdin);in[n]=0;cJSON*r=cJSON_Parse(in),*o;if(!strcmp(jstr(r,"action"),"state")){o=cJSON_CreateObject();control_clash_sections(o);}else if(!strcmp(jstr(r,"action"),"test_proxy")){char proxy[180];o=reply(cc_quota_proxy(proxy,sizeof(proxy)),"test");if(cJSON_IsTrue(jget(o,"ok")))cJSON_AddStringToObject(o,"proxy",proxy);}else o=control_clash_action(jstr(r,"action"),jget(r,"args"));char*s=cJSON_PrintUnformatted(o);puts(s?s:"{}");free(s);cJSON_Delete(o);cJSON_Delete(r);return 0;}')
  cls.bin=cls.p/'test';subprocess.run(['cc','-D_GNU_SOURCE',f'-DCC_ROOT="{cls.p}"',f'-DCC_API_PORT={cls.server.server_port}',f'-DCC_PROFILE_SCRIPT="{cls.p}/network-profile.sh"','-Wno-unused-function','-I'+str(ROOT/'panel/vendor'),str(wrapper),str(ROOT/'panel/vendor/cJSON.c'),'-lm','-o',str(cls.bin)],check=True,capture_output=True)
  cls.t=threading.Thread(target=cls.server.serve_forever,daemon=True);cls.t.start()
 @classmethod
 def tearDownClass(cls):cls.server.shutdown();cls.server.server_close();cls.temp.cleanup()
 def setUp(self):
  API.mode='rule';API.selected='香港 01';API.calls=[];API.reject=False;API.proxies=None;API.rules=[{'type':'Match','proxy':'默认代理'}];API.provider_nodes=[]
  self.config='mode: rule\nsecret: "fixture-not-real"\nrules:\n  - MATCH,DIRECT\n';(self.p/'config.yaml').write_text(self.config);(self.p/'reject').unlink(missing_ok=True)
 def call(self,action,**args):
  p=subprocess.run([str(self.bin)],input=json.dumps({'action':action,'args':args}),capture_output=True,text=True,check=True);return json.loads(p.stdout)
 def test_quota_respects_lan_listener(self):
  (self.p/'config.yaml').write_text('mixed-port: 17890\nbind-address: "192.168.0.1"\n');self.assertEqual(self.call('test_proxy')['proxy'],'http://192.168.0.1:17890')
 def test_quota_wildcard_and_ipv6_listeners(self):
  for bind,proxy in [('*','http://127.0.0.1:7890'),('0.0.0.0','http://127.0.0.1:7890'),('::1','http://[::1]:7890')]:
   (self.p/'config.yaml').write_text(f'mixed-port: 7890\nbind-address: "{bind}"\n');self.assertEqual(self.call('test_proxy')['proxy'],proxy)
 def test_quota_invalid_listener_not_used(self):
  for port,bind in [('0','127.0.0.1'),('70000','127.0.0.1'),('7890','invalid.example')]:
   (self.p/'config.yaml').write_text(f'mixed-port: {port}\nbind-address: "{bind}"\n');self.assertFalse(self.call('test_proxy')['ok'])
 def verify_script(self,payload):
  p=self.p/'network-profile.sh';p.write_text("#!/bin/sh\nprintf '%s\\n' '"+json.dumps(payload)+"'\n");p.chmod(0o755);return p
 def test_coverage_item_separates_serving_from_takeover(self):
  self.node_fixture()
  items=self.call('state')['sections'][0]['items']
  coverage=[i for i in items if i['id']=='coverage']
  self.assertEqual(len(coverage),1);self.assertEqual(coverage[0]['type'],'info');self.assertIn('未接管',coverage[0]['value'])
  self.assertEqual(len([i for i in items if i['id']=='scope' and 'UDP' in i['value']]),1)
  diagnose=[i for i in items if i.get('action')=='clash.diagnose']
  self.assertEqual(len(diagnose),1);self.assertFalse(diagnose[0]['confirm'])
 def test_diagnose_reports_serving_and_interception_separately(self):
  self.verify_script({'ok':True,'profile':'clash','redirect':{'prerouting_tcp':True,'dns':True,'udp443_reject':True,'guard':False},'ipv6_default_route':True})
  r=self.call('clash.diagnose')
  self.assertTrue(r['ok'],r);self.assertIn('已接管 IPv4 TCP 与 DNS',r['message']);self.assertIn('UDP 443 已拒绝',r['message']);self.assertIn('IPv6 可能绕过',r['message'])
 def test_diagnose_refuses_partial_direct_and_error_profiles(self):
  self.verify_script({'ok':True,'profile':'clash','redirect':{'prerouting_tcp':True,'dns':False,'udp443_reject':True,'guard':False},'ipv6_default_route':False})
  r=self.call('clash.diagnose');self.assertFalse(r['ok']);self.assertIn('DNS 未生效',r['message'])
  self.verify_script({'ok':True,'profile':'direct','redirect':{},'ipv6_default_route':False})
  r=self.call('clash.diagnose');self.assertFalse(r['ok']);self.assertIn('未接管上网',r['message'])
  self.verify_script({'ok':True,'profile':'error','redirect':{'guard':True},'ipv6_default_route':False})
  r=self.call('clash.diagnose');self.assertFalse(r['ok']);self.assertIn('失败保护已生效',r['message'])
 def test_diagnose_without_coverage_helper_does_not_claim_success(self):
  (self.p/'network-profile.sh').unlink(missing_ok=True)
  r=self.call('clash.diagnose');self.assertFalse(r['ok']);self.assertIn('自检脚本不可用',r['message'])
 def test_single_node_entry_per_mode_and_no_builtin_choices(self):
  self.node_fixture()
  for mode,group in [('rule','示例分组B'),('global','示例分组A')]:
   API.mode=mode;API.proxies[group]['all']+=['DIRECT','REJECT','REJECT-DROP']
   items=self.call('state')['sections'][0]['items']
   switches=[i for i in items if i.get('action')=='clash.select']
   self.assertEqual(len(switches),1);self.assertEqual(switches[0]['args']['group'],group)
   self.assertFalse({'DIRECT','REJECT','REJECT-DROP'} & {c['args']['name'] for c in switches[0]['choices']})
   self.assertFalse(any(i.get('action')=='network.profile' for i in items))
   modes=next(i for i in items if i['id']=='mode')['choices'];self.assertEqual([x['args']['mode'] for x in modes],['rule','global'])
   rule=next(i for i in items if i['id']=='add-rule');policies=next(f for f in rule['fields'] if f['key']=='policy')['choices']
   self.assertTrue({'DIRECT','REJECT'} <= {x['value'] for x in policies})
 def test_rule_sets_share_one_named_entry(self):
  items=self.call('state')['sections'][0]['items'];entries=[i for i in items if i.get('action')=='clash.rule_provider']
  self.assertEqual(len(entries),1);self.assertEqual(entries[0]['type'],'choice')
  self.assertEqual([c['label'] for c in entries[0]['choices']],['国内域名 · 直连','国内 IP · 直连','国外域名 · 代理'])
 def test_delay_uses_deduplicated_selectable_lines(self):
  API.proxies={'默认代理-示例分组A':{'type':'Selector','now':'示例分组A','all':['示例分组A','示例节点B','示例分组A','DIRECT','REJECT']},'示例分组A':{'type':'URLTest'},'示例节点B':{'type':'Shadowsocks'},'GLOBAL':{'type':'Selector'}}
  API.rules=[{'type':'Match','proxy':'默认代理-示例分组A'}]
  items=self.call('state')['sections'][0]['items'];delay=next(i for i in items if i['id']=='delay')
  self.assertEqual([c['args']['name'] for c in delay['choices']],['示例分组A','示例节点B'])
  self.assertTrue(all(method=='GET' for method,path in API.calls))
 def test_missing_match_disables_node_control_instead_of_guessing(self):
  API.rules=[];items=self.call('state')['sections'][0]['items'];node=next(i for i in items if i['id']=='node');self.assertFalse(node['enabled'])
 def test_nested_rule_selector_exposes_leaf_nodes_to_screen_picker(self):
  API.proxies={'Main':{'type':'Selector','now':'Mojie','all':['DIRECT','Mojie']},'Mojie':{'type':'Selector','now':'Leaf 02','all':['Leaf 01','Leaf 02']},'Leaf 01':{'type':'Vless'},'Leaf 02':{'type':'Vless'},'GLOBAL':{'type':'Selector','now':'DIRECT','all':['DIRECT','Main']}}
  API.rules=[{'type':'Match','proxy':'Main'}]
  items=self.call('state')['sections'][0]['items'];node=next(i for i in items if i['id']=='node')
  self.assertTrue(node['enabled']);self.assertEqual(node['args']['group'],'Mojie')
  self.assertEqual([c['args']['name'] for c in node['choices']],['Leaf 01','Leaf 02'])
 def test_state_secret_allowlist(self):
  r=self.call('state');s=json.dumps(r);self.assertNotIn('secretURL',s);self.assertNotIn('fixture-not-real',s);self.assertEqual(r['data']['clash']['quota_remaining'],700);self.assertTrue(r['data']['clash']['online'])
 def node_fixture(self):
  API.proxies={'示例分组A':{'type':'Selector','now':'示例节点 A01','all':['示例节点 A01']},'GLOBAL':{'type':'Selector','now':'示例分组A','all':['示例分组A','默认代理']},'默认代理':{'type':'Selector','now':'示例分组B','all':['示例分组A','示例分组B']},'示例分组B':{'type':'Selector','now':'示例节点 B01','all':['示例节点 B01']},'示例节点 A01':{'type':'Vless'},'示例节点 B01':{'type':'Vless'}}
  API.rules=[{'type':'DomainSuffix','payload':'example.com','proxy':'示例分组A'},{'type':'Match','proxy':'默认代理'}]
 def current_node(self):
  result=self.call('state')['data']['clash']['node'];self.assertTrue(all(method=='GET' for method,path in API.calls));return result
 def test_rule_node_uses_live_match_and_nested_selection(self):
  self.node_fixture();self.assertEqual(self.current_node(),'示例节点 B01');self.assertIn(('GET','/rules'),API.calls)
 def test_global_node_uses_global_chain(self):
  self.node_fixture();API.mode='global';self.assertEqual(self.current_node(),'示例节点 A01')
  API.proxies['GLOBAL']['now']='默认代理';self.assertEqual(self.current_node(),'示例节点 B01')
 def test_direct_node_does_not_guess_from_groups(self):
  self.node_fixture();API.mode='direct';self.assertEqual(self.current_node(),'DIRECT')
 def test_provider_only_leaf_is_resolved_without_duplicate_fetch(self):
  for mode in ['rule','global']:
   with self.subTest(mode=mode):
    self.node_fixture();API.mode=mode;API.calls=[];API.proxies['GLOBAL']['now']='默认代理';del API.proxies['示例节点 B01']
    API.provider_nodes=[{'name':'示例分组A-100.00GB','type':'Shadowsocks'},{'name':'示例节点 B01','type':'Vless'}]
    self.assertEqual(self.current_node(),'示例节点 B01');self.assertEqual(API.calls.count(('GET','/providers/proxies')),1)
 def test_truly_missing_leaf_is_unknown_even_with_other_provider_nodes(self):
  self.node_fixture();del API.proxies['示例节点 B01'];API.provider_nodes=[{'name':'示例分组A-100.00GB','type':'Shadowsocks'}]
  self.assertEqual(self.current_node(),'未知');self.assertEqual(API.calls.count(('GET','/providers/proxies')),1)
 def test_match_direct_and_first_match_order(self):
  self.node_fixture();API.rules=[{'type':'Match','proxy':'DIRECT'},{'type':'Match','proxy':'默认代理'}];self.assertEqual(self.current_node(),'DIRECT')
 def test_node_cycles_are_unknown(self):
  for mode in ['rule','global']:
   with self.subTest(mode=mode):
    self.node_fixture();API.mode=mode;API.proxies['GLOBAL']['now']='默认代理';API.proxies['示例分组B']['now']='默认代理';self.assertEqual(self.current_node(),'未知')
 def test_node_missing_target_or_group_selection_is_unknown(self):
  for change in ['missing-leaf','missing-now','empty-now','malformed-now','missing-root']:
   with self.subTest(change=change):
    self.node_fixture()
    if change=='missing-leaf':del API.proxies['示例节点 B01']
    elif change=='missing-now':del API.proxies['默认代理']['now']
    elif change=='empty-now':API.proxies['默认代理']['now']=''
    elif change=='malformed-now':API.proxies['默认代理']['now']=7
    else:del API.proxies['默认代理']
    self.assertEqual(self.current_node(),'未知')
 def test_node_missing_live_match_rules_or_mode_is_unknown(self):
  for rules in [None,[],[{'type':'DomainSuffix','proxy':'默认代理'}],[{'type':'Match'}],{'bad-schema':{'type':'Match','proxy':'默认代理'}}]:
   with self.subTest(rules=rules):
    self.node_fixture();API.rules=rules;self.assertEqual(self.current_node(),'未知')
  self.node_fixture();API.mode='unknown';self.assertEqual(self.current_node(),'未知')
  self.node_fixture();API.mode='global';del API.proxies['GLOBAL'];self.assertEqual(self.current_node(),'未知')
 def test_unicode_group_selection(self):
  self.assertTrue(self.call('clash.select',group='默认代理',name='日本 02')['ok']);self.assertEqual(API.selected,'日本 02')
 def test_reject_unknown_member(self):
  self.assertFalse(self.call('clash.select',group='默认代理',name='不存在')['ok']);self.assertFalse(any(x[0]=='PUT' for x in API.calls))
 def test_invalid_mode_no_write(self):
  self.assertFalse(self.call('clash.mode',mode='rule;reboot')['ok']);self.assertFalse(API.calls)
 def test_mode_persists(self):
  self.assertTrue(self.call('clash.mode',mode='global')['ok']);self.assertEqual((self.p/'mode').read_text(),'global\n');self.assertIn('mode: global\n',(self.p/'config.yaml').read_text())
 def test_persist_failure_restores_runtime(self):
  (self.p/'mode').unlink(missing_ok=True);(self.p/'mode').mkdir()
  try:
   r=self.call('clash.mode',mode='global');self.assertFalse(r['ok']);self.assertEqual(API.mode,'rule');self.assertEqual((self.p/'config.yaml').read_text(),self.config)
  finally:(self.p/'mode').rmdir()
 def test_api_reject_does_not_persist(self):
  API.reject=True;self.assertFalse(self.call('clash.mode',mode='direct')['ok'])
 def test_rule_lifecycle(self):
  args={'type':'DOMAIN-SUFFIX','pattern':'example.com','policy':'DIRECT'}
  self.assertTrue(self.call('clash.rule_add',**args)['ok']);txt=(self.p/'config.yaml').read_text();self.assertIn('DOMAIN-SUFFIX,example.com,DIRECT',txt);self.assertIn('MATCH,DIRECT',txt)
  args.update(index=0,expected_rule='DOMAIN-SUFFIX,example.com,DIRECT',operation='toggle');self.assertTrue(self.call('clash.rule_edit',**args)['ok']);self.assertIn('# off - DOMAIN-SUFFIX',(self.p/'config.yaml').read_text())
  args['operation']='delete';self.assertTrue(self.call('clash.rule_edit',**args)['ok']);self.assertNotIn('example.com',(self.p/'config.yaml').read_text())
 def test_successful_verbose_validator_is_not_a_failed_rule_save(self):
  path=self.p/'mihomo';original=path.read_text()
  try:
   path.write_text('#!/bin/sh\ni=0\nwhile [ "$i" -lt 500 ]; do echo "validation progress"; i=$((i+1)); done\nexit 0\n')
   self.assertTrue(self.call('clash.rule_add',type='DOMAIN',pattern='u60-test.invalid',policy='DIRECT')['ok'])
  finally:path.write_text(original)
 def test_stale_rule_rejected(self):
  self.assertFalse(self.call('clash.rule_edit',index=0,expected_rule='missing',operation='delete')['ok']);self.assertEqual((self.p/'config.yaml').read_text(),self.config)
 def test_rule_injection_rejected(self):
  self.assertFalse(self.call('clash.rule_add',type='DOMAIN',pattern='test\nsecret:bad',policy='DIRECT')['ok']);self.assertEqual((self.p/'config.yaml').read_text(),self.config)
 def test_invalid_yaml_keeps_original(self):
  (self.p/'reject').touch();self.assertFalse(self.call('clash.rule_add',type='DOMAIN',pattern='example.com',policy='DIRECT')['ok']);self.assertEqual((self.p/'config.yaml').read_text(),self.config)
 def test_reload_failure_rolls_back(self):
  API.reject=True;self.assertFalse(self.call('clash.rule_add',type='DOMAIN',pattern='example.com',policy='DIRECT')['ok']);self.assertEqual((self.p/'config.yaml').read_text(),self.config)
if __name__=='__main__':unittest.main()
