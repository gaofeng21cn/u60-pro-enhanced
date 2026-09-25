#ifndef PANEL_MENU_LAYOUT_H
#define PANEL_MENU_LAYOUT_H
/* Screen-only grouping. Backend state and action payloads remain authoritative. */
static const char *menu_str(cJSON *o,const char *key){
 cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsString(v)?v->valuestring:"";
}
static cJSON *menu_section(cJSON *root,const char *id){
 cJSON *s;cJSON_ArrayForEach(s,cJSON_GetObjectItemCaseSensitive(root,"sections"))
  if(!strcmp(menu_str(s,"id"),id))return s;
 return NULL;
}
static int menu_rank(cJSON *item){
 const char *type=menu_str(item,"type"),*action=menu_str(item,"action");
 if(!strcmp(type,"info"))return 2;
 if(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(item,"enabled")))return 3;
 if(*action||!strcmp(type,"report")||!strcmp(type,"navigation"))return 0;
 return 2;
}
static int menu_priority(cJSON *item,const char *section){
 if(!strcmp(menu_str(item,"id"),"menu.unavailable"))return 104;
 if(!strcmp(section,"clash")){
  static const char *const daily[]={"coverage","service","mode","node","favorite","recent","favorite-current","menu.clash-more"};
  for(int n=0;n<8;n++)if(!strcmp(menu_str(item,"id"),daily[n]))return n;
  if(menu_rank(item))return 100+menu_rank(item);
  return 20;
 }
 if(!strcmp(section,"clash-more")&&menu_rank(item)==0){
  static const char *const actions[]={"clash.provider","clash.rule_provider","clash.rule_add","clash.diagnose","diag.connections","clash.close_connections","clash.delay","clash.flush_dns","clash.about"};
  for(int n=0;n<9;n++)if(!strcmp(menu_str(item,"action"),actions[n]))return n;
 }
 if(!strcmp(section,"tailscale")){
  static const char *const daily[]={"backend","connected","lan-gateway","advertise_lan","self-ip","menu.tailscale-more"};
  for(int n=0;n<6;n++)if(!strcmp(menu_str(item,"id"),daily[n]))return n;
 }
 if(menu_rank(item))return 100+menu_rank(item);
 if(!strcmp(section,"system")){
  const char *ids[]={"theme","brightness","blank","system.fastboot"};
  for(int n=0;n<4;n++)if(!strcmp(menu_str(item,"id"),ids[n]))return n;
 }
 return 10;
}
static void menu_line(char *buf,size_t cap,const char *label,const char *value){
 size_t n=strlen(buf);if(n<cap)snprintf(buf+n,cap-n,"%s：%s\n",label,*value?value:"未知");
}
static cJSON *menu_report(const char *id,const char *label,const char *value,const char *detail){
 cJSON *r=cJSON_CreateObject();cJSON_AddStringToObject(r,"id",id);cJSON_AddStringToObject(r,"label",label);
 cJSON_AddStringToObject(r,"value",value);cJSON_AddStringToObject(r,"detail",detail);
 cJSON_AddStringToObject(r,"type","report");cJSON_AddBoolToObject(r,"enabled",1);return r;
}
static int menu_problem(const char *text){
 return strstr(text,"错误")||strstr(text,"损坏")||strstr(text,"暂停")||strstr(text,"待处理")||strstr(text,"未完成")||strstr(text,"不完整")||strstr(text,"未验收");
}
static void menu_compact(cJSON *section){
 cJSON *items=cJSON_GetObjectItemCaseSensitive(section,"items");int battery=!strcmp(menu_str(section,"id"),"battery");
 char detail[8192]="",summary[160]="查看实时状态";int count=0,unavailable=0,fault=0;
 for(int n=0;n<cJSON_GetArraySize(items);){
  cJSON *it=cJSON_GetArrayItem(items,n);int info=!strcmp(menu_str(it,"type"),"info");
  int blocked=!info&&cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(it,"enabled"));
  if((battery&&info)||blocked){
   const char *id=menu_str(it,"id"),*label=menu_str(it,"label"),*value=menu_str(it,"value");
   if(!strcmp(id,"charge.capability"))label="充电控制校验";
   if(blocked){menu_line(detail,sizeof(detail),label,menu_str(it,"reason"));unavailable++;}
   else menu_line(detail,sizeof(detail),label,value);
   if(battery&&!fault&&!strcmp(id,"usb.charge_state"))snprintf(summary,sizeof(summary),"%s",value);
   if(battery&&(!strcmp(id,"charge.policy_status")||!strcmp(id,"power.standby_state"))&&menu_problem(value)){
    snprintf(summary,sizeof(summary),"%s",value);fault=1;
   }
   cJSON_DeleteItemFromArray(items,n);count++;
  }else n++;
 }
 if(count){
  if(battery)cJSON_AddItemToArray(items,menu_report("menu.battery_details","电池与待机详情",summary,detail));
  else {snprintf(summary,sizeof(summary),"%d 项 · 查看原因",unavailable);cJSON_AddItemToArray(items,menu_report("menu.unavailable","暂不可用",summary,detail));}
 }
}
/* Create real child menus instead of decorating an otherwise flat long list.
 * Detach the original objects so confirmations, gates and args stay intact. */
