#ifndef PANEL_MENU_LAYOUT_H
#define PANEL_MENU_LAYOUT_H
/* Screen-only projection of the Clash menu: keep daily controls on the first
 * page and push configuration work into an explicit "更多" entry. Action
 * payloads are never rebuilt, only reordered or moved between sections. */
/* Screen presentation only. Keep action payloads and backend state intact. */
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
 if(*action||!strcmp(type,"report"))return 0;
 return 2;
}
static int menu_priority(cJSON *item,const char *section){
 if(!strcmp(menu_str(item,"id"),"menu.unavailable"))return 104;
 if(!strcmp(section,"clash")){
  /* The proxy page is curated: state, switch, mode, quick picks, then the
   * verdict, so the first screen answers "how do I get online" and "is it
   * actually on". Everything else falls back to the generic ranking. */
  static const char *const daily[]={"service","mode","node","favorite","recent","favorite-current","coverage","diagnose","scope"};
  for(int n=0;n<9;n++)if(!strcmp(menu_str(item,"id"),daily[n]))return n;
  if(menu_rank(item))return 100+menu_rank(item);
  return 20;
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
static void panel_menu_layout(cJSON *root){
 cJSON *sys=menu_section(root,"system"),*battery=menu_section(root,"battery");
 /* Clash keeps daily controls first; configuration-heavy entries are marked so
   * the scrolling list shows an obvious split instead of one flat wall. */
 cJSON *clash=menu_section(root,"clash");
 if(clash){
  /* The bottom tab reads "代理"; keep the header consistent on the screen. */
  cJSON_ReplaceItemInObjectCaseSensitive(clash,"title",cJSON_CreateString("代理"));
  static const char *const advanced[]={"clash.provider","rule-providers","connections","dns","add-rule"};
  cJSON *it;cJSON_ArrayForEach(it,cJSON_GetObjectItemCaseSensitive(clash,"items")){
   const char *id=menu_str(it,"id"),*label=menu_str(it,"label");
   if(!*label||!strncmp(label,"更多 · ",9))continue;
   int move=!strncmp(id,"local-rule-",11);
   for(size_t n=0;n<sizeof(advanced)/sizeof(advanced[0])&&!move;n++)if(!strcmp(id,advanced[n]))move=1;
   if(move){char next[96];snprintf(next,sizeof(next),"更多 · %s",label);cJSON_ReplaceItemInObjectCaseSensitive(it,"label",cJSON_CreateString(next));}
  }
 }
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
