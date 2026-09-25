/* Shared, side-effect-free schema handling; strings are rendered with text(). */
(function(root,factory){if(typeof define==='function'&&define.amd)define([],factory);else if(typeof module==='object'&&module.exports)module.exports=factory();else root.U60Model=factory();}(this,function(){
 'use strict';
 function interactive(item){return !!item&&item.enabled!==false&&item.type!=='info'&&(item.type==='report'||!!item.action);}
 function argumentsFor(item,choice,fields){
  var result=Object.assign({},item.args||{},choice&&choice.args||{});
  if(fields)(item.fields||[]).forEach(function(f){var v=String(fields[f.key]==null?'':fields[f.key]);if(f.required&&!v)throw new Error('请填写 '+f.label);if(f.kind==='number'&&v&&!isFinite(Number(v)))throw new Error('请输入有效数字');result[f.key]=v;});
  return result;
 }
 function readable(value){if(value===null||typeof value==='undefined'||value==='')return '—';if(typeof value==='boolean')return value?'开启':'关闭';return String(value);}
 function bytes(v,speed){if(typeof v!=='number'||!isFinite(v)||v<0)return '—';var units=speed?['B/s','KB/s','MB/s','GB/s']:['B','KB','MB','GB','TB'],i=0;while(v>=1024&&i<units.length-1){v/=1024;i++;}return v.toFixed(v>=100||i===0?0:1)+' '+units[i];}
 function selectableNodes(group){
  if(!group)return [];
  var source=Array.isArray(group.selectable_nodes)?group.selectable_nodes:group.nodes||[];
  return source.filter(function(name){return typeof name==='string'&&!['DIRECT','REJECT','REJECT-DROP','PASS','PASS-RULE','COMPATIBLE'].includes(name);});
 }
 function defaultNodeGroup(groups,active){
  groups=groups||[];
  var current=groups.find(function(g){return g.name===active&&selectableNodes(g).length>0;});
  return current||groups.find(function(g){return selectableNodes(g).length>0;})||groups[0]||null;
 }
 // Web-only presentation: keep additions; stock controls stay in the stock pages.
 var additions={
  wifi:['relay','relay-scan','relay-health','relay-fallback','relay-autostart','relay-forget'],usb:['role','status','macnet.mode','macnet.link','macnet.help','wiring'],
  battery:['charge.manual','charge.policy','usb.power_role','power.standby','usb.charge_state','charge.connected','charge.temp','charge.voltage','charge.current','charge.policy_status','power.standby_state'],
  band:['band.lte','band.sa','band.nsa','wan_active_band','nr5g_action_band','nr5g_pci','nr5g_action_channel'],
  signal:['signal.serving','signal.neighbors'],diagnostics:['diag.web']
 };
 function filterSections(sections,tab){
  var groups={network:['wifi','usb'],clash:['clash'],tailscale:['tailscale'],device:['battery'],more:['band','signal','diagnostics']};
  var filtered=(sections||[]).filter(function(s){return groups[tab]&&groups[tab].indexOf(s.id)>=0;}).map(function(s){
   var items=(s.items||[]).filter(function(i){
    if(s.id==='clash')return ['clash.select','clash.delay','clash.provider','clash.rule_add','clash.rule_edit','diag.connections','clash.close_connections'].indexOf(i.action)<0;
    if(s.id==='tailscale')return i.action!=='tailscale.peer_test'&&i.id!=='self-ip'&&i.id!=='backend'&&(i.id||'').indexOf('peer_')!==0;
    return (additions[s.id]||[]).indexOf(i.id)>=0;
   });
   if(additions[s.id])items.sort(function(a,b){return additions[s.id].indexOf(a.id)-additions[s.id].indexOf(b.id);});
   return Object.assign({},s,{title:s.id==='wifi'?'Wi-Fi 中继':s.id==='battery'?'充电与深待机':s.title,items:items});
  }).filter(function(s){return s.items.length>0;});
  function split(section,parts){return parts.map(function(p){return {id:p[0],title:p[1],collapsed:!!p[3],items:section.items.filter(function(i){return p[2].indexOf(i.id)>=0;})};}).filter(function(s){return s.items.length;});}
  return filtered.reduce(function(out,s){
   if(s.id==='usb')return out.concat(split(s,[['usb-adapter','外接 USB 网卡',['role','status','wiring']],['usb-cable','USB 数据线直连',['macnet.mode','macnet.link','macnet.help']]]));
   if(s.id==='tailscale')return out.concat(split(s,[['tailscale','组网与访问',['connected','lan-gateway','advertise_lan']],['tailscale-exit','互联网出口',['exit','offer_exit']],['tailscale-options','高级组网设置',['mode','RouteAll','CorpDNS','ExitNodeAllowLANAccess'],true]]));
   if(s.id==='battery')return out.concat(split(s,[['power-controls','充电与待机设置',['charge.manual','charge.policy','usb.power_role','power.standby']],['power-status','电池与供电状态',['usb.charge_state','charge.connected','charge.temp','charge.voltage','charge.current','charge.policy_status','power.standby_state']]]));
   return out.concat(s);
  },[]);
 }
 return {interactive:interactive,argumentsFor:argumentsFor,readable:readable,bytes:bytes,selectableNodes:selectableNodes,defaultNodeGroup:defaultNodeGroup,filterSections:filterSections};
}));
