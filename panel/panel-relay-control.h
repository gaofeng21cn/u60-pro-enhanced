#include "panel-relay-frequency.h"
/* Screen orchestration only; credentials travel through stdin, never argv. */
static int relay_enabled(void){return fixture?cJSON_IsTrue(jget(jget(fixture,"relay.status"),"enabled")):access("/data/u60-panel/relay-private/enabled",F_OK)==0;}
static cJSON *relay_run(const char*command,const cJSON*args){
 if(fixture){if(!strcmp(command,"status"))return mock("relay.status");if(!strcmp(command,"scan"))return mock("relay.scan");fixture_write_count++;return reply(!cJSON_IsTrue(jget(fixture,"reject_writes")),"测试中继请求");}
 char out[32768];char*body=args?cJSON_PrintUnformatted(args):NULL;
 int binary=!strcmp(command,"connect")||!strcmp(command,"scan");const char*path=binary?"/data/u60-panel/panel-relay":"/data/u60-panel/wifi-relay.sh";char*v[]={(char*)path,(char*)command,NULL};int ok=run_cmd(path,v,body,out,sizeof(out));if(body){memset(body,0,strlen(body));free(body);}
 if(binary||!strcmp(command,"status")){cJSON*r=cJSON_Parse(out);if(ok&&cJSON_IsObject(r))return r;cJSON_Delete(r);return reply(0,"中继操作未确认，请查看当前上游状态");}
 return reply(ok,ok?(!strcmp(command,"off")?"中继已断开，恢复原出口；已保存的网络仍保留":!strcmp(command,"forget")?"已忘记保存的上游网络":"正在连接已保存的上游"):"操作未完成，请确认USB为LAN、5G热点开启、访客热点关闭，且协调服务可运行");
}
static cJSON *relay_action(const char*action,const cJSON*args){
 if(!strcmp(action,"wifi.relay.policy")){
  const char *key=jstr(args,"key"),*value=jstr(args,"value");
  if(!((!strcmp(key,"fallback")&&(!strcmp(value,"cellular")||!strcmp(value,"wifi-only")))||(!strcmp(key,"autostart")&&(!strcmp(value,"0")||!strcmp(value,"1")))))return reply(0,"无效的接力策略");
  if(fixture){fixture_write_count++;return reply(!cJSON_IsTrue(jget(fixture,"reject_writes")),"接力策略已保存");}
  char out[512];char *v[]={"wifi-relay.sh","policy",(char*)key,(char*)value,NULL};
  int ok=run_cmd("/data/u60-panel/wifi-relay.sh",v,NULL,out,sizeof(out));return reply(ok,ok?"接力策略已应用并保存":"策略未能完整应用，请查看当前状态");
 }
 if(!strcmp(action,"wifi.relay.forget")){if(relay_enabled())return reply(0,"请先关闭 Wi-Fi 接力，再忘记网络");return relay_run("forget",NULL);}
 if(!strcmp(action,"wifi.relay.off"))return relay_run("off",NULL);
 if(!strcmp(action,"wifi.relay.on"))return relay_run("on",NULL);
 if(!strcmp(action,"wifi.relay.connect"))return relay_run("connect",args);
 if(!strcmp(action,"wifi.relay.select")){
  const char*ssid=jstr(args,"ssid"),*security=jstr(args,"security"),*bssid=jstr(args,"bssid");
  const cJSON*fv=jget(args,"frequency");int freq=cJSON_IsNumber(fv)?fv->valueint:0;
  if(!relay_frequency(freq))return reply(0,"仅支持2.4GHz或非DFS的5GHz；请将上游改为36–48或149–165信道");
  if(!*ssid||strlen(ssid)>32||strlen(bssid)!=17||(strcmp(security,"WPA2")&&strcmp(security,"WPA3")&&strcmp(security,"OPEN")))return reply(0,"该网络暂不支持，请选择个人Wi-Fi");
  cJSON*r=reply(1,"在屏幕输入上游密码"),*s=cJSON_CreateObject();cJSON_AddArrayToObject(s,"items");
  cJSON*i=item(s,"relay-connect",ssid,"form","输入密码","wifi.relay.connect",1,"连接并在U60保存上游凭据，按开机连接与断线策略运行。支持双频热点；同频热点信道会跟随上游，IPv6互联网暂停。");
  cJSON_AddStringToObject(jget(i,"args"),"ssid",ssid);cJSON_AddStringToObject(jget(i,"args"),"security",security);cJSON_AddStringToObject(jget(i,"args"),"bssid",bssid);cJSON_AddNumberToObject(jget(i,"args"),"frequency",freq);
  cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(0));
  if(strcmp(security,"OPEN")){field(i,"password","上游 Wi-Fi 密码","","password",1);cJSON*f=cJSON_GetArrayItem(jget(i,"fields"),0);cJSON_AddNumberToObject(f,"minLength",8);cJSON_AddNumberToObject(f,"maxLength",63);}else field(i,"password","开放网络，无需密码","","password",0);
  cJSON_AddItemToObject(r,"picker",cJSON_DetachItemFromArray(jget(s,"items"),0));cJSON_Delete(s);return r;
 }
 if(!strcmp(action,"wifi.relay.scan")){
  cJSON*r=relay_run("scan",NULL);if(!r||!cJSON_IsTrue(jget(r,"ok")))return r?r:reply(0,"扫描不可用");
  cJSON*out=reply(1,"请选择上游Wi-Fi"),*picker=cJSON_AddObjectToObject(out,"picker"),*choices=cJSON_AddArrayToObject(picker,"choices");
  cJSON_AddStringToObject(picker,"label","选择上游 Wi-Fi");cJSON_AddStringToObject(picker,"type","choice");cJSON_AddStringToObject(picker,"action","wifi.relay.select");cJSON_AddBoolToObject(picker,"enabled",1);cJSON_AddBoolToObject(picker,"confirm",0);
  int hidden=0;
  cJSON*n;cJSON_ArrayForEach(n,jget(r,"networks")){
   if(strcmp(jstr(n,"security"),"unsupported")==0){hidden++;continue;}
   int f=jget(n,"frequency")?jget(n,"frequency")->valueint:0;
   if(!relay_frequency(f)){hidden++;continue;}
   char label[128];snprintf(label,sizeof(label),"%s · %s",f<3000?"2.4G":"5G",jstr(n,"ssid"));
   double signal=jget(n,"signal")?jget(n,"signal")->valuedouble:-150;
   int duplicate=-1,at=0;cJSON*old;
   cJSON_ArrayForEach(old,choices){if(!strcmp(jstr(old,"label"),label)&&!strcmp(jstr(jget(old,"args"),"security"),jstr(n,"security"))){duplicate=at;break;}at++;}
   if(duplicate>=0){if(jget(old,"_signal")->valuedouble>=signal)continue;cJSON_DeleteItemFromArray(choices,duplicate);}
   cJSON*c=cJSON_CreateObject();cJSON_AddStringToObject(c,"label",label);cJSON_AddNumberToObject(c,"_signal",signal);
   char desc[64];snprintf(desc,sizeof(desc),"信道%d · %s · %.0f dBm",f<3000?(f-2407)/5:(f-5000)/5,jstr(n,"security"),signal);cJSON_AddStringToObject(c,"description",desc);
   cJSON*args=cJSON_AddObjectToObject(c,"args");for(int k=0;k<3;k++){const char*key=(const char*[]){"ssid","bssid","security"}[k];cJSON_AddStringToObject(args,key,jstr(n,key));}
   cJSON_AddNumberToObject(args,"frequency",jget(n,"frequency")->valueint);
   at=0;cJSON_ArrayForEach(old,choices){if(jget(old,"_signal")->valuedouble<signal)break;at++;}cJSON_InsertItemInArray(choices,at,c);
  }
  int n2=0,n5=0;cJSON*choice;cJSON_ArrayForEach(choice,choices){cJSON_DeleteItemFromObject(choice,"_signal");if(jget(jget(choice,"args"),"frequency")->valueint<3000)n2++;else n5++;}
  char summary[180];snprintf(summary,sizeof(summary),"2.4G %d / 5G %d · 不兼容 %d",n2,n5,hidden);
  cJSON_AddStringToObject(picker,"description",summary);
  cJSON_AddStringToObject(picker,"reason","支持2.4GHz与非DFS的5GHz。近距离优先5GHz，隔墙或远距离可选2.4GHz。DFS、6GHz和不支持的认证网络已隐藏；缺少5GHz时检查上游信道36–48或149–165。");
  cJSON_Delete(r);if(!cJSON_GetArraySize(choices)){cJSON_Delete(out);return reply(0,hidden?"附近网络暂不支持：DFS/6GHz/企业认证不可连接。5GHz请改用36–48或149–165信道。":"未发现网络，请靠近上游重扫；支持2.4GHz和非DFS的5GHz。");}return out;
 }
 return reply(0,"未知中继操作");
}
static void relay_items(cJSON*s,cJSON*data){
 if(fixture&&!jget(fixture,"relay.status"))return;
 cJSON*r=relay_run("status",NULL);int known=cJSON_IsTrue(jget(r,"ok")),enabled=cJSON_IsTrue(jget(r,"enabled")),active=cJSON_IsTrue(jget(r,"active")),saved=cJSON_IsTrue(jget(r,"saved"));
 const char*state=jstr(r,"state"),*text=!known?"不可用":active?"Wi-Fi 上游":!enabled?"关闭":!strcmp(state,"CONFLICT")?"网段冲突 · 原出口":!strcmp(state,"POLICY")?"热点/USB冲突 · 原出口":!strcmp(state,"SERVICE_DOWN")?"协调服务未运行":!strcmp(state,"ERROR")?"故障 · 请查看策略":"未连通 · 原出口";
 cJSON*i=item(s,"relay",enabled?"停止 Wi-Fi 中继":saved?"启动 Wi-Fi 中继":"Wi-Fi 中继",enabled||saved?"action":"info",text,enabled?"wifi.relay.off":saved?"wifi.relay.on":"wifi.relay.scan",known,enabled?"停止后恢复蜂窝等原出口；热点保持开启，已保存的上游密码保留。再次连接可点启动。":"连接已保存的上游；同频热点可能短暂重连，是否回退蜂窝由断线策略决定。");cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(enabled||saved));
 cJSON*scan_item=item(s,"relay-scan",saved?"连接 / 更换上游 Wi-Fi":"连接上游 Wi-Fi","action","扫描 2.4G / 5G","wifi.relay.scan",known,enabled?"扫描时保留当前中继；确认新网络后切换，失败尝试恢复原网络":"请开启5G热点、关闭访客热点，USB设为LAN");
 cJSON_ReplaceItemInObject(scan_item,"confirm",cJSON_CreateBool(0));
 if(active){int freq=jget(r,"frequency")?jget(r,"frequency")->valueint:0;const char*label=freq>0&&freq<3000?"2.4G 上游中继":freq>=5000?"5G 上游中继":"Wi-Fi 上游中继";cJSON_ReplaceItemInObject(data,"wifi_status",cJSON_CreateString(label));cJSON_ReplaceItemInObject(i,"value",cJSON_CreateString(label));}
 const char *health=jstr(r,"health");
 item(s,"relay-health","上游互联网","info",!active?"尚未接入上游":!strcmp(health,"ONLINE")?"互联网探测通过":!strcmp(health,"PORTAL")?"可能需要网页认证":!strcmp(health,"UNREACHABLE")?"探测未通过 · 请检查上游":"尚未探测",NULL,0,"探测固定公网 204 地址；失败可能是上游或检测站点限制，不自动切换出口");
 cJSON *policy=item(s,"relay-fallback","接力断线策略","choice",!strcmp(jstr(r,"fallback"),"wifi-only")?"禁止蜂窝回退":"允许蜂窝回退","wifi.relay.policy",known,"禁止回退时，接力开启期间阻断设备与下游经蜂窝的数据包，含本机代理；不关闭基带，不保证整机零流量。关闭接力会恢复原出口；远程蜂窝管理也可能断开。");
 cJSON_AddStringToObject(jget(policy,"args"),"key","fallback");choice(policy,"允许蜂窝回退","value","cellular");choice(policy,"禁止蜂窝回退","value","wifi-only");
 int auto_on=!cJSON_IsNumber(jget(r,"autostart"))||jget(r,"autostart")->valueint;
 policy=item(s,"relay-autostart","开机连接已保存网络","choice",auto_on?"开启":"关闭","wifi.relay.policy",known,"仅在关机前接力处于开启状态时恢复；关闭此项不影响当前连接。");
 cJSON_AddStringToObject(jget(policy,"args"),"key","autostart");choice(policy,"开启","value","1");choice(policy,"关闭","value","0");
 item(s,"relay-forget","忘记保存的上游","action","删除此网络的本机凭据","wifi.relay.forget",saved&&!enabled,"请先关闭接力；忘记后再次连接需要重新输入密码");
 cJSON_AddItemToObject(data,"wifi_relay",r?r:cJSON_CreateObject());
 // Front-load the relay controls without moving the existing hotspot controls apart.
 cJSON*items=jget(s,"items");if(enabled){cJSON*it;cJSON_ArrayForEach(it,items){const char*action=jstr(it,"action");if(!strncmp(action,"wifi.",5)&&strncmp(action,"wifi.relay.",11)&&strcmp(action,"wifi.power")&&strcmp(action,"wifi.sleep")&&strcmp(action,"wifi.show_password")&&!( !strcmp(action,"wifi.ap")&&!strcmp(jstr(jget(it,"args"),"section"),"main_2g"))){cJSON_ReplaceItemInObject(it,"enabled",cJSON_CreateBool(0));cJSON_ReplaceItemInObject(it,"reason",cJSON_CreateString("中继期间允许切换2.4G；其他热点设置请先断开中继"));}}}
 i=cJSON_DetachItemViaPointer(items,i);cJSON_InsertItemInArray(items,0,i);
 {cJSON*scan;cJSON_ArrayForEach(scan,items)if(!strcmp(jstr(scan,"id"),"relay-scan")){cJSON_DetachItemViaPointer(items,scan);cJSON_InsertItemInArray(items,1,scan);break;}}
}
