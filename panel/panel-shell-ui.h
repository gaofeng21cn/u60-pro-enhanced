/* Native RGB565 shell. No browser, framebuffer-independent draw primitives. */
#ifndef PANEL_SHELL_UI_H
#define PANEL_SHELL_UI_H
#define SH_RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
#include "panel-theme.h"
#include "panel-menu-layout.h"
static int sh_theme=0;
/* Shared geometry and hit targets. Classic uses three semantic color families. */
static const uint16_t sh_palettes[PANEL_THEME_COUNT][8]={
 {SH_RGB(18,26,36),SH_RGB(29,39,52),SH_RGB(39,51,66),SH_RGB(240,246,250),SH_RGB(207,217,225),SH_RGB(120,222,206),SH_RGB(12,39,39),SH_RGB(255,190,108)},
 {SH_RGB(190,203,213),SH_RGB(208,217,225),SH_RGB(185,199,214),SH_RGB(53,68,83),SH_RGB(67,83,101),SH_RGB(24,77,145),SH_RGB(255,255,255),SH_RGB(153,75,10)},
 {SH_RGB(28,54,70),SH_RGB(42,73,89),SH_RGB(57,88,103),SH_RGB(243,248,251),SH_RGB(212,228,237),SH_RGB(173,235,220),SH_RGB(20,60,61),SH_RGB(255,207,145)}
};
#define SH_BG sh_palettes[sh_theme][0]
#define SH_CARD sh_palettes[sh_theme][1]
#define SH_RAISED sh_palettes[sh_theme][2]
#define SH_TEXT sh_palettes[sh_theme][3]
#define SH_MUTED sh_palettes[sh_theme][4]
#define SH_CYAN sh_palettes[sh_theme][5]
#define SH_DARK sh_palettes[sh_theme][6]
#define SH_WARN sh_palettes[sh_theme][7]
/* Static RGB565 glass: cache the soft background once, then composite card
 * surfaces with integer alpha. No animation, blur pass or extra wake timer. */
