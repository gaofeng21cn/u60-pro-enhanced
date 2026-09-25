#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <arpa/inet.h>
#include "cJSON.h"

/* One stdin request, one stdout JSON response. No shell, arbitrary executable,
 * file path, ubus method or command argument is accepted from the request. */
static cJSON *fixture;
static int fixture_write_count;
static cJSON *jget(const cJSON *o,const char *k){return cJSON_GetObjectItemCaseSensitive(o,k);}
static const char *jstr(const cJSON *o,const char *k){cJSON *v=jget(o,k);return cJSON_IsString(v)?v->valuestring:"";}
static cJSON *reply(int ok,const char *msg){cJSON *r=cJSON_CreateObject();cJSON_AddBoolToObject(r,"ok",ok);cJSON_AddStringToObject(r,"message",msg);return r;}
static long long ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static int run_cmd(const char *path,char *const argv[],const char *input,char *out,size_t cap){
 int in[2],op[2],status=0;size_t used=0,sent=0,total=input?strlen(input):0;pid_t pid;long long end=ms()+(!strcmp(path,"/data/u60-panel/panel-relay")?210000:!strcmp(path,"/data/u60-panel/wifi-relay.sh")?100000:!strcmp(path,"/data/u60-panel/wifi-band.sh")?95000:!strcmp(path,"/data/u60-panel/tailscale-mode.sh")?65000:((!strcmp(path,"/data/u60-panel/network-profile.sh")||!strcmp(path,"/data/u60-panel/tailscale-mode.sh")||!strcmp(path,"/data/u60-panel/wifi-band.sh"))?30000:8000));
 if(!cap)return 0;if(out)out[0]=0;if(pipe(in))return 0;if(pipe(op)){close(in[0]);close(in[1]);return 0;}
 pid=fork();if(pid<0){close(in[0]);close(in[1]);close(op[0]);close(op[1]);return 0;}
 if(!pid){dup2(in[0],0);dup2(op[1],1);int nul=open("/dev/null",O_WRONLY);if(nul>=0)dup2(nul,2);close(in[0]);close(in[1]);close(op[0]);close(op[1]);if(nul>2)close(nul);char *env[]={"PATH=/usr/sbin:/usr/bin:/sbin:/bin","LANG=C",NULL};execve(path,argv,env);_exit(127);}
 close(in[0]);close(op[1]);fcntl(in[1],F_SETFL,O_NONBLOCK);fcntl(op[0],F_SETFL,O_NONBLOCK);int writing=1,reading=1,done=0,bad=0;
 while(!done||reading){
  if(ms()>end){bad=1;break;}
  if(writing&&sent==total){close(in[1]);writing=0;}
  struct pollfd p[2]={{op[0],reading?POLLIN:0,0},{in[1],writing?POLLOUT:0,0}};poll(p,2,50);
  if(writing&&(p[1].revents&POLLOUT)){ssize_t n=write(in[1],input+sent,total-sent);if(n>0)sent+=(size_t)n;else if(n<0&&errno!=EAGAIN&&errno!=EINTR){bad=1;break;}}
  if(reading&&(p[0].revents&(POLLIN|POLLHUP))){char buf[4096];ssize_t n=read(op[0],buf,sizeof(buf));if(n>0){if(out){if(used+(size_t)n>=cap){bad=1;break;}memcpy(out+used,buf,(size_t)n);used+=(size_t)n;out[used]=0;}}else if(n==0)reading=0;}
  if(!done){pid_t r=waitpid(pid,&status,WNOHANG);if(r==pid)done=1;}
 }
 if(bad&&!done){kill(pid,SIGKILL);waitpid(pid,&status,0);}if(writing)close(in[1]);close(op[0]);return !bad&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
}
static cJSON *mock(const char *key){return cJSON_Duplicate(jget(fixture,key),1);}
static cJSON *ubus_call(const char *obj,const char *method,cJSON *args){
 if(fixture){char key[200];snprintf(key,sizeof(key),"%s.%s",obj,method);if(!strcmp(method,"wlan_uci_get_section"))snprintf(key,sizeof(key),"wifi.%s",jstr(args,"section"));
  if(!strcmp(obj,"zwrt_zte_sleep_faw.wakelock")&&!strcmp(method,"set_ufi_sleep")){fixture_write_count++;if(cJSON_IsTrue(jget(fixture,"reject_writes")))return NULL;if(!cJSON_IsTrue(jget(fixture,"stale_readback")))cJSON_ReplaceItemInObject(fixture,"wifi_idle_minutes",cJSON_CreateString(jstr(args,"ufiSleepTime")));return cJSON_CreateObject();}
  if(!strcmp(method,"set")){fixture_write_count++;if(cJSON_IsTrue(jget(fixture,"reject_writes")))return NULL;
   if(!cJSON_IsTrue(jget(fixture,"stale_readback"))){cJSON *p; cJSON_ArrayForEach(p,args){char k[128];snprintf(k,sizeof(k),"wifi.%s",p->string);cJSON *dest=jget(fixture,k),*v;if(dest)cJSON_ArrayForEach(v,p)cJSON_ReplaceItemInObjectCaseSensitive(dest,v->string,cJSON_Duplicate(v,1));}}
   return cJSON_CreateObject();}return mock(key);}
 if(access("/data/u60-panel/panel-ubus",X_OK)==0){cJSON *r=cJSON_CreateObject();cJSON_AddStringToObject(r,"object",obj);cJSON_AddStringToObject(r,"method",method);cJSON_AddItemToObject(r,"args",args?cJSON_Duplicate(args,1):cJSON_CreateObject());char *in=cJSON_PrintUnformatted(r),out[65536];char *v[]={"panel-ubus",NULL};int ok=run_cmd("/data/u60-panel/panel-ubus",v,in,out,sizeof(out));memset(in,0,strlen(in));free(in);cJSON_Delete(r);return ok?cJSON_Parse(out):NULL;}
 char *json=args?cJSON_PrintUnformatted(args):strdup("{}");char buf[65536];char *av[]={"ubus","-t","5","call",(char*)obj,(char*)method,json,NULL};int ok=run_cmd("/bin/ubus",av,NULL,buf,sizeof(buf));free(json);return ok?cJSON_Parse(buf):NULL;
}
static int firmware_success(const cJSON *r){if(!cJSON_IsObject(r))return 0;cJSON *v=jget(r,"error_code");if(cJSON_IsNumber(v)&&v->valuedouble!=0)return 0;if(cJSON_IsString(v)&&*v->valuestring&&strcmp(v->valuestring,"0"))return 0;v=jget(r,"result");if(cJSON_IsString(v)&&strcmp(v->valuestring,"success")&&strcmp(v->valuestring,"0"))return 0;if(cJSON_IsBool(v)&&!cJSON_IsTrue(v))return 0;if(cJSON_IsNumber(v)&&v->valuedouble!=0)return 0;return !jget(r,"error");}
static cJSON *ubus_read(const char *obj,const char *method){return ubus_call(obj,method,NULL);}
static cJSON *wifi_read(const char *section){cJSON *a=cJSON_CreateObject();cJSON_AddStringToObject(a,"section",section);cJSON *r=ubus_call("zwrt_wlan","wlan_uci_get_section",a);cJSON_Delete(a);return r;}
static cJSON *ts_api(const char *endpoint,cJSON *patch){
 if(fixture){if(patch){fixture_write_count++;if(cJSON_IsTrue(jget(fixture,"reject_writes")))return NULL;cJSON *p=jget(fixture,"ts.prefs"),*v;if(!cJSON_IsTrue(jget(fixture,"stale_readback")))cJSON_ArrayForEach(v,patch){if(!strstr(v->string,"Set")){cJSON_ReplaceItemInObjectCaseSensitive(p,v->string,cJSON_Duplicate(v,1));if(!strcmp(v->string,"WantRunning"))cJSON_ReplaceItemInObject(jget(fixture,"ts.status"),"BackendState",cJSON_CreateString(cJSON_IsTrue(v)?"Running":"Stopped"));}}return cJSON_Duplicate(p,1);}return mock(!strcmp(endpoint,"prefs")?"ts.prefs":"ts.status");}
 char url[160],out[262144];snprintf(url,sizeof(url),"http://local-tailscaled.sock/localapi/v0/%s",endpoint);char *body=patch?cJSON_PrintUnformatted(patch):NULL;
 char *av[]={"curl","--silent","--fail","--max-time","6","--unix-socket","/tmp/tailscale/tailscaled.sock","-H","Content-Type: application/json",patch?"-XPATCH":"-XGET",url,patch?"--data-binary":NULL,patch?"@-":NULL,NULL};
 int ok=run_cmd("/usr/bin/curl",av,body,out,sizeof(out));free(body);return ok?cJSON_Parse(out):NULL;
}
static int tun_ready(void){return fixture?cJSON_IsTrue(jget(fixture,"tun_ready")):access("/sys/class/net/tailscale0",F_OK)==0;}
static void copy_value(cJSON *out,const char *key,const cJSON *src,const char *src_key){cJSON *v=jget(src,src_key);if(v&&!(cJSON_IsString(v)&&!v->valuestring[0]))cJSON_AddItemToObject(out,key,cJSON_Duplicate(v,1));else cJSON_AddNullToObject(out,key);}
static void scalar_text(const cJSON *v,char *out,size_t cap){if(cJSON_IsString(v))snprintf(out,cap,"%s",v->valuestring[0]?v->valuestring:"未知");else if(cJSON_IsNumber(v))snprintf(out,cap,"%.0f",v->valuedouble);else if(cJSON_IsBool(v))snprintf(out,cap,"%s",cJSON_IsTrue(v)?"开启":"关闭");else snprintf(out,cap,"未知");}
static cJSON *section(cJSON *root,const char *id,const char *title){cJSON *s=cJSON_CreateObject();cJSON_AddStringToObject(s,"id",id);cJSON_AddStringToObject(s,"title",title);cJSON_AddArrayToObject(s,"items");cJSON_AddItemToArray(jget(root,"sections"),s);return s;}
static cJSON *item(cJSON *s,const char *id,const char *label,const char *type,const char *value,const char *action,int enabled,const char *reason){cJSON *i=cJSON_CreateObject();cJSON_AddStringToObject(i,"id",id);cJSON_AddStringToObject(i,"label",label);cJSON_AddStringToObject(i,"value",value?value:"未知");cJSON_AddStringToObject(i,"type",type);cJSON_AddStringToObject(i,"action",action?action:"");cJSON_AddBoolToObject(i,"enabled",enabled);cJSON_AddBoolToObject(i,"confirm",strcmp(type,"info")!=0);cJSON_AddStringToObject(i,"reason",reason?reason:"");cJSON_AddObjectToObject(i,"args");cJSON_AddItemToArray(jget(s,"items"),i);return i;}
static void info(cJSON *s,const char *id,const char *label,const cJSON *obj,const char *key){char v[160];scalar_text(jget(obj,key),v,sizeof(v));item(s,id,label,"info",v,NULL,0,NULL);}
static cJSON *toggle(cJSON *s,const char *id,const char *label,const char *action,int current,int known,const char *reason){cJSON *i=item(s,id,label,"toggle",known?(current?"开启":"关闭"):"未知",action,known,reason);cJSON_AddBoolToObject(jget(i,"args"),"enabled",!current);return i;}
static void field(cJSON *i,const char *key,const char *label,const char *value,const char *kind,int required){cJSON *a=jget(i,"fields");if(!a)a=cJSON_AddArrayToObject(i,"fields");cJSON *f=cJSON_CreateObject();cJSON_AddStringToObject(f,"key",key);cJSON_AddStringToObject(f,"label",label);cJSON_AddStringToObject(f,"value",!strcmp(kind,"password")?"":value);cJSON_AddStringToObject(f,"kind",kind);cJSON_AddBoolToObject(f,"required",required);cJSON_AddItemToArray(a,f);}
static void choice(cJSON *i,const char *label,const char *key,const char *value){cJSON *a=jget(i,"choices");if(!a)a=cJSON_AddArrayToObject(i,"choices");cJSON *c=cJSON_CreateObject();cJSON_AddStringToObject(c,"label",label);cJSON *args=cJSON_AddObjectToObject(c,"args");cJSON_AddStringToObject(args,key,value);cJSON_AddItemToArray(a,c);}
static int boolarg(const cJSON *a,const char *k,int *out){cJSON *v=jget(a,k);if(!cJSON_IsBool(v))return 0;*out=cJSON_IsTrue(v);return 1;}
static int stringeq(const cJSON *a,const char *k,const char *s){return cJSON_IsString(jget(a,k))&&!strcmp(jstr(a,k),s);}
#if defined(__has_include)
#if __has_include("panel-clash-control.h")
#include "panel-clash-control.h"
#define HAVE_CLASH_CONTROL 1
#endif
#endif


