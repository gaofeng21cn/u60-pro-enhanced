const assert=require('assert');const m=require('../web/factory/u60-web-model.js');
assert(!m.interactive({type:'info',enabled:true,action:'state'}));
assert(!m.interactive({type:'toggle',enabled:false,action:'wifi.ap'}));
assert(m.interactive({type:'form',enabled:true,action:'usage.allowance'}));
const item={args:{section:'main_5g'},fields:[{key:'ssid',label:'SSID',required:true,kind:'text'},{key:'days',label:'Day',kind:'number'}]};
assert.deepStrictEqual(m.argumentsFor(item,null,{ssid:'A < B',days:'12'}),{section:'main_5g',ssid:'A < B',days:'12'});
assert.deepStrictEqual(item.args,{section:'main_5g'});
assert.throws(()=>m.argumentsFor(item,null,{ssid:'',days:'12'}));assert.throws(()=>m.argumentsFor(item,null,{ssid:'X',days:'NaN'}));
assert.deepStrictEqual(m.argumentsFor({args:{group:'GLOBAL'}},{args:{name:'node'}}),{group:'GLOBAL',name:'node'});
assert.strictEqual(m.bytes(1073741824,false),'1.0 GB');assert.strictEqual(m.bytes(null,true),'—');
const nodeGroups=[
 {name:'GLOBAL',nodes:['DIRECT','主策略'],selectable_nodes:[],selected:'DIRECT'},
 {name:'主策略',nodes:['DIRECT','Mojie'],selectable_nodes:[],selected:'Mojie'},
 {name:'Mojie',nodes:['Leaf 01','Leaf 02'],selectable_nodes:['Leaf 01','Leaf 02'],selected:'Leaf 02'}
];
assert.strictEqual(m.defaultNodeGroup(nodeGroups,'Mojie'),nodeGroups[2]);
assert.deepStrictEqual(m.selectableNodes(nodeGroups[1]),[]);
assert.strictEqual(m.defaultNodeGroup(nodeGroups,'missing'),nodeGroups[2]);
const sections=[
 {id:'wifi',items:[{id:'power',action:'wifi.power'},{id:'relay',type:'toggle',action:'wifi.relay.on',args:{enabled:true}},{id:'relay-scan',action:'wifi.relay.scan'}]},
 {id:'usb',items:[{id:'role',action:'usb.role'},{id:'status',type:'info'}]},
 {id:'router',items:[{id:'dns.edit',action:'router.dns'}]},
 {id:'usage',items:[{id:'usage.allowance',action:'usage.allowance'}]},
 {id:'battery',items:[{id:'charge.current',type:'info'},{id:'charge.policy',action:'charge.policy'},{id:'charge.manual',action:'charge.manual'},{id:'charge.capability',type:'report'},{id:'power.standby',action:'power.standby'}]},
 {id:'clash',items:[{id:'service',action:'clash.service'},{id:'node',action:'clash.select'},{id:'rules',action:'clash.rule_provider'}]},
 {id:'tailscale',items:[{id:'connected',action:'tailscale.connected'},{id:'peer_0',type:'info'},{id:'peer-test',action:'tailscale.peer_test'}]},
 {id:'signal',items:[{id:'signal.serving',action:'signal.serving'},{id:'signal.scan',enabled:false,action:'signal.scan'}]}
];
const before=JSON.stringify(sections);
const network=m.filterSections(sections,'network');
assert.deepStrictEqual(network.map(x=>x.id),['wifi-relay','usb-adapter']);
assert.deepStrictEqual(network[0].items.map(x=>x.id),['relay','relay-scan']);
assert.strictEqual(network[0].items[0],sections[0].items[1]); // Control payload is unchanged.
assert.deepStrictEqual(m.filterSections(sections,'device')[0].items.map(x=>x.id),['charge.manual','charge.policy','power.standby']);
assert.deepStrictEqual(m.filterSections(sections,'clash')[0].items.map(x=>x.id),['service','rules']);
assert.deepStrictEqual(m.filterSections(sections,'tailscale')[0].items.map(x=>x.id),['connected']);
assert.deepStrictEqual(m.filterSections(sections,'more')[0].items.map(x=>x.id),['signal.serving']);
assert.deepStrictEqual(m.filterSections([{id:'wifi',items:[{id:'power'}]}],'network'),[]);
assert.deepStrictEqual(m.filterSections(sections,'unknown'),[]);
assert.strictEqual(JSON.stringify(sections),before);
const usb=m.filterSections([{id:'usb',items:[{id:'macnet.mode',type:'info'},{id:'macnet.link',type:'info'},{id:'macnet.help',type:'info'}]}],'network')[0];
assert.strictEqual(usb.id,'usb-cable');assert.strictEqual(usb.items.length,3);assert(usb.items.every(x=>!m.interactive(x)));
const power=m.filterSections(sections,'device');assert.strictEqual(power[1].id,'power-status');assert.strictEqual(power[1].items[0].id,'charge.current');
console.log('web model: passed; stock controls excluded, additions preserved, snapshot unchanged');