static uint16_t sh_glass_bg[320*480];
static int sh_glass_ready;
static uint16_t sh_mix(uint16_t a,uint16_t b,int alpha){
 int r=(((a>>11)&31)*(255-alpha)+((b>>11)&31)*alpha+127)/255;
 int g=(((a>>5)&63)*(255-alpha)+((b>>5)&63)*alpha+127)/255;
 int z=((a&31)*(255-alpha)+(b&31)*alpha+127)/255;
 return (uint16_t)((r<<11)|(g<<5)|z);
}
static void sh_glass_init(void){
 if(sh_glass_ready)return;
 static const int dither[4][4]={{0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
 for(int y=0;y<480;y++)for(int x=0;x<320;x++){
  float dx=x-305.0f,dy=y-15.0f;
  float cool=.52f*expf(-(dx*dx/(260*260.0f)+dy*dy/(290*290.0f)));
  dx=x-5.0f;dy=y-330.0f;
  float sea=.48f*expf(-(dx*dx/(220*220.0f)+dy*dy/(270*270.0f)));
  float r=27+(100-27)*cool,g=52+(126-52)*cool,z=72+(146-72)*cool;
  r+=(71-r)*sea;g+=(126-g)*sea;z+=(134-z)*sea;
  /* Ordered sub-pixel quantization avoids hard bands on the RGB565 LCD. */
  float q=(dither[y&3][x&3]-7.5f)/16.0f;
  int rr=(int)(r*31/255+q+.5f),gg=(int)(g*63/255+q+.5f),bb=(int)(z*31/255+q+.5f);
  sh_glass_bg[y*320+x]=(uint16_t)((rr<<11)|(gg<<5)|bb);
 }sh_glass_ready=1;
}
static void sh_background(struct drm_buf*b,int x0,int y0,int x1,int y1){
 if(sh_theme!=2){fill_rect(b,x0,y0,x1,y1,SH_BG);return;}
 sh_glass_init();if(x0<0)x0=0;if(x1>320)x1=320;if(y0<draw_clip_top)y0=draw_clip_top;if(y1>draw_clip_bottom)y1=draw_clip_bottom;
 for(int y=y0;y<y1;y++){uint16_t*row=(uint16_t*)((uint8_t*)b->map+y*b->pitch);memcpy(row+x0,sh_glass_bg+y*320+x0,(size_t)(x1-x0)*2);}
}
static int sh_round_contains(int x,int y,int x0,int y0,int x1,int y1,int r){
 if(x<x0||x>=x1||y<y0||y>=y1)return 0;
 int cx=x<x0+r?x0+r:x>=x1-r?x1-r-1:x;
 int cy=y<y0+r?y0+r:y>=y1-r?y1-r-1:y;
 return (x-cx)*(x-cx)+(y-cy)*(y-cy)<=r*r;
}
static void sh_surface(struct drm_buf*b,int x0,int y0,int x1,int y1,int r,uint16_t c){
 if(sh_theme!=2){fill_round(b,x0,y0,x1,y1,r,c);return;}
 sh_glass_init();
 for(int y=y0;y<y1;y++){
  if(y<draw_clip_top||y>=draw_clip_bottom)continue;
  uint16_t*row=(uint16_t*)((uint8_t*)b->map+y*b->pitch);
  for(int x=x0;x<x1;x++){
   if(x<0||x>=320||!sh_round_contains(x,y,x0,y0,x1,y1,r))continue;
   uint16_t base=sh_mix(sh_glass_bg[y*320+x],c,c==SH_CYAN?225:166);
   int edge=!sh_round_contains(x,y,x0+1,y0+1,x1-1,y1-1,r>1?r-1:0);
   row[x]=sh_mix(base,SH_RGB(219,245,252),edge?(y<y0+r?65:32):12+(y1-y)*12/(y1-y0));
  }
 }
}
/* Tiny native strokes stay present offline and do not depend on font glyphs. */
static void sh_icon_line(struct drm_buf*b,int x0,int y0,int x1,int y1,uint16_t c){
 int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
 for(;;){fill_rect(b,x0,y0,x0+2,y0+2,c);if(x0==x1&&y0==y1)break;int e=2*err;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}
}
static void sh_nav_icon(struct drm_buf*b,int type,int x,int y,uint16_t c){
 #define IL(a,d,e,f) sh_icon_line(b,x+(a),y+(d),x+(e),y+(f),c)
 if(type==0){IL(0,7,8,0);IL(8,0,16,7);IL(3,6,3,16);IL(3,16,13,16);IL(13,16,13,6);IL(7,16,7,11);IL(7,11,10,11);}
 else if(type==1){IL(0,4,4,1);IL(4,1,12,1);IL(12,1,16,4);IL(3,8,6,6);IL(6,6,10,6);IL(10,6,13,8);IL(6,12,8,10);IL(8,10,10,12);fill_circle(b,x+9,y+16,1,c);}
 else if(type==2){IL(8,0,1,3);IL(1,3,2,10);IL(2,10,8,17);IL(8,17,14,10);IL(14,10,15,3);IL(15,3,8,0);IL(5,8,8,11);IL(8,11,12,6);}
 else if(type==3){IL(8,5,8,9);IL(2,9,14,9);IL(2,9,2,12);IL(14,9,14,12);fill_round_border(b,x+5,y,x+12,y+6,1,SH_CARD,c);fill_round_border(b,x-1,y+12,x+6,y+18,1,SH_CARD,c);fill_round_border(b,x+11,y+12,x+18,y+18,1,SH_CARD,c);}
 else {IL(0,3,16,3);IL(0,9,16,9);IL(0,15,16,15);fill_circle(b,x+5,y+4,2,c);fill_circle(b,x+12,y+10,2,c);fill_circle(b,x+6,y+16,2,c);}
 #undef IL
}
enum { SH_BACK=2000,SH_REFRESH,SH_PREV,SH_NEXT,SH_CANCEL,SH_APPLY,SH_CONFIRM,
 SH_KEYMODE,SH_SHIFT,SH_DELETE,SH_SPACE,SH_DONE,SH_REVEAL,SH_FIELD_PREV,SH_FIELD_NEXT,
 SH_POWER_OFF,SH_POWER_REBOOT,SH_FACTORY,SH_SEARCH=2031,SH_SEARCH_CLEAR,SH_ALPHA,SH_DIGITS,SH_SYMBOLS,SH_STATUS=2030,SH_TAB=2020,SH_SECTION=2040,
 SH_ITEM=10000,SH_CHOICE=20000,SH_FIELD=30000,SH_KEY=2600 };
static cJSON *sh_get(cJSON *o,const char *k){return cJSON_GetObjectItemCaseSensitive(o,k);}
static const char *sh_str(cJSON *o,const char *k,const char *fallback){cJSON *v=sh_get(o,k);return cJSON_IsString(v)&&v->valuestring[0]?v->valuestring:fallback;}
static int sh_num(cJSON *o,const char *k,int fallback){cJSON *v=sh_get(o,k);return cJSON_IsNumber(v)?v->valueint:fallback;}
static void sh_value(cJSON *v,char *out,size_t n){
 if(cJSON_IsString(v))snprintf(out,n,"%s",v->valuestring);
 else if(cJSON_IsBool(v))snprintf(out,n,"%s",cJSON_IsTrue(v)?"已开启":"已关闭");
 else if(cJSON_IsNumber(v))snprintf(out,n,"%.8g",v->valuedouble);
 else snprintf(out,n,"—");
}
static cJSON *sh_section(struct app *a,const char *id){cJSON *s; cJSON_ArrayForEach(s,sh_get(a->shell.snapshot,"sections")){if(!strcmp(sh_str(s,"id",""),id))return s;}return NULL;}
/* Blue = physical network; teal = resources/device; violet = routed services.
 * Neutral backgrounds and warning/battery status glyphs are not category cards. */
enum {SH_NETWORK_COLOR,SH_DEVICE_COLOR,SH_SERVICE_COLOR};
static int sh_color_group(const char *section){
 if(!strncmp(section,"clash",5)||!strncmp(section,"tailscale",9)||!strcmp(section,"sms"))return SH_SERVICE_COLOR;
 const char*network[]={"wifi","cell","usb","router","band","signal","diagnostics"};
 for(size_t n=0;n<sizeof(network)/sizeof(network[0]);n++)if(!strcmp(section,network[n]))return SH_NETWORK_COLOR;
 return SH_DEVICE_COLOR;
}
static int sh_current_group(struct app*a){
 if(a->shell.section[0])return sh_color_group(a->shell.section);
 return a->shell.tab==1?SH_NETWORK_COLOR:(a->shell.tab==2||a->shell.tab==3)?SH_SERVICE_COLOR:SH_DEVICE_COLOR;
}
static uint16_t sh_category_card(int group){
 static const uint16_t colors[]={SH_RGB(32,52,78),SH_RGB(24,58,59),SH_RGB(57,43,69)};
 return sh_theme?SH_CARD:colors[group];
}
static uint16_t sh_category_ink(int group){
 static const uint16_t colors[]={SH_RGB(146,188,245),SH_RGB(120,222,206),SH_RGB(208,181,240)};
 return sh_theme?SH_CYAN:colors[group];
}
static void sh_text(struct drm_buf*b,int x,int y,const char*t,int sz,uint16_t col,int w){draw_text_clip(b,x,y,t,sz<14?14:sz,col,w);}
static void sh_button(struct drm_buf*b,struct app*a,int x,int y,int w,int h,const char*t,int id,int active){
 sh_surface(b,x,y,x+w,y+h,10,active?SH_CYAN:SH_RAISED);draw_text_center(b,x,x+w,y+(h-15)/2,t,15,active?SH_DARK:SH_TEXT);hit_add(a,x,y,x+w,y+h,id);
}
static void sh_wrap(struct drm_buf*b,int x,int y,const char*t,int maxw,int lines,uint16_t col){
 /* UTF-8 boundaries, real glyph width; long descriptions remain visible. */
 char row[256];int n=0,line=0;const char*p=t;
 while(*p&&line<lines){const char*next=p;utf8_next(&next);int bytes=(int)(next-p);
  if(n+bytes>250)break;memcpy(row+n,p,bytes);row[n+bytes]=0;
  if(n&&text_width(row,14)>maxw){row[n]=0;sh_text(b,x,y+line*22,row,14,col,maxw);line++;n=0;continue;}
  n+=bytes;p+=bytes;
 }
 if(n&&line<lines){row[n]=0;sh_text(b,x,y+line*22,row,14,col,maxw);}
}
static void sh_close(struct app*a){struct panel_shell*s=&a->shell;cJSON_Delete(s->draft);s->draft=NULL;cJSON_Delete(s->pending_args);s->pending_args=NULL;cJSON_Delete(s->report_lines);s->report_lines=NULL;s->report_page=0;s->modal=s->editor=0;s->nfields=0;s->reveal=0;s->message[0]=0;memset(s->values,0,sizeof(s->values));s->choice_search[0]=s->search_edit[0]=0;s->search_saved_page=0;s->choice_page=0;}
/* Reports wrap at glyph boundaries and keep a full paged in-memory copy. */
static void sh_show_report(struct app*a,const cJSON*report){
 sh_close(a);struct panel_shell*s=&a->shell;s->draft=cJSON_CreateObject();
 cJSON_AddStringToObject(s->draft,"label",sh_str((cJSON*)report,"title","结果"));s->report_lines=cJSON_CreateArray();cJSON*line;
 cJSON_ArrayForEach(line,sh_get((cJSON*)report,"lines")){
  if(!cJSON_IsString(line))continue;const char*p=line->valuestring;char row[256];int used=0;
  while(*p&&cJSON_GetArraySize(s->report_lines)<1200){const char*q=p;utf8_next(&q);int n=(int)(q-p);
   if(*p=='\n'){row[used]=0;cJSON_AddItemToArray(s->report_lines,cJSON_CreateString(row));used=0;p++;continue;}
   if(used+n>250){row[used]=0;cJSON_AddItemToArray(s->report_lines,cJSON_CreateString(row));used=0;continue;}
   memcpy(row+used,p,n);row[used+n]=0;
   if(used&&text_width(row,16)>272){row[used]=0;cJSON_AddItemToArray(s->report_lines,cJSON_CreateString(row));used=0;continue;}
   used+=n;p=q;
  }
  if(cJSON_GetArraySize(s->report_lines)>=1200){cJSON_AddItemToArray(s->report_lines,cJSON_CreateString("结果过长，仅展示前 1200 行。"));break;}
  row[used]=0;cJSON_AddItemToArray(s->report_lines,cJSON_CreateString(row));
 }
 if(!cJSON_GetArraySize(s->report_lines))cJSON_AddItemToArray(s->report_lines,cJSON_CreateString("暂无数据"));s->modal=6;s->status_until=0;
}
static void sh_open_section(struct app*a,const char*section){snprintf(a->shell.section,sizeof(a->shell.section),"%s",section);a->shell.subpage=1;a->shell.item_page=0;}
static void sh_issue(struct app*a,cJSON*args){
 const char*action=sh_str(a->shell.draft,"action","");
 if(a->shell.busy){snprintf(a->shell.message,sizeof(a->shell.message),"上一项操作仍在执行，请稍候");a->shell.modal=3;return;}
 if(!*action){snprintf(a->shell.message,sizeof(a->shell.message),"此项暂无可执行操作");a->shell.modal=3;return;}
 int retry=!strcmp(action,"wifi.relay.connect");
 shell_dispatch(a,action,args);
 if(retry&&a->shell.busy){a->shell.modal=a->shell.editor=0;a->shell.reveal=0;cJSON_Delete(a->shell.pending_args);a->shell.pending_args=NULL;}
 else sh_close(a);
}
static void sh_prepare(struct app*a,cJSON*args){
 if(cJSON_IsTrue(sh_get(a->shell.draft,"confirm"))){cJSON_Delete(a->shell.pending_args);a->shell.pending_args=cJSON_Duplicate(args,1);a->shell.modal=2;a->shell.editor=0;}
 else sh_issue(a,args);
}
static int sh_item_interactive(cJSON*item){
 if(!item||cJSON_IsFalse(sh_get(item,"enabled"))||!strcmp(sh_str(item,"type","info"),"info"))return 0;
 return !strcmp(sh_str(item,"type",""),"navigation")||!strcmp(sh_str(item,"type",""),"report")||sh_str(item,"action","")[0];
}
static void sh_open_item(struct app*a,cJSON*item){
 if(!sh_item_interactive(item))return;
 struct panel_shell*s=&a->shell;const char*type;int i;
 if(s->busy){snprintf(s->status,sizeof(s->status),"正在执行操作，请稍候再打开设置");s->status_until=now_ms()+8000;return;}
 sh_close(a);s->draft=cJSON_Duplicate(item,1);if(!s->draft)return;
 if(cJSON_IsFalse(sh_get(item,"enabled"))&&strcmp(sh_str(item,"type","info"),"info")){snprintf(s->message,sizeof(s->message),"%s",sh_str(item,"reason","暂不可用，请刷新后重试"));s->modal=3;return;}
 type=sh_str(item,"type","info");
 if(!strcmp(type,"navigation")){sh_open_section(a,sh_str(item,"section",""));sh_close(a);return;}
 if(!strcmp(type,"form")){
  cJSON*fs=sh_get(s->draft,"fields");int total=cJSON_GetArraySize(fs);
  if(total>SHELL_MAX_FIELDS){snprintf(s->message,sizeof(s->message),"此表单字段超出屏幕容量，暂不能安全提交");s->modal=3;return;}
  s->nfields=total;s->modal=4;s->field_page=0;s->key_page=0;s->shift=0;
  for(i=0;i<total;i++){cJSON*f=cJSON_GetArrayItem(fs,i);cJSON*v=sh_get(f,"value");if(cJSON_IsString(v)&&strlen(v->valuestring)>=SHELL_VALUE_CAP){snprintf(s->message,sizeof(s->message),"字段内容过长，无法安全编辑");s->modal=3;return;}if(v&&!cJSON_IsNull(v))sh_value(v,s->values[i],sizeof(s->values[i]));}
 if(!strcmp(sh_str(s->draft,"action",""),"wifi.relay.connect")&&total==1&&cJSON_IsTrue(sh_get(cJSON_GetArrayItem(fs,0),"required"))){s->field=0;s->editor=1;}
 }else if(!strcmp(type,"choice")){s->modal=1;s->choice_page=0;}
 else if(!strcmp(type,"report")){cJSON*r=cJSON_CreateObject();cJSON_AddStringToObject(r,"title",sh_str(item,"label","详情"));cJSON*ls=cJSON_AddArrayToObject(r,"lines");cJSON_AddItemToArray(ls,cJSON_CreateString(sh_str(item,"detail",sh_str(item,"value","暂无数据"))));sh_show_report(a,r);cJSON_Delete(r);}
 else if(!strcmp(type,"info")){char v[180];sh_value(sh_get(item,"value"),v,sizeof(v));snprintf(s->message,sizeof(s->message),"%s",v);s->modal=3;}
 else {cJSON*args=sh_get(s->draft,"args");cJSON*copy=args?cJSON_Duplicate(args,1):cJSON_CreateObject();sh_prepare(a,copy);cJSON_Delete(copy);}
}
static void sh_units(double bytes,char*out,size_t n,int speed){
 const char*units[]={"B","KiB","MiB","GiB","TiB"};int u=0;while(bytes>=1024&&u<4){bytes/=1024;u++;}snprintf(out,n,bytes>=100?"%.0f %s%s":"%.1f %s%s",bytes,units[u],speed?"/s":"");
}
static void sh_metric(cJSON*data,const char*key,char*out,size_t n,int speed){cJSON*v=sh_get(data,key);if(cJSON_IsNumber(v)&&v->valuedouble>=0)sh_units(v->valuedouble,out,n,speed);else snprintf(out,n,"—");}
/* A viewport clips pixels AND hit targets. Absolute indices keep actions stable
 * when a row is partially visible; no paging controls or empty pager reserve. */
static void sh_scroll_begin(struct app*a,int*offset,int height,int top,int bottom){
 struct panel_shell*s=&a->shell;s->scroll_offset=offset;s->scroll_top=top;s->scroll_bottom=bottom;
 s->scroll_max=height-(bottom-top);if(s->scroll_max<0)s->scroll_max=0;
 if(*offset<0)*offset=0;if(*offset>s->scroll_max)*offset=s->scroll_max;
 draw_clip_top=top;draw_clip_bottom=bottom;
}
static void sh_scroll_end(struct drm_buf*b,struct app*a){
 struct panel_shell*s=&a->shell;draw_clip_top=0;draw_clip_bottom=H;
 if(s->scroll_max>0){int h=s->scroll_bottom-s->scroll_top,len=h*h/(h+s->scroll_max);if(len<18)len=18;
 int y=s->scroll_top+(*s->scroll_offset)*(h-len)/s->scroll_max;
 fill_round(b,314,y,317,y+len,1,SH_MUTED);}
}
static void sh_row(struct drm_buf*b,struct app*a,int y,const char*title,const char*value,int id,int enabled,int group){
 sh_surface(b,12,y,308,y+57,11,sh_category_card(group));
 int title_w=text_width(title,16),value_w=text_width(value,18);
 if(title_w+value_w+20<=249){sh_text(b,24,y+19,title,16,SH_MUTED,title_w+2);sh_text(b,273-value_w,y+18,value,18,enabled?SH_TEXT:SH_MUTED,value_w+2);}
 else {sh_text(b,24,y+8,title,14,SH_MUTED,255);sh_text(b,24,y+30,value,17,enabled?SH_TEXT:SH_MUTED,251);}
 if(id&&enabled){sh_text(b,285,y+20,"›",20,sh_category_ink(group),16);hit_add(a,12,y,308,y+57,id);}
}
static int sh_choice_current(struct app*a,cJSON*c){
 struct panel_shell*s=&a->shell;
 if(s->modal==5){cJSON*v=sh_get(c,"value");return cJSON_IsString(v)&&!strcmp(v->valuestring,s->values[s->field]);}
 const char *current=sh_str(s->draft,"value","");
 if(!strcmp(sh_str(s->draft,"action",""),"clash.service")){
  const char*operation=sh_str(sh_get(c,"args"),"operation","");
  return (!strcmp(current,"开启")&&!strcmp(operation,"start"))||(!strcmp(current,"关闭")&&!strcmp(operation,"stop"));
 }
 if(!strcmp(sh_str(s->draft,"action",""),"clash.select")){
  const char*node=sh_str(sh_get(sh_get(s->snapshot,"data"),"clash"),"node","");
  const char*name=sh_str(sh_get(c,"args"),"name","");return *node&&*name&&!strcmp(node,name);
 }
 return *current&&!strcmp(current,sh_str(c,"label",""));
}
/* Completion is shared with the real worker and preview regressions. */
static void sh_action_result(struct app*a,cJSON*r){
 struct panel_shell*s=&a->shell;s->busy=0;s->status_until=now_ms()+8000;
 snprintf(s->status,sizeof(s->status),"%s",sh_str(r,"message","操作超时或结果未知，请刷新核对"));
 if(cJSON_IsObject(sh_get(r,"picker")))sh_open_item(a,sh_get(r,"picker"));
 else if(cJSON_IsObject(sh_get(r,"report")))sh_show_report(a,sh_get(r,"report"));
 else if(!r||!cJSON_IsTrue(sh_get(r,"ok"))){
  snprintf(s->message,sizeof(s->message),"%s",s->status);s->status_until=0;
  s->modal=s->draft&&!strcmp(sh_str(s->draft,"action",""),"wifi.relay.connect")?4:3;
 }else if(s->draft&&!strcmp(sh_str(s->draft,"action",""),"wifi.relay.connect"))sh_close(a);
}
static void sh_choice_row(struct drm_buf*b,struct app*a,int y,cJSON*c,int id){
 int enabled=!cJSON_IsFalse(sh_get(c,"enabled")),selected=sh_choice_current(a,c);const char*label=sh_str(c,"label","选项"),*description=sh_str(c,"description","");
 int width=selected?208:249;
 sh_surface(b,12,y,308,y+57,14,selected?SH_RAISED:sh_category_card(sh_current_group(a)));
 if(text_width(label,15)>width)sh_wrap(b,24,y+7,label,width,2,enabled?SH_TEXT:SH_MUTED);
 else {sh_text(b,24,y+(*description?8:19),label,15,SH_TEXT,width);if(*description)sh_text(b,24,y+32,description,14,SH_MUTED,width);}
 if(selected)sh_text(b,251,y+21,"当前",14,SH_CYAN,45);
 else if(enabled)sh_text(b,285,y+20,"›",20,SH_CYAN,16);
 if(enabled)hit_add(a,12,y,308,y+57,id);

}
static const char *sh_net_sections[]={"wifi","cell","usb","router","clients"};
static const char *sh_net_titles[]={"Wi-Fi","蜂窝网络","USB 与网口","路由与安全","已连接设备"};
static const char *sh_net_notes[]={"名称、密码与无线设置","选网、APN 与 SIM","接口模式与网络共享","局域网、DHCP 与 DNS","查看接入终端"};
static const char *sh_settings_sections[]={"system","battery","usage","signal","sms","band","sim","diagnostics"};
static const char *sh_settings_titles[]={"电源与屏幕","电池与省电","套餐流量账本","信号与邻区","短信收件箱","频段与小区","SIM 卡状态","网络诊断"};
static const char *sh_settings_notes[]={"主题、亮度与设备信息","充电、待机与供电策略","额度、月结日与用量","载波、信号与锁定工具","读取通知与验证码","频段限制与恢复自动","卡槽与安全状态","分流记录与网站测试"};
/* The asynchronous snapshot owns service state, while the clock is local.
 * Neither may fall back to fields from the retired synchronous UI. */
static const char *sh_outlet(cJSON*d){
 const char *verdict=sh_str(sh_get(d,"clash"),"verdict","");
 if(!strcmp(verdict,"takeover"))return "代理生效";
 if(!strcmp(verdict,"partial"))return "代理不完整";
 if(!strcmp(verdict,"unverified"))return "代理未核验";
 if(!strcmp(verdict,"core_down"))return "代理核心未运行";
 if(!strcmp(verdict,"error"))return "代理异常";
 if(!strcmp(verdict,"direct"))return "直连";
 if(!strcmp(verdict,"tailscale"))return "Tailscale";
 const char *profile=sh_str(d,"network_profile","");
 return !strcmp(profile,"clash")?"代理未核验":!strcmp(profile,"tailscale")?"Tailscale":!strcmp(profile,"direct")?"直连":"未确认";
}
static void sh_clock_text(time_t now,char*out,size_t n){
 struct tm tm;if(localtime_r(&now,&tm)&&tm.tm_year>=120)strftime(out,n,"%H:%M",&tm);else snprintf(out,n,"--:--");
}
/* Firmware scalars may be JSON numbers or decimal strings. Unknown and
 * out-of-range values stay unknown; do not turn an RSRP reading into bars. */
static int sh_status_value(cJSON*v,int maximum){
 if(cJSON_IsNumber(v))return v->valuedouble>=0&&v->valuedouble<=maximum&&v->valuedouble==v->valueint?v->valueint:-1;
 if(!cJSON_IsString(v)||!v->valuestring[0])return -1;
 int value=0;for(const char*p=v->valuestring;*p;p++){if(*p<'0'||*p>'9')return -1;value=value*10+(*p-'0');if(value>maximum)return -1;}return value;
}
static int sh_signal_bars(struct app*a){
 /* read_stats() exposes the original signalbar in cell.signal. data.signal
  * instead prefers nr5g_rsrp, so only a verified 0..5 value is a fallback. */
 cJSON*it;cJSON_ArrayForEach(it,sh_get(sh_section(a,"cell"),"items"))if(!strcmp(sh_str(it,"id",""),"signal"))return sh_status_value(sh_get(it,"value"),5);
 return sh_status_value(sh_get(sh_get(a->shell.snapshot,"data"),"signal"),5);
}
static void sh_statusbar(struct drm_buf*b,struct app*a,time_t now){
 cJSON*d=sh_get(a->shell.snapshot,"data");char clock_text[16],battery[16];int bat=sh_status_value(sh_get(d,"battery"),100),bars=sh_signal_bars(a);
 sh_background(b,144,0,320,44);sh_clock_text(now,clock_text,sizeof(clock_text));
 /* Fixed digit cells keep HH:MM and the colon stationary across every minute. */
 static const int clock_x[]={144,156,168,174,186},clock_w[]={12,12,6,12,12};
 for(int i=0;i<5;i++){char digit[]={clock_text[i],0};draw_text_center(b,clock_x[i],clock_x[i]+clock_w[i],14,digit,20,SH_TEXT);}
 if(bat>=0)snprintf(battery,sizeof(battery),"%d",bat);else snprintf(battery,sizeof(battery),"—");
 const char*usb_badge=sh_str(sh_get(d,"usb"),"badge","");
 const char*port=!strcmp(usb_badge,"WAN")?"WAN":!strcmp(usb_badge,"LAN")?"LAN":!strcmp(usb_badge,"WAIT")?"···":!strcmp(usb_badge,"ERROR")?"!":"";
 if(*port)draw_text_center(b,199,230,16,port,12,!strcmp(usb_badge,"ERROR")?SH_WARN:SH_CYAN);
 enum panel_battery_power power=a->battery_power;
 /* Fixed 8px gaps: USB slot ends at 230, battery starts at 238;
  * battery terminal ends at 274, signal starts at 282. Symbols live inside
  * the battery; only the centered content moves when power direction changes. */
 uint16_t battery_color=sh_theme?SH_RGB(20,125,68):SH_RGB(27,132,79);
 uint16_t battery_edge=sh_theme?SH_RGB(17,112,60):SH_RGB(67,164,109);
 fill_round(b,238,15,272,29,2,battery_edge);
 fill_round(b,239,16,271,28,1,battery_color);
 fill_rect(b,272,19,274,25,battery_edge);
 int direction=power==BP_CHARGING||power==BP_OUTPUT;
 int number_w=text_width(battery,14),content_w=number_w+(direction?7:0);
 int number_x=238+(34-content_w)/2;
 draw_text(b,number_x,15,battery,14,SH_RGB(255,255,255));
 if(direction){
  int symbol_x=number_x+number_w+2;
  fill_rect(b,symbol_x,22,symbol_x+5,23,SH_RGB(255,255,255));
  if(power==BP_CHARGING)fill_rect(b,symbol_x+2,20,symbol_x+3,25,SH_RGB(255,255,255));
 }
 for(int i=0;i<5;i++){int x=282+i*5,h=4+i*3;fill_rect(b,x,30-h,x+3,30,bars>=i+1?SH_CYAN:SH_RAISED);}
 if(bars<=0){fill_rect(b,282,11,305,30,SH_BG);sh_text(b,286,12,bars<0?"?":"×",18,SH_MUTED,13);}

}
static void sh_home(struct drm_buf*b,struct app*a){
 struct panel_shell*s=&a->shell;cJSON*d=sh_get(s->snapshot,"data"),*cl=sh_get(d,"clash");char tmp[160],down[40],up[40],today[40],month[40],quota[40],used[40];
 if(!strcmp(sh_str(d,"physical_iface",""),"u60sta"))snprintf(tmp,sizeof(tmp),"%s",sh_str(d,"wifi_status","Wi-Fi 上游中继"));else snprintf(tmp,sizeof(tmp),"%s · %s",sh_str(d,"operator",a->operator[0]?a->operator:"运营商未知"),sh_str(d,"network",a->net_type[0]?a->net_type:"网络未知"));sh_text(b,16,51,tmp,15,SH_CYAN,287);
 snprintf(tmp,sizeof(tmp),"出口 · %s",sh_outlet(d));sh_text(b,16,74,tmp,14,SH_MUTED,287);
 sh_surface(b,12,96,308,167,13,sh_category_card(SH_NETWORK_COLOR));sh_metric(d,"download_bps",down,sizeof(down),1);sh_metric(d,"upload_bps",up,sizeof(up),1);
 sh_text(b,24,105,"↓ 下载",14,SH_MUTED,130);sh_text(b,170,105,"↑ 上传",14,SH_MUTED,126);sh_text(b,24,128,down,24,SH_TEXT,135);sh_text(b,170,128,up,24,SH_TEXT,126);
 sh_metric(d,"today_bytes",today,sizeof(today),0);sh_metric(d,"month_bytes",month,sizeof(month),0);sh_surface(b,12,174,308,230,11,sh_category_card(SH_DEVICE_COLOR));
 sh_text(b,24,184,"蜂窝流量",14,SH_MUTED,100);cJSON*usage=sh_get(d,"usage");const char*warning_state=sh_str(usage,"warning","");int warning=!strcmp(warning_state,"threshold")||!strcmp(warning_state,"exceeded");sh_text(b,170,184,warning?"套餐用量预警 ›":"套餐台账 ›",14,warning?SH_CYAN:SH_MUTED,126);snprintf(tmp,sizeof(tmp),"今日 %s",today);sh_text(b,24,206,tmp,17,SH_TEXT,138);snprintf(tmp,sizeof(tmp),"本月 %s",month);sh_text(b,170,206,tmp,17,SH_TEXT,126);hit_add(a,12,174,308,230,SH_SECTION+24);
 sh_surface(b,12,237,308,326,12,sh_category_card(SH_SERVICE_COLOR));snprintf(tmp,sizeof(tmp),"已选节点 · %s",mode_label(sh_str(cl,"mode","")));sh_text(b,24,247,tmp,16,sh_category_ink(SH_SERVICE_COLOR),181);
 char connections[24];sh_value(sh_get(cl,"connections"),connections,sizeof(connections));snprintf(tmp,sizeof(tmp),"%s 连接",connections);sh_text(b,220,248,tmp,15,SH_MUTED,78);
 sh_text(b,24,272,sh_str(cl,"node","尚未选择节点"),17,SH_TEXT,268);
 sh_metric(cl,"quota_remaining",quota,sizeof(quota),0);snprintf(tmp,sizeof(tmp),"剩余 %s",quota);sh_text(b,24,302,tmp,15,SH_TEXT,139);
 cJSON*u=sh_get(cl,"upload"),*v=sh_get(cl,"download");if(cJSON_IsNumber(u)&&cJSON_IsNumber(v))sh_units(u->valuedouble+v->valuedouble,used,sizeof(used),0);else snprintf(used,sizeof(used),"—");snprintf(tmp,sizeof(tmp),"累计 %s",used);sh_text(b,170,302,tmp,15,SH_MUTED,126);hit_add(a,12,237,308,326,SH_SECTION+22);
 const char*labels[]={cJSON_IsTrue(sh_get(sh_get(d,"wifi_relay"),"enabled"))?"中继管理 / 停止":"Wi-Fi 热点","USB 与网口"};const char*keys[]={"wifi_status","usb_status"};
 for(int i=0;i<2;i++){int x=12+i*151;sh_surface(b,x,333,x+145,378,10,sh_category_card(SH_NETWORK_COLOR));sh_text(b,x+10,341,labels[i],14,SH_MUTED,125);sh_text(b,x+10,358,sh_str(d,keys[i],"状态未知"),15,SH_TEXT,125);hit_add(a,x,333,x+145,378,SH_SECTION+20+i);}
 char clients[24];sh_value(sh_get(d,"clients"),clients,sizeof(clients));
 sh_surface(b,12,383,308,430,9,sh_category_card(SH_DEVICE_COLOR));snprintf(tmp,sizeof(tmp),"已连接设备 · %s 台",clients);sh_text(b,24,399,tmp,16,SH_TEXT,250);sh_text(b,285,398,"›",20,SH_CYAN,16);hit_add(a,12,383,308,430,SH_SECTION+25);

}
static void sh_navigation(struct drm_buf*b,struct app*a){
 static const char*t[]={"总览","网络","代理","组网","设置"};
 sh_background(b,0,432,320,480);fill_rect(b,0,432,320,433,SH_RAISED);
 for(int i=0;i<5;i++){
  int x=i*64,group=i==1?SH_NETWORK_COLOR:(i==2||i==3)?SH_SERVICE_COLOR:SH_DEVICE_COLOR;
  int selected=a->shell.tab==i;uint16_t ink=selected?sh_category_ink(group):SH_MUTED;
  if(selected)sh_surface(b,x+5,436,x+59,478,10,sh_theme?SH_RAISED:sh_category_card(group));
  sh_nav_icon(b,i,x+23,439,ink);draw_text_center(b,x,x+64,461,t[i],14,ink);
  hit_add(a,x,433,x+64,480,SH_TAB+i);
 }
}
static int sh_menu_count(struct app*a){return a->shell.tab==1?5:(int)(sizeof(sh_settings_sections)/sizeof(sh_settings_sections[0]))+1;}
static void sh_section_list(struct drm_buf*b,struct app*a){
 int n=sh_menu_count(a);const char**names=a->shell.tab==1?sh_net_titles:sh_settings_titles;const char**notes=a->shell.tab==1?sh_net_notes:sh_settings_notes;
 sh_scroll_begin(a,&a->shell.menu_page,n*63+8,52,428);
 for(int i=0;i<n;i++){int y=56+i*63-a->shell.menu_page;if(y+57<=52||y>=428)continue;
 if(a->shell.tab==4&&i==n-1)sh_row(b,a,y,"切换到原厂界面","备用管理入口",SH_FACTORY,1,SH_DEVICE_COLOR);
 else sh_row(b,a,y,names[i],notes[i],SH_SECTION+i,1,sh_color_group(a->shell.tab==1?sh_net_sections[i]:sh_settings_sections[i]));}
 sh_scroll_end(b,a);
}
static void sh_detail(struct drm_buf*b,struct app*a){
 struct panel_shell*s=&a->shell;cJSON*sec=sh_section(a,s->section),*items=sh_get(sec,"items");int n=cJSON_GetArraySize(items);char val[256];
 if(!sec){sh_wrap(b,24,100,"正在读取设置，请稍候，页面会自动更新。",268,4,SH_MUTED);return;}
 sh_scroll_begin(a,&s->item_page,n*63+8,52,428);
 for(int i=0;i<n;i++){int y=56+i*63-s->item_page;if(y+57<=52||y>=428)continue;
 cJSON*it=cJSON_GetArrayItem(items,i);sh_value(sh_get(it,"value"),val,sizeof(val));
 if(!strcmp(sh_str(it,"type","info"),"action")&&!sh_get(it,"value"))snprintf(val,sizeof(val),"轻点执行");
 int interactive=sh_item_interactive(it);sh_row(b,a,y,sh_str(it,"label","设置"),val,interactive?SH_ITEM+i:0,interactive,sh_current_group(a));}
 sh_scroll_end(b,a);
}
/* Filtering only indexes the immutable cloned draft; never edit choices/args. */
static cJSON *sh_choices(struct panel_shell*s){return s->modal==5?sh_get(cJSON_GetArrayItem(sh_get(s->draft,"fields"),s->field),"choices"):sh_get(s->draft,"choices");}
static unsigned char sh_ascii_lower(unsigned char c){return c>='A'&&c<='Z'?(unsigned char)(c+32):c;}
static int sh_contains_ascii(const char*text,const char*term){
 if(!*term)return 1;
 for(;*text;text++){const char*a=text,*b=term;while(*a&&*b&&sh_ascii_lower((unsigned char)*a)==sh_ascii_lower((unsigned char)*b)){a++;b++;}if(!*b)return 1;}return 0;
}
static int sh_region_match(const char*label,const char*term){
 static const struct {const char*code,*zh,*traditional;} regions[]={
  {"hk","香港","香港"},{"tw","台湾","臺灣"},{"jp","日本","日本"},{"kr","韩国","韓國"},
  {"sg","新加坡","新加坡"},{"us","美国","美國"},{"uk","英国","英國"},{"gb","英国","英國"},
  {"de","德国","德國"},{"fr","法国","法國"},{"ca","加拿大","加拿大"},{"au","澳大利亚","澳洲"},
  {"in","印度","印度"},{"ru","俄罗斯","俄羅斯"},{"vn","越南","越南"},{"th","泰国","泰國"},
  {"my","马来西亚","馬來西亞"},{"tr","土耳其","土耳其"}};
 for(size_t n=0;n<sizeof(regions)/sizeof(regions[0]);n++)if(!strcasecmp(term,regions[n].code))return strstr(label,regions[n].zh)!=NULL||strstr(label,regions[n].traditional)!=NULL;
 return 0;
}
static int sh_choice_matches(cJSON*choice,const char*query){
 const char*label=sh_str(choice,"label","");char term[SHELL_SEARCH_CAP];const char*p=query;
 /* Space-separated terms combine with AND: e.g. HK 04 matches 香港 044. */
 while(*p){while(*p==' '||*p=='\t')p++;if(!*p)break;size_t n=0;while(*p&&*p!=' '&&*p!='\t'){if(n<sizeof(term)-1)term[n++]=*p;p++;}term[n]=0;if(!sh_contains_ascii(label,term)&&!sh_region_match(label,term))return 0;}
 return 1;
}
static int sh_choice_count(struct panel_shell*s){int count=0;cJSON*c;cJSON_ArrayForEach(c,sh_choices(s))if(sh_choice_matches(c,s->choice_search))count++;return count;}
static cJSON *sh_choice_at(struct panel_shell*s,int filtered_index){cJSON*c;if(filtered_index<0)return NULL;cJSON_ArrayForEach(c,sh_choices(s))if(sh_choice_matches(c,s->choice_search)){if(!filtered_index--)return c;}return NULL;}
static void sh_search_reset(struct panel_shell*s){s->choice_search[0]=s->search_edit[0]=0;s->choice_page=0;s->search_saved_page=0;}
static const char *sh_keyboard(struct panel_shell*s){
 static const char*pages[]={"abcdefghijklmnopqrstuvwxyz","0123456789.-/:_@+", "!#$%&*()=,?[]{}<>;\\\"'`~^|"};return pages[s->key_page%3];
}
static size_t sh_input_capacity(struct panel_shell*s){
 if(s->editor==2)return SHELL_SEARCH_CAP;
 cJSON*f=cJSON_GetArrayItem(sh_get(s->draft,"fields"),s->field);
 int limit=sh_num(f,"maxLength",SHELL_VALUE_CAP-1);
 return limit>0&&limit<SHELL_VALUE_CAP?(size_t)limit+1:SHELL_VALUE_CAP;
}
static void sh_editor(struct drm_buf*b,struct app*a){
 struct panel_shell*s=&a->shell;int search=s->editor==2;cJSON*f=search?NULL:cJSON_GetArrayItem(sh_get(s->draft,"fields"),s->field);const char*kind=sh_str(f,"kind","text");const char*input=search?s->search_edit:s->values[s->field];size_t capacity=sh_input_capacity(s);char value[SHELL_VALUE_CAP],key[2]={0,0};const char*keys=sh_keyboard(s);int i,n=(int)strlen(keys),password=!search&&!strcmp(kind,"password");
 hit_reset(a);sh_background(b,0,0,320,480);
 sh_text(b,12,8,search?"搜索选项":sh_str(f,"label","编辑"),18,SH_TEXT,296);
 sh_button(b,a,8,35,96,44,"返回",SH_CANCEL,0);sh_button(b,a,210,35,102,44,"完成",SH_DONE,1);
 snprintf(value,sizeof(value),"%s",input);if(password&&!s->reveal)for(i=0;value[i];i++)value[i]='*';
 fill_round(b,8,85,312,135,10,SH_CARD);
 const char*visible=value;while(*visible&&text_width(visible,17)>276){visible++;while((*visible&0xC0)==0x80)visible++;}
 sh_text(b,18,101,*visible?visible:"|",17,SH_TEXT,280);
 char count[60];snprintf(count,sizeof(count),"%zu / %zu%s",strlen(input),capacity-1,password&&sh_num(f,"minLength",0)?" · 至少8位":"");sh_text(b,12,151,count,14,SH_MUTED,188);
 if(password)sh_button(b,a,204,140,108,44,s->reveal?"隐藏密码":"显示密码",SH_REVEAL,0);else if(search||!strcmp(kind,"number"))sh_button(b,a,204,140,108,44,"清空",SH_SEARCH_CLEAR,0);
 sh_button(b,a,8,190,96,44,"ABC",SH_ALPHA,s->key_page==0);
 sh_button(b,a,112,190,96,44,"123",SH_DIGITS,s->key_page==1);
 sh_button(b,a,216,190,96,44,"符号",SH_SYMBOLS,s->key_page==2);
 for(i=0;i<n;i++){int x=2+(i%7)*45,y=240+(i/7)*45;key[0]=keys[i];if(s->shift&&key[0]>='a'&&key[0]<='z')key[0]-=32;sh_button(b,a,x,y,44,44,key,SH_KEY+i,0);}
 sh_button(b,a,8,426,96,48,s->shift?"大写锁定":"小写",SH_SHIFT,s->shift);
 sh_button(b,a,112,426,96,48,"空格",SH_SPACE,0);sh_button(b,a,216,426,96,48,"退格",SH_DELETE,0);
}
static void sh_form(struct drm_buf*b,struct app*a){
 struct panel_shell*s=&a->shell;int i;char val[SHELL_VALUE_CAP];cJSON*fs=sh_get(s->draft,"fields");
 hit_reset(a);sh_background(b,0,0,320,480);sh_text(b,16,16,sh_str(s->draft,"label","修改设置"),20,SH_TEXT,285);sh_text(b,16,50,"轻点输入框编辑 · 保存后应用",14,SH_MUTED,285);
 sh_scroll_begin(a,&s->field_page,s->nfields*63+8,76,396);
 for(i=0;i<s->nfields;i++){int k=i;int y=80+i*63-s->field_page;if(y+57<=76||y>=396)continue;cJSON*f=cJSON_GetArrayItem(fs,k);snprintf(val,sizeof(val),"%s",s->values[k][0]?s->values[k]:"轻点输入");if(!strcmp(sh_str(f,"kind","text"),"choice")){cJSON*c;cJSON_ArrayForEach(c,sh_get(f,"choices")){char cv[SHELL_VALUE_CAP];sh_value(sh_get(c,"value"),cv,sizeof(cv));if(!strcmp(cv,s->values[k])){snprintf(val,sizeof(val),"%s",sh_str(c,"label",cv));break;}}}if(!strcmp(sh_str(f,"kind","text"),"password")&&s->values[k][0])snprintf(val,sizeof(val),"••••••••");sh_row(b,a,y,sh_str(f,"label","字段"),val,SH_FIELD+i,1,sh_current_group(a));}
 sh_scroll_end(b,a);
 if(s->message[0]){if(!strcmp(sh_str(s->draft,"action",""),"wifi.relay.connect"))sh_wrap(b,16,275,s->message,288,6,SH_WARN);else sh_text(b,16,400,s->message,14,SH_WARN,288);}else if(!strcmp(sh_str(s->draft,"action",""),"wifi.relay.connect"))sh_wrap(b,16,220,"也可在手机或电脑登录原厂管理页 → 增强功能 → 网络，输入上游密码。",288,6,SH_MUTED);
 sh_button(b,a,12,429,142,42,"取消",SH_CANCEL,0);sh_button(b,a,166,429,142,42,!strcmp(sh_str(s->draft,"action",""),"wifi.relay.connect")?"连接":"保存",SH_APPLY,1);
}
static void sh_modal(struct drm_buf*b,struct app*a){
 struct panel_shell*s=&a->shell;int i;
 s->scroll_offset=NULL;
 if(s->editor){sh_editor(b,a);return;}if(s->modal==4){sh_form(b,a);return;}
 if(s->modal==6){
  hit_reset(a);sh_background(b,0,0,320,480);sh_text(b,16,15,sh_str(s->draft,"label","结果"),20,SH_TEXT,288);
  int total=cJSON_GetArraySize(s->report_lines);sh_scroll_begin(a,&s->report_page,total*23+8,52,424);
  for(int j=0;j<total;j++){int y=56+j*23-s->report_page;if(y+22<=52||y>=424)continue;cJSON*l=cJSON_GetArrayItem(s->report_lines,j);sh_text(b,20,y,cJSON_IsString(l)?l->valuestring:"",16,SH_TEXT,276);}
  sh_scroll_end(b,a);sh_button(b,a,12,432,296,40,"返回",SH_CANCEL,0);return;
 }
 hit_reset(a);fill_rect(b,0,44,320,480,SH_BG);
 sh_text(b,20,65,sh_str(s->draft,"label","操作"),20,SH_TEXT,280);
 if(s->modal==1||s->modal==5){int total=cJSON_GetArraySize(sh_choices(s)),n=sh_choice_count(s),searchable=total>=12||s->choice_search[0];int y=searchable?128:106;
  fill_rect(b,0,0,144,44,SH_BG);sh_text(b,16,13,"‹ 返回",18,SH_CYAN,122);hit_add(a,0,0,140,44,SH_CANCEL);
  if(searchable){fill_rect(b,12,45,308,92,SH_BG);sh_text(b,20,62,sh_str(s->draft,"label","选项"),18,SH_TEXT,179);sh_button(b,a,210,50,98,38,"搜索",SH_SEARCH,1);char summary[120];snprintf(summary,sizeof(summary),"%s · %d / %d 项",s->choice_search[0]?s->choice_search:"全部",n,total);sh_text(b,16,103,summary,14,SH_MUTED,s->choice_search[0]?209:288);if(s->choice_search[0])sh_button(b,a,234,94,74,30,"清空",SH_SEARCH_CLEAR,0);}
  const char*hint=sh_str(s->draft,"description","");if(*hint){sh_wrap(b,16,y,hint,288,2,SH_MUTED);y+=text_width(hint,14)>288?48:26;}
  sh_scroll_begin(a,&s->choice_page,n*63+4,y,424);
  for(i=0;i<n;i++){int yy=y+i*63-s->choice_page;if(yy+57<=y||yy>=424)continue;cJSON*c=sh_choice_at(s,i);sh_choice_row(b,a,yy,c,SH_CHOICE+i);}
  sh_scroll_end(b,a);
  if(!n)sh_wrap(b,24,151,"没有匹配选项，请修改或清空搜索。",272,3,SH_MUTED);
  sh_button(b,a,12,432,296,40,"取消",SH_CANCEL,0);
 }
 else {int y=117;if(s->modal==2&&s->message[0]){sh_text(b,20,y,"将应用",14,SH_MUTED,278);sh_wrap(b,20,y+28,s->message,278,3,SH_TEXT);y+=110;}sh_wrap(b,20,y,s->modal==2?sh_str(s->draft,"reason","确认后立即应用此设置，请核对所选内容。"):s->message,278,s->modal==2?5:10,SH_MUTED);sh_button(b,a,20,365,s->modal==2?132:280,48,s->modal==2?"取消":"返回",SH_CANCEL,0);if(s->modal==2)sh_button(b,a,168,365,132,48,"确认应用",SH_CONFIRM,1);}
}
static void shell_render(struct drm_buf*b,struct app*a){
#ifndef PANEL_PREVIEW
 static int theme_loaded=0;if(!theme_loaded){sh_theme=panel_theme_load();theme_loaded=1;logline("screen theme loaded=%s",panel_theme_key(sh_theme));}
#endif
 struct panel_shell*s=&a->shell;const char*titles[]={"U60 Pro","网络","代理","组网","设置"};
 s->scroll_offset=NULL;draw_clip_top=0;draw_clip_bottom=H;
 if(s->tab<0||s->tab>4)s->tab=0;sh_background(b,0,0,320,480);hit_reset(a);
 const char*title=s->subpage?sh_str(sh_section(a,s->section),"title",s->section):titles[s->tab];
 int child=sh_str(sh_section(a,s->section),"parent","")[0]!=0;
 int back=s->subpage&&(child||(s->tab!=2&&s->tab!=3));
 sh_text(b,16,13,back?"‹":"",21,SH_CYAN,20);sh_text(b,back?38:16,14,title,19,SH_TEXT,back?100:122);if(back)hit_add(a,0,0,140,44,SH_BACK);
 sh_statusbar(b,a,time(NULL));
 if(s->tab==0)sh_home(b,a);else if(s->subpage)sh_detail(b,a);else if(s->tab==1||s->tab==4)sh_section_list(b,a);else {sh_open_section(a,s->tab==2?"clash":"tailscale");sh_detail(b,a);}
 sh_navigation(b,a);
 if(s->busy){fill_rect(b,0,0,140,44,SH_RAISED);sh_text(b,12,14,"正在处理…",16,SH_CYAN,122);sh_surface(b,12,325,308,420,12,SH_RAISED);sh_wrap(b,24,340,s->status,272,3,SH_TEXT);}
 else if(s->status[0]&&now_ms()<s->status_until){
  /* The title area doubles as a dismissible operation receipt, without blocking navigation. */
  fill_rect(b,0,0,140,44,SH_RAISED);sh_text(b,12,5,"结果 · 轻点查看",14,SH_CYAN,122);sh_text(b,12,24,s->status,14,SH_TEXT,122);
  int receipt_hit=0;for(int h=0;h<a->nhits;h++)if(a->hits[h].id==SH_BACK){a->hits[h].id=SH_STATUS;receipt_hit=1;}if(!receipt_hit)hit_add(a,0,0,140,44,SH_STATUS);
 }
 if(s->modal||s->editor)sh_modal(b,a);
 if(s->power_open){s->scroll_offset=NULL;hit_reset(a);sh_background(b,0,0,320,480);sh_text(b,24,70,"电源",25,SH_TEXT,272);sh_text(b,24,110,"选择设备操作",14,SH_MUTED,272);sh_button(b,a,24,169,272,58,"关闭设备",SH_POWER_OFF,0);sh_button(b,a,24,243,272,58,"重新启动",SH_POWER_REBOOT,0);sh_button(b,a,24,337,272,54,"取消",SH_CANCEL,1);}
}
static void sh_save_form(struct app*a){
 struct panel_shell*s=&a->shell;cJSON*fs=sh_get(s->draft,"fields"),*base=sh_get(s->draft,"args"),*args=base?cJSON_Duplicate(base,1):cJSON_CreateObject();int i;
 if(!args)return;
 for(i=0;i<s->nfields;i++){cJSON*f=cJSON_GetArrayItem(fs,i);const char*key=sh_str(f,"key","");const char*kind=sh_str(f,"kind","text");
  if(cJSON_IsTrue(sh_get(f,"required"))&&!s->values[i][0]){snprintf(s->message,sizeof(s->message),"请填写 %s",sh_str(f,"label","必填项"));cJSON_Delete(args);return;}
  int min=sh_num(f,"minLength",0),max=sh_num(f,"maxLength",SHELL_VALUE_CAP-1);
  if((int)strlen(s->values[i])<min||(int)strlen(s->values[i])>max){snprintf(s->message,sizeof(s->message),"%s需 %d 至 %d 位",sh_str(f,"label","输入"),min,max);cJSON_Delete(args);return;}
  if(!*key){snprintf(s->message,sizeof(s->message),"字段缺少名称，未提交");cJSON_Delete(args);return;}
  if(!strcmp(kind,"number")&&s->values[i][0]){char*end;strtod(s->values[i],&end);if(*end){snprintf(s->message,sizeof(s->message),"请输入有效数字");cJSON_Delete(args);return;}}
  cJSON_DeleteItemFromObjectCaseSensitive(args,key);cJSON_AddStringToObject(args,key,s->values[i]);
 }
 sh_prepare(a,args);cJSON_Delete(args);
}
static int shell_hit(struct app*a,int id){
 struct panel_shell*s=&a->shell;int i;
 if(id<2000)return 0;
 if(s->busy&&s->draft&&!strcmp(sh_str(s->draft,"action",""),"wifi.relay.connect"))return 1;
 if(id==SH_STATUS&&!s->modal&&!s->power_open){snprintf(s->message,sizeof(s->message),"%s",s->status);s->modal=3;s->status_until=0;return 1;}
 if(s->power_open){if(id==SH_CANCEL)s->power_open=0;else if(id==SH_POWER_OFF||id==SH_POWER_REBOOT){s->power_open=0;shell_power(a,id==SH_POWER_REBOOT);}return 1;}
 if(s->editor){
  int search=s->editor==2;char*v=search?s->search_edit:s->values[s->field];size_t n=strlen(v),capacity=sh_input_capacity(s);const char*keys=sh_keyboard(s);
  if(id==SH_DONE){if(search){snprintf(s->choice_search,sizeof(s->choice_search),"%s",s->search_edit);s->choice_page=0;}s->editor=0;s->reveal=0;}
  else if(id==SH_CANCEL){s->editor=0;s->reveal=0;if(search)s->choice_page=s->search_saved_page;}
  else if(id==SH_SEARCH_CLEAR&&(search||!strcmp(sh_str(cJSON_GetArrayItem(sh_get(s->draft,"fields"),s->field),"kind",""),"number")))v[0]=0;
  else if(id==SH_REVEAL)s->reveal=!s->reveal;
  else if(id==SH_ALPHA)s->key_page=0;
  else if(id==SH_DIGITS)s->key_page=1;
  else if(id==SH_SYMBOLS)s->key_page=2;
  else if(id==SH_KEYMODE)s->key_page=(s->key_page+1)%3;
  else if(id==SH_SHIFT)s->shift=!s->shift;
  else if(id==SH_DELETE&&n){n--;while(n&&(v[n]&0xC0)==0x80)n--;v[n]=0;}
  else if((id==SH_SPACE||(id>=SH_KEY&&id<SH_KEY+(int)strlen(keys)))&&n<capacity-1){char c=id==SH_SPACE?' ':keys[id-SH_KEY];if(s->shift&&c>='a'&&c<='z')c-=32;v[n]=c;v[n+1]=0;}
  return 1;
 }
 if(s->modal){
  if(s->modal==6){if(id==SH_CANCEL||id==SH_BACK)sh_close(a);return 1;}
  if(id==SH_CANCEL){if(s->modal==5){s->modal=4;sh_search_reset(s);}else sh_close(a);return 1;}
  if(s->modal==1||s->modal==5){if(id==SH_REFRESH){shell_request_refresh(a);return 1;}if(id==SH_SEARCH&&cJSON_GetArraySize(sh_choices(s))>=12){snprintf(s->search_edit,sizeof(s->search_edit),"%s",s->choice_search);s->search_saved_page=s->choice_page;s->editor=2;s->key_page=0;s->shift=0;s->reveal=0;return 1;}if(id==SH_SEARCH_CLEAR){sh_search_reset(s);return 1;}}
  if(s->modal==2&&id==SH_CONFIRM){cJSON*args=cJSON_Duplicate(s->pending_args,1);sh_issue(a,args);cJSON_Delete(args);return 1;}
  if(s->modal==4){if(id==SH_APPLY)sh_save_form(a);else if(id>=SH_FIELD&&id<SH_FIELD+SHELL_MAX_FIELDS){s->field=id-SH_FIELD;if(s->field<s->nfields){cJSON*f=cJSON_GetArrayItem(sh_get(s->draft,"fields"),s->field);const char*k=sh_str(f,"kind","text");s->key_page=(!strcmp(k,"number")||!strcmp(k,"ip"))?1:0;if(!strcmp(k,"choice")){s->modal=5;sh_search_reset(s);}else s->editor=1;s->reveal=0;}}return 1;}
  if(s->modal==5){int n=sh_choice_count(s);if(id>=SH_CHOICE&&id<SH_CHOICE+n){cJSON*c=sh_choice_at(s,id-SH_CHOICE);if(c&&!cJSON_IsFalse(sh_get(c,"enabled"))){cJSON*v=sh_get(c,"value");if(cJSON_IsString(v)&&strlen(v->valuestring)>=SHELL_VALUE_CAP){snprintf(s->message,sizeof(s->message),"选项内容过长");}else sh_value(v,s->values[s->field],SHELL_VALUE_CAP);s->modal=4;sh_search_reset(s);}}return 1;}
  if(s->modal==1){int n=sh_choice_count(s);if(id>=SH_CHOICE&&id<SH_CHOICE+n){cJSON*c=sh_choice_at(s,id-SH_CHOICE);if(c){if(cJSON_IsFalse(sh_get(c,"enabled")))return 1;else{cJSON*base=sh_get(s->draft,"args");cJSON*args=base?cJSON_Duplicate(base,1):cJSON_CreateObject();cJSON*v;cJSON_ArrayForEach(v,sh_get(c,"args")){if(v->string){cJSON_DeleteItemFromObjectCaseSensitive(args,v->string);cJSON_AddItemToObject(args,v->string,cJSON_Duplicate(v,1));}}const char*override=sh_str(c,"action","");if(*override){cJSON_DeleteItemFromObject(s->draft,"action");cJSON_AddStringToObject(s->draft,"action",override);}snprintf(s->message,sizeof(s->message),"%s",sh_str(c,"label","所选设置"));sh_prepare(a,args);cJSON_Delete(args);}}}return 1;}return 1;
 }
 if(id>=SH_TAB&&id<SH_TAB+5){s->menu_offsets[s->tab]=s->menu_page;s->tab=id-SH_TAB;s->menu_page=s->menu_offsets[s->tab];s->subpage=0;s->section[0]=0;s->item_page=0;return 1;}
 if(id==SH_REFRESH){shell_request_refresh(a);return 1;}
 if(id==SH_FACTORY){shell_factory(a);return 1;}
 if(id==SH_BACK){const char*parent=sh_str(sh_section(a,s->section),"parent","");if(*parent){sh_open_section(a,parent);return 1;}if(s->subpage){s->subpage=0;s->item_page=0;if(s->tab==2||s->tab==3)s->tab=0;}else s->tab=0;return 1;}
 if(id>=SH_SECTION&&id<SH_SECTION+30){i=id-SH_SECTION;if(s->tab==0){if(i==1){s->tab=1;sh_open_section(a,"cell");}else if(i==20||i==21){s->tab=1;sh_open_section(a,i==20?"wifi":"usb");}else if(i==22){s->tab=2;sh_open_section(a,"clash");}else if(i==23){s->tab=3;sh_open_section(a,"tailscale");}else if(i==24){s->tab=4;sh_open_section(a,"usage");}else if(i==25){s->tab=1;sh_open_section(a,"clients");}}else if(s->tab==1&&i<5)sh_open_section(a,sh_net_sections[i]);else if(s->tab==4&&i<(int)(sizeof(sh_settings_sections)/sizeof(sh_settings_sections[0])))sh_open_section(a,sh_settings_sections[i]);return 1;}
 if(!s->subpage&&(s->tab==1||s->tab==4))return 1;
 cJSON*items=sh_get(sh_section(a,s->section),"items");int n=cJSON_GetArraySize(items);
 if(id>=SH_ITEM&&id<SH_ITEM+n){cJSON*it=cJSON_GetArrayItem(items,id-SH_ITEM);if(it)sh_open_item(a,it);}
 return 1;
}
/* Contact starts on a specific hit; a drag can never become a click on release.
 * Only a gesture beginning inside the current viewport moves it. */
static int shell_pointer(struct app*a,int x,int y,int down,int released){
 struct panel_shell*s=&a->shell;
 if(down&&!s->drag_active){s->drag_active=1;s->drag_moved=0;s->drag_x=x;s->drag_y=s->drag_last_y=y;s->drag_hit=hit_find(a,x,y);s->drag_scroll=s->scroll_offset&&y>=s->scroll_top&&y<s->scroll_bottom;}
 if(!s->drag_active)return 0;
 if(abs(y-s->drag_y)>8||abs(x-s->drag_x)>8)s->drag_moved=1;
 int changed=0;
 if(s->drag_moved&&s->drag_scroll&&s->scroll_offset){int old=*s->scroll_offset;*s->scroll_offset+=s->drag_last_y-y;if(*s->scroll_offset<0)*s->scroll_offset=0;if(*s->scroll_offset>s->scroll_max)*s->scroll_offset=s->scroll_max;changed=old!=*s->scroll_offset;}
 s->drag_last_y=y;
 if(released||!down){if(!s->drag_moved&&s->drag_hit>=0&&s->drag_hit==hit_find(a,x,y)){shell_hit(a,s->drag_hit);changed=1;}s->drag_active=0;}
 return changed;
}
#endif
