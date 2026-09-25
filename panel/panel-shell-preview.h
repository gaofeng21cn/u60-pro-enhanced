/* Offline fixtures only; include after shell UI. Never invokes actual settings. */
#include <assert.h>
static void shell_preview_fixture(struct app*a){
 memset(a,0,sizeof(*a));a->bat=86;a->temp=38;a->clash_online=1;strcpy(a->operator,"中国电信");strcpy(a->net_type,"5G SA");strcpy(a->ts_state,"已连接");strcpy(a->wifi_on,"1");strcpy(a->usb_mode,"网络共享");
 a->shell.snapshot=cJSON_Parse("{\"ok\":true,\"data\":{\"download_bps\":12792627,\"upload_bps\":1814036,\"today_bytes\":2329282334,\"month_bytes\":72209832263,\"battery\":86,\"temperature\":38,\"clients\":4,\"operator\":\"中国电信\",\"network\":\"5G SA\",\"wifi_status\":\"双频已开启\",\"usb_status\":\"USB 网络共享\",\"clash_status\":\"运行中 · 规则\",\"tailscale_status\":\"已连接\"},\"sections\":[{\"id\":\"wifi\",\"title\":\"Wi-Fi\",\"items\":[{\"label\":\"Wi-Fi 开关\",\"value\":true,\"type\":\"toggle\",\"action\":\"wifi.toggle\",\"args\":{\"enabled\":false}},{\"label\":\"无线名称与密码\",\"value\":\"U60-PRO\",\"type\":\"form\",\"action\":\"wifi.settings\",\"fields\":[{\"key\":\"ssid\",\"label\":\"网络名称\",\"value\":\"U60-PRO\",\"required\":true},{\"key\":\"password\",\"label\":\"Wi-Fi 密码\",\"kind\":\"password\",\"value\":\"preview-only\"},{\"key\":\"band\",\"label\":\"频段\",\"kind\":\"choice\",\"value\":\"5g\",\"choices\":[{\"label\":\"2.4 GHz\",\"value\":\"2g\"},{\"label\":\"5 GHz\",\"value\":\"5g\"}]}]},{\"label\":\"MLO\",\"value\":\"未提供\",\"type\":\"toggle\",\"enabled\":false,\"reason\":\"设备尚未提供可靠的开关状态，因此暂不能修改。\"}]},{\"id\":\"clash\",\"title\":\"Clash\",\"items\":[{\"label\":\"代理服务\",\"value\":\"运行中\",\"type\":\"info\"},{\"label\":\"工作模式\",\"value\":\"规则\",\"type\":\"choice\",\"action\":\"clash.mode\",\"choices\":[{\"label\":\"规则\",\"args\":{\"mode\":\"rule\"}},{\"label\":\"全局\",\"args\":{\"mode\":\"global\"}},{\"label\":\"直连\",\"args\":{\"mode\":\"direct\"}}]},{\"label\":\"默认代理\",\"value\":\"香港 · 优选 01\",\"type\":\"choice\",\"action\":\"clash.select\",\"choices\":[{\"label\":\"香港 · 优选 01\",\"args\":{\"name\":\"香港 · 优选 01\"}},{\"label\":\"日本 · 东京 02\",\"args\":{\"name\":\"日本 · 东京 02\"}}]},{\"label\":\"订阅剩余额度\",\"value\":\"168.4 / 200 GB\",\"type\":\"info\"},{\"label\":\"当前连接\",\"value\":\"18 个\",\"type\":\"info\"}]},{\"id\":\"tailscale\",\"title\":\"Tailscale\",\"items\":[{\"label\":\"组网状态\",\"value\":\"已连接\",\"type\":\"info\"},{\"label\":\"设备地址\",\"value\":\"100.64.0.2\",\"type\":\"info\"},{\"label\":\"断开组网\",\"type\":\"action\",\"action\":\"tailscale.down\",\"confirm\":true}]}]}");
 assert(a->shell.snapshot);
 cJSON_AddItemToArray(sh_get(a->shell.snapshot,"sections"),cJSON_Parse("{\"id\":\"cell\",\"title\":\"蜂窝网络\",\"items\":[{\"id\":\"signal\",\"label\":\"信号格数\",\"type\":\"info\",\"value\":\"5\"}]}"));
 cJSON*d=sh_get(a->shell.snapshot,"data");cJSON_AddNumberToObject(d,"cpu_percent",12.123456);cJSON_AddNumberToObject(d,"memory_percent",48.345678);cJSON_AddStringToObject(d,"network_profile","clash");cJSON_AddNumberToObject(d,"uptime_seconds",13320);cJSON_AddStringToObject(d,"clock_text","14:32");cJSON_AddStringToObject(d,"signal","-83 dBm");cJSON_AddStringToObject(d,"band","n78");cJSON_AddStringToObject(d,"physical_iface","蜂窝");cJSON_AddItemToObject(d,"clash",cJSON_Parse("{\"online\":true,\"mode\":\"rule\",\"node\":\"香港 · 优选 01\",\"upload\":127534254,\"download\":2132353321,\"quota_remaining\":180818123161,\"connections\":18}"));
}
static void shell_preview_hits(struct app*a){int i,j;for(i=0;i<a->nhits;i++){assert(a->hits[i].id!=SH_REFRESH && a->hits[i].id!=SH_PREV && a->hits[i].id!=SH_NEXT && a->hits[i].id!=SH_FIELD_PREV && a->hits[i].id!=SH_FIELD_NEXT);assert(a->hits[i].x0>=0&&a->hits[i].y0>=0&&a->hits[i].x1<=320&&a->hits[i].y1<=480);for(j=0;j<i;j++)assert(!(a->hits[i].x0<a->hits[j].x1&&a->hits[i].x1>a->hits[j].x0&&a->hits[i].y0<a->hits[j].y1&&a->hits[i].y1>a->hits[j].y0));}}
static void shell_preview_write(struct drm_buf*b,const char*dir,int page){char path[1024];int i;snprintf(path,sizeof(path),"%s/shell-%02d.ppm",dir,page);FILE*f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 480\n255\n");for(i=0;i<320*480;i++){uint16_t p=b->map[i];unsigned char rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,f);}fclose(f);}
static cJSON *shell_preview_large_choices(struct app*a){
 cJSON*it=cJSON_CreateObject();cJSON_AddStringToObject(it,"label","全部代理节点");cJSON_AddStringToObject(it,"type","choice");cJSON_AddStringToObject(it,"action","preview.select");cJSON_AddBoolToObject(it,"confirm",1);
 cJSON*base=cJSON_AddObjectToObject(it,"args");cJSON_AddStringToObject(base,"group","策略甲");cJSON_AddStringToObject(base,"name","must-be-overridden");cJSON*choices=cJSON_AddArrayToObject(it,"choices");
 const char*regions[]={"香港","日本","美国","德国"};
 for(int i=0;i<96;i++){char label[100],value[40];snprintf(label,sizeof(label),"%s · 节点 %03d",regions[i%4],i);if(i==95)snprintf(label,sizeof(label),"SPECIAL Test 095");snprintf(value,sizeof(value),"node-id-%03d",i);cJSON*c=cJSON_CreateObject();cJSON_AddStringToObject(c,"label",label);cJSON_AddStringToObject(c,"value",value);cJSON*args=cJSON_AddObjectToObject(c,"args");cJSON_AddStringToObject(args,"name",label);cJSON_AddNumberToObject(args,"index",i);cJSON_AddItemToArray(choices,c);}
 cJSON_AddItemToArray(sh_get(sh_section(a,"clash"),"items"),it);return it;
}
static void shell_preview_type_search(struct app*a,const char*query){
 shell_hit(a,SH_SEARCH);assert(a->shell.editor==2);shell_hit(a,SH_SEARCH_CLEAR);
 for(const char*p=query;*p;p++){if(*p==' '){shell_hit(a,SH_SPACE);continue;}int page=(*p>='0'&&*p<='9')?1:0;while(a->shell.key_page!=page)shell_hit(a,SH_KEYMODE);char c=*p;if(c>='A'&&c<='Z'){if(!a->shell.shift)shell_hit(a,SH_SHIFT);c+=32;}else if(a->shell.shift)shell_hit(a,SH_SHIFT);const char*k=strchr(sh_keyboard(&a->shell),c);assert(k);shell_hit(a,SH_KEY+(int)(k-sh_keyboard(&a->shell)));}
 assert(!strcmp(a->shell.search_edit,query));shell_hit(a,SH_DONE);assert(a->shell.editor==0&&a->shell.choice_page==0);
}
static void shell_preview_search_tests(struct drm_buf*b,const char*dir){
 struct app a;shell_preview_fixture(&a);a.shell.tab=2;sh_open_section(&a,"clash");cJSON*original=shell_preview_large_choices(&a);char*before=cJSON_PrintUnformatted(original);
 sh_open_item(&a,original);assert(sh_choice_count(&a.shell)==96);shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,15);
 shell_hit(&a,SH_SEARCH);shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,16);shell_hit(&a,SH_CANCEL);assert(sh_choice_count(&a.shell)==96);
 shell_preview_type_search(&a,"HK");assert(sh_choice_count(&a.shell)==24);assert(sh_num(sh_get(sh_choice_at(&a.shell,1),"args"),"index",-1)==4);
 a.shell.choice_page=504;assert(a.shell.choice_page==504);shell_hit(&a,SH_SEARCH);snprintf(a.shell.search_edit,sizeof(a.shell.search_edit),"jp");shell_hit(&a,SH_CANCEL);assert(!strcmp(a.shell.choice_search,"HK")&&a.shell.choice_page==504&&sh_choice_count(&a.shell)==24);
 a.shell.choice_page=252;shell_render(b,&a);assert(a.shell.choice_page==252);a.shell.choice_page=504;shell_render(b,&a);assert(a.shell.choice_page==504);
 /* Refresh can reorder or replace every live option without changing the
  * open cloned choice payload or the active filtered row mapping. */
 shell_hit(&a,SH_REFRESH);assert(!strcmp(a.shell.choice_search,"HK")&&a.shell.choice_page==504);
 cJSON*live=cJSON_GetArrayItem(sh_get(original,"choices"),0);cJSON_ReplaceItemInObject(live,"label",cJSON_CreateString("REFRESHED ONLY"));assert(sh_choice_count(&a.shell)==24);
 shell_preview_type_search(&a,"hk 04");assert(sh_choice_count(&a.shell)==4);shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,17);
 cJSON_Delete(a.shell.snapshot);a.shell.snapshot=cJSON_CreateObject();assert(sh_choice_count(&a.shell)==4);
 shell_hit(&a,SH_CHOICE+2);assert(a.shell.modal==2);assert(sh_num(a.shell.pending_args,"index",-1)==44);assert(!strcmp(sh_str(a.shell.pending_args,"group",""),"策略甲"));assert(!strcmp(sh_str(a.shell.pending_args,"name",""),"香港 · 节点 044"));assert(!strcmp(sh_str(a.shell.draft,"action",""),"preview.select"));shell_hit(&a,SH_CANCEL);assert(!a.shell.draft&&!a.shell.choice_search[0]);cJSON_Delete(a.shell.snapshot);
 /* Reopening/cancelling filtering never changes the original option array. */
 shell_preview_fixture(&a);original=shell_preview_large_choices(&a);char*restored=cJSON_PrintUnformatted(original);assert(!strcmp(before,restored));free(restored);
 sh_open_item(&a,original);shell_preview_type_search(&a,"special");assert(sh_choice_count(&a.shell)==1);assert(sh_num(sh_get(sh_choice_at(&a.shell,0),"args"),"index",-1)==95);
 shell_preview_type_search(&a,"087");assert(sh_choice_count(&a.shell)==1);assert(sh_num(sh_get(sh_choice_at(&a.shell,0),"args"),"index",-1)==87);
 shell_preview_type_search(&a,"zzzz");assert(sh_choice_count(&a.shell)==0);shell_hit(&a,SH_CHOICE);assert(a.shell.modal==1&&!a.shell.pending_args);shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,18);
 shell_hit(&a,SH_SEARCH_CLEAR);assert(sh_choice_count(&a.shell)==96&&a.shell.choice_page==0);shell_hit(&a,SH_SEARCH);memset(a.shell.search_edit,'x',SHELL_SEARCH_CAP-1);a.shell.search_edit[SHELL_SEARCH_CAP-1]=0;shell_hit(&a,SH_KEY);assert(strlen(a.shell.search_edit)==SHELL_SEARCH_CAP-1);shell_hit(&a,SH_CANCEL);assert(!a.shell.choice_search[0]);shell_hit(&a,SH_CANCEL);
 restored=cJSON_PrintUnformatted(original);assert(!strcmp(before,restored));free(restored);free(before);
 /* Large field choices use the same stable mapping without modifying args
  * belonging to a different field or the parent form. */
 cJSON*form=cJSON_CreateObject();cJSON_AddStringToObject(form,"type","form");cJSON_AddStringToObject(form,"label","策略选项");cJSON_AddStringToObject(form,"action","preview.form");cJSON*fs=cJSON_AddArrayToObject(form,"fields"),*field=cJSON_CreateObject();cJSON_AddStringToObject(field,"key","policy");cJSON_AddStringToObject(field,"label","策略名称");cJSON_AddStringToObject(field,"kind","choice");cJSON_AddStringToObject(field,"value","untouched");cJSON_AddItemToObject(field,"choices",cJSON_Duplicate(sh_get(original,"choices"),1));cJSON_AddItemToArray(fs,field);
 sh_open_item(&a,form);cJSON_Delete(form);shell_hit(&a,SH_FIELD);shell_preview_type_search(&a,"JP");assert(sh_choice_count(&a.shell)==24);a.shell.choice_page=252;shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,19);shell_hit(&a,SH_CANCEL);assert(a.shell.modal==4&&!strcmp(a.shell.values[0],"untouched"));shell_hit(&a,SH_FIELD);assert(!a.shell.choice_search[0]&&sh_choice_count(&a.shell)==96);shell_preview_type_search(&a,"JP");a.shell.choice_page=252;shell_render(b,&a);shell_hit(&a,SH_CHOICE+6);assert(a.shell.modal==4&&!strcmp(a.shell.values[0],"node-id-025"));assert(!a.shell.choice_search[0]);sh_close(&a);cJSON_Delete(a.shell.snapshot);
}
static int shell_preview_same_region(struct drm_buf*b,const uint16_t*before,int x0,int y0,int x1,int y1){
 for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++)if(b->map[y*320+x]!=before[y*320+x])return 0;return 1;
}
static void shell_preview_statusbar(struct drm_buf*b,const char*dir){
 struct app a;shell_preview_fixture(&a);cJSON*d=sh_get(a.shell.snapshot,"data"),*signal=cJSON_GetArrayItem(sh_get(sh_section(&a,"cell"),"items"),0);
 const char*bad[]={"", "—", "-83", "-83 dBm", "6", "5 bars", "4.5", "999999999999999999999999999"};
 for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++){cJSON*v=cJSON_CreateString(bad[i]);assert(sh_status_value(v,5)==-1);cJSON_Delete(v);}
 for(int value=0;value<=5;value++){char text[8];snprintf(text,sizeof(text),"%d",value);cJSON_ReplaceItemInObject(signal,"value",cJSON_CreateString(text));assert(sh_signal_bars(&a)==value);}
 /* data.signal is a negative RSRP while the raw cell signalbar stays 5. */
 assert(sh_signal_bars(&a)==5);cJSON_ReplaceItemInObject(signal,"value",cJSON_CreateNull());assert(sh_signal_bars(&a)==-1);
 cJSON_DeleteItemFromObject(signal,"id");assert(sh_signal_bars(&a)==-1);cJSON_ReplaceItemInObject(d,"signal",cJSON_CreateNumber(3));assert(sh_signal_bars(&a)==3);
 cJSON_ReplaceItemInObject(d,"signal",cJSON_CreateNumber(-83));assert(sh_signal_bars(&a)==-1);cJSON_AddStringToObject(signal,"id","signal");cJSON_ReplaceItemInObject(signal,"value",cJSON_CreateString("5"));
 cJSON*invalid_number=cJSON_CreateNumber(3.5);assert(sh_status_value(invalid_number,5)==-1);cJSON_Delete(invalid_number);
 struct tm local={.tm_year=126,.tm_mon=8,.tm_mday=20,.tm_hour=9,.tm_min=59,.tm_sec=0,.tm_isdst=-1};time_t at=mktime(&local);uint16_t before[320*480];
 shell_render(b,&a);sh_statusbar(b,&a,at);memcpy(before,b->map,sizeof(before));sh_statusbar(b,&a,at+59);assert(shell_preview_same_region(b,before,0,0,320,480));
 sh_statusbar(b,&a,at+60);assert(shell_preview_same_region(b,before,0,0,144,480));assert(shell_preview_same_region(b,before,198,0,320,480));assert(shell_preview_same_region(b,before,168,0,174,44));
 for(int n=0;n<=9;n++){char digit[]={(char)('0'+n),0};assert(text_width(digit,22)<=12);}assert(text_width(":",22)<=6&&text_width("100",14)<=28);
 /* Every regular tab and a long section title keep the status region intact. */
 shell_render(b,&a);time_t header_minute=time(NULL)/60;memcpy(before,b->map,sizeof(before));
 for(int tab=0;tab<5;tab++){a.shell.tab=tab;a.shell.subpage=0;shell_render(b,&a);shell_preview_hits(&a);assert(shell_preview_same_region(b,before,198,0,320,44));if(time(NULL)/60==header_minute)assert(shell_preview_same_region(b,before,144,0,198,44));for(int i=0;i<a.nhits;i++)if(a.hits[i].y0<44)assert(a.hits[i].x1<=140);}
 cJSON_ReplaceItemInObject(sh_section(&a,"cell"),"title",cJSON_CreateString("蜂窝网络与高级配置超长标题"));a.shell.tab=1;sh_open_section(&a,"cell");shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,80);
 cJSON*choice=shell_preview_large_choices(&a);cJSON_ReplaceItemInObject(choice,"label",cJSON_CreateString("全部代理节点与超长策略组名称"));sh_open_item(&a,choice);shell_render(b,&a);shell_preview_hits(&a);assert(shell_preview_same_region(b,before,198,0,320,44));if(time(NULL)/60==header_minute)assert(shell_preview_same_region(b,before,144,0,198,44));shell_preview_write(b,dir,81);
 /* Choice return has no hit over the clock, battery or former refresh area. */
 for(int i=0;i<a.nhits;i++)if(a.hits[i].y0<44)assert(a.hits[i].x1<=140);sh_close(&a);a.shell.tab=0;a.shell.subpage=0;
 const int battery_values[]={-1,0,1,15,86,100,101};
 for(int i=0;i<7;i++){cJSON_ReplaceItemInObject(d,"battery",battery_values[i]<0?cJSON_CreateNull():cJSON_CreateNumber(battery_values[i]));cJSON_ReplaceItemInObject(signal,"value",i==0||i==6?cJSON_CreateNull():cJSON_CreateNumber(i==1?0:i==2?1:i==3?2:5));shell_render(b,&a);shell_preview_hits(&a);sh_statusbar(b,&a,at);
  int lit=0;for(int y=14;y<30;y++)for(int x=282;x<305;x++)if(b->map[y*320+x]==SH_CYAN)lit++;assert(lit==(i==0||i==1||i==6?0:i==2?12:i==3?33:150));
  for(int y=0;y<44;y++)for(int x=274;x<282;x++)assert(b->map[y*320+x]==(sh_theme==2?sh_glass_bg[y*320+x]:SH_BG));
  shell_preview_write(b,dir,82+i);
 }
 cJSON_ReplaceItemInObject(d,"battery",cJSON_CreateString("100"));assert(sh_status_value(sh_get(d,"battery"),100)==100);shell_render(b,&a);memcpy(before,b->map,sizeof(before));cJSON_ReplaceItemInObject(d,"battery",cJSON_CreateNumber(100));shell_render(b,&a);assert(shell_preview_same_region(b,before,200,0,284,44));
 sh_close(&a);cJSON_Delete(a.shell.snapshot);puts("PASS: fixed HH:MM cells, all tab/choice headers, no refresh hits, raw signalbar/RSRP distinction, unknown/zero/full battery and signal states");
}
static void shell_preview_usb_badge(struct drm_buf*b,const char*dir){
 struct app a;shell_preview_fixture(&a);cJSON*d=sh_get(a.shell.snapshot,"data");
 cJSON*usb=cJSON_AddObjectToObject(d,"usb");cJSON_AddStringToObject(usb,"badge","");
 uint16_t before[320*480];time_t now=time(NULL);const char*values[]={"WAN","LAN","WAIT","ERROR",""};
 for(int theme=0;theme<PANEL_THEME_COUNT;theme++){
  sh_theme=theme;shell_render(b,&a);sh_statusbar(b,&a,now);memcpy(before,b->map,sizeof(before));
  for(int n=0;n<5;n++){
   cJSON_ReplaceItemInObject(usb,"badge",cJSON_CreateString(values[n]));sh_statusbar(b,&a,now);
   assert(shell_preview_same_region(b,before,0,0,199,480));assert(shell_preview_same_region(b,before,230,0,320,480));
   if(n<4)assert(!shell_preview_same_region(b,before,199,0,230,44));
   shell_preview_write(b,dir,110+theme*5+n);
  }
 }
 sh_theme=0;cJSON_Delete(a.shell.snapshot);puts("PASS: USB WAN/LAN/wait/error badge confined left of battery in all themes");
}
static void shell_preview_battery_power(struct drm_buf*b,const char*dir){
 assert(battery_power_effective("Charging","sink",1,1)==BP_INPUT_IDLE);
 assert(battery_power_effective("Full","sink",1,1)==BP_INPUT_IDLE);
 assert(battery_power_effective("Charging","sink",1,0)==BP_CHARGING);
 assert(battery_power_effective("Discharging","source",1,1)==BP_OUTPUT);

 struct app a;shell_preview_fixture(&a);uint16_t before[320*480];time_t now=time(NULL);
 shell_render(b,&a);sh_statusbar(b,&a,now);memcpy(before,b->map,sizeof(before));
 const char*status[]={"Charging","Discharging","Not charging","Full","Discharging","Charging","unknown","Charging","Full"};
 const char*roles[]={"source [sink]","[source] sink","sink","sink","source","source","sink","sink","sink"};
 const int attached[]={1,1,1,1,0,1,1,0,-1};
 const enum panel_battery_power expected[]={BP_CHARGING,BP_OUTPUT,BP_INPUT_IDLE,BP_FULL,BP_BATTERY,BP_UNKNOWN,BP_UNKNOWN,BP_UNKNOWN,BP_UNKNOWN};
 for(int i=0;i<9;i++){
  a.battery_power=battery_power_state(status[i],roles[i],attached[i]);assert(a.battery_power==expected[i]);
  sh_statusbar(b,&a,now);assert(shell_preview_same_region(b,before,0,0,238,480));assert(shell_preview_same_region(b,before,272,0,320,480));
  if(i<2)assert(!shell_preview_same_region(b,before,239,16,271,28));
  else assert(shell_preview_same_region(b,before,238,0,272,44));
  for(int y=0;y<44;y++)for(int x=230;x<238;x++)assert(b->map[y*320+x]==(sh_theme==2?sh_glass_bg[y*320+x]:SH_BG));
  if(i<4)shell_preview_write(b,dir,89+i);
 }
 /* Snapshot refreshes cannot replace local power telemetry. */
 /* Worst-case 100 plus a direction mark must stay inside the fixed body.
  * Compare outside the body across capacity/theme/direction changes. */
 const int capacities[]={0,1,86,100};
 cJSON*d=sh_get(a.shell.snapshot,"data");cJSON*usb=cJSON_AddObjectToObject(d,"usb");cJSON_AddStringToObject(usb,"badge","WAN");
 for(int theme=0;theme<PANEL_THEME_COUNT;theme++)for(int value=0;value<4;value++)for(int state=0;state<3;state++){
  sh_theme=theme;a.battery_power=state==0?BP_CHARGING:state==1?BP_OUTPUT:BP_BATTERY;
  cJSON_ReplaceItemInObject(d,"battery",cJSON_CreateNumber(capacities[value]));
  char digits[8];snprintf(digits,sizeof(digits),"%d",capacities[value]);assert(text_width(digits,14)+7<=30);
  shell_render(b,&a);sh_statusbar(b,&a,now);
  for(int y=0;y<44;y++)for(int x=230;x<238;x++)assert(b->map[y*320+x]==(sh_theme==2?sh_glass_bg[y*320+x]:SH_BG));
  for(int y=0;y<44;y++)for(int x=274;x<282;x++)assert(b->map[y*320+x]==(sh_theme==2?sh_glass_bg[y*320+x]:SH_BG));
  shell_preview_write(b,dir,130+theme*12+value*3+state);
 }
 sh_theme=0;
 a.battery_power=BP_CHARGING;cJSON_Delete(a.shell.snapshot);a.shell.snapshot=cJSON_CreateObject();sh_statusbar(b,&a,now);assert(a.battery_power==BP_CHARGING);
 cJSON_Delete(a.shell.snapshot);puts("PASS: green battery with internal number, centered internal charging plus/output minus, neutral input-idle/full/unplugged/unknown/conflicting states, fixed clock and signal, snapshot independence");
}
static void shell_preview_live_regressions(struct drm_buf*b){
 struct app a;shell_preview_fixture(&a);cJSON*d=sh_get(a.shell.snapshot,"data"),*cl=sh_get(d,"clash");
 cJSON_DeleteItemFromObject(d,"clash_status");cJSON_ReplaceItemInObject(cl,"online",cJSON_CreateBool(1));
 a.clash_online=1;shell_render(b,&a);uint16_t before[320*480];memcpy(before,b->map,sizeof(before));
 a.clash_online=0;shell_render(b,&a);int clash_ok=shell_preview_same_region(b,before,12,70,308,94);
 memcpy(before,b->map,sizeof(before));cJSON_DeleteItemFromObject(d,"clock_text");shell_render(b,&a);
 int clock_ok=shell_preview_same_region(b,before,151,0,264,40);
 fprintf(stderr,"Live snapshot regression: Clash=%s clock=%s\n",clash_ok?"PASS":"FAIL",clock_ok?"PASS":"FAIL");
 assert(clash_ok&&clock_ok);
 /* The verified routing verdict, not core-online or legacy text, owns outlet. */
 a.clash_online=1;cJSON_AddStringToObject(d,"clash_status","运行中");cJSON_ReplaceItemInObject(cl,"online",cJSON_CreateBool(1));cJSON_AddStringToObject(cl,"verdict","direct");shell_render(b,&a);
 assert(!shell_preview_same_region(b,before,12,70,308,94));
 cJSON_DeleteItemFromObject(cl,"verdict");assert(!strcmp(sh_outlet(d),"代理未核验"));
 char clock_buf[16];sh_clock_text(0,clock_buf,sizeof(clock_buf));assert(!strcmp(clock_buf,"--:--"));
 assert(text_width("100",14)<=28&&text_width("23:59",22)<=58);
 cJSON*usage=cJSON_AddObjectToObject(d,"usage");cJSON_AddStringToObject(usage,"warning","normal");shell_render(b,&a);memcpy(before,b->map,sizeof(before));cJSON_ReplaceItemInObject(usage,"warning",cJSON_CreateString("threshold"));shell_render(b,&a);assert(!shell_preview_same_region(b,before,170,184,305,201));
 sh_close(&a);cJSON_Delete(a.shell.snapshot);
}
static void shell_preview_reports(struct drm_buf*b,const char*dir){
 struct app a;shell_preview_fixture(&a);a.shell.tab=4;shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,20);
 shell_pointer(&a,150,350,1,0);shell_pointer(&a,150,150,1,0);shell_pointer(&a,150,150,0,1);assert(a.shell.menu_page>0);shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,21);
 shell_hit(&a,SH_SECTION+7);assert(!strcmp(a.shell.section,"diagnostics")&&a.shell.subpage);shell_hit(&a,SH_BACK);assert(!a.shell.subpage&&a.shell.menu_page>0);
 cJSON*r=cJSON_Parse("{\"title\":\"实际分流记录\",\"lines\":[\"设备：192.168.0.2\",\"目标：www.google.com\",\"规则：DomainSuffix\",\"匹配内容：google.com\",\"策略：美国专线\",\"这是长内容分页测试，完整保留中文字符，不能在边界丢掉文字，也不能把正文裁切为短暂提示。\"]}");
 for(int n=0;n<20;n++)cJSON_AddItemToArray(sh_get(r,"lines"),cJSON_CreateString("通知正文仅保存在内存中，不写入日志。"));
 sh_show_report(&a,r);cJSON_Delete(r);assert(a.shell.modal==6&&cJSON_GetArraySize(a.shell.report_lines)>12);
 shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,22);shell_pointer(&a,150,350,1,0);shell_pointer(&a,150,150,1,0);shell_pointer(&a,150,150,0,1);assert(a.shell.report_page==200);shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,23);
 for(int n=0;n<20;n++){shell_pointer(&a,150,350,1,0);shell_pointer(&a,150,150,1,0);shell_pointer(&a,150,150,0,1);shell_render(b,&a);}assert(a.shell.report_page==a.shell.scroll_max);shell_hit(&a,SH_CANCEL);assert(!a.shell.report_lines&&!a.shell.modal);
 /* Report-item details survive an item backed by a transient snapshot. */
 cJSON*it=cJSON_Parse("{\"label\":\"短信预览\",\"type\":\"report\",\"detail\":\"这是一段合成测试正文。\"}");sh_open_item(&a,it);cJSON_Delete(it);assert(a.shell.modal==6);shell_hit(&a,SH_CANCEL);
 cJSON*picker=cJSON_Parse("{\"label\":\"短信收件箱\",\"type\":\"choice\",\"action\":\"sms.read\",\"confirm\":true,\"choices\":[{\"label\":\"合成消息\",\"args\":{\"page\":0,\"id\":7}},{\"label\":\"下一页\",\"action\":\"sms.list\",\"args\":{\"page\":1}}]}");
 sh_open_item(&a,picker);shell_hit(&a,SH_CHOICE+1);assert(a.shell.modal==2&&!strcmp(sh_str(a.shell.draft,"action",""),"sms.list")&&sh_num(a.shell.pending_args,"page",-1)==1);shell_hit(&a,SH_CANCEL);sh_close(&a);
 sh_open_item(&a,picker);cJSON_Delete(picker);shell_hit(&a,SH_CHOICE);assert(a.shell.modal==2&&!strcmp(sh_str(a.shell.draft,"action",""),"sms.read")&&sh_num(a.shell.pending_args,"id",-1)==7);sh_close(&a);
 a.shell.tab=0;shell_hit(&a,SH_SECTION+24);assert(a.shell.tab==4&&!strcmp(a.shell.section,"usage"));
 cJSON_Delete(a.shell.snapshot);puts("PASS: expanded menu routes, scrolling 16px reports, UTF8 wrapping, close cleanup, home usage entry");
}
/* Optional sanitized live schema: preview-only, never shipped as UI state. */
static void shell_preview_schema(struct drm_buf*b,const char*dir){
 const char*path=getenv("U60_PREVIEW_SCHEMA");if(!path)return;FILE*f=fopen(path,"rb");assert(f);char buf[65536];size_t n=fread(buf,1,sizeof(buf)-1,f);assert(feof(f));fclose(f);buf[n]=0;
 cJSON*r=cJSON_Parse(buf);assert(r);struct app a;shell_preview_fixture(&a);cJSON_Delete(a.shell.snapshot);a.shell.snapshot=r;panel_menu_layout(r);a.shell.tab=4;int page=24;cJSON*section;
 cJSON_ArrayForEach(section,sh_get(r,"sections")){sh_open_section(&a,sh_str(section,"id",""));int count=cJSON_GetArraySize(sh_get(section,"items"));for(int k=0;k*5<count;k++){a.shell.item_page=k*315;shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,page++);}}
 if(sh_section(&a,"battery")){
  sh_open_section(&a,"battery");cJSON*items=sh_get(sh_section(&a,"battery"),"items");
  for(int n=0;n<cJSON_GetArraySize(items);n++)if(!strcmp(sh_str(cJSON_GetArrayItem(items,n),"id",""),"menu.battery_details")){
   for(int theme=0;theme<PANEL_THEME_COUNT;theme++){sh_theme=theme;a.shell.item_page=n*63;shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,180+theme*2);
    shell_hit(&a,SH_ITEM+n);assert(a.shell.modal==6&&cJSON_GetArraySize(a.shell.report_lines)>0);shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,181+theme*2);shell_hit(&a,SH_CANCEL);assert(!a.shell.modal);
   }sh_theme=0;break;
  }
 }
 if(sh_section(&a,"usage")){
  sh_open_section(&a,"usage");cJSON*items=sh_get(sh_section(&a,"usage"),"items");
  for(int i=0;i<cJSON_GetArraySize(items);i++){
   cJSON*it=cJSON_GetArrayItem(items,i);if(strcmp(sh_str(it,"type",""),"form")||cJSON_IsFalse(sh_get(it,"enabled")))continue;
   shell_hit(&a,SH_ITEM+i);assert(a.shell.modal==4&&a.shell.nfields==1);shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,30+i);
   shell_hit(&a,SH_FIELD);assert(a.shell.editor==1&&a.shell.key_page==1);shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,35+i);
   shell_hit(&a,SH_SEARCH_CLEAR);assert(!a.shell.values[0][0]);shell_hit(&a,SH_KEY+1);shell_hit(&a,SH_KEY+5);shell_hit(&a,SH_DONE);shell_hit(&a,SH_APPLY);
   assert(a.shell.modal==2);cJSON*f=cJSON_GetArrayItem(sh_get(it,"fields"),0);assert(!strcmp(sh_str(a.shell.pending_args,sh_str(f,"key",""),""),"15"));assert(cJSON_GetArraySize(a.shell.pending_args)==1);shell_hit(&a,SH_CANCEL);
  }
  puts("PASS: ledger first-page form/number keyboard/clear/edit/save confirmation sends only edited field");
 }
 for(int tab=2;tab<=3;tab++){
  sh_close(&a);a.shell.tab=tab;sh_open_section(&a,tab==2?"clash":"tailscale");shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,90+tab-2);
  if(tab==2){cJSON*items=sh_get(sh_section(&a,"clash"),"items"),*it;cJSON_ArrayForEach(it,items)if(!strcmp(sh_str(it,"id",""),"node")){sh_open_item(&a,it);shell_render(b,&a);shell_preview_write(b,dir,92);break;}}
 }
 sh_close(&a);cJSON_Delete(a.shell.snapshot);puts("PASS: sanitized live module schema rendered with valid hit targets");
}
static void shell_preview_root_navigation(struct drm_buf*b){
 struct app a;shell_preview_fixture(&a);
 for(int tab=2;tab<=3;tab++){
  a.shell.tab=tab;sh_open_section(&a,tab==2?"clash":"tailscale");shell_render(b,&a);
  for(int h=0;h<a.nhits;h++)assert(a.hits[h].id!=SH_BACK);
  snprintf(a.shell.status,sizeof(a.shell.status),"操作完成");a.shell.status_until=now_ms()+10000;shell_render(b,&a);int found=0;for(int h=0;h<a.nhits;h++)if(a.hits[h].id==SH_STATUS)found=1;assert(found);a.shell.status[0]=0;
 }
 a.shell.tab=1;sh_open_section(&a,"wifi");shell_render(b,&a);int found=0;for(int h=0;h<a.nhits;h++)if(a.hits[h].id==SH_BACK)found=1;assert(found);cJSON_Delete(a.shell.snapshot);
 puts("PASS: Clash/Tailscale roots have no back hit; nested settings and action receipts remain accessible");
}
static void shell_preview_readonly(struct drm_buf*b){
 struct app a;shell_preview_fixture(&a);a.shell.tab=2;sh_open_section(&a,"clash");shell_render(b,&a);
 for(int i=0;i<a.nhits;i++)assert(a.hits[i].id!=SH_ITEM&&a.hits[i].id!=SH_ITEM+3&&a.hits[i].id!=SH_ITEM+4);
 shell_hit(&a,SH_ITEM);assert(!a.shell.modal&&!a.shell.draft);shell_hit(&a,SH_ITEM+1);assert(a.shell.modal==1);shell_hit(&a,SH_CANCEL);
 a.shell.tab=1;sh_open_section(&a,"wifi");shell_render(b,&a);for(int i=0;i<a.nhits;i++)assert(a.hits[i].id!=SH_ITEM+2);shell_hit(&a,SH_ITEM+2);assert(!a.shell.modal&&!a.shell.draft);
 cJSON*it=cJSON_GetArrayItem(sh_get(sh_section(&a,"wifi"),"items"),0);cJSON_AddBoolToObject(it,"enabled",0);shell_hit(&a,SH_ITEM);assert(!a.shell.modal);
 cJSON_Delete(a.shell.snapshot);puts("PASS: info and disabled rows have no hit or modal; editable controls remain reachable; stale hits recheck availability");
}
static void shell_preview_nav_icons(struct drm_buf*b){
 struct app a;shell_preview_fixture(&a);
 for(int theme=0;theme<PANEL_THEME_COUNT;theme++)for(int tab=0;tab<5;tab++){
  sh_theme=theme;a.shell.tab=tab;shell_render(b,&a);
  for(int icon=0;icon<5;icon++){
   int pixels=0,group=icon==1?SH_NETWORK_COLOR:(icon==2||icon==3)?SH_SERVICE_COLOR:SH_DEVICE_COLOR;
   uint16_t ink=tab==icon?sh_category_ink(group):SH_MUTED;
   for(int y=439;y<458;y++)for(int x=icon*64+21;x<icon*64+43;x++)if(b->map[y*320+x]==ink)pixels++;
   assert(pixels>25);int hit=0;for(int n=0;n<a.nhits;n++)if(a.hits[n].id==SH_TAB+icon){assert(a.hits[n].y0==433&&a.hits[n].y1==480);hit++;}assert(hit==1);
  }
 }
 cJSON_Delete(a.shell.snapshot);sh_theme=0;puts("PASS: five visible native navigation icons with unchanged hit targets in every theme/tab");
}
static void shell_preview_theme_menu(struct drm_buf*b,const char*dir){
 for(int theme=0;theme<PANEL_THEME_COUNT;theme++){
  struct app a;shell_preview_fixture(&a);sh_theme=theme;panel_menu_layout(a.shell.snapshot);
  for(int tab=0;tab<5;tab++){a.shell.tab=tab;a.shell.subpage=0;shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,300+theme*10+tab);}
  a.shell.tab=2;sh_open_section(&a,"clash");shell_render(b,&a);
  shell_hit(&a,SH_ITEM);assert(a.shell.modal==1&&!strcmp(sh_str(a.shell.draft,"action",""),"clash.mode"));shell_hit(&a,SH_CANCEL);
  shell_hit(&a,SH_ITEM+1);assert(a.shell.modal==1&&!strcmp(sh_str(a.shell.draft,"action",""),"clash.select"));shell_hit(&a,SH_CANCEL);
  shell_hit(&a,SH_ITEM+2);assert(!a.shell.modal);
  cJSON_Delete(a.shell.snapshot);
 }
 sh_theme=0;puts("PASS: all palettes, sorted rendered row to action mapping, readonly rows stay inert");
}
static void shell_preview_scroll_gestures(struct drm_buf*b,const char*dir){
 for(int theme=0;theme<PANEL_THEME_COUNT;theme++){
 sh_theme=theme;struct app a;shell_preview_fixture(&a);a.shell.tab=4;shell_render(b,&a);
 uint16_t before[320*480];memcpy(before,b->map,sizeof(before));
 shell_pointer(&a,130,350,1,0);shell_pointer(&a,130,300,1,0);shell_render(b,&a);shell_pointer(&a,130,180,1,0);shell_render(b,&a);shell_pointer(&a,130,180,0,1);
 assert(a.shell.menu_page==170&&!a.shell.subpage&&!a.shell.modal&&!a.shell.busy);shell_preview_hits(&a);
 assert(shell_preview_same_region(b,before,0,432,320,480));assert(shell_preview_same_region(b,before,198,0,320,44));
 /* Touch at a partially clipped row cannot hit navigation or the header. */
 for(int i=0;i<a.nhits;i++)if(a.hits[i].id>=SH_SECTION&&a.hits[i].id<SH_SECTION+30)assert(a.hits[i].y0>=52&&a.hits[i].y1<=428);
 shell_preview_write(b,dir,200+theme);
 int old=a.shell.menu_page;shell_pointer(&a,130,20,1,0);shell_pointer(&a,130,200,1,0);shell_pointer(&a,130,200,0,1);assert(a.shell.menu_page==old);
 shell_hit(&a,SH_TAB+1);shell_render(b,&a);shell_hit(&a,SH_TAB+4);shell_render(b,&a);assert(a.shell.menu_page==old);
 /* 96 nodes: bottom row mapping after multiple drags, no inadvertent select. */
 cJSON*it=shell_preview_large_choices(&a);sh_open_item(&a,it);shell_render(b,&a);
 for(int i=0;i<35;i++){shell_pointer(&a,100,390,1,0);shell_pointer(&a,100,160,1,0);shell_pointer(&a,100,160,0,1);shell_render(b,&a);assert(a.shell.modal==1&&!a.shell.pending_args);}
 assert(a.shell.choice_page==a.shell.scroll_max);shell_preview_hits(&a);int row=-1;
 for(int i=0;i<a.nhits;i++)if(a.hits[i].id==SH_CHOICE+95)row=i;assert(row>=0);
 int y=(a.hits[row].y0+a.hits[row].y1)/2;shell_pointer(&a,100,y,1,0);shell_pointer(&a,100,y,0,1);assert(a.shell.modal==2&&sh_num(a.shell.pending_args,"index",-1)==95);sh_close(&a);
 /* Long forms expose last field while fixed save/cancel remain separate. */
 cJSON*form=cJSON_CreateObject();cJSON_AddStringToObject(form,"type","form");cJSON_AddStringToObject(form,"action","preview.form");cJSON*fs=cJSON_AddArrayToObject(form,"fields");
 for(int i=0;i<24;i++){cJSON*f=cJSON_CreateObject();cJSON_AddStringToObject(f,"label","字段");cJSON_AddStringToObject(f,"key","key");cJSON_AddItemToArray(fs,f);}sh_open_item(&a,form);cJSON_Delete(form);shell_render(b,&a);
 for(int i=0;i<10;i++){shell_pointer(&a,100,370,1,0);shell_pointer(&a,100,120,1,0);shell_pointer(&a,100,120,0,1);shell_render(b,&a);}
 assert(a.shell.field_page==a.shell.scroll_max);shell_preview_hits(&a);row=-1;for(int i=0;i<a.nhits;i++)if(a.hits[i].id==SH_FIELD+23)row=i;assert(row>=0);
 y=(a.hits[row].y0+a.hits[row].y1)/2;shell_pointer(&a,100,y,1,0);shell_pointer(&a,100,y,0,1);assert(a.shell.editor&&a.shell.field==23);shell_render(b,&a);assert(!a.shell.scroll_offset);sh_close(&a);cJSON_Delete(a.shell.snapshot);
 }sh_theme=0;puts("PASS: all themes gesture suppression, edge clipping, fixed chrome, remembered menu, last of 96 nodes, last of 24 fields");
}
static void shell_preview_management(struct drm_buf*b,const char*dir){
 struct app a;shell_preview_fixture(&a);
 cJSON *items=sh_get(sh_section(&a,"clash"),"items");
 cJSON_AddItemToArray(items,cJSON_Parse("{\"id\":\"add-rule\",\"label\":\"添加规则\",\"type\":\"form\",\"action\":\"preview.rule\",\"fields\":[]}"));
 panel_menu_layout(a.shell.snapshot);a.shell.tab=2;sh_open_section(&a,"clash");shell_render(b,&a);
 int link=-1;for(int i=0;i<cJSON_GetArraySize(items);i++)if(!strcmp(sh_str(cJSON_GetArrayItem(items,i),"type",""),"navigation"))link=i;
 assert(link>=0);shell_hit(&a,SH_ITEM+link);assert(!strcmp(a.shell.section,"clash-more")&&!a.shell.modal);shell_render(b,&a);shell_preview_hits(&a);
 int back=0;for(int i=0;i<a.nhits;i++)if(a.hits[i].id==SH_BACK)back=1;assert(back);shell_preview_write(b,dir,330);
 shell_hit(&a,SH_ITEM);assert(a.shell.modal==4&&!strcmp(sh_str(a.shell.draft,"action",""),"preview.rule"));shell_hit(&a,SH_CANCEL);shell_hit(&a,SH_BACK);assert(!strcmp(a.shell.section,"clash")&&a.shell.tab==2);
 cJSON*choice=cJSON_Parse("{\"label\":\"代理开关\",\"value\":\"开启代理\",\"type\":\"choice\",\"action\":\"preview.service\",\"confirm\":true,\"choices\":[{\"label\":\"开启代理\",\"args\":{\"operation\":\"start\"}},{\"label\":\"关闭代理 · 直连\",\"args\":{\"operation\":\"stop\"}}]}");
 sh_open_item(&a,choice);cJSON_Delete(choice);assert(sh_choice_current(&a,sh_choice_at(&a.shell,0)));assert(!sh_choice_current(&a,sh_choice_at(&a.shell,1)));cJSON_ReplaceItemInObject(a.shell.draft,"action",cJSON_CreateString("clash.service"));cJSON_ReplaceItemInObject(a.shell.draft,"value",cJSON_CreateString("开启"));assert(sh_choice_current(&a,sh_choice_at(&a.shell,0)));shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,331);
 shell_hit(&a,SH_CHOICE+1);assert(a.shell.modal==2&&!strcmp(a.shell.message,"关闭代理 · 直连"));assert(!strcmp(sh_str(a.shell.pending_args,"operation",""),"stop"));shell_render(b,&a);shell_preview_hits(&a);shell_preview_write(b,dir,332);shell_hit(&a,SH_CANCEL);assert(!a.shell.busy&&!a.shell.pending_args);
 a.shell.tab=0;a.shell.subpage=0;shell_render(b,&a);shell_preview_hits(&a);for(int i=0;i<a.nhits;i++)assert(a.hits[i].y1-a.hits[i].y0>=44);
 shell_hit(&a,SH_SECTION+25);assert(a.shell.tab==1&&!strcmp(a.shell.section,"clients"));
 sh_close(&a);cJSON_Delete(a.shell.snapshot);puts("PASS: child management navigation/back, preserved action routing, current choices, selected confirmation, no write on cancel, home targets >=44px");
}
static int shell_preview_main(const char*dir){
 struct app a;struct drm_buf b={0};int page;
 /* Malformed or truncated labels from the backend cannot escape the buffer. */
 const char *invalid[]={"\xE4","\xE4\xB8","\xF0\x9F\x92","\x80","\xC0\xAF"};
 for(size_t t=0;t<sizeof(invalid)/sizeof(invalid[0]);t++){char*owned=strdup(invalid[t]);const char*q=owned;while(*q){const char*before=q;assert(utf8_next(&q)==0x3f);assert(q>before&&q<=owned+strlen(owned));}free(owned);}
 const char*q="中";assert(utf8_next(&q)==0x4e2d&&!*q);
 if(font_load()<0)return 1;b.pitch=640;b.size=320*480*2;b.map=calloc(320*480,2);
 shell_preview_live_regressions(&b);shell_preview_statusbar(&b,dir);shell_preview_battery_power(&b,dir);shell_preview_usb_badge(&b,dir);shell_preview_readonly(&b);shell_preview_root_navigation(&b);
 for(page=0;page<15;page++){
  shell_preview_fixture(&a);
  if(page<5)a.shell.tab=page;
  else if(page==5){a.shell.tab=1;sh_open_section(&a,"wifi");}
  else if(page<13){a.shell.tab=1;sh_open_section(&a,"wifi");sh_open_item(&a,cJSON_GetArrayItem(sh_get(sh_section(&a,"wifi"),"items"),1));
   if(page>=7&&page<=10){shell_hit(&a,SH_FIELD+1);if(page==8)shell_hit(&a,SH_REVEAL);if(page==9)a.shell.key_page=1;if(page==10)a.shell.key_page=2;}
   if(page==11)shell_hit(&a,SH_FIELD+2);
   if(page==12){sh_close(&a);sh_open_item(&a,cJSON_GetArrayItem(sh_get(sh_section(&a,"wifi"),"items"),2));}
  }else if(page==13){a.shell.tab=3;sh_open_item(&a,cJSON_GetArrayItem(sh_get(sh_section(&a,"tailscale"),"items"),2));}
  else a.shell.power_open=1;
  shell_render(&b,&a);shell_preview_hits(&a);shell_preview_write(&b,dir,page);sh_close(&a);cJSON_Delete(a.shell.snapshot);
 }
 /* A new snapshot cannot destroy the open form or its unsaved edits. */
 shell_preview_fixture(&a);sh_open_item(&a,cJSON_GetArrayItem(sh_get(sh_section(&a,"wifi"),"items"),1));shell_hit(&a,SH_FIELD);shell_hit(&a,SH_KEY);assert(!strcmp(a.shell.values[0],"U60-PROa"));cJSON_Delete(a.shell.snapshot);a.shell.snapshot=cJSON_CreateObject();assert(!strcmp(a.shell.values[0],"U60-PROa"));assert(!strcmp(sh_str(a.shell.draft,"label",""),"无线名称与密码"));shell_hit(&a,SH_DONE);shell_hit(&a,SH_FIELD+2);assert(a.shell.modal==5);shell_hit(&a,SH_CHOICE);assert(!strcmp(a.shell.values[2],"2g"));assert(a.shell.modal==4);
 shell_hit(&a,SH_FIELD);memset(a.shell.values[0],'x',SHELL_VALUE_CAP-1);a.shell.values[0][SHELL_VALUE_CAP-1]=0;shell_hit(&a,SH_KEY);assert(strlen(a.shell.values[0])==SHELL_VALUE_CAP-1);strcpy(a.shell.values[0],"中文");shell_hit(&a,SH_DELETE);assert(!strcmp(a.shell.values[0],"中"));shell_hit(&a,SH_DONE);shell_hit(&a,SH_CANCEL);assert(!a.shell.draft);cJSON_Delete(a.shell.snapshot);
 shell_preview_management(&b,dir);shell_preview_nav_icons(&b);shell_preview_scroll_gestures(&b,dir);shell_preview_theme_menu(&b,dir);shell_preview_search_tests(&b,dir);shell_preview_reports(&b,dir);shell_preview_schema(&b,dir);free(b.map);puts("PASS: 20 native shell states; nonoverlapping hits; draft isolation; 96-choice search, region aliases, scrolling, refreshed snapshot selection args, cancel/clear, field choice mapping, UTF-8 and buffer bounds");return 0;
}