static void menu_group(cJSON *root,const char *source,const char *child,const char *title,const char *note){
 cJSON *section=menu_section(root,source);if(!section||menu_section(root,child))return;
 cJSON *items=cJSON_GetObjectItemCaseSensitive(section,"items"),*more=cJSON_CreateObject();
 cJSON_AddStringToObject(more,"id",child);cJSON_AddStringToObject(more,"title",title);cJSON_AddStringToObject(more,"parent",source);
 cJSON *dest=cJSON_AddArrayToObject(more,"items");
 for(int n=0;n<cJSON_GetArraySize(items);){
  cJSON *it=cJSON_GetArrayItem(items,n);const char *id=menu_str(it,"id");
  int keep=!*id;
  const char *clash[]={"coverage","service","mode","node","favorite","recent","favorite-current","global-target"};
  const char *tailscale[]={"backend","connected","lan-gateway","advertise_lan","self-ip"};
  const char **daily=!strcmp(source,"clash")?clash:tailscale;size_t count=!strcmp(source,"clash")?8:5;
  for(size_t k=0;k<count;k++)if(!strcmp(id,daily[k]))keep=1;
  if(keep)n++;else {cJSON_DetachItemViaPointer(items,it);cJSON_AddItemToArray(dest,it);}
 }
 if(!cJSON_GetArraySize(dest)){cJSON_Delete(more);return;}
 cJSON_AddItemToArray(cJSON_GetObjectItemCaseSensitive(root,"sections"),more);
 cJSON *link=cJSON_CreateObject();char id[64];snprintf(id,sizeof(id),"menu.%s",child);
 cJSON_AddStringToObject(link,"id",id);cJSON_AddStringToObject(link,"type","navigation");cJSON_AddStringToObject(link,"label",title);
 cJSON_AddStringToObject(link,"value",note);cJSON_AddStringToObject(link,"section",child);cJSON_AddBoolToObject(link,"enabled",1);cJSON_AddItemToArray(items,link);
}
static void panel_menu_layout(cJSON *root){
 cJSON *sys=menu_section(root,"system"),*battery=menu_section(root,"battery"),*clash=menu_section(root,"clash");
 if(clash){
  cJSON_ReplaceItemInObjectCaseSensitive(clash,"title",cJSON_CreateString("代理"));
  cJSON *connections=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,"data"),"clash"),"connections");
  cJSON *it;cJSON_ArrayForEach(it,cJSON_GetObjectItemCaseSensitive(clash,"items")){
   if(!strcmp(menu_str(it,"id"),"connections")&&cJSON_IsNumber(connections)&&connections->valuedouble==0){
    cJSON_ReplaceItemInObjectCaseSensitive(it,"type",cJSON_CreateString("info"));
    cJSON_ReplaceItemInObjectCaseSensitive(it,"value",cJSON_CreateString("暂无活动连接"));
    cJSON_ReplaceItemInObjectCaseSensitive(it,"enabled",cJSON_CreateBool(0));
   }
  }
  cJSON_ArrayForEach(it,cJSON_GetObjectItemCaseSensitive(clash,"items"))if(!strcmp(menu_str(it,"id"),"coverage")){
   cJSON_ReplaceItemInObjectCaseSensitive(it,"type",cJSON_CreateString("report"));
   cJSON_ReplaceItemInObjectCaseSensitive(it,"label",cJSON_CreateString("实际代理状态"));
   cJSON_ReplaceItemInObjectCaseSensitive(it,"enabled",cJSON_CreateBool(1));
  }
 }
 menu_group(root,"clash","clash-more","代理管理","订阅、规则与诊断");
 cJSON *ts=menu_section(root,"tailscale");
 if(ts){
  cJSON_ReplaceItemInObjectCaseSensitive(ts,"title",cJSON_CreateString("组网"));
  cJSON *items=cJSON_GetObjectItemCaseSensitive(ts,"items"),*it;int online=0;char peers[8192]="";int count=0;
  cJSON_ArrayForEach(it,items)if(!strcmp(menu_str(it,"id"),"backend")){
   const char *value=menu_str(it,"value");online=!strcmp(value,"Running")||!strcmp(value,"已连接");
   const char *raw[]={"Running","Stopped","NeedsLogin","NeedsMachineAuth","Starting","NoState"};
   const char *zh[]={"已连接","已停止","需要登录","等待授权","连接中","状态未知"};
   for(int k=0;k<6;k++)if(!strcmp(value,raw[k])){cJSON_ReplaceItemInObjectCaseSensitive(it,"value",cJSON_CreateString(zh[k]));break;}
  }
  for(int n=0;n<cJSON_GetArraySize(items);){
   it=cJSON_GetArrayItem(items,n);
   if(!strncmp(menu_str(it,"id"),"peer_",5)){
    menu_line(peers,sizeof(peers),menu_str(it,"label"),menu_str(it,"value"));cJSON_DeleteItemFromArray(items,n);count++;
   }else {if(!online&&!strcmp(menu_str(it,"id"),"self-ip"))cJSON_ReplaceItemInObjectCaseSensitive(it,"label",cJSON_CreateString("最近分配的地址"));n++;}
  }
  if(count){char summary[80];snprintf(summary,sizeof(summary),"%d 台 · %s",count,online?"查看状态":"最近记录");
   if(!online){char detail[8500];snprintf(detail,sizeof(detail),"组网未连接，以下为最近获取的设备记录，不能据此判断当前是否可达。\n\n%s",peers);cJSON_AddItemToArray(items,menu_report("menu.peers","组网设备记录",summary,detail));}
   else cJSON_AddItemToArray(items,menu_report("menu.peers","组网设备",summary,peers));
  }
 }
 menu_group(root,"tailscale","tailscale-more","组网管理","设备、出口与高级设置");
 if(battery){
  cJSON_ReplaceItemInObjectCaseSensitive(battery,"title",cJSON_CreateString("电池与省电"));
  cJSON *src=cJSON_GetObjectItemCaseSensitive(sys,"items"),*dst=cJSON_GetObjectItemCaseSensitive(battery,"items"),*it;
  cJSON_ArrayForEach(it,src)if(!strcmp(menu_str(it,"id"),"system.saver")){
   cJSON_DetachItemViaPointer(src,it);cJSON_AddItemToArray(dst,it);break;
  }
  cJSON_ArrayForEach(it,dst){
   if(!strcmp(menu_str(it,"id"),"system.saver"))
    cJSON_ReplaceItemInObjectCaseSensitive(it,"label",cJSON_CreateString("原厂节能开关"));
   if(!strcmp(menu_str(it,"id"),"power.standby")){
    cJSON_ReplaceItemInObjectCaseSensitive(it,"label",cJSON_CreateString("待机服务策略"));
    cJSON *ch;cJSON_ArrayForEach(ch,cJSON_GetObjectItemCaseSensitive(it,"choices"))
     if(!strcmp(menu_str(cJSON_GetObjectItemCaseSensitive(ch,"args"),"mode"),"deep"))
      cJSON_ReplaceItemInObjectCaseSensitive(ch,"label",cJSON_CreateString("空闲暂停服务"));
    if(!strcmp(menu_str(it,"value"),"深度省电"))
     cJSON_ReplaceItemInObjectCaseSensitive(it,"value",cJSON_CreateString("空闲暂停服务"));
   }
  }
 }
 cJSON *section;cJSON_ArrayForEach(section,cJSON_GetObjectItemCaseSensitive(root,"sections")){
  menu_compact(section);
  cJSON *items=cJSON_GetObjectItemCaseSensitive(section,"items");
  /* Stable insertion sort: controls first, then status, then unavailable items.
   * Never reconstruct items: choice args, form drafts and safety gates survive. */
  for(int n=1;n<cJSON_GetArraySize(items);n++){
   cJSON *it=cJSON_GetArrayItem(items,n);int priority=menu_priority(it,menu_str(section,"id")),at=n;
   while(at>0&&menu_priority(cJSON_GetArrayItem(items,at-1),menu_str(section,"id"))>priority)at--;
   if(at<n){cJSON_DetachItemViaPointer(items,it);cJSON_InsertItemInArray(items,at,it);}
  }
 }
}
#endif