#if defined(__has_include)
#if __has_include("panel-advanced-control.h")
#include "panel-advanced-control.h"
#define HAVE_ADVANCED_CONTROL 1
#include "panel-diagnostics.h"
#include "panel-ledger.h"
#include "panel-charge.h"
#include "panel-power-role.h"
#include "panel-standby-control.h"
#include "panel-radio-tools.h"
#endif
#endif
static cJSON *profile_run(const char *profile){if(fixture){if(!strcmp(profile,"status")){cJSON *r=cJSON_CreateObject();cJSON_AddStringToObject(r,"profile",*jstr(fixture,"profile")?jstr(fixture,"profile"):"direct");return r;}fixture_write_count++;if(cJSON_IsTrue(jget(fixture,"profile_failure")))return reply(0,"fixture profile failure");cJSON_DeleteItemFromObject(fixture,"profile");cJSON_AddStringToObject(fixture,"profile",profile);return reply(1,"fixture profile success");}char out[4096];char *av[]={"network-profile.sh",(char*)profile,NULL};int ok=run_cmd("/data/u60-panel/network-profile.sh",av,NULL,out,sizeof(out));cJSON *r=cJSON_Parse(out);if(cJSON_IsObject(r)&&cJSON_IsBool(jget(r,"ok"))&&(!cJSON_IsTrue(jget(r,"ok"))||ok)){if(!*jstr(r,"message"))cJSON_AddStringToObject(r,"message",cJSON_IsTrue(jget(r,"ok"))?"出口状态已回读":cJSON_IsTrue(jget(r,"fail_closed"))?"出口回退失败，已阻断公网转发；本地管理仍可用":*jstr(r,"kept")?"切换失败，已恢复原出口":"出口操作失败，未确认生效，请检查网络");return r;}cJSON_Delete(r);return reply(0,"上网出口脚本执行失败；请检查当前出口状态");}
static cJSON *network_profile(const cJSON *args){const char *p=jstr(args,"profile");if(strcmp(p,"clash")&&strcmp(p,"direct")&&strcmp(p,"tailscale"))return reply(0,"无效上网出口");if(!strcmp(p,"tailscale")&&!tun_ready())return reply(0,"需要先完成 TUN 迁移并选择可用出口节点");cJSON *r=profile_run(p);int ok=cJSON_IsTrue(jget(r,"ok"));if(!ok)return r;cJSON_Delete(r);if(ok){r=profile_run("status");ok=stringeq(r,"profile",p);cJSON_Delete(r);}return reply(ok,ok?"上网出口已切换并回读确认":"出口切换未通过验收；请检查当前配置和回退状态");}

static cJSON *ts_mode_run(const char *mode){
 if(fixture){cJSON*r=reply(1,"模式已回读");int tun=cJSON_IsTrue(jget(fixture,"tun_ready"));if(strcmp(mode,"status")){fixture_write_count++;if(cJSON_IsTrue(jget(fixture,"mode_failure"))){cJSON_Delete(r);r=reply(0,"模式迁移失败，已恢复原模式");cJSON_AddBoolToObject(r,"rolled_back",1);return r;}tun=!strcmp(mode,"tun");cJSON_ReplaceItemInObject(fixture,"tun_ready",cJSON_CreateBool(tun));}cJSON_AddStringToObject(r,"mode",tun?"tun":"userspace");return r;}
 char out[4096];char*av[]={"tailscale-mode.sh",(char*)mode,NULL};int ok=run_cmd("/data/u60-panel/tailscale-mode.sh",av,NULL,out,sizeof(out));cJSON*r=cJSON_Parse(out);
 if(cJSON_IsObject(r)&&cJSON_IsBool(jget(r,"ok"))&&(!cJSON_IsTrue(jget(r,"ok"))||ok))return r;cJSON_Delete(r);return reply(0,"Tailscale 模式操作未返回有效结果，请刷新核对");
}
static cJSON *ts_lan_run(const char *command){
 if(fixture){int on=cJSON_IsTrue(jget(fixture,"lan_gateway"));if(strcmp(command,"status")){if(!strcmp(command,"on")&&!tun_ready())return reply(0,"请先开启 TUN");fixture_write_count++;on=!strcmp(command,"on");cJSON_DeleteItemFromObject(fixture,"lan_gateway");cJSON_AddBoolToObject(fixture,"lan_gateway",on);}cJSON *r=reply(1,"");cJSON_AddBoolToObject(r,"enabled",on);cJSON_AddBoolToObject(r,"active",on&&tun_ready());return r;}
 char out[4096];char *v[]={"tailscale-lan.sh",(char*)command,NULL};int ok=run_cmd("/data/u60-panel/tailscale-lan.sh",v,NULL,out,sizeof(out));cJSON*r=cJSON_Parse(out);if(r&&cJSON_IsBool(jget(r,"ok"))&&(!cJSON_IsTrue(jget(r,"ok"))||ok))return r;cJSON_Delete(r);return reply(0,"组网网关暂不可用");
}
static cJSON *ts_mode_action(const cJSON *args){
 const char*mode=jstr(args,"mode");if(strcmp(mode,"tun")&&strcmp(mode,"userspace"))return reply(0,"无效 Tailscale 转发模式");
 if(!strcmp(mode,"userspace")){cJSON *gateway=ts_lan_run("status");int enabled=cJSON_IsTrue(jget(gateway,"enabled"));cJSON_Delete(gateway);if(enabled)return reply(0,"请先关闭热点设备访问组网，再切回 userspace");}
 cJSON*r=ts_mode_run(mode);if(!cJSON_IsTrue(jget(r,"ok"))){const char *why=jstr(r,"message");const char *message=cJSON_IsTrue(jget(r,"rolled_back"))?"模式切换失败，已恢复原模式与偏好":strstr(why,"Disable")?"请先停用已选出口、发布路由和接受路由，再切回 userspace":strstr(why,"missing")||strstr(why,"unavailable")?"转发模式所需组件未就绪，未完成切换":"模式切换未完成，请刷新检查服务状态";cJSON_AddStringToObject(r,"detail",why);cJSON_ReplaceItemInObject(r,"message",cJSON_CreateString(message));return r;}cJSON_Delete(r);r=ts_mode_run("status");int ok=cJSON_IsTrue(jget(r,"ok"))&&stringeq(r,"mode",mode);cJSON_Delete(r);
 return reply(ok,ok?"转发模式已切换并回读，网络转发效果仍需实际链路验证":"模式回读不一致，请检查 Tailscale 状态");
}

