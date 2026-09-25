define(['jquery','service_helper','config/config','u60-web-model','u60-web-advanced'],function($,helper,config,model,advanced){
 'use strict';
 var root,dialog,snapshot,tab='overview',busy=false,reading=false,alive=0,lastRead=0,lastInteraction=0,focusBack,advancedReading=false,advancedData={},advancedAt={};
 var tabs=[['overview','概览'],['network','网络'],['clash','Clash'],['tailscale','Tailscale'],['device','电源'],['more','工具']];
 var hashTabs={"#u60_enhanced":"overview","#u60_enhanced_network":"network","#u60_enhanced_clash":"clash","#u60_enhanced_tailscale":"tailscale","#u60_enhanced_device":"device","#u60_enhanced_more":"more"};
 function hashFor(value){return value==='overview'?'#u60_enhanced':'#u60_enhanced_'+value;}
 function tabFromHash(){return hashTabs[location.hash]||'overview';}
 function node(tag,cls,text){var n=$('<'+tag+'>');if(cls)n.addClass(cls);if(text!==undefined)n.text(model.readable(text));return n;}
 function present(){return root&&root[0]&&document.documentElement.contains(root[0])&&!!hashTabs[location.hash];}
 function status(text,error){if(!present())return;root.find('.u60-notice').text(text||'').toggleClass('error',!!error).attr('role',error?'alert':'status');}
 function advancedApi(){return {open:openItem,call:call,status:status,refresh:function(){lastRead=0;lastInteraction=0;advancedAt={};refresh(false);}};}
 function rpc(method,args){
  if(!config.isLogin)return $.Deferred().reject(new Error('登录已失效，请重新登录原厂后台')).promise();
  var request=helper.createRequest('zwrt_u60_panel',method,args);request.params[3].session=request.params[0];
  return $.ajax({type:'POST',url:'/ubus/?t='+Date.now(),contentType:'application/json',dataType:'json',cache:false,timeout:12000,headers:{'Z-Mode':'0','Z-Tag':method},data:JSON.stringify([request])}).then(function(response){
   var reply=response&&response[0];
   if(!reply||reply.error||!reply.result||reply.result[0]!==0){if(reply&&(reply.error&&reply.error.code===-32002||reply.result&&reply.result[0]===6))throw new Error('会话无效或没有权限，请重新登录原厂后台');throw new Error('后台请求未确认，请刷新状态后再操作');}
   var value=reply.result[1];if(!value||value.ok!==true)throw new Error(value&&value.message||'操作未成功');return value;
  },function(){throw new Error('连接中断或请求超时；请恢复连接并刷新，勿重复提交设置');});
 }
 function call(action,args){
  var deferred=$.Deferred(),started=Date.now();
  rpc('start',{request:JSON.stringify({action:action,args:args||{}})}).then(function(start){
   function poll(){rpc('result',{id:start.id}).then(function(result){if(result.done){deferred.resolve(result.payload);return;}if(Date.now()-started>150000){deferred.reject(new Error('等待超时，请刷新并核对实际状态'));return;}setTimeout(poll,500);},deferred.reject);}
   poll();
  },deferred.reject);return deferred.promise();
 }
 function setBusy(value){busy=value;if(present())root.find('.u60-refresh').prop('disabled',busy||reading||advancedReading);}
 function refresh(manual){
  if(!present()||busy||reading||advancedReading||dialog&&dialog[0].open)return;
  reading=true;var generation=alive;root.find('.u60-refresh').prop('disabled',true);if(manual)status('正在读取设备状态…');
  call('state',{}).then(function(result){if(generation!==alive||!present())return;if(!result||!result.ok){status(result&&result.message||'状态读取失败',true);return;}var first=!snapshot;snapshot=result;lastRead=Date.now();if(!(dialog&&dialog[0].open)&&(first||manual||Date.now()-lastInteraction>2000))render();root.find('.u60-sync').text('已同步 '+new Date().toLocaleTimeString('zh-CN',{hour12:false}));if(first||manual)status('');},function(error){if(generation===alive)status(error.message||String(error),true);}).always(function(){if(generation===alive){reading=false;if(present())root.find('.u60-refresh').prop('disabled',busy||advancedReading);loadAdvanced(!!manual);}});
 }
 function closeDialog(){if(dialog&&dialog[0].open)dialog[0].close();if(dialog)dialog.empty();if(focusBack&&document.documentElement.contains(focusBack))focusBack.focus();}
 function openDialog(title){closeDialog();focusBack=document.activeElement;dialog.append(node('div','u60-dialog-head').append(node('h3','',title),node('button','u60-close','×').attr({'type':'button','aria-label':'关闭'}).on('click',closeDialog)));dialog[0].showModal();return node('div','u60-dialog-body').appendTo(dialog);}
 function showReport(report){var b=openDialog(report.title||'操作结果');(report.lines||[]).forEach(function(line){b.append(node('p','u60-report-line',line));});b.append(node('button','u60-primary','完成').on('click',closeDialog));}
 function execute(item,args){
  if(busy){status('另一个操作正在执行，请稍候',false);return;}
  closeDialog();setBusy(true);status('正在执行：'+item.label+'…');var generation=alive;
  function perform(){if(generation!==alive||!present())return;if(reading||advancedReading){setTimeout(perform,150);return;}
  call(item.action,args).then(function(result){if(generation!==alive||!present())return;if(!result||result.ok!==true){status(result&&result.message||'操作未确认，请刷新核对',true);return;}
   status(result.message||'操作已完成');if(result.picker)openItem(result.picker,true);else if(result.report)showReport(result.report);
  },function(error){if(generation===alive)status(error.message||String(error),true);}).always(function(){if(generation!==alive)return;setBusy(false);lastRead=0;lastInteraction=0;advancedAt={};if(present()&&!(dialog&&dialog[0].open))setTimeout(function(){refresh(false);},600);});}
  perform();
 }
 function confirm(item,args){if(item.confirm===false){execute(item,args);return;}
  var b=openDialog(item.label);b.append(node('p','','确认应用此设置？'));if(item.reason)b.append(node('p','u60-help',item.reason));
  var details=node('dl','u60-confirm-values'),chosen=(item.choices||[]).filter(function(c){return Object.keys(c.args||{}).every(function(k){return c.args[k]===args[k];});})[0];
  if(chosen)details.append(node('dt','','所选设置'),node('dd','',chosen.label));
  else if(item.fields)(item.fields||[]).forEach(function(f){if(f.kind==='password')return;var v=args[f.key],c=(f.choices||[]).filter(function(x){return String(x.value)===String(v);})[0];details.append(node('dt','',f.label),node('dd','',c?c.label:v));});
  else if(typeof args.enabled==='boolean')details.append(node('dt','','设置为'),node('dd','',args.enabled?'开启':'关闭'));if(details.children().length)b.append(details);
  b.append(node('div','u60-dialog-actions').append(node('button','u60-secondary','取消').on('click',closeDialog),node('button','u60-primary','应用设置').on('click',function(){execute(item,args);})));}
 function openItem(item,internal){
  if(!internal&&busy){status('正在同步或执行操作，请稍候');return;}if(!model.interactive(item))return;
  if(item.type==='report'){showReport({title:item.label,lines:[item.detail||item.value]});return;}
  if(item.type==='choice'){
   var b=openDialog(item.label),list=node('div','u60-choices').appendTo(b),choices=item.choices||[];
   if(item.reason)b.prepend(node('p','u60-help',item.reason));
   if(choices.length>8){var search=node('input','u60-input').attr({'type':'search','placeholder':'搜索选项','aria-label':'搜索选项'});b.prepend(search);search.on('input',function(){var q=this.value.toLowerCase();list.children().each(function(){$(this).toggle($(this).text().toLowerCase().indexOf(q)>=0);});});}
   choices.forEach(function(choice){list.append(node('button','u60-choice').append(node('span','',choice.label),choice.description?node('small','',choice.description):null).on('click',function(){confirm(item,model.argumentsFor(item,choice));}));});
   if(!choices.length)list.append(node('p','u60-help','暂无可用选项'));return;
  }
  if(item.type==='form'){
   var body=openDialog(item.label),form=node('form','u60-form').attr('autocomplete','off'),inputs={};if(item.reason)body.append(node('p','u60-help',item.reason));
   (item.fields||[]).forEach(function(f,index){var id='u60-field-'+index,field;
    if(f.kind==='choice'){field=node('select','u60-input');(f.choices||[]).forEach(function(c){field.append(node('option','',c.label).val(String(c.value)));});}
    else if(f.kind==='textarea')field=node('textarea','u60-input').attr('rows',4);
    else field=node('input','u60-input').attr('type',f.kind==='password'?'password':f.kind==='number'?'number':'text');
    field.attr({id:id,name:f.key,autocomplete:f.kind==='password'?'new-password':'off'}).prop('required',!!f.required);if(f.kind==='number')field.attr('step','any');field.val(f.value==null?'':String(f.value));inputs[f.key]=field;
    form.append(node('label','u60-field-label',f.label).attr('for',id),field);
   });
   form.append(node('div','u60-dialog-actions').append(node('button','u60-secondary','取消').attr('type','button').on('click',closeDialog),node('button','u60-primary','继续').attr('type','submit')));
   form.on('submit',function(e){e.preventDefault();var values={};Object.keys(inputs).forEach(function(k){values[k]=inputs[k].val();});try{confirm(item,model.argumentsFor(item,null,values));}catch(error){body.find('.u60-form-error').remove();body.append(node('p','u60-form-error',error.message));}});body.append(form);return;
  }
  confirm(item,model.argumentsFor(item));
 }
 function renderSection(s){var card=node('section','u60-card').append(node('h3','u60-card-title',s.title));(s.items||[]).forEach(function(item){
  var enabled=model.interactive(item),row=node(enabled?'button':'div','u60-setting'+(enabled?' actionable':''));if(enabled)row.attr('type','button').on('click',function(){openItem(item);});
  row.append(node('span','u60-setting-label',item.label),node('span','u60-setting-value',item.value));if(enabled)row.append(node('span','u60-chevron','›').attr('aria-hidden','true'));else if(item.reason&&item.enabled===false)row.attr('title',item.reason);card.append(row);
 });return card;}
 function render(){if(!snapshot||!present())return;var content=root.find('.u60-content').empty(),d=snapshot.data||{};
  if(tab==='overview'){
   var metrics=node('div','u60-metrics'),relay=d.wifi_relay||{},usb=d.usb||{},clash=d.clash||{};
   var verdict=clash.verdict||'';var verdictLabel={takeover:'代理已生效',partial:'转发不完整',unverified:'未核验',core_down:'核心未运行',tailscale:'Tailscale 出口',error:'异常'}[verdict]||(clash.online?({rule:'规则分流',global:'全局代理',direct:'直连'}[clash.mode]||'运行中'):'已停止');
   var values=[['代理',verdictLabel],['Tailscale',({Running:'已连接',Stopped:'已停止',NeedsLogin:'待登录'})[d.tailscale_status]||d.tailscale_status],['USB 网口',d.usb_status],['上网模式',({clash:'Clash 代理',direct:'直连',tailscale:'Tailscale 出口'})[d.network_profile]||d.network_profile]];
   values.forEach(function(x){metrics.append(node('div','u60-metric').append(node('span','u60-metric-label',x[0]),node('strong','',x[1])));});content.append(metrics);
   if(verdict==='partial'||verdict==='unverified'||verdict==='core_down')content.append(node('p','u60-notice error','代理尚未完整生效：'+model.readable(clash.verdict_text||'请检查转发规则')));
   var shortcuts=node('div','u60-shortcuts');[['network','中继与网口','Wi-Fi 上游、AUTO / WAN / LAN'],['clash','Clash','订阅、节点、规则与连接'],['tailscale','Tailscale','组网设备、出口与子网路由'],['device','充电与深待机','充电上限、供电方向、待机服务']].forEach(function(x){shortcuts.append(node('button','u60-shortcut').append(node('strong','',x[1]),node('span','',x[2]),node('b','','↗')).on('click',function(){selectTab(x[0]);}));});content.append(shortcuts);
   content.append(node('p','u60-help','热点、蜂窝网络、流量套餐、短信及路由设置，请使用原厂菜单。'));

  }else{var grid=node('div','u60-grid');model.filterSections(snapshot.sections,tab).forEach(function(s){grid.append(renderSection(s));});content.append(grid);if(tab==='clash'||tab==='tailscale'){if(advancedData[tab])content.append(advanced.render(tab,advancedData[tab],advancedApi()));else content.append(node('p','u60-help','正在加载高级管理功能…'));}
   if(tab==='clash'){var clashData=d.clash||{},current=clashData.mode,modes=[['rule','规则分流','按规则自动分流'],['global','全局代理','全部经代理节点（需先选节点）'],['direct','直连','仅运行核心，不接管流量']],picker=node('section','u60-card u60-wide-card').append(node('h3','u60-card-title','分流模式'));var mrow=node('div','u60-card-body');modes.forEach(function(x){var b=node('button',x[0]===current?'u60-primary':'u60-secondary',x[1]+(x[0]===current?' · 当前':'')).attr('type','button').on('click',function(){openItem(act('切换为'+x[1],'web.clash.mode',{mode:x[0]}));});mrow.append(b,node('p','u60-help',x[2]));});picker.append(mrow);content.append(picker);
    /* First-run guide: without a subscription there is nothing to select, so
     * point at the exact next step instead of showing an empty node list. */
    var hasProvider=((advancedData.clash||{}).providers||[]).length>0;
    if(advancedData.clash&&!hasProvider){var guide=node('section','u60-card u60-wide-card').append(node('h3','u60-card-title','首次配置')),gb=node('div','u60-card-body').appendTo(guide);gb.append(node('p','u60-help','还没有订阅。按顺序完成：'));['添加 Mihomo proxy-provider 订阅','更新订阅并确认加载出节点','选择节点并确认回读','保持规则分流并开启代理','用手机断开移动数据验证代理'].forEach(function(step,i){gb.append(node('p','u60-help',(i+1)+'. '+step));});content.append(guide);}
   }
  }}
 function loadAdvanced(force){
  if(!present()||busy||reading||advancedReading||(tab!=='clash'&&tab!=='tailscale')||dialog&&dialog[0].open)return;
  /* Connections are the one live view: refresh faster there, and keep the
   * slower cadence everywhere else so routine state reads stay cheap. */
  var ttl=advanced.intervalFor?advanced.intervalFor(tab):20000;
  if(!force&&advancedData[tab]&&(Date.now()-(advancedAt[tab]||0)<ttl||Date.now()-lastInteraction<8000))return;var kind=tab,generation=alive;advancedReading=true;root.find('.u60-refresh').prop('disabled',true);
  call('web.'+kind+'.state',{}).then(function(r){if(generation!==alive||!present())return;if(!r||!r.ok){status(r&&r.message||'高级状态读取失败',true);return;}advancedData[kind]=r;advancedAt[kind]=Date.now();if(!(dialog&&dialog[0].open)&&tab===kind&&(force||Date.now()-lastInteraction>2000||!root.find('.u60-advanced').length))render();},function(e){if(generation===alive)status(e.message||String(e),true);}).always(function(){if(generation!==alive)return;advancedReading=false;root.find('.u60-refresh').prop('disabled',busy||reading);if(tab!==kind)loadAdvanced(false);});
 }
 function selectTab(value){tab=value;var target=hashFor(value);if(location.hash!==target)location.hash=target;render();root[0].scrollIntoView({block:'start'});loadAdvanced(false);}
 function init(){alive++;busy=false;reading=false;advancedReading=false;advancedData={};advancedAt={};snapshot=null;lastRead=0;tab=tabFromHash();root=$('#u60-enhanced');root.on('pointerdown keydown',function(){lastInteraction=Date.now();});dialog=root.find('dialog');dialog.on('close',function(){if(!dialog[0].open)dialog.empty();});dialog.on('click',function(e){if(e.target===dialog[0]){var r=dialog[0].getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)closeDialog();}});
  $(window).off('hashchange.u60Enhanced').on('hashchange.u60Enhanced',function(){if(!hashTabs[location.hash])return;tab=tabFromHash();render();loadAdvanced(true);});
  root.find('.u60-refresh').on('click',function(){refresh(true);});refresh(true);
  addInterval(function(){if(present()&&!document.hidden&&Date.now()-lastInteraction>8000&&!/^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement.tagName)&&Date.now()-lastRead>20000)refresh(false);},5000);
 }
 return {init:init};
});
