define(['jquery','u60-web-model'],function($,m){
 'use strict';var subtab='nodes',nodeGroup='',query='',nodeRequest=0,nodeVisible=12;
 function n(tag,cls,text){var e=$('<'+tag+'>');if(cls)e.addClass(cls);if(text!==undefined)e.text(m.readable(text));return e;}
 function button(label,fn,cls){return n('button',cls||'u60-small-button',label).attr('type','button').on('click',fn);}
 function field(key,label,value,kind,required,choices){return {key:key,label:label,value:value||'',kind:kind||'text',required:!!required,choices:choices};}
 function act(label,action,args,confirm){return {label:label,type:'action',action:action,args:args||{},confirm:confirm!==false,enabled:true};}
 function form(label,action,args,fields,reason){return {label:label,type:'form',action:action,args:args,fields:fields,reason:reason,confirm:true,enabled:true};}
 function card(title){return n('section','u60-card u60-wide-card').append(n('h3','u60-card-title',title));}
 function date(v){if(!v||String(v).startsWith('0001'))return '—';var d=new Date(v);return isNaN(d.getTime())?String(v):d.toLocaleString('zh-CN',{hour12:false});}
 function expiry(v){var s=Number(v);if(!isFinite(s)||s<=0)return '';if(s>1e12)s=s/1000;return date(new Date(s*1000));}
 function pathOf(data){var head=data.mode==='global'?'GLOBAL':'MATCH',path=(data.active_path||[]).slice();if(!path.length)return '';return head+' → '+path.join(' → ')+(data.active_node?' → '+data.active_node:'');}
 function row(title,value){return n('div','u60-setting').append(n('span','u60-setting-label',title),n('span','u60-setting-value',value));}
 function sourceButton(api,id,label){var item=(api.items||[]).find(function(i){return i.id===id;});if(!item)return null;return button(label||item.label,function(){api.open(item);}).prop('disabled',!m.interactive(item));}
 function about(data){var box=n('details','u60-disclosure u60-about').attr('id','u60-clash-about');box.append(n('summary','','版本与验证范围'),n('p','u60-help','已安装版本：'+(data.release||'未知')),n('p','u60-help','已实机验证：增强屏幕、双击电源切换、原厂网页、热点上网、节点选择与回读、订阅节点连通延迟、规则/全局模式切换'),n('p','u60-help','B31 已验证：2.4GHz 中继与热点客户端 IPv4 HTTPS。待验证：5GHz 完整下游、外接 USB 网卡、冷启动、长待机与第二台首装'),n('p','u60-help','回退：USB ADB 的 restore-boot；升级前的程序备份保留在设备本机'));return box;}
 function coverage(data,api){
  var box=card('代理概况'),body=n('div','u60-card-body').appendTo(box),verdict=data.verdict||'direct',summary=data.verdict_text||'当前直连';
  var headline=n('div','u60-status-head').append(n('strong','u60-status-title',summary));
  var service=(api.items||[]).find(function(i){return i.id==='service';});if(service)headline.append(sourceButton(api,'service','代理开关 · '+m.readable(service.value)));body.append(headline);
  if(data.active_node)body.append(n('p','u60-help','当前节点：'+data.active_node));
  var modes=n('div','u60-mode-options').attr({'role':'group','aria-label':'分流模式'});
  [['rule','规则分流','按规则选择直连或代理'],['global','全局代理','全部使用代理节点'],['direct','直连','停止代理接管']].forEach(function(x){var current=data.mode===x[0];var item=act('切换为'+x[1],'web.clash.mode',{mode:x[0]},true);item.reason=x[2]+'；应用后会重新读取实际状态。';modes.append(button(x[1]+(current?' · 当前':''),function(){api.open(item);},'u60-mode-option'+(current?' active':'')).attr('aria-pressed',String(current)).prop('disabled',current).append(n('small','',x[2])));});body.append(modes);
  if(verdict==='partial'||verdict==='unverified'||verdict==='core_down')body.append(n('p','u60-help u60-warning','转发尚未完整生效，客户端可能直接暴露；请在连接与诊断中运行覆盖自检。'));
  body.append(n('p','u60-scope','覆盖范围：IPv4 TCP 与 DNS；UDP 与 IPv6 不经过代理，UDP 443 被拒绝。'+(data.ipv6_default_route?' 检测到 IPv6 默认路由，客户端可能绕过代理。':'')));
  var detail=n('details','u60-disclosure').attr('id','u60-clash-coverage').append(n('summary','','查看接管详情'));
  detail.append(row('Mihomo 核心',data.core_online?'运行中':data.online?'接口异常':'未运行'),row('转发规则',data.coverage_checked?(data.coverage_tcp?'TCP 已生效':'TCP 未生效')+' · '+(data.coverage_dns?'DNS 已生效':'DNS 未生效'):'未能核验'));
  var path=pathOf(data);if(path)detail.append(row(data.active_path_label||'默认代理路径',path));body.append(detail);return box;
 }
 function subscriptions(data,api){
  var box=card('订阅管理'),body=n('div','u60-card-body').appendTo(box);body.append(n('p','u60-help','新增后保持当前节点。订阅链接仅在 U60 本机保存，列表不显示完整链接。'));
  function edit(p){var creating=!p,groups=(data.destination_groups||[]).map(function(x){return {label:x,value:x};}),fields=[];
   if(creating)fields.push(field('name','订阅名称','','text',true));
   fields.push(field('url',creating?'Clash / Mihomo 订阅链接':'新订阅链接（留空保留原链接）','','password',creating));fields.push(field('interval','自动更新间隔（秒，至少 300）',p?p.interval:3600,'number',true));
   if(creating)fields.push(field('destination','加入策略组',groups.filter(function(g){return g.value==='选择节点';})[0]?.value||groups.filter(function(g){return g.value!=='GLOBAL';})[0]?.value||'','choice',true,groups));
   api.open(form(creating?'新增订阅':'编辑 '+p.name,'web.clash.subscription_save',{existing:creating?'':p.name,name:creating?'':p.name,revision:data.revision},fields,'保存前会校验配置，并验证订阅能够加载节点；失败自动回退。新订阅不会自动切换当前节点。'));
  }
  body.append(button('＋ 新增订阅',function(){edit(null);},'u60-primary'));
  (data.providers||[]).forEach(function(p){var entry=n('div','u60-provider'),usage=p.usage||{},total=usage.Total||usage.total,expire=expiry(usage.Expire||usage.expire);
   var updated=Date.parse(p.updated||''),stale=isFinite(updated)&&p.interval>0&&Date.now()-updated>p.interval*2000;
   entry.append(n('div','u60-provider-heading').append(n('strong','',p.name),n('span','u60-badge',(p.count||0)+' 个节点')));entry.append(n('p','u60-help',(p.host||'未读取来源')+' · 每 '+p.interval+' 秒更新'));entry.append(n('small','u60-help','最近更新：'+date(p.updated)+(stale?' · 已超过两个更新周期，节点可能过期':'')+(!total?' · 用量未提供':'')));
   if(total){var used=(usage.Upload||usage.upload||0)+(usage.Download||usage.download||0);entry.append(n('p','u60-help','用量 '+m.bytes(used,false)+' / '+m.bytes(total,false)+(expire?' · 到期 '+expire:'')));}
   else if(expire)entry.append(n('p','u60-help','到期 '+expire));
   var actions=n('div','u60-inline-actions').append(button('更新',function(){api.open(act('更新 '+p.name,'clash.provider',{name:p.name},false));}),button('编辑',function(){edit(p);}),button('删除',function(){var a=act('删除订阅 '+p.name,'web.clash.subscription_delete',{name:p.name,revision:data.revision});a.reason='正在被选中的订阅会拒绝删除；可先切换到其他节点。';api.open(a);},'u60-small-button u60-danger'));entry.append(actions);body.append(entry);
  });if(!(data.providers||[]).length)body.append(n('p','u60-help','尚未配置订阅。'));return box;
 }
 function nodes(data,api){
  var box=card('节点选择'),body=n('div','u60-card-body').appendTo(box),groups=data.groups||[],options=n('select','u60-input').attr({'aria-label':'节点策略组','id':'u60-node-group'});
  var prefs=data.prefs||{},favorites=(prefs.favorites||[]).filter(function(x){return typeof x==='string';}),recents=(prefs.recents||[]).filter(function(x){return typeof x==='string';}),delays=prefs.delays||{};
  groups.forEach(function(g){var count=m.selectableNodes(g).length;if(count)options.append(n('option','',g.name+' · '+count+' 个节点'+(g.name===data.active_group?' · 当前使用':'')).val(g.name));});
  if(!groups.some(function(g){return g.name===nodeGroup&&m.selectableNodes(g).length>0;}))nodeGroup=(m.defaultNodeGroup(groups,data.active_group)||{}).name||'';
  options.val(nodeGroup);body.append(n('label','u60-field-label','节点策略组').attr('for','u60-node-group'),options);
  var search=n('input','u60-input').attr({type:'search',placeholder:'搜索当前策略组的节点','aria-label':'搜索节点'}).val(query),list=n('div','u60-node-list');body.append(search,list);
  function delayOf(name){var d=delays[name],ms=d&&typeof d==='object'?d.ms:d;return typeof ms==='number'&&ms>0?ms:0;}
  function entryFor(name,group,selectable){
   var current=group.selected===name,line=n('div','u60-node-row'+(current?' current':'')),identity=n('div','u60-node-identity').append(n('strong','',name));line.append(identity);
   if(current)identity.append(n('span','u60-badge','当前'));if(favorites.indexOf(name)>=0)identity.append(n('span','u60-badge','收藏'));var ms=delayOf(name);identity.append(n('span','u60-node-delay',ms?ms+' ms':'暂无延迟记录'));
   var actions=n('div','u60-inline-actions');
   if(group.type==='Selector'&&selectable.indexOf(name)>=0)actions.append(button(current?'使用中':'使用',function(){var request=++nodeRequest;list.find('button').prop('disabled',true);api.status('正在切换节点并核对运行状态…');Promise.resolve(api.call('web.clash.select',{group:group.name,name:name})).then(function(result){if(request!==nodeRequest)return;if(!result||result.ok!==true)throw new Error(result&&result.message||'切换未确认，请刷新后核对');api.status(result.message||'节点已切换并回读确认');api.refresh();},function(error){if(request===nodeRequest)api.status(error&&error.message||'切换失败，请刷新后核对',true);}).finally(function(){if(request===nodeRequest){list.find('button').prop('disabled',false);list.find('button[data-current="true"]').prop('disabled',true);}});},'u60-small-button').attr('data-current',String(current)).prop('disabled',current));
   actions.append(button(favorites.indexOf(name)>=0?'取消收藏':'收藏',function(){api.open(act((favorites.indexOf(name)>=0?'取消收藏 ':'收藏 ')+name,'web.clash.favorite',{name:name},false));}),button('查延迟',function(){api.open(act('测试连通延迟 '+name,'clash.delay',{name:name},false));}));line.append(actions);return line;
  }
  function render(){
   list.empty();var group=groups.find(function(g){return g.name===nodeGroup;});if(!group){list.append(n('p','u60-help','尚无可选节点，请先在订阅页添加并更新订阅。'));return;}
   var selectable=m.selectableNodes(group),filtered=selectable.filter(function(name){return name.toLowerCase().includes(query.toLowerCase());}),shown=[];
   if(group.name!==data.active_group)list.append(n('p','u60-help','当前查看其他策略组，修改它不一定改变当前流量路径。'));
   function section(title,names){names=names.filter(function(name){return filtered.indexOf(name)>=0&&shown.indexOf(name)<0;});if(!names.length)return;list.append(n('h4','u60-subheading',title));names.forEach(function(name){shown.push(name);list.append(entryFor(name,group,selectable));});}
   section('当前节点',[group.selected]);section('收藏',favorites);section('最近使用',recents.slice(0,5));
   var remaining=filtered.filter(function(name){return shown.indexOf(name)<0;});if(remaining.length){list.append(n('h4','u60-subheading',query?'搜索结果':'其他节点'),n('p','u60-help','共 '+filtered.length+' 个匹配节点 · 延迟来自最近健康检查，不代表带宽测速'));
    remaining.slice(0,nodeVisible).forEach(function(name){list.append(entryFor(name,group,selectable));});
    if(remaining.length>nodeVisible)list.append(button('再显示 '+Math.min(12,remaining.length-nodeVisible)+' 个节点',function(){nodeVisible+=12;render();}));
   }
   if(!filtered.length)list.append(n('p','u60-help',selectable.length?'没有匹配的节点。':'此策略组没有具体节点，请选择其他策略组。'));
  }
  options.on('change',function(){nodeGroup=this.value;nodeVisible=12;render();});search.on('input',function(){query=this.value;nodeVisible=12;render();});render();
  var providers=(data.providers||[]).filter(function(p){return p.name;}),autoGroup=groups.find(function(g){return /url-?test/i.test(g.type||'');});
  var auto=n('details','u60-disclosure u60-auto-select').attr('id','u60-clash-auto').append(n('summary','','自动选择 · '+(autoGroup?'已配置':'未启用')),n('p','u60-help',autoGroup?'「'+autoGroup.name+'」按连通延迟自动切换。手动固定节点前请关闭自动选择。':'启用后每 5 分钟按连通延迟选择订阅节点。'));
  if(autoGroup)auto.append(button('关闭自动选择',function(){api.open(act('关闭自动选择','web.clash.autoselect',{group:autoGroup.name,enabled:false},true));}));
  else providers.forEach(function(p){auto.append(button('对「'+p.name+'」启用',function(){api.open(act('启用自动选择','web.clash.autoselect',{group:'自动选择',provider:p.name,enabled:true},true));}));});
  if(!providers.length)auto.append(n('p','u60-help','先添加订阅，再启用自动选择。'));body.append(auto);return box;
 }
 function ruleFields(rule,data){var parts=(rule||'DOMAIN-SUFFIX,example.com,DIRECT').split(','),policies=[{label:'直连',value:'DIRECT'},{label:'拒绝',value:'REJECT'}].concat((data.groups||[]).map(g=>({label:g.name,value:g.name})));return [field('type','规则类型',parts[0],'choice',true,[{label:'域名后缀',value:'DOMAIN-SUFFIX'},{label:'完整域名',value:'DOMAIN'},{label:'IPv4 网段',value:'IP-CIDR'},{label:'IPv6 网段',value:'IP-CIDR6'}]),field('pattern','域名或网段',parts[1],'text',true),field('policy','处理方式',parts[2],'choice',true,policies)];}
 function rules(data,api){var box=card('规则管理'),body=n('div','u60-card-body').appendTo(box);body.append(n('p','u60-help','本地规则优先生效。下方可搜索完整运行规则；规则集中的条目由各规则集管理。'));
  body.append(sourceButton(api,'rule-providers','更新规则集'));body.append(button('＋ 添加本地规则',function(){api.open(form('添加分流规则','clash.rule_add',{},ruleFields(null,data)));},'u60-primary'));
  (data.local_rules||[]).forEach(function(r,index){var args={index:r.index,expected_rule:r.rule},line=n('div','u60-provider').append(n('strong','u60-rule-text',r.rule),n('span','u60-badge',r.enabled?'启用':'停用'));
   var actions=n('div','u60-inline-actions').append(button('编辑',function(){api.open(form('编辑规则','clash.rule_edit',args,ruleFields(r.rule,data)));}));[['toggle',r.enabled?'停用':'启用'],['up','上移'],['down','下移'],['delete','删除']].forEach(function(x){if(x[0]==='up'&&index===0||x[0]==='down'&&index===(data.local_rules||[]).length-1)return;actions.append(button(x[1],function(){api.open(act(x[1]+'规则','clash.rule_edit',Object.assign({},args,{operation:x[0]})));}));});body.append(line.append(actions));});
  body.append(n('h4','u60-subheading','完整运行规则'));var search=n('input','u60-input').attr({type:'search',placeholder:'搜索域名、网段、规则或策略组','aria-label':'搜索规则'}),list=n('div','u60-rule-list');body.append(search,list);
  function show(){list.empty();var q=search.val().toLowerCase(),matched=(data.rules||[]).filter(function(r){return [r.type,r.payload,r.proxy].join(' ').toLowerCase().includes(q);});matched.slice(0,200).forEach(function(r){list.append(n('div','u60-rule-line').append(n('span','',r.type+' · '+(r.payload||'全部匹配')),n('strong','',r.proxy)));});list.prepend(n('p','u60-help','共 '+matched.length+' 条'+(matched.length>200?'，显示前 200 条':'')));}search.on('input',show);show();return box;
 }
 function connections(data,api){var box=card('实时连接 · '+(data.connection_count||0)),body=n('div','u60-card-body').appendTo(box),search=n('input','u60-input').attr({type:'search',placeholder:'搜索设备、域名、IP、规则或节点','aria-label':'搜索连接'}),list=n('div','u60-connections');body.append(n('div','u60-inline-actions').append(button('代理覆盖自检',function(){api.open(act('代理覆盖自检','web.clash.diagnose',{},false));}),sourceButton(api,'dns')),about(data),search,button('断开全部连接',function(){api.open(act('断开全部连接','clash.close_connections',{}));}).prop('disabled',!(data.connection_count>0)),list);
  function show(){list.empty();var q=search.val().toLowerCase();var matched=(data.connections||[]).filter(function(c){return [c.host,c.source,c.destination,c.rule,c.rule_payload,(c.chains||[]).join(' ')].join(' ').toLowerCase().includes(q);});if(!matched.length)list.append(n('p','u60-help',q?'没有匹配的连接。':'当前没有连接记录。客户端开始联网后会在这里显示。'));matched.forEach(function(c){var entry=n('div','u60-provider');entry.append(n('strong','u60-rule-text',c.host||c.destination),n('p','u60-help',c.source+' → '+c.destination+':'+c.port+' · '+c.network),n('p','u60-help',c.rule+' · '+(c.rule_payload||'')+' → '+(c.chains||[]).join(' → ')),n('p','u60-help','↓ '+m.bytes(c.download,false)+'  ↑ '+m.bytes(c.upload,false)),button('断开此连接',function(){api.open(act('断开 '+(c.host||c.destination),'web.clash.connection_close',{id:c.id}));}));list.append(entry);});}search.on('input',show);show();return box;
 }
 function clash(data,api){var wrap=n('div','u60-advanced'),nav=n('nav','u60-subtabs').attr('aria-label','Clash 高级功能'),body=n('div');wrap.append(coverage(data,api),nav,body);[['nodes','节点'],['subscriptions','订阅'],['rules','分流规则'],['connections','连接与诊断']].forEach(function(t){nav.append(button(t[1],function(){subtab=t[0];render();},'u60-subtab').attr('data-subtab',t[0]));});function render(){nav.children().each(function(){var active=$(this).attr('data-subtab')===subtab;$(this).toggleClass('active',active).attr('aria-pressed',String(active));});body.empty();if(!(data.providers||[]).length&&subtab==='nodes')body.append(n('div','u60-empty-state').append(n('strong','','还没有订阅'),n('p','u60-help','先添加并更新 Mihomo 订阅，再选择节点并启用代理。'),button('添加订阅',function(){subtab='subscriptions';render();},'u60-primary')));else body.append(({subscriptions:subscriptions,nodes:nodes,rules:rules,connections:connections}[subtab]||nodes)(data,api));}render();return wrap;}
 function tailscale(data,api){var wrap=n('div','u60-advanced'),info=n('details','u60-card u60-wide-card u60-disclosure').attr('id','u60-tailscale-account').append(n('summary','u60-card-title','账号与高级管理')),body=n('div','u60-card-body').appendTo(info);wrap.append(info);
  [['主机名',data.hostname],['组网域名',data.dns_name],['Tailnet',data.tailnet],['版本',data.version],['本机地址',(data.ips||[]).join(' · ')],['密钥到期',date(data.key_expiry)]].forEach(function(x){body.append(row(x[0],x[1]));});
  var health=data.health||[];if(data.status&&data.status!=='Running')wrap.prepend(n('p','u60-help','本机组网'+(({Stopped:'已停止',NeedsLogin:'尚未登录',NeedsMachineAuth:'等待管理端批准'})[data.status]||data.status)+'；下方设备列表为最近获取的状态。'));body.append(n('p','u60-help',health.length?health.join('\n'):'当前没有健康告警'));
  var prefs=data.prefs||{},routes=(prefs.AdvertiseRoutes||[]).filter(function(x){return x!=='0.0.0.0/0'&&x!=='::/0';});
  var controls=n('div','u60-inline-actions').append(button('修改主机名',function(){api.open(form('Tailscale 主机名','web.tailscale.hostname',{},[field('hostname','主机名',prefs.Hostname||data.hostname,'text',true)]));}),button('编辑发布子网',function(){api.open(form('发布子网路由','web.tailscale.routes',{},[field('routes','CIDR，可用逗号或换行分隔',routes.join('\n'),'textarea',false)],'将本机能够转发的子网发布给 Tailnet；新增路由仍需管理后台批准和访问规则允许。留空会撤销子网发布，出口节点设置保留。'));}),button('网络诊断',function(){api.open(act('Tailscale 网络诊断','web.tailscale.netcheck',{},false));}));
  controls.append(n('a','u60-small-button','打开 Tailscale 管理后台').attr({href:'https://console.tailscale.com/admin/machines',target:'_blank',rel:'noopener noreferrer'}));body.append(controls,n('p','u60-help','已发布子网：'+(routes.join('，')||'无')));
  var peers=card('组网设备 · '+(data.peers||[]).length),pb=n('div','u60-card-body').appendTo(peers),search=n('input','u60-input').attr({type:'search',placeholder:'搜索设备名或组网 IP','aria-label':'搜索组网设备'}),list=n('div');pb.append(search,list);wrap.append(peers);
  function show(){list.empty();var q=search.val().toLowerCase();(data.peers||[]).slice().sort((a,b)=>Number(b.online)-Number(a.online)).filter(function(p){return [p.name,(p.ips||[]).join(' '),p.os].join(' ').toLowerCase().includes(q);}).forEach(function(p){var line=n('div','u60-provider').append(n('div','u60-provider-heading').append(n('strong','',p.name),n('span','u60-badge'+(p.online?' online':''),p.online?'在线':'离线')),n('p','u60-help',(p.ips||[]).join(' · ')+' · '+p.os),n('p','u60-help',p.active?(p.direct?'当前直连':p.relay?'当前经 DERP '+p.relay:'正在建立连接'):'当前没有活跃连接'));
   if(p.exit_node)line.append(n('span','u60-badge','可作出口节点'));if(!p.online)line.append(n('p','u60-help','最近在线：'+date(p.last_seen)));
   if(p.online&&(p.ips||[]).length)line.append(button('连通测试',function(){api.open(act('测试 '+p.name,'tailscale.peer_test',{ip:p.ips[0]},false));}));list.append(line);});}search.on('input',show);show();return wrap;
 }
 return {
  render:function(tab,data,api){return tab==='clash'?clash(data,api):tailscale(data,api);},
  /* Only the live connection list needs a short refresh interval. */
  intervalFor:function(tab){return tab==='clash'&&subtab==='connections'?5000:20000;}
 };
});