static cJSON *wifi_crypto(cJSON *req){char *input=cJSON_PrintUnformatted(req),out[4096];char *av[]={"panel-wifi-crypto",NULL};int ok=run_cmd("/data/u60-panel/panel-wifi-crypto",av,input,out,sizeof(out));memset(input,0,strlen(input));free(input);cJSON *r=ok?cJSON_Parse(out):NULL;memset(out,0,sizeof(out));return r;}
static int wifi_password_verified(void){if(fixture)return cJSON_IsTrue(jget(fixture,"crypto_ready"));struct stat st;if(lstat("/data/u60-panel/wifi-password-verified",&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||(st.st_mode&0022))return 0;FILE *f=fopen("/data/u60-panel/wifi-password-verified","r");if(!f)return 0;char marker[512];size_t n=fread(marker,1,sizeof(marker)-1,f);marker[n]=0;fclose(f);cJSON *m=cJSON_Parse(marker);const char *paths[]={"/usr/bin/zte_topsw_wlan","/usr/lib/libztecrypto.so"},*keys[]={"wlan_sha256","crypto_sha256"};int ok=m!=NULL;for(int i=0;i<2&&ok;i++){const char *want=jstr(m,keys[i]);if(strlen(want)!=64){ok=0;break;}char out[256];char *av[]={"sha256sum",(char*)paths[i],NULL};const char *sha=access("/usr/bin/sha256sum",X_OK)==0?"/usr/bin/sha256sum":"/bin/sha256sum";ok=run_cmd(sha,av,NULL,out,sizeof(out))&&!strncmp(out,want,64)&&(out[64]==' '||out[64]=='\t');}cJSON_Delete(m);return ok;}
static int wifi_crypto_ready(void){if(fixture)return cJSON_IsTrue(jget(fixture,"crypto_ready"));if(!wifi_password_verified()||access("/data/u60-panel/panel-ubus",X_OK)||access("/data/u60-panel/panel-wifi-crypto",X_OK))return 0;cJSON *q=cJSON_CreateObject();cJSON_AddStringToObject(q,"action","selftest");cJSON *r=wifi_crypto(q);int ok=cJSON_IsTrue(jget(r,"ok"));cJSON_Delete(q);cJSON_Delete(r);return ok;}
static int uci_read_value(const char *key,char *out,size_t cap){char *av[]={"uci","-q","get",(char*)key,NULL};int ok=run_cmd("/sbin/uci",av,NULL,out,cap);if(ok){size_t n=strlen(out);while(n&&(out[n-1]=='\n'||out[n-1]=='\r'))out[--n]=0;}return ok;}
/* Explicit screen request only: secrets never enter routine state snapshots. */
static cJSON *wifi_show_password(const cJSON *args){
 const char *ap=jstr(args,"section");if(strcmp(ap,"main_2g")&&strcmp(ap,"main_5g"))return reply(0,"无效 Wi-Fi 接口");
 cJSON *a=wifi_read(ap),*decoded=NULL;const char *security=jstr(a,"encryption");
 if(!strcmp(security,"none")){cJSON_Delete(a);return reply(1,"此热点未设置密码");}
 if(fixture)decoded=mock("wifi_password_read");else{
  char key[2048]={0},path[128],iface[32]={0},line[4096];int confirmed=0;
  snprintf(path,sizeof(path),"wireless.%s.key",ap);int got=uci_read_value(path,key,sizeof(key));
  snprintf(path,sizeof(path),"wireless.%s.ifname",ap);uci_read_value(path,iface,sizeof(iface));
  /* This firmware stores the operational key in plaintext. Compare with the
   * original hostapd config before presenting it; never guess ciphertext. */
  if(got&&strlen(key)>=8&&strlen(key)<=63&&(!strcmp(iface,"wlan0")||!strcmp(iface,"wlan2"))){
   snprintf(path,sizeof(path),"/data/vendor/wifi/hostapd-%s.conf",iface);FILE *f=fopen(path,"r");
   if(f){while(fgets(line,sizeof(line),f)){size_t n=strlen(line);while(n&&(line[n-1]=='\n'||line[n-1]=='\r'))line[--n]=0;if(!strncmp(line,"wpa_passphrase=",15)&&!strcmp(line+15,key))confirmed=1;}fclose(f);}
  }
  if(confirmed){decoded=reply(1,"");cJSON_AddStringToObject(decoded,"password",key);}memset(key,0,sizeof(key));memset(line,0,sizeof(line));

 }
 if(!cJSON_IsTrue(jget(decoded,"ok"))||!*jstr(decoded,"password")){cJSON_Delete(a);cJSON_Delete(decoded);return reply(0,"暂时无法读取当前密码，未修改 Wi-Fi");}
 cJSON *r=reply(1,"当前 Wi-Fi 密码"),*report=cJSON_AddObjectToObject(r,"report");cJSON_AddStringToObject(report,"title",!strcmp(ap,"main_2g")?"2.4 GHz 密码":"5 GHz 密码");cJSON *lines=cJSON_AddArrayToObject(report,"lines");
 cJSON_AddItemToArray(lines,cJSON_CreateString(jstr(a,"ssid")));cJSON_AddItemToArray(lines,cJSON_CreateString("密码（区分大小写）："));cJSON_AddItemToArray(lines,cJSON_CreateString(jstr(decoded,"password")));
 cJSON_Delete(a);cJSON_Delete(decoded);return r;
}
static cJSON *wifi_password(const cJSON *args){const char *ap=jstr(args,"section"),*pass=jstr(args,"password");if(strcmp(ap,"main_2g")&&strcmp(ap,"main_5g"))return reply(0,"无效 Wi-Fi 接口");size_t len=strlen(pass);if(len<8||len>63)return reply(0,"密码必须是 8–63 位可打印 ASCII 字符");for(size_t n=0;n<len;n++)if((unsigned char)pass[n]<32||(unsigned char)pass[n]>126)return reply(0,"密码必须是 8–63 位可打印 ASCII 字符");if(!wifi_crypto_ready())return reply(0,"该固件密码写入尚未验收或安全加密通道不可用，未修改密码");
 cJSON *apstate=wifi_read(ap);const char *security=jstr(apstate,"encryption");int secured=!strcmp(security,"sae")||!strcmp(security,"sae-mixed")||!strcmp(security,"psk2+ccmp")||!strcmp(security,"psk-mixed+tkip+ccmp");cJSON_Delete(apstate);if(!secured)return reply(0,"当前认证模式不支持该密码表单，未修改密码");
 int type=0;char val[2048]={0};if(!fixture&&uci_read_value("wireless.zte_mbb.wifi_key_encryption_type",val,sizeof(val))&&*val){if(!strcmp(val,"private"))type=1;else if(strcmp(val,"common"))return reply(0,"未知固件密码加密类型，未修改密码");}
 cJSON *q=cJSON_CreateObject();cJSON_AddStringToObject(q,"action","encode");cJSON_AddStringToObject(q,"password",pass);cJSON_AddNumberToObject(q,"type",type);cJSON *encrypted=fixture?cJSON_Parse("{\"ok\":true,\"key\":\"fixture-encrypted-key\"}"):wifi_crypto(q);cJSON_Delete(q);if(!cJSON_IsTrue(jget(encrypted,"ok"))||!*jstr(encrypted,"key")){cJSON_Delete(encrypted);return reply(0,"原厂加密验证失败，未修改密码");}
 cJSON *body=cJSON_CreateObject(),*entry=cJSON_AddObjectToObject(body,ap);cJSON_AddStringToObject(entry,"key",jstr(encrypted,"key"));cJSON_AddStringToObject(body,"source_module","web");cJSON *r=ubus_call("zwrt_wlan","set",body);cJSON_Delete(body);cJSON_Delete(encrypted);if(!firmware_success(r)){cJSON_Delete(r);return reply(0,"原厂接口未接受密码修改");}cJSON_Delete(r);int verified=0;
 if(fixture)verified=!cJSON_IsTrue(jget(fixture,"stale_readback"));else{char keypath[80];snprintf(keypath,sizeof(keypath),"wireless.%s.key",ap);for(int n=0;n<6;n++){memset(val,0,sizeof(val));if(uci_read_value(keypath,val,sizeof(val))){q=cJSON_CreateObject();cJSON_AddStringToObject(q,"action","verify");cJSON_AddStringToObject(q,"password",pass);cJSON_AddNumberToObject(q,"type",type);cJSON_AddStringToObject(q,"key",val);r=wifi_crypto(q);verified=cJSON_IsTrue(jget(r,"ok"));cJSON_Delete(q);cJSON_Delete(r);}if(verified)break;struct timespec pause={0,200000000};nanosleep(&pause,NULL);}}
 memset(val,0,sizeof(val));return reply(verified,verified?"密码已写入，并在内存中解密回读确认；请使用新密码连接":"密码修改已请求，但解密回读未确认；请刷新检查");}
/* Firmware web UI uses this dedicated timer API, not zwrt_wlan.set/reload.
 * -1 is Always on. It keeps hotspot idle policy separate from LCD blanking. */
static int wifi_sleep_valid(const char *v){const char*allowed[]={"-1","5","10","20","30","60","120"};for(size_t n=0;n<sizeof(allowed)/sizeof(allowed[0]);n++)if(!strcmp(v,allowed[n]))return 1;return 0;}
static int wifi_sleep_read(char*out,size_t cap){
 if(fixture)snprintf(out,cap,"%s",jstr(fixture,"wifi_idle_minutes"));
 else if(!uci_read_value("zwrt_sleep.ztmp_time.SysIdTime",out,cap))return 0;
 return wifi_sleep_valid(out);
}
static cJSON *wifi_sleep_action(const cJSON *args){
 const char*minutes=jstr(args,"minutes");char before[24],after[24];
 if(!wifi_sleep_valid(minutes))return reply(0,"请选择永不休眠或设备支持的待机时间");
 if(!wifi_sleep_read(before,sizeof(before)))return reply(0,"无法确认当前热点待机设置，未修改");
 if(!strcmp(before,minutes))return reply(1,!strcmp(minutes,"-1")?"热点已设为永不休眠，屏幕仍可自动熄灭":"热点待机时间未变化");
 cJSON*args_fw=cJSON_CreateObject();cJSON_AddStringToObject(args_fw,"ufiSleepTime",minutes);
 cJSON*r=ubus_call("zwrt_zte_sleep_faw.wakelock","set_ufi_sleep",args_fw);cJSON_Delete(args_fw);int ok=firmware_success(r);cJSON_Delete(r);
 if(!ok)return reply(0,"原厂待机接口未确认，请刷新核对");
 for(int n=0;n<6;n++){if(wifi_sleep_read(after,sizeof(after))&&!strcmp(after,minutes))return reply(1,!strcmp(minutes,"-1")?"热点已设为永不休眠，屏幕仍可自动熄灭":"热点待机时间已保存并回读确认");if(!fixture){struct timespec t={0,200000000};nanosleep(&t,NULL);}}
 return reply(0,"待机设置已请求但回读不一致，请刷新核对");
}
static void wifi_sleep_item(cJSON*s,cJSON*wd){
 char minutes[24],label[64];int known=wifi_sleep_read(minutes,sizeof(minutes));
 if(known){cJSON_AddStringToObject(wd,"idle_minutes",minutes);if(!strcmp(minutes,"-1"))snprintf(label,sizeof(label),"永不休眠");else snprintf(label,sizeof(label),"空闲 %s 分钟后休眠",minutes);}else{cJSON_AddNullToObject(wd,"idle_minutes");snprintf(label,sizeof(label),"未确认");}
 cJSON*i=item(s,"sleep","Wi-Fi 自动休眠","choice",label,"wifi.sleep",known,"仅调整热点空闲休眠；屏幕熄灭时间独立设置");
 choice(i,"永不休眠（保持热点在线）","minutes","-1");const char*v[]={"5","10","20","30","60","120"};for(size_t n=0;n<6;n++){snprintf(label,sizeof(label),"空闲 %s 分钟后休眠",v[n]);choice(i,label,"minutes",v[n]);}
}
static cJSON *wifi_band(const char *command){if(fixture){if(!strcmp(command,"status"))return mock("wifi_runtime");fixture_write_count++;cJSON*r=mock("wifi_helper_reply");if(!r)r=reply(1,"fixture runtime confirmed");cJSON_AddStringToObject(r,"fixture_command",command);return r;}char out[2048];char *v[]={"wifi-band.sh",(char*)command,NULL};int ok=run_cmd("/data/u60-panel/wifi-band.sh",v,NULL,out,sizeof(out));cJSON*r=cJSON_Parse(out);if(r&&cJSON_IsBool(jget(r,"ok"))&&(!cJSON_IsTrue(jget(r,"ok"))||ok))return r;cJSON_Delete(r);return reply(0,"无线开关尚未完成，请稍后查看实际热点状态");}
#include "panel-relay-control.h"
static cJSON *wifi_action(const char *action,const cJSON *args){
 if(!strncmp(action,"wifi.relay.",11))return relay_action(action,args);
 if(relay_enabled()&&strcmp(action,"wifi.show_password")&&strcmp(action,"wifi.sleep")&& !(!strcmp(action,"wifi.ap")&&!strcmp(jstr(args,"section"),"main_2g"))){
  int off=0;if(!strcmp(action,"wifi.power")&&boolarg(args,"enabled",&off)&&!off){cJSON*r=relay_run("off",NULL);int ok=cJSON_IsTrue(jget(r,"ok"));cJSON_Delete(r);if(!ok)return reply(0,"中继尚未退出，未关闭热点");}
  else return reply(0,"请先断开Wi-Fi中继，再修改热点设置");
 }
 if(!strcmp(action,"wifi.show_password"))return wifi_show_password(args);
 if(!strcmp(action,"wifi.sleep"))return wifi_sleep_action(args);
 if((!fixture||jget(fixture,"wifi_runtime"))&&(!strcmp(action,"wifi.power")||!strcmp(action,"wifi.ap"))){
  int on;if(!boolarg(args,"enabled",&on))return reply(0,"enabled 必须是布尔值");
  if(!strcmp(action,"wifi.power"))return wifi_band(on?"power-on":"power-off");
  const char*band=jstr(args,"section");if(!strcmp(band,"main_2g"))return wifi_band(on?"on":"off");if(!strcmp(band,"main_5g"))return wifi_band(on?"on-5g":"off-5g");return reply(0,"无效的 Wi-Fi 频段");
 }
 cJSON *ws=ubus_read("zwrt_wlan","status");
 int known=cJSON_IsNumber(jget(ws,"pending"))&&*jstr(ws,"app_status")&&*jstr(ws,"driver_status");
 int busy=known&&(jget(ws,"pending")->valuedouble!=0||strcmp(jstr(ws,"app_status"),"idle")||strcmp(jstr(ws,"driver_status"),"idle"));
 cJSON_Delete(ws);
 if(!known)return reply(0,"无法确认无线服务状态，未修改；请稍后重试");
 if(busy)return reply(0,"无线正在重配，请等待热点恢复后再操作");
 if(!strcmp(action,"wifi.password"))return wifi_password(args);
 const char *sec=jstr(args,"section"),*key=NULL,*value=NULL;int enabled=0;cJSON *before=NULL,*change=cJSON_CreateObject(),*body=cJSON_CreateObject();
 if(!strcmp(action,"wifi.power")){sec="zte_mbb";key="wifi_onoff";if(!boolarg(args,"enabled",&enabled))goto invalid;value=enabled?"1":"0";before=wifi_read(sec);if(!strcmp(jstr(before,"wifi_onoff"),""))goto unavailable;/* preserve band steering on power-up */if(enabled&&jget(before,"lbd"))cJSON_AddItemToObject(change,"lbd",cJSON_Duplicate(jget(before,"lbd"),1));}
 else if(!strcmp(action,"wifi.ap")){if(strcmp(sec,"main_2g")&&strcmp(sec,"main_5g"))goto invalid;key="disabled";if(!boolarg(args,"enabled",&enabled))goto invalid;value=enabled?"0":"1";before=wifi_read(sec);if(!jget(before,key))goto unavailable;}
 else if(!strcmp(action,"wifi.ssid")){if(strcmp(sec,"main_2g")&&strcmp(sec,"main_5g"))goto invalid;key="ssid";value=jstr(args,"ssid");size_t len=strlen(value);if(!len||len>32)goto invalid;for(size_t i=0;i<len;i++)if((unsigned char)value[i]<32||(unsigned char)value[i]==127)goto invalid;before=wifi_read(sec);if(!jget(before,key))goto unavailable;}
 else if(!strcmp(action,"wifi.channel")){if(strcmp(sec,"wifi0")&&strcmp(sec,"wifi1"))goto invalid;key="channel";value=jstr(args,key);before=wifi_read(sec);if(!jget(before,key))goto unavailable;char list[512];snprintf(list,sizeof(list),",%s,",jstr(before,"channellist"));char needle[32];if(!*value||strlen(value)>4)goto invalid;for(const char *p=value;*p;p++)if(*p<'0'||*p>'9')goto invalid;snprintf(needle,sizeof(needle),",%s,",value);if(strcmp(value,"0")&&!strstr(list,needle))goto invalid;}
 else {cJSON_Delete(change);cJSON_Delete(body);return reply(0,"该 Wi-Fi 操作尚未建立安全的原厂接口映射");}
 cJSON_AddStringToObject(change,key,value);cJSON_AddItemToObject(body,sec,change);change=NULL;cJSON_AddStringToObject(body,"source_module","web");cJSON *result=ubus_call("zwrt_wlan","set",body);cJSON_Delete(body);cJSON_Delete(before);if(!firmware_success(result)){cJSON_Delete(result);return reply(0,"原厂 Wi-Fi 设置调用失败，未确认生效");}cJSON_Delete(result);
 for(int n=0;n<6;n++){cJSON *after=wifi_read(sec);int ok=stringeq(after,key,value);cJSON_Delete(after);if(ok)return reply(1,"已保存并回读确认；Wi-Fi 可能短暂重连");if(!fixture){struct timespec t={0,200000000};nanosleep(&t,NULL);}}
 return reply(0,"设置已请求，但回读不一致；请刷新检查，未确认成功");
invalid:cJSON_Delete(before);cJSON_Delete(change);cJSON_Delete(body);return reply(0,"参数无效：请检查开关、SSID（1–32 字节）或设备允许的信道");
unavailable:cJSON_Delete(before);cJSON_Delete(change);cJSON_Delete(body);return reply(0,"无法读取原厂当前配置，已拒绝修改");
}
static int arr_has(const cJSON *a,const char *v){cJSON *p;cJSON_ArrayForEach(p,a)if(cJSON_IsString(p)&&!strcmp(p->valuestring,v))return 1;return 0;}
static int lan_cidr(char *out,size_t cap){cJSON *r=ubus_read("zwrt_router.api","router_get_dhcp_router");struct in_addr ip,mask;int ok=inet_pton(AF_INET,jstr(r,"lan_addr"),&ip)==1&&inet_pton(AF_INET,jstr(r,"lan_netmask"),&mask)==1;if(ok){uint32_t m=ntohl(mask.s_addr);int prefix=0;while(m&0x80000000U){prefix++;m<<=1;}if(m||prefix<8||prefix>30)ok=0;else{ip.s_addr&=mask.s_addr;char ipstr[32];inet_ntop(AF_INET,&ip,ipstr,sizeof(ipstr));snprintf(out,cap,"%s/%d",ipstr,prefix);}}cJSON_Delete(r);return ok;}
/* A bounded peer test, never a generic host/command execution endpoint. */
static cJSON *ts_peer_test(const cJSON *args){
 const char *ip=jstr(args,"ip");struct in_addr address;if(inet_pton(AF_INET,ip,&address)!=1||(ntohl(address.s_addr)&0xffc00000U)!=0x64400000U)return reply(0,"请选择当前在线的组网设备");
 cJSON *st=ts_api("status",NULL),*peer;int found=0;char name[160]={0};
 cJSON_ArrayForEach(peer,jget(st,"Peer"))if(cJSON_IsTrue(jget(peer,"Online"))&&arr_has(jget(peer,"TailscaleIPs"),ip)){found=1;snprintf(name,sizeof(name),"%s",jstr(peer,"HostName"));break;}
 cJSON_Delete(st);if(!found)return reply(0,"设备已离线或已不在当前组网中，请刷新重试");
 char tunnel[2048]={0},packets[2048]={0};int tunnel_ok=0,ip_ok=0;
 if(fixture){tunnel_ok=cJSON_IsTrue(jget(fixture,"peer_tunnel_ok"));ip_ok=tun_ready()&&cJSON_IsTrue(jget(fixture,"peer_ip_ok"));}
 else{
  char *argv[]={"tailscale","--socket=/tmp/tailscale/tailscaled.sock","ping","--c=1","--timeout=4s","--until-direct=false",(char*)ip,NULL};
  tunnel_ok=run_cmd("/data/tailscale/bin/tailscale",argv,NULL,tunnel,sizeof(tunnel))&&strstr(tunnel,"pong from ")!=NULL;
  if(tun_ready()){char *ping[]={"ping","-I","tailscale0","-c","1","-W","3",(char*)ip,NULL};ip_ok=run_cmd("/bin/ping",ping,NULL,packets,sizeof(packets));}
 }
 cJSON *r=reply(ip_ok,"组网测试完成"),*report=cJSON_AddObjectToObject(r,"report");cJSON_AddStringToObject(report,"title","设备连通测试");cJSON *lines=cJSON_AddArrayToObject(report,"lines");
 cJSON_AddItemToArray(lines,cJSON_CreateString(name));cJSON_AddItemToArray(lines,cJSON_CreateString(ip));
 cJSON_AddItemToArray(lines,cJSON_CreateString(tunnel_ok?"组网隧道：已连通":"组网隧道：未通过"));
 cJSON_AddItemToArray(lines,cJSON_CreateString(!tun_ready()?"系统 IP：需开启 TUN":ip_ok?"系统 IP：已连通":"系统 IP：未通过（检查对端防火墙）"));
 if(tunnel_ok&&!fixture)cJSON_AddItemToArray(lines,cJSON_CreateString(strstr(tunnel,"via DERP(")?"连接方式：中继":"连接方式：点对点"));
 cJSON_AddItemToArray(lines,cJSON_CreateString("具体网页或服务仍需按端口访问验证。"));
 cJSON_AddBoolToObject(r,"tunnel_ok",tunnel_ok);cJSON_AddBoolToObject(r,"ip_ok",ip_ok);return r;
}
static cJSON *ts_action(const char *action,const cJSON *args){
 if(!strcmp(action,"tailscale.peer_test"))return ts_peer_test(args);
 if(!strcmp(action,"tailscale.lan_gateway")){int enabled;if(!boolarg(args,"enabled",&enabled))return reply(0,"enabled 必须为布尔值");cJSON *r=ts_lan_run(enabled?"on":"off");cJSON_ReplaceItemInObject(r,"message",cJSON_CreateString(cJSON_IsTrue(jget(r,"ok"))?(enabled?"热点设备可通过 U60 访问组网 IPv4 地址":"热点组网共享已关闭"):"组网共享未成功，请检查 TUN 与局域网状态"));return r;}
 cJSON *prefs=ts_api("prefs",NULL),*patch=cJSON_CreateObject();const char *key=NULL;int value=0;
 if(!prefs){cJSON_Delete(patch);return reply(0,"无法读取 Tailscale 当前偏好，已拒绝修改");}
 if(!strcmp(action,"tailscale.connected"))key="WantRunning";
 else if(!strcmp(action,"tailscale.accept_routes"))key="RouteAll";
 else if(!strcmp(action,"tailscale.accept_dns"))key="CorpDNS";
 else if(!strcmp(action,"tailscale.allow_lan"))key="ExitNodeAllowLANAccess";
 if(key){if(!boolarg(args,"enabled",&value))goto invalid;if(!strcmp(key,"RouteAll")&&value&&!tun_ready())goto migration;cJSON_AddBoolToObject(patch,key,value);char setkey[80];snprintf(setkey,sizeof(setkey),"%sSet",key);cJSON_AddBoolToObject(patch,setkey,1);}
 else if(!strcmp(action,"tailscale.exit")){
  const char *id=jstr(args,"id");if(!cJSON_IsString(jget(args,"id")))goto invalid;if(*id&&!tun_ready())goto migration;
  if(*id){cJSON *s=ts_api("status",NULL),*peer;int found=0;cJSON_ArrayForEach(peer,jget(s,"Peer"))if(stringeq(peer,"ID",id)&&cJSON_IsTrue(jget(peer,"ExitNodeOption"))&&cJSON_IsTrue(jget(peer,"Online")))found=1;cJSON_Delete(s);if(!found)goto invalid;}
  cJSON_AddStringToObject(patch,"ExitNodeID",id);cJSON_AddStringToObject(patch,"ExitNodeIP","");cJSON_AddBoolToObject(patch,"ExitNodeIDSet",1);cJSON_AddBoolToObject(patch,"ExitNodeIPSet",1);
 }else if(!strcmp(action,"tailscale.advertise_lan")||!strcmp(action,"tailscale.offer_exit")){
  if(!boolarg(args,"enabled",&value))goto invalid;if(value&&!tun_ready())goto migration;
  char cidr[64];int lan=!strcmp(action,"tailscale.advertise_lan");if(lan&&!lan_cidr(cidr,sizeof(cidr)))goto invalid;
  cJSON *routes=cJSON_CreateArray(),*v;cJSON_ArrayForEach(v,jget(prefs,"AdvertiseRoutes")){if(!cJSON_IsString(v))continue;int target=lan?!strcmp(v->valuestring,cidr):(!strcmp(v->valuestring,"0.0.0.0/0")||!strcmp(v->valuestring,"::/0"));if(!target&&!arr_has(routes,v->valuestring))cJSON_AddItemToArray(routes,cJSON_Duplicate(v,1));}
  if(value){cJSON_AddItemToArray(routes,cJSON_CreateString(lan?cidr:"0.0.0.0/0"));if(!lan)cJSON_AddItemToArray(routes,cJSON_CreateString("::/0"));}cJSON_AddItemToObject(patch,"AdvertiseRoutes",routes);cJSON_AddBoolToObject(patch,"AdvertiseRoutesSet",1);
 }else goto invalid;
 cJSON_Delete(prefs);prefs=NULL;cJSON *result=ts_api("prefs",patch);if(!result){cJSON_Delete(patch);return reply(0,"Tailscale 拒绝偏好修改，未确认生效");}cJSON_Delete(result);
 for(int n=0;n<6;n++){cJSON *after=ts_api("prefs",NULL),*v;int ok=after!=NULL;cJSON_ArrayForEach(v,patch){if(strstr(v->string,"Set"))continue;cJSON *got=jget(after,v->string);if(cJSON_IsArray(v)){if(cJSON_GetArraySize(v)!=cJSON_GetArraySize(got))ok=0;cJSON *r;cJSON_ArrayForEach(r,v)if(!arr_has(got,r->valuestring))ok=0;}else if(!cJSON_Compare(got,v,1))ok=0;}cJSON_Delete(after);
  if(ok&&key&&!strcmp(key,"WantRunning")){cJSON *s=ts_api("status",NULL);ok=stringeq(s,"BackendState",value?"Running":"Stopped");cJSON_Delete(s);}if(ok){cJSON_Delete(patch);return reply(1,"Tailscale 偏好已保存并回读确认；路由发布仍受管理端审批约束");}if(!fixture){struct timespec t={0,200000000};nanosleep(&t,NULL);}}
 cJSON_Delete(patch);return reply(0,"修改已请求但回读未确认，连接可能仍在切换");
invalid:cJSON_Delete(prefs);cJSON_Delete(patch);return reply(0,"参数无效、出口不在线或局域网配置未知");
migration:cJSON_Delete(prefs);cJSON_Delete(patch);return reply(0,"当前为 userspace 模式；需先完成 TUN 与转发链路迁移验收");
}
static cJSON *ts_exit_transaction(const cJSON *args){
 cJSON *pr=profile_run("status");char previous[24];snprintf(previous,sizeof(previous),"%s",jstr(pr,"profile"));cJSON_Delete(pr);if(strcmp(previous,"clash")&&strcmp(previous,"direct")&&strcmp(previous,"tailscale"))return reply(0,"无法确认当前上网出口，未修改");
 cJSON *old=ts_api("prefs",NULL);if(!old)return reply(0,"无法读取当前出口配置，未修改");cJSON *restore=cJSON_CreateObject();copy_value(restore,"ExitNodeID",old,"ExitNodeID");copy_value(restore,"ExitNodeIP",old,"ExitNodeIP");cJSON_AddBoolToObject(restore,"ExitNodeIDSet",1);cJSON_AddBoolToObject(restore,"ExitNodeIPSet",1);cJSON_Delete(old);
 cJSON *r=ts_action("tailscale.exit",args);if(!cJSON_IsTrue(jget(r,"ok"))){cJSON_Delete(restore);return r;}cJSON_Delete(r);const char *target=*jstr(args,"id")?"tailscale":!strcmp(previous,"tailscale")?"direct":previous;r=profile_run(target);int ok=cJSON_IsTrue(jget(r,"ok"));cJSON_Delete(r);if(ok){r=profile_run("status");ok=stringeq(r,"profile",target);cJSON_Delete(r);}if(ok){cJSON_Delete(restore);r=reply(1,"Tailscale 出口与上网模式已协调切换并回读确认");cJSON_AddStringToObject(r,"profile",target);return r;}
 r=ts_api("prefs",restore);int restored=r!=NULL;cJSON_Delete(r);r=ts_api("prefs",NULL);restored=restored&&cJSON_Compare(jget(r,"ExitNodeID"),jget(restore,"ExitNodeID"),1)&&cJSON_Compare(jget(r,"ExitNodeIP"),jget(restore,"ExitNodeIP"),1);cJSON_Delete(r);cJSON_Delete(restore);return reply(0,restored?"上网模式切换失败，已恢复原 Tailscale 出口偏好":"上网模式切换失败，出口偏好回退未确认；请立即检查网络");}
static void wifi_sections(cJSON *root){
 cJSON *s=section(root,"wifi","Wi-Fi"),*global=wifi_read("zte_mbb"),*data=jget(root,"data"),*wd=cJSON_AddObjectToObject(data,"wifi");const char *power=jstr(global,"wifi_onoff");int known=!strcmp(power,"0")||!strcmp(power,"1");toggle(s,"power","Wi-Fi 总开关","wifi.power",!strcmp(power,"1"),known,known?NULL:"原厂接口未返回开关状态");copy_value(wd,"enabled",global,"wifi_onoff");cJSON_AddStringToObject(data,"wifi_status",known?(!strcmp(power,"1")?"开启":"关闭"):"未知");cJSON_Delete(global);
 wifi_sleep_item(s,wd);
 int crypto_ready=wifi_crypto_ready();for(int b=0;b<2;b++){const char *ap=b?"main_5g":"main_2g",*radio=b?"wifi1":"wifi0",*name=b?"5 GHz":"2.4 GHz";cJSON *a=wifi_read(ap),*r=wifi_read(radio);char label[96],id[64];snprintf(label,sizeof(label),"%s 开关",name);snprintf(id,sizeof(id),"%s.enabled",ap);cJSON *i=toggle(s,id,label,"wifi.ap",!strcmp(jstr(a,"disabled"),"0"),!strcmp(jstr(a,"disabled"),"0")||!strcmp(jstr(a,"disabled"),"1"),"应用时两个频段可能短暂重连，请等待热点恢复");cJSON_AddStringToObject(jget(i,"args"),"section",ap);
 snprintf(label,sizeof(label),"%s 名称",name);snprintf(id,sizeof(id),"%s.ssid",ap);i=item(s,id,label,"form",jstr(a,"ssid"),"wifi.ssid",*jstr(a,"ssid")!=0,"修改后需要重新连接 Wi-Fi");cJSON_AddStringToObject(jget(i,"args"),"section",ap);field(i,"ssid","Wi-Fi 名称",jstr(a,"ssid"),"text",1);
 snprintf(label,sizeof(label),"%s 密码",name);snprintf(id,sizeof(id),"%s.password",ap);i=item(s,id,label,"action","点击查看当前密码","wifi.show_password",1,NULL);cJSON_AddStringToObject(jget(i,"args"),"section",ap);
 if(crypto_ready){snprintf(label,sizeof(label),"修改 %s 密码",name);snprintf(id,sizeof(id),"%s.password_edit",ap);i=item(s,id,label,"form","设置新密码","wifi.password",1,"8–63 位 ASCII");cJSON_AddStringToObject(jget(i,"args"),"section",ap);field(i,"password","新密码","","password",1);}

 snprintf(label,sizeof(label),"%s 信道",name);snprintf(id,sizeof(id),"%s.channel",radio);i=item(s,id,label,"choice",jstr(r,"channel"),"wifi.channel",*jstr(r,"channel")!=0,NULL);cJSON_AddStringToObject(jget(i,"args"),"section",radio);choice(i,"自动","channel","0");char *channels=strdup(jstr(r,"channellist")),*save=NULL;for(char *t=strtok_r(channels,",",&save);t;t=strtok_r(NULL,",",&save))choice(i,t,"channel",t);free(channels);
 cJSON *safe=cJSON_AddObjectToObject(wd,ap);copy_value(safe,"ssid",a,"ssid");copy_value(safe,"disabled",a,"disabled");copy_value(safe,"security",a,"encryption");copy_value(safe,"channel",r,"channel");cJSON_Delete(a);cJSON_Delete(r);
 }
 /* Keep both band switches on the first page, before detailed band settings. */
 cJSON *items=jget(s,"items");
 if(!fixture||jget(fixture,"wifi_runtime")){
  cJSON*runtime=wifi_band("status"),*entry;int can=cJSON_IsTrue(jget(runtime,"available")),on2=cJSON_IsTrue(jget(runtime,"enabled")),on5=cJSON_IsTrue(jget(runtime,"enabled_5g")),active=on2||on5;
  int power_on=cJSON_IsTrue(jget(runtime,"power_configured")),busy=cJSON_IsTrue(jget(runtime,"busy"));
  const char*summary=!can?"状态未知":busy?"切换中":on2&&on5?"双频开启":on2?"仅 2.4G":on5?"仅 5G":power_on?"休眠 / 未运行":"关闭";
  cJSON_ReplaceItemInObject(data,"wifi_status",cJSON_CreateString(summary));
  copy_value(wd,"configured_enabled",wd,"enabled");cJSON_ReplaceItemInObject(wd,"enabled",can?cJSON_CreateString(active?"1":"0"):cJSON_CreateNull());copy_value(wd,"busy",runtime,"busy");
  cJSON_ArrayForEach(entry,items){
   const char*id=jstr(entry,"id");int b=!strcmp(id,"main_2g.enabled")?0:!strcmp(id,"main_5g.enabled")?1:-1;
   if(!strcmp(id,"power")){
    cJSON_ReplaceItemInObject(entry,"value",cJSON_CreateString(!can?"未知":active?"开启":power_on?"未运行 · 点击恢复":"关闭"));cJSON_ReplaceItemInObject(entry,"enabled",cJSON_CreateBool(can));cJSON_ReplaceItemInObject(jget(entry,"args"),"enabled",cJSON_CreateBool(!active));cJSON_ReplaceItemInObject(entry,"reason",cJSON_CreateString("等待实际热点完成启停；未运行时点击可恢复无线服务"));
   }else if(b>=0){
    int on=b?on5:on2;const char*state=jstr(runtime,b?"state_5g":"state_2g");
    cJSON_ReplaceItemInObject(entry,"value",cJSON_CreateString(!can?"未知":on?"开启":!strcmp(state,"DISABLED")?"关闭":*state&&strcmp(state,"MISSING")?"正在启动":"未运行 · 点击开启"));
    cJSON_ReplaceItemInObject(entry,"enabled",cJSON_CreateBool(can));cJSON_ReplaceItemInObject(jget(entry,"args"),"enabled",cJSON_CreateBool(!on));cJSON_ReplaceItemInObject(entry,"reason",cJSON_CreateString("独立控制本频段；开启时可恢复无线服务，等待实际热点确认"));
    cJSON*band=jget(wd,b?"main_5g":"main_2g");copy_value(band,"configured_disabled",band,"disabled");cJSON_ReplaceItemInObject(band,"disabled",on?cJSON_CreateString("0"):!strcmp(state,"DISABLED")?cJSON_CreateString("1"):cJSON_CreateNull());copy_value(band,"managed_off",runtime,b?"managed_off_5g":"managed_off");cJSON_AddStringToObject(band,"runtime_state",state);
   }
  }cJSON_Delete(runtime);
 }
 for(int b=0;b<2;b++){const char *id=b?"main_5g.enabled":"main_2g.enabled";cJSON *entry;
  cJSON_ArrayForEach(entry,items)if(!strcmp(jstr(entry,"id"),id)){cJSON_DetachItemViaPointer(items,entry);cJSON_InsertItemInArray(items,1+b,entry);break;}
 }
 relay_items(s,data);
}
static void ts_sections(cJSON *root){
 cJSON *s=section(root,"tailscale","Tailscale"),*p=ts_api("prefs",NULL),*st=ts_api("status",NULL),*d=jget(root,"data"),*td=cJSON_AddObjectToObject(d,"tailscale");int tun=tun_ready();const char *backend=jstr(st,"BackendState");cJSON_AddStringToObject(d,"tailscale_status",*backend?backend:"不可用");copy_value(td,"state",st,"BackendState");cJSON_AddBoolToObject(td,"tun_ready",tun);copy_value(td,"ips",st,"TailscaleIPs");
 cJSON *self_ip=cJSON_GetArrayItem(jget(st,"TailscaleIPs"),0);item(s,"self-ip","本机组网 IP","info",cJSON_IsString(self_ip)?self_ip->valuestring:"未连接",NULL,0,NULL);
 cJSON *test=item(s,"peer-test","设备连通测试","choice","选择在线设备","tailscale.peer_test",!strcmp(backend,"Running"),NULL),*candidate;
 cJSON_ReplaceItemInObject(test,"confirm",cJSON_CreateBool(0));
 cJSON_ArrayForEach(candidate,jget(st,"Peer")){cJSON *ip=cJSON_GetArrayItem(jget(candidate,"TailscaleIPs"),0);if(cJSON_IsTrue(jget(candidate,"Online"))&&cJSON_IsString(ip))choice(test,*jstr(candidate,"HostName")?jstr(candidate,"HostName"):ip->valuestring,"ip",ip->valuestring);}
 if(!cJSON_GetArraySize(jget(test,"choices")))cJSON_ReplaceItemInObject(test,"enabled",cJSON_CreateBool(0));
 toggle(s,"connected","私网组网开关","tailscale.connected",cJSON_IsTrue(jget(p,"WantRunning")),cJSON_IsBool(jget(p,"WantRunning")),"保留当前身份与其他偏好");
 cJSON *gateway=ts_lan_run("status");cJSON *gw=toggle(s,"lan-gateway","热点设备访问组网","tailscale.lan_gateway",cJSON_IsTrue(jget(gateway,"enabled")),cJSON_IsBool(jget(gateway,"enabled")),"所有热点设备可访问 Tailnet 的 100.x 地址，Clash 上网不变");
 if(cJSON_IsTrue(jget(gateway,"enabled"))&&!cJSON_IsTrue(jget(gateway,"active")))cJSON_ReplaceItemInObject(gw,"value",cJSON_CreateString("开启 · 等待 TUN"));copy_value(td,"lan_gateway",gateway,"enabled");copy_value(td,"lan_gateway_active",gateway,"active");cJSON_Delete(gateway);
 cJSON *mode_status=ts_mode_run("status");const char *mode_name=*jstr(mode_status,"mode")?jstr(mode_status,"mode"):(tun?"tun":"userspace");cJSON *mi=item(s,"mode","转发模式","choice",!strcmp(mode_name,"tun")?"TUN":"userspace","network.tailscale_mode",fixture||access("/data/u60-panel/tailscale-mode.sh",X_OK)==0,"切换会短暂重连；失败恢复原模式，复用现有身份");choice(mi,"TUN：内核转发","mode","tun");choice(mi,"userspace：本机组网","mode","userspace");cJSON_Delete(mode_status);
 const char *keys[]={"RouteAll","CorpDNS","ExitNodeAllowLANAccess"},*acts[]={"tailscale.accept_routes","tailscale.accept_dns","tailscale.allow_lan"},*labels[]={"接受其他设备路由","接受 Tailscale DNS","使用出口时允许访问局域网"};
 for(int n=0;n<3;n++){cJSON *control=toggle(s,keys[n],labels[n],acts[n],cJSON_IsTrue(jget(p,keys[n])),cJSON_IsBool(jget(p,keys[n])),n==0?"接收子网路由需先启用 TUN 转发模式":NULL);if(n==0&&!tun&&!cJSON_IsTrue(jget(p,keys[n])))cJSON_ReplaceItemInObject(control,"enabled",cJSON_CreateBool(0));copy_value(td,keys[n],p,keys[n]);}
 char cidr[64]="";int lan=lan_cidr(cidr,sizeof(cidr));int advertising=lan&&arr_has(jget(p,"AdvertiseRoutes"),cidr),offering=arr_has(jget(p,"AdvertiseRoutes"),"0.0.0.0/0");
 cJSON *publish=toggle(s,"advertise_lan","远程访问 U60 内网","tailscale.advertise_lan",advertising,p&&lan,tun?"需要管理端批准子网路由":"请先开启 TUN 转发模式");if(!tun&&!advertising)cJSON_ReplaceItemInObject(publish,"enabled",cJSON_CreateBool(0));
 if(advertising){int approved=arr_has(jget(jget(st,"Self"),"PrimaryRoutes"),cidr)||arr_has(jget(jget(st,"Self"),"AllowedIPs"),cidr);cJSON_ReplaceItemInObject(publish,"value",cJSON_CreateString(approved?"已发布 · 已批准":"已发布 · 等待后台批准"));}toggle(s,"offer_exit","共享本机互联网","tailscale.offer_exit",offering,p&&(tun||offering),tun?"需要管理端批准出口节点":"userspace 模式尚未完成 TUN 转发迁移");
 cJSON *i=item(s,"exit","使用远程出口","choice",*jstr(p,"ExitNodeID")?"已选择":"不使用","tailscale.exit",p&&(tun||*jstr(p,"ExitNodeID")),tun?"取消远程出口后使用本地直连；开启 Clash 请到 Clash 页面":"选择出口需要先完成 TUN 转发迁移");choice(i,"不使用出口","id","");cJSON *peers=cJSON_AddArrayToObject(td,"peers"),*peer;int count=0;cJSON_ArrayForEach(peer,jget(st,"Peer")){if(count++>=64)break;cJSON *safe=cJSON_CreateObject();copy_value(safe,"name",peer,"HostName");copy_value(safe,"ips",peer,"TailscaleIPs");copy_value(safe,"online",peer,"Online");copy_value(safe,"exit_available",peer,"ExitNodeOption");cJSON_AddItemToArray(peers,safe);if(tun&&cJSON_IsTrue(jget(peer,"ExitNodeOption"))&&cJSON_IsTrue(jget(peer,"Online"))&&*jstr(peer,"ID"))choice(i,jstr(peer,"HostName"),"id",jstr(peer,"ID"));}
 copy_value(td,"exit_node",p,"ExitNodeID");copy_value(td,"advertised_routes",p,"AdvertiseRoutes");info(s,"backend","连接状态",st,"BackendState");
 count=0;cJSON_ArrayForEach(peer,jget(st,"Peer")){if(count>=64)break;char id[32],value[160];snprintf(id,sizeof(id),"peer_%d",count++);cJSON*ip=cJSON_GetArrayItem(jget(peer,"TailscaleIPs"),0);snprintf(value,sizeof(value),"%s · %s",cJSON_IsTrue(jget(peer,"Online"))?"在线":"离线",cJSON_IsString(ip)?ip->valuestring:"地址未知");item(s,id,*jstr(peer,"HostName")?jstr(peer,"HostName"):"未命名设备","info",value,NULL,0,NULL);}
 cJSON *ordered=jget(s,"items");const char *front[]={"connected","lan-gateway","advertise_lan","peer-test","self-ip"};for(int k=4;k>=0;k--){for(int n=0;n<cJSON_GetArraySize(ordered);n++)if(!strcmp(jstr(cJSON_GetArrayItem(ordered,n),"id"),front[k])){cJSON *moved=cJSON_DetachItemFromArray(ordered,n);cJSON_InsertItemInArray(ordered,0,moved);break;}}
 cJSON_Delete(p);cJSON_Delete(st);
}
static void sum_numbers(cJSON *dest,const char *key,const cJSON *src,const char *a,const char *b){cJSON *x=jget(src,a),*y=jget(src,b);if(cJSON_IsNumber(x)&&cJSON_IsNumber(y))cJSON_AddNumberToObject(dest,key,x->valuedouble+y->valuedouble);else cJSON_AddNullToObject(dest,key);}
static void read_stats(cJSON *root){
 cJSON *d=jget(root,"data"),*cell=section(root,"cell","蜂窝网络"),*sys=section(root,"system","设备状态"),*n=ubus_read("zte_nwinfo_api","nwinfo_get_netinfo"),*b=ubus_read("zwrt_bsp.battery","list"),*t=ubus_read("zwrt_bsp.thermal","get_cpu_temp"),*clients=ubus_read("zwrt_router.api","router_get_user_list_num");
 copy_value(d,"operator",n,"network_provider_fullname");copy_value(d,"network",n,"network_type");if(!strcmp(jstr(n,"network_type"),"SA"))cJSON_ReplaceItemInObject(d,"network",cJSON_CreateString("5G SA"));copy_value(d,"band",n,*jstr(n,"nr5g_action_band")?"nr5g_action_band":"wan_active_band");copy_value(d,"signal",n,*jstr(n,"nr5g_rsrp")?"nr5g_rsrp":"signalbar");copy_value(d,"battery",b,"battery_capacity");copy_value(d,"temperature",t,"cpuss_temp");copy_value(d,"clients",clients,"access_total_num");info(cell,"operator","运营商",n,"network_provider_fullname");info(cell,"network","网络",n,"network_type");info(cell,"band","频段",d,"band");info(cell,"signal","信号格数",n,"signalbar");info(cell,"rsrp","5G RSRP (dBm)",n,"nr5g_rsrp");info(sys,"battery","电量 (%)",b,"battery_capacity");info(sys,"temperature","CPU 温度 (°C)",t,"cpuss_temp");
 cJSON *args=cJSON_CreateObject();cJSON_AddStringToObject(args,"source_module","web");cJSON_AddNumberToObject(args,"cid",1);cJSON_AddNumberToObject(args,"type",4);cJSON *traffic=ubus_call("zwrt_data","get_wwandst",args);cJSON_Delete(args);sum_numbers(d,"today_bytes",traffic,"day_rx_bytes","day_tx_bytes");sum_numbers(d,"month_bytes",traffic,"month_rx_bytes","month_tx_bytes");copy_value(d,"download_bps",traffic,"real_rx_speed");copy_value(d,"upload_bps",traffic,"real_tx_speed");cJSON_AddStringToObject(d,"traffic_source","zwrt_data.get_wwandst source_module=web cid=1 type=4 (cellular)");const char *traffic_keys[]={"today_bytes","month_bytes"},*traffic_ids[]={"today","month"},*traffic_labels[]={"今日蜂窝流量","本月蜂窝流量"};for(int n=0;n<2;n++){cJSON *value=jget(d,traffic_keys[n]);char text[64];if(cJSON_IsNumber(value)&&value->valuedouble>=0)snprintf(text,sizeof(text),"%.2f GB",value->valuedouble/1000000000.0);else snprintf(text,sizeof(text),"—");item(cell,traffic_ids[n],traffic_labels[n],"info",text,NULL,0,NULL);}
 cJSON_Delete(n);cJSON_Delete(b);cJSON_Delete(t);cJSON_Delete(clients);cJSON_Delete(traffic);
 double uptime=0;FILE *f=fopen("/proc/uptime","r");if(f){if(fscanf(f,"%lf",&uptime)==1)cJSON_AddNumberToObject(d,"uptime_seconds",uptime);fclose(f);}if(!jget(d,"uptime_seconds"))cJSON_AddNullToObject(d,"uptime_seconds");info(sys,"uptime","运行时间 (秒)",d,"uptime_seconds");
 unsigned long long total=0,available=0;f=fopen("/proc/meminfo","r");if(f){char line[256];while(fgets(line,sizeof(line),f)){sscanf(line,"MemTotal: %llu kB",&total);sscanf(line,"MemAvailable: %llu kB",&available);}fclose(f);}if(total){cJSON_AddNumberToObject(d,"memory_total_bytes",(double)total*1024);cJSON_AddNumberToObject(d,"memory_available_bytes",(double)available*1024);cJSON_AddNumberToObject(d,"memory_used_percent",100.0*(total-available)/total);}else cJSON_AddNullToObject(d,"memory_used_percent");info(sys,"memory","内存占用 (%)",d,"memory_used_percent");
 /* Individual leaf interface counters only: excludes bridges, loopback, TUN and
  * duplicate IPv4/IPv6 references. No sum that double-counts a routed packet. */
 cJSON *netdev=cJSON_AddArrayToObject(d,"physical_interfaces");f=fopen("/proc/net/dev","r");if(f){char line[512];while(fgets(line,sizeof(line),f)){char dev[64];unsigned long long rx=0,tx=0;if(sscanf(line," %63[^:]: %llu %*u %*u %*u %*u %*u %*u %*u %llu",dev,&rx,&tx)!=3)continue;if(strncmp(dev,"eth",3)&&strncmp(dev,"usb",3)&&strncmp(dev,"rndis",5)&&strncmp(dev,"rmnet",5)&&strncmp(dev,"wlan",4)&&strncmp(dev,"mhi",3))continue;cJSON *v=cJSON_CreateObject();cJSON_AddStringToObject(v,"name",dev);cJSON_AddNumberToObject(v,"rx_bytes",(double)rx);cJSON_AddNumberToObject(v,"tx_bytes",(double)tx);cJSON_AddItemToArray(netdev,v);}fclose(f);}
 /* A short sample avoids presenting lifetime cumulative CPU as current load. */
 unsigned long long v1[8]={0},v2[8]={0};int got=0;f=fopen("/proc/stat","r");if(f){got=fscanf(f,"cpu %llu %llu %llu %llu %llu %llu %llu %llu",&v1[0],&v1[1],&v1[2],&v1[3],&v1[4],&v1[5],&v1[6],&v1[7]);fclose(f);}if(got==8){struct timespec delay={0,100000000};nanosleep(&delay,NULL);f=fopen("/proc/stat","r");if(f){got=fscanf(f,"cpu %llu %llu %llu %llu %llu %llu %llu %llu",&v2[0],&v2[1],&v2[2],&v2[3],&v2[4],&v2[5],&v2[6],&v2[7]);fclose(f);}unsigned long long a=0,z=0;for(int k=0;k<8;k++){a+=v1[k];z+=v2[k];}if(got==8&&z>a)cJSON_AddNumberToObject(d,"cpu_percent",100.0*((z-a)-(v2[3]-v1[3])-(v2[4]-v1[4]))/(z-a));}if(!jget(d,"cpu_percent"))cJSON_AddNullToObject(d,"cpu_percent");info(sys,"cpu","CPU 使用率 (%)",d,"cpu_percent");
}
static cJSON *usb_role_run(const char *role){
 if(role&&!strcmp(role,"AUTO")&&relay_enabled())return reply(0,"请先断开Wi-Fi中继，再切换USB AUTO");
 if(fixture){
  if(!role)return mock("usb.role.status");
  fixture_write_count++;return reply(!cJSON_IsTrue(jget(fixture,"reject_writes")),"已保存，正在切换；以网口状态为准");
 }
 char out[4096];char *av[]={"usb-role.sh",role?"set":"status",(char*)role,NULL};
 int ok=run_cmd("/data/u60-panel/usb-role.sh",av,NULL,out,sizeof(out));cJSON*r=cJSON_Parse(out);
 if(cJSON_IsObject(r)&&(!cJSON_IsTrue(jget(r,"ok"))||ok))return r;
 cJSON_Delete(r);return reply(0,"网口协调服务尚未就绪");
}
static cJSON *usb_macnet_status(void){
 if(fixture)return mock("usb.macnet.status");
 char out[2048];char *av[]={"enable-usb-macnet.sh","status",NULL};
 int ok=run_cmd("/data/u60-panel/enable-usb-macnet.sh",av,NULL,out,sizeof(out));cJSON*r=cJSON_Parse(out);
 if(ok&&cJSON_IsObject(r)&&cJSON_IsBool(jget(r,"ok")))return r;
 cJSON_Delete(r);return reply(0,"USB 主机模式读取失败");
}
static cJSON *state(void){
 cJSON *root=reply(1,"状态已读取；空值表示接口未提供数据");cJSON_AddObjectToObject(root,"data");cJSON_AddArrayToObject(root,"sections");wifi_sections(root);ts_sections(root);cJSON *d=jget(root,"data"),*s=section(root,"usb","USB 网络"),*usb=ubus_read("zwrt_bsp.usb","list"),*wan=ubus_read("zwrt_router.api","router_get_wan_mode_para");cJSON *ud=cJSON_AddObjectToObject(d,"usb");copy_value(ud,"mode",usb,"mode");copy_value(ud,"connected",usb,"connect");copy_value(ud,"adapter",usb,"usb2rj45");copy_value(ud,"wan_mode",wan,"opms_wan_mode");
 cJSON *us=usb_role_run(NULL);cJSON_AddStringToObject(ud,"badge",jstr(us,"badge"));copy_value(ud,"state",us,"state");copy_value(ud,"requested",us,"requested");
 const char *message=*jstr(us,"message")?jstr(us,"message"):"网口状态未知";
 const char *port_state=jstr(us,"state");
 const char *short_state=!strcmp(port_state,"WAN")?"有线上网":!strcmp(port_state,"LAN")?"对外供网":!strcmp(port_state,"WAIT_ADAPTER")?(cJSON_IsNumber(jget(usb,"connect"))&&jget(usb,"connect")->valueint?"USB 直连":"未接网卡"):!strcmp(port_state,"WAIT_CABLE")?"等待网线":!strcmp(port_state,"CONFLICT")?"地址冲突":!strcmp(port_state,"RESTORING")?"恢复蜂窝":!strcmp(port_state,"NO_UPSTREAM")?"蜂窝上网":"切换中";
 cJSON_AddStringToObject(d,"usb_status",short_state);
 cJSON *i=item(s,"role","外接网卡模式","choice",!strcmp(jstr(us,"requested"),"AUTO")?"AUTO · 自动有线":"LAN · 对外供网","usb.role",cJSON_IsTrue(jget(us,"ok")),"AUTO 接上级路由器 LAN 口；切 LAN 前先拔网线，再接电脑");
 if(relay_enabled()){cJSON_ReplaceItemInObject(i,"enabled",cJSON_CreateBool(0));cJSON_ReplaceItemInObject(i,"reason",cJSON_CreateString("中继期间网口保持LAN，断开中继后可切AUTO"));}
 choice(i,"AUTO · 自动有线 / 蜂窝","role","AUTO");choice(i,"LAN · 给电脑供网","role","LAN");
 item(s,"status","外接网卡状态","info",message,NULL,0,NULL);
 if(*jstr(us,"ipv4"))info(s,"ipv4","上级分配地址",us,"ipv4");
 if(*jstr(us,"gateway"))info(s,"gateway","上级网关",us,"gateway");
 item(s,"wiring","接线提示","info",!strcmp(jstr(us,"requested"),"AUTO")?"网卡接上级 LAN 口；拔线回蜂窝":"网卡通过网线接下游电脑",NULL,0,NULL);
 cJSON *mac=usb_macnet_status();const char *mm=jstr(mac,"mode");
 int known=cJSON_IsTrue(jget(mac,"ok"));
 copy_value(ud,"host",mac,"mode");
 cJSON_AddItemToObject(ud,"gadget",mac?cJSON_Duplicate(mac,1):cJSON_CreateObject());
 item(s,"macnet.mode","USB 直连协议","info",!known?"未知":!strcmp(mm,"ecm")?"ECM":!strcmp(mm,"rndis")?"RNDIS":"未知",NULL,0,NULL);
 const char *link_state=!known?"读取失败":!cJSON_IsTrue(jget(mac,"bound"))?"USB 功能未绑定":!cJSON_IsTrue(jget(mac,"configured"))?"等待电脑识别":!cJSON_IsTrue(jget(mac,"carrier"))?"USB 已枚举，网络链路未建立":!cJSON_IsTrue(jget(mac,"bridged"))?"网口已连接，未加入内网":"USB 内网链路已连接，上网待验证";
 item(s,"macnet.link","USB 直连链路","info",link_state,NULL,0,NULL);
 item(s,"macnet.help","Mac 连接说明","info","在线切换暂不可用；请通过 Wi-Fi 管理",NULL,0,NULL);
 if(!strcmp(port_state,"WAIT_ADAPTER"))cJSON_ReplaceItemInObject(d,"usb_status",cJSON_CreateString(link_state));
 cJSON_Delete(mac);
 info(s,"mode","USB 模式",usb,"mode");cJSON_Delete(us);cJSON_Delete(usb);cJSON_Delete(wan);
 cJSON *profile=profile_run("status");copy_value(d,"network_profile",profile,"profile");s=section(root,"router","路由与上网");i=item(s,"profile","当前上网出口","info",*jstr(profile,"profile")?jstr(profile,"profile"):"未知",NULL,0,"代理在 Clash 页面控制，远程出口在组网页面控制");cJSON_Delete(profile);cJSON *lan=ubus_read("zwrt_router.api","router_get_dhcp_router");info(s,"lan_ip","局域网地址",lan,"lan_addr");info(s,"netmask","子网掩码",lan,"lan_netmask");cJSON_Delete(lan);read_stats(root);
#ifdef HAVE_ADVANCED_CONTROL
 if(!fixture)control_advanced_sections(root);
#endif
#ifdef HAVE_CLASH_CONTROL
 if(!fixture)control_clash_sections(root);
#endif
 if(!fixture){diagnostics_sections(root);ledger_sections(root);power_role_sections(root);charge_sections(root);standby_sections(root);radio_tools_sections(root);}
 return root;
}
static cJSON *dispatch(const cJSON *r){const char *a=jstr(r,"action");cJSON *args=jget(r,"args");if(!strcmp(a,"state"))return state();if(!cJSON_IsObject(args))return reply(0,"args 必须是对象");if(!strcmp(a,"system.power")){int reboot=0;if(!boolarg(args,"reboot",&reboot))return reply(0,"reboot 必须是布尔值");if(fixture){fixture_write_count++;return reply(1,"测试：已请求电源操作");}cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"moduleName","web");cJSON *res=ubus_call("zwrt_mc.device.manager",reboot?"device_reboot":"device_poweroff",p);cJSON_Delete(p);if(!firmware_success(res)){cJSON_Delete(res);return reply(0,"电源操作未获原厂接口确认");}cJSON_Delete(res);return reply(1,reboot?"已请求重启":"已请求关机");}if(!strncmp(a,"wifi.",5))return wifi_action(a,args);if(!strcmp(a,"tailscale.exit"))return ts_exit_transaction(args);if(!strncmp(a,"tailscale.",10))return ts_action(a,args);if(!strcmp(a,"network.profile"))return network_profile(args);if(!strcmp(a,"network.tailscale_mode"))return ts_mode_action(args);
#ifdef HAVE_ADVANCED_CONTROL
 if(!fixture){
  cJSON*standby=standby_action(a,args);if(standby)return standby;
  if(!strcmp(a,"maintenance.tick")){cJSON*r=reply(1,"后台采样完成");cJSON*l=ledger_tick(),*c=charge_tick();int ok=cJSON_IsTrue(jget(l,"ok"))&&cJSON_IsTrue(jget(c,"ok"));cJSON_ReplaceItemInObject(r,"ok",cJSON_CreateBool(ok));cJSON_AddItemToObject(r,"usage",l);cJSON_AddItemToObject(r,"charge",c);return r;}
  cJSON *d=diagnostics_action(a,args);if(d)return d;d=ledger_action(a,args);if(d)return d;d=charge_action(a,args);if(d)return d;d=power_role_action(a,args);if(d)return d;d=radio_tools_action(a,args);if(d)return d;
 }
 if(!fixture){cJSON *advanced=control_advanced_action(a,args);if(advanced)return advanced;}
#endif

#ifdef HAVE_CLASH_CONTROL
 if(!fixture){cJSON *cr=control_clash_action(a,args);if(cr)return cr;}
#endif
 if(!strcmp(a,"usb.role")){const char*role=jstr(args,"role");if(strcmp(role,"AUTO")&&strcmp(role,"LAN"))return reply(0,"请选择 AUTO 或 LAN");return usb_role_run(role);}if(!strcmp(a,"usb.macnet.enable"))return reply(0,"USB 在线切换尚未通过枚举与回退验证，未修改设备");if(!strcmp(a,"usb.macnet.restore"))return reply(0,"USB 在线切换尚未通过枚举与回退验证，未修改设备");if(!strcmp(a,"internet.profile"))return reply(0,"上网出口编排尚需迁移验收；未修改路由");return reply(0,"不支持的操作");}
