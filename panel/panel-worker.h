/* One asynchronous helper request at a time. Device secrets stay in pipes/memory. */
#define WORKER_CAP (1024*1024)
struct panel_worker {pid_t pid;int fd,action;long started,timeout;char *buf;size_t used;};
static struct panel_worker pw={.fd=-1};
static long next_snapshot;
static void worker_clear(int terminate) {
 if(pw.pid>0){if(terminate){kill(-pw.pid,SIGKILL);kill(pw.pid,SIGKILL);}waitpid(pw.pid,NULL,WNOHANG);}
 if(pw.fd>=0)close(pw.fd);free(pw.buf);memset(&pw,0,sizeof(pw));pw.fd=-1;
}
static int worker_start(const cJSON *request,int action) {
 if(pw.pid)return 0;
 char *wire=cJSON_PrintUnformatted(request);if(!wire)return 0;
 if(strlen(wire)>8192){free(wire);return 0;}
 int in[2],out[2];if(pipe(in)){free(wire);return 0;}if(pipe(out)){close(in[0]);close(in[1]);free(wire);return 0;}
 pid_t p=fork();if(p<0){close(in[0]);close(in[1]);close(out[0]);close(out[1]);free(wire);return 0;}
 if(!p){setsid();dup2(in[0],0);dup2(out[1],1);int nul=open("/dev/null",O_WRONLY);if(nul>=0)dup2(nul,2);for(int fd=3;fd<1024;fd++)close(fd);execl("/data/u60-panel/panel-control","panel-control",(char*)NULL);_exit(127);}
 close(in[0]);close(out[1]);size_t n=strlen(wire),at=0;
 while(at<n){ssize_t w=write(in[1],wire+at,n-at);if(w<0&&errno==EINTR)continue;if(w<=0)break;at+=(size_t)w;}
 memset(wire,0,n);free(wire);close(in[1]);fcntl(out[0],F_SETFL,O_NONBLOCK);
 pw.pid=p;pw.fd=out[0];pw.action=action;pw.started=now_ms();const cJSON*cv=cJSON_GetObjectItemCaseSensitive(request,"action");const char*command=cJSON_IsString(cv)?cv->valuestring:"";pw.timeout=action&&!strncmp(command,"wifi.relay.",11)?220000:action&&(!strcmp(command,"wifi.power")||!strcmp(command,"wifi.ap"))?105000:action&&!strcmp(command,"network.tailscale_mode")?75000:action&&!strcmp(command,"diag.network")?60000:45000;pw.buf=malloc(WORKER_CAP+1);pw.used=0;
 if(!pw.buf||at<n){worker_clear(1);return 0;}return 1;
}
static void live_num(cJSON *o,const char*k,double value){cJSON_DeleteItemFromObjectCaseSensitive(o,k);cJSON_AddNumberToObject(o,k,value);}
static void live_str(cJSON *o,const char*k,const char*value){cJSON_DeleteItemFromObjectCaseSensitive(o,k);cJSON_AddStringToObject(o,k,value);}
static cJSON *live_data(struct app *a){
 if(!a->shell.snapshot){a->shell.snapshot=cJSON_CreateObject();cJSON_AddObjectToObject(a->shell.snapshot,"data");cJSON_AddArrayToObject(a->shell.snapshot,"sections");}
 return cJSON_GetObjectItem(a->shell.snapshot,"data");
}
static void append_screen_controls(struct app *a){
 cJSON *sections=cJSON_GetObjectItem(a->shell.snapshot,"sections"),*s,*items=NULL;
 cJSON_ArrayForEach(s,sections)if(!strcmp(jstr(s,"id"),"system"))items=cJSON_GetObjectItem(s,"items");
 if(!items){s=cJSON_CreateObject();cJSON_AddStringToObject(s,"id","system");cJSON_AddStringToObject(s,"title","电源与屏幕");items=cJSON_AddArrayToObject(s,"items");cJSON_AddItemToArray(sections,s);}
 cJSON_InsertItemInArray(items,0,panel_theme_item(sh_theme));
 cJSON *i=cJSON_CreateObject();cJSON_AddStringToObject(i,"id","brightness");cJSON_AddStringToObject(i,"label","屏幕亮度");cJSON_AddStringToObject(i,"type","choice");cJSON_AddStringToObject(i,"action","screen.brightness");cJSON_AddStringToObject(i,"value",a->bl_saved>200?"高":a->bl_saved>100?"中":"低");cJSON_AddBoolToObject(i,"enabled",1);
 cJSON *ch=cJSON_AddArrayToObject(i,"choices");for(int n=0;n<3;n++){cJSON *v=cJSON_CreateObject();cJSON_AddStringToObject(v,"label",(const char*[]){"低","中","高"}[n]);cJSON *ar=cJSON_AddObjectToObject(v,"args");cJSON_AddNumberToObject(ar,"value",(int[]){80,160,255}[n]);cJSON_AddItemToArray(ch,v);}cJSON_AddItemToArray(items,i);
 i=cJSON_CreateObject();cJSON_AddStringToObject(i,"id","blank");cJSON_AddStringToObject(i,"label","自动熄屏");cJSON_AddStringToObject(i,"type","choice");cJSON_AddStringToObject(i,"action","screen.blank");char desc[50];snprintf(desc,sizeof(desc),a->blank_sec?"%d 秒":"不自动熄屏",a->blank_sec);cJSON_AddStringToObject(i,"value",desc);cJSON_AddBoolToObject(i,"enabled",1);ch=cJSON_AddArrayToObject(i,"choices");for(int n=0;n<5;n++){cJSON *v=cJSON_CreateObject();cJSON_AddStringToObject(v,"label",(const char*[]){"不自动熄屏","15 秒","30 秒","1 分钟","5 分钟"}[n]);cJSON *ar=cJSON_AddObjectToObject(v,"args");cJSON_AddNumberToObject(ar,"seconds",(int[]){0,15,30,60,300}[n]);cJSON_AddItemToArray(ch,v);}cJSON_AddItemToArray(items,i);
 panel_menu_layout(a->shell.snapshot);
}
static void shell_request_refresh(struct app *a){(void)a;next_snapshot=0;}
static void shell_dispatch(struct app *a,const char *action,cJSON *args){
 if(a->shell.busy){snprintf(a->shell.status,sizeof(a->shell.status),"上一项操作仍在执行");return;}
 if(!strcmp(action,"screen.theme")){
  const char*name=jstr(args,"theme");int ok=panel_theme_save(name);if(ok){sh_theme=panel_theme_id(name);logline("screen theme applied=%s",name);}
  snprintf(a->shell.status,sizeof(a->shell.status),"%s",ok?"主题已切换并保存":"主题保存失败，保留原配色");a->shell.status_until=now_ms()+8000;next_snapshot=0;return;
 }
 if(!strcmp(action,"screen.brightness")){
  cJSON*v=cJSON_GetObjectItem(args,"value");int ok=cJSON_IsNumber(v)&&(v->valueint==80||v->valueint==160||v->valueint==255);
  if(ok)ok=lcd_set(v->valueint);if(ok){a->bl=a->bl_saved=v->valueint;ok=save_setting_int("brightness",v->valueint)&&load_brightness()==v->valueint;}
  snprintf(a->shell.status,sizeof(a->shell.status),"%s",ok?"亮度已保存并确认":"亮度设置或保存失败");a->shell.status_until=now_ms()+8000;next_snapshot=0;return;
 }
 if(!strcmp(action,"screen.blank")){
  cJSON*v=cJSON_GetObjectItem(args,"seconds");int ok=cJSON_IsNumber(v)&&(v->valueint==0||v->valueint==15||v->valueint==30||v->valueint==60||v->valueint==300);
  if(ok)ok=save_blank_sec(v->valueint)&&load_blank_sec()==v->valueint;if(ok)a->blank_sec=v->valueint;
  snprintf(a->shell.status,sizeof(a->shell.status),"%s",ok?"熄屏时间已保存并确认":"熄屏时间保存失败");a->shell.status_until=now_ms()+8000;next_snapshot=0;return;
 }
 if(pw.pid&&!pw.action)worker_clear(1);
 cJSON *r=cJSON_CreateObject();cJSON_AddStringToObject(r,"action",action);cJSON_AddItemToObject(r,"args",args?cJSON_Duplicate(args,1):cJSON_CreateObject());
 if(worker_start(r,1)){a->shell.busy=1;snprintf(a->shell.status,sizeof(a->shell.status),"%s",!strcmp(action,"wifi.relay.scan")?"正在扫描附近网络，请稍候…":!strcmp(action,"wifi.relay.connect")?"正在连接；热点恢复可能需要约一分钟…":"正在应用，请稍候…");}else snprintf(a->shell.status,sizeof(a->shell.status),"无法启动控制服务");a->shell.status_until=now_ms()+8000;cJSON_Delete(r);
}
static void shell_factory(struct app *a){(void)a;g_stop=1;}
static void shell_power(struct app *a,int reboot){cJSON *args=cJSON_CreateObject();cJSON_AddBoolToObject(args,"reboot",reboot);shell_dispatch(a,"system.power",args);cJSON_Delete(args);}
static int worker_poll(struct app *a){
 int changed=0;
 if(!pw.pid&&!screen_notice.pid)while(waitpid(-1,NULL,WNOHANG)>0){}
 if(pw.pid){
  int eof=0,fail=0;for(;;){ssize_t n=read(pw.fd,pw.buf+pw.used,WORKER_CAP-pw.used);if(n>0){pw.used+=(size_t)n;if(pw.used>=WORKER_CAP){fail=1;break;}}else{if(n==0)eof=1;else if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)fail=1;break;}}
  if(now_ms()-pw.started>pw.timeout)fail=1;
  if(eof||fail){pw.buf[pw.used]=0;cJSON*r=!fail?cJSON_Parse(pw.buf):NULL;
   if(pw.action){sh_action_result(a,r);next_snapshot=0;}
   else if(r&&cJSON_IsObject(cJSON_GetObjectItem(r,"data"))&&cJSON_IsArray(cJSON_GetObjectItem(r,"sections"))){
    /* The slow service snapshot does not sample the physical route. Preserve
     * fields owned by sample_local until its next tick; otherwise every refresh
     * briefly loses the relay interface and renders the cellular operator.
     * A real route change still overwrites these fields on the next local tick. */
    cJSON*old=cJSON_GetObjectItem(a->shell.snapshot,"data"),*fresh=cJSON_GetObjectItem(r,"data");
    const char*keys[]={"physical_iface","download_bps","upload_bps","cpu_percent","memory_percent","uptime_seconds"};
    for(size_t k=0;k<sizeof(keys)/sizeof(keys[0]);k++){
     const cJSON*v=cJSON_GetObjectItem(old,keys[k]);cJSON*copy=v?cJSON_Duplicate(v,1):NULL;
     if(copy){cJSON_DeleteItemFromObject(fresh,keys[k]);cJSON_AddItemToObject(fresh,keys[k],copy);}
    }
    cJSON_Delete(a->shell.snapshot);a->shell.snapshot=r;r=NULL;append_screen_controls(a);
   }
   else {snprintf(a->shell.status,sizeof(a->shell.status),"状态读取失败，保留上次结果");a->shell.status_until=now_ms()+8000;}
   cJSON_Delete(r);worker_clear(fail);changed=1;
  }
 }
 if(!a->blanked&&!a->shell.modal&&!a->shell.editor&&!a->shell.power_open&&!pw.pid&&now_ms()>=next_snapshot){cJSON*r=cJSON_CreateObject();cJSON_AddStringToObject(r,"action","state");if(!worker_start(r,0))snprintf(a->shell.status,sizeof(a->shell.status),"控制程序未就绪");cJSON_Delete(r);next_snapshot=now_ms()+(a->blanked?30000:10000);}
 return changed;
}
/* Fast local telemetry runs separately from slow ubus/API calls. */
static void sample_local(struct app*a){
 enum panel_battery_power power=battery_power_sample();
 if(power!=a->battery_power)logline("battery indicator=%s",(const char*[]){"unknown","battery","charging","output","input-idle","full"}[power]);
 a->battery_power=power;
 static unsigned long long last_idle,last_total,last_rx,last_tx;static long prev_ms;static char old_iface[64];
 cJSON*d=live_data(a);long n=now_ms();double up=0;FILE*f=fopen("/proc/uptime","r");if(f){if(fscanf(f,"%lf",&up)==1)live_num(d,"uptime_seconds",up);fclose(f);}
 f=fopen("/proc/stat","r");if(f){unsigned long long user=0,nice=0,sys=0,idle=0,wait=0,irq=0,soft=0,steal=0;if(fscanf(f,"cpu %llu %llu %llu %llu %llu %llu %llu %llu",&user,&nice,&sys,&idle,&wait,&irq,&soft,&steal)>=4){unsigned long long total=user+nice+sys+idle+wait+irq+soft+steal;if(last_total&&total>last_total)live_num(d,"cpu_percent",100.0*(1.0-(double)((idle+wait)-last_idle)/(total-last_total)));last_total=total;last_idle=idle+wait;}fclose(f);}
 f=fopen("/proc/meminfo","r");if(f){char line[160];unsigned long total=0,avail=0;while(fgets(line,sizeof(line),f)){sscanf(line,"MemTotal: %lu",&total);sscanf(line,"MemAvailable: %lu",&avail);}if(total)live_num(d,"memory_percent",100.0*(total-avail)/total);fclose(f);}
 char iface[64]="";f=fopen("/proc/net/route","r");if(f){char line[256];unsigned best=~0u;fgets(line,sizeof(line),f);while(fgets(line,sizeof(line),f)){char name[64];unsigned dest,gate,flags,metric;if(sscanf(line,"%63s %x %x %x %*u %*u %u",name,&dest,&gate,&flags,&metric)==5&&!dest&&(flags&1)&&metric<best&&strcmp(name,"tailscale0")&&strcmp(name,"lo")){strcpy(iface,name);best=metric;}}fclose(f);}
 if(*iface){unsigned long long rx=0,tx=0;char path[160];snprintf(path,sizeof(path),"/sys/class/net/%s/statistics/rx_bytes",iface);f=fopen(path,"r");int valid=f&&fscanf(f,"%llu",&rx)==1;if(f)fclose(f);snprintf(path,sizeof(path),"/sys/class/net/%s/statistics/tx_bytes",iface);f=fopen(path,"r");valid=valid&&f&&fscanf(f,"%llu",&tx)==1;if(f)fclose(f);
  live_str(d,"physical_iface",iface);if(valid&&prev_ms&&n>prev_ms&&!strcmp(iface,old_iface)&&rx>=last_rx&&tx>=last_tx){live_num(d,"download_bps",(double)(rx-last_rx)*1000/(n-prev_ms));live_num(d,"upload_bps",(double)(tx-last_tx)*1000/(n-prev_ms));}else{cJSON_DeleteItemFromObject(d,"download_bps");cJSON_DeleteItemFromObject(d,"upload_bps");}last_rx=rx;last_tx=tx;snprintf(old_iface,sizeof(old_iface),"%s",iface);prev_ms=n;
 }
 /* Clock is rendered from local time, independent of snapshot replacement. */
}