int main(int argc,char **argv){signal(SIGPIPE,SIG_IGN);
 if(argc==3&&!strcmp(argv[1],"--fixture")){FILE *f=fopen(argv[2],"rb");if(f){char b[131072];size_t n=fread(b,1,sizeof(b)-1,f);b[n]=0;fclose(f);fixture=cJSON_Parse(b);}if(!fixture){puts("{\"ok\":false,\"message\":\"无效的测试 fixture\"}");return 0;}}
 else if(argc!=1){puts("{\"ok\":false,\"message\":\"只支持标准输入 JSON 请求\"}");return 0;}
 char input[16385];size_t n=fread(input,1,sizeof(input)-1,stdin);input[n]=0;const char *end=NULL;cJSON *request=NULL,*result=NULL;
 if(n<sizeof(input)-1)request=cJSON_ParseWithOpts(input,&end,1);
 if(!cJSON_IsObject(request))result=reply(0,"无效 JSON 请求或请求超过 16 KiB");else {
 int lock=-1,locked=0;int mutation=strcmp(jstr(request,"action"),"state")&&strcmp(jstr(request,"action"),"wifi.show_password")&&strcmp(jstr(request,"action"),"tailscale.peer_test");
 if(mutation&&!fixture){lock=open("/tmp/u60-control.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);long long until=ms()+3000;if(lock>=0){while(ms()<until){if(!flock(lock,LOCK_EX|LOCK_NB)){locked=1;break;}struct timespec t={0,50000000};nanosleep(&t,NULL);}}}
 if(mutation&&!fixture&&!locked)result=reply(0,"另一个设置正在执行，请稍后重试");
 else if(mutation&&!fixture&&strcmp(jstr(request,"action"),"power.standby")&&access("/tmp/u60-standby/active",F_OK)==0)result=reply(0,"正在恢复休眠前的服务，请稍后重试");
 else result=dispatch(request);
 if(lock>=0){if(locked)flock(lock,LOCK_UN);close(lock);}
 }if(fixture)cJSON_AddNumberToObject(result,"fixture_write_count",fixture_write_count);char *out=cJSON_PrintUnformatted(result);puts(out?out:"{\"ok\":false,\"message\":\"内存不足\"}");free(out);memset(input,0,sizeof(input));cJSON_Delete(request);cJSON_Delete(result);cJSON_Delete(fixture);return 0;
}
