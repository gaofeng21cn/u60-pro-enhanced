#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <arpa/inet.h>
#include <time.h>
#include "cJSON.h"
#ifndef RUN
#define RUN "/tmp/u60-wifi-relay"
#endif
#ifndef PRIVATE
#define PRIVATE "/data/u60-panel/relay-private"
#endif
#ifndef RELAY_HELPER
#define RELAY_HELPER "/data/u60-panel/wifi-relay.sh"
#endif
#ifndef CONNECT_TRIES
#define CONNECT_TRIES 30
#endif
static const char *str(const cJSON *j,const char *k){cJSON*v=cJSON_GetObjectItemCaseSensitive(j,k);return cJSON_IsString(v)?v->valuestring:"";}
static cJSON *result(int ok,const char*m){cJSON*j=cJSON_CreateObject();cJSON_AddBoolToObject(j,"ok",ok);cJSON_AddStringToObject(j,"message",m);return j;}
static int helper(const char *command){pid_t p=fork();if(!p){int n=open("/dev/null",O_RDWR);dup2(n,0);dup2(n,1);dup2(n,2);if(n>2)close(n);execl(RELAY_HELPER,"wifi-relay.sh",command,(char*)NULL);_exit(127);}int s=0;if(p<0)return 0;while(waitpid(p,&s,0)<0)if(errno!=EINTR)return 0;return WIFEXITED(s)&&!WEXITSTATUS(s);}
static int ctrl(const char *command,char *out,size_t cap){
 int fd=socket(AF_UNIX,SOCK_DGRAM,0);if(fd<0)return 0;fcntl(fd,F_SETFD,FD_CLOEXEC);
 struct sockaddr_un local={.sun_family=AF_UNIX},remote={.sun_family=AF_UNIX};
 snprintf(local.sun_path,sizeof(local.sun_path),RUN "/client-%ld",(long)getpid());snprintf(remote.sun_path,sizeof(remote.sun_path),RUN "/ctrl/u60sta");
 unlink(local.sun_path);int ok=!bind(fd,(void*)&local,sizeof(local))&&!connect(fd,(void*)&remote,sizeof(remote));
 if(ok)ok=send(fd,command,strlen(command),0)==(ssize_t)strlen(command);
 struct pollfd f={fd,POLLIN,0};if(ok)ok=poll(&f,1,2500)>0;
 ssize_t n=ok?recv(fd,out,cap-1,0):-1;close(fd);unlink(local.sun_path);if(n<0||n==(ssize_t)cap-1){out[0]=0;return 0;}out[n]=0;return 1;
}
static int ack(const char*c){char b[128];return ctrl(c,b,sizeof(b))&&!strcmp(b,"OK\n");}
/* Keep the event socket attached until this scan completes. Cached BSS entries
 * can be present before a scan has visited both bands; never treat them as done.
 * Scanning an enabled relay leaves its association, DHCP and routes untouched. */
static int fresh_scan(void){
 int fd=socket(AF_UNIX,SOCK_DGRAM,0);if(fd<0)return 0;fcntl(fd,F_SETFD,FD_CLOEXEC);
 struct sockaddr_un local={.sun_family=AF_UNIX},remote={.sun_family=AF_UNIX};
 snprintf(local.sun_path,sizeof(local.sun_path),RUN "/scan-%ld",(long)getpid());snprintf(remote.sun_path,sizeof(remote.sun_path),RUN "/ctrl/u60sta");
 unlink(local.sun_path);int ok=!bind(fd,(void*)&local,sizeof(local))&&!connect(fd,(void*)&remote,sizeof(remote));
 char b[4096];struct pollfd f={fd,POLLIN,0};
 if(ok)ok=send(fd,"ATTACH",6,0)==6&&poll(&f,1,2500)>0;
 ssize_t n=ok?recv(fd,b,sizeof(b)-1,0):-1;if(n>=0)b[n]=0;
 ok=n>=0&&!strcmp(b,"OK\n");
 if(ok)ok=send(fd,"SCAN",4,0)==4;
 int done=0,accepted=0;struct timespec start,now;clock_gettime(CLOCK_MONOTONIC,&start);
 while(ok){
  clock_gettime(CLOCK_MONOTONIC,&now);if(now.tv_sec-start.tv_sec>=12)break;
  int ready=poll(&f,1,250);if(ready<0){if(errno==EINTR)continue;break;}if(!ready)continue;
  n=recv(fd,b,sizeof(b)-1,0);if(n<=0)break;b[n]=0;
  if(!strncmp(b,"FAIL",4)||strstr(b,"CTRL-EVENT-SCAN-FAILED"))break;
  if(!strcmp(b,"OK\n"))accepted=1;
  if(accepted&&strstr(b,"CTRL-EVENT-SCAN-RESULTS")){done=1;break;}
 }
 send(fd,"DETACH",6,0);close(fd);unlink(local.sun_path);return done;
}
static int keyval(const char *b,const char*k,char*out,size_t cap){size_t n=strlen(k);for(const char*p=b;*p;){const char*e=strchr(p,'\n');if(!e)e=p+strlen(p);if((size_t)(e-p)>n&&!strncmp(p,k,n)&&p[n]=='='){size_t len=e-p-n-1;if(len>=cap)return 0;memcpy(out,p+n+1,len);out[len]=0;return 1;}p=*e?e+1:e;}out[0]=0;return 0;}
static int hexval(int c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static int decode_ssid(const char*in,char*out){size_t n=0;while(*in){unsigned char ch=*in++;if(ch=='\\'&&*in){if(in[0]=='x'&&in[1]&&in[2]&&hexval(in[1])>=0&&hexval(in[2])>=0){ch=(hexval(in[1])<<4)|hexval(in[2]);in+=3;}else if(*in=='\\'){ch='\\';in++;}else return 0;}if(ch<32||ch==127||n>=32)return 0;out[n++]=ch;}out[n]=0;return n>0;}
static int ssid_valid(const char*s){size_t n=strlen(s);if(!n||n>32)return 0;for(size_t i=0;i<n;i++)if((unsigned char)s[i]<32||(unsigned char)s[i]==127)return 0;return 1;}
static int mac_valid(const char*s){if(strlen(s)!=17)return 0;for(int i=0;i<17;i++)if(i%3==2?s[i]!=':':hexval(s[i])<0)return 0;return 1;}
static const char *security(const char*f){if(strstr(f,"EAP"))return "unsupported";if(strstr(f,"PSK"))return "WPA2";if(strstr(f,"SAE"))return "WPA3";if(strstr(f,"WEP")||strstr(f,"RSN")||strstr(f,"WPA"))return "unsupported";return "OPEN";}
static cJSON *parse_scan(char*b){cJSON*a=cJSON_CreateArray();char*save=NULL,*line=strtok_r(b,"\n",&save);while((line=strtok_r(NULL,"\n",&save))&&cJSON_GetArraySize(a)<96){char*col[5];col[0]=line;int n=1;for(char*p=line;*p&&n<5;p++)if(*p=='\t'){*p=0;col[n++]=p+1;}if(n!=5||!mac_valid(col[0]))continue;char ssid[33];if(!decode_ssid(col[4],ssid))continue;int freq=atoi(col[1]),signal=atoi(col[2]);if(freq<2400||freq>7125||signal>0||signal< -150)continue;cJSON*j=cJSON_CreateObject();cJSON_AddStringToObject(j,"ssid",ssid);cJSON_AddStringToObject(j,"bssid",col[0]);cJSON_AddStringToObject(j,"security",security(col[3]));cJSON_AddNumberToObject(j,"frequency",freq);cJSON_AddNumberToObject(j,"signal",signal);cJSON_AddItemToArray(a,j);}return a;}
#include "panel-relay-radio.h"
static cJSON *scan(void){if(!helper("prepare")){helper("scan-stop");return result(0,"暂不能扫描。请确认 5G 热点已开启、访客热点已关闭、USB 为 LAN；若开关失败，请先恢复热点。");}if(!fresh_scan()){helper("scan-stop");return result(0,"无线扫描未完成，请稍后重试");}char b[32768];cJSON*net=ctrl("SCAN_RESULTS",b,sizeof(b))?parse_scan(b):NULL;helper("scan-stop");if(!net)return result(0,"扫描结果读取失败");cJSON*j=result(1,"请选择上游Wi-Fi");cJSON_AddItemToObject(j,"networks",net);return j;}
static cJSON *connect_ap(const cJSON*a){
 const char *ssid=str(a,"ssid"),*pass=str(a,"password"),*sec=str(a,"security"),*bssid=str(a,"bssid");size_t pn=strlen(pass);
 if(!ssid_valid(ssid)||(*bssid&&!mac_valid(bssid)))return result(0,"Wi-Fi名称或地址无效");
 if(strcmp(sec,"OPEN")&&strcmp(sec,"WPA2")&&strcmp(sec,"WPA3"))return result(0,"当前支持开放网络、WPA2/WPA3个人网络");
 if(strcmp(sec,"OPEN")&&(pn<8||pn>63))return result(0,"密码需8至63位");for(size_t i=0;i<pn;i++)if((unsigned char)pass[i]<32||(unsigned char)pass[i]>126)return result(0,"密码包含不支持的字符");
 const cJSON*fv=cJSON_GetObjectItemCaseSensitive(a,"frequency");int frequency=cJSON_IsNumber(fv)?fv->valueint:0;
 if(!relay_frequency(frequency))return result(0,"请选择2.4G或非DFS的5G上游信道");
 int resume=access(PRIVATE "/enabled",F_OK)==0;
 const char*stage="无线准备失败，请先确认 5G 热点可开启";
 char b[4096],cmd[512],quoted[132],hex[65];int id=-1;
 if(resume&&!helper("pause"))return result(0,"当前中继未能退出，未切换网络");
 if(!helper("prepare"))goto fail;
 stage="无线连接参数未被接受";
 if(!ack("REMOVE_NETWORK all")||!ctrl("ADD_NETWORK",b,sizeof(b)))goto fail;
 char *end;long parsed=strtol(b,&end,10);if(parsed<0||parsed>4096||(*end&&strcmp(end,"\n")))goto fail;id=(int)parsed;
 for(size_t i=0;i<strlen(ssid);i++)snprintf(hex+i*2,3,"%02x",(unsigned char)ssid[i]);
 snprintf(cmd,sizeof(cmd),"SET_NETWORK %d ssid %s",id,hex);if(!ack(cmd))goto fail;
 if(*bssid){snprintf(cmd,sizeof(cmd),"SET_NETWORK %d bssid %s",id,bssid);if(!ack(cmd))goto fail;}
 snprintf(cmd,sizeof(cmd),"SET_NETWORK %d key_mgmt %s",id,!strcmp(sec,"OPEN")?"NONE":!strcmp(sec,"WPA3")?"SAE":"WPA-PSK");if(!ack(cmd))goto fail;
 if(strcmp(sec,"OPEN")){size_t at=0;quoted[at++]='"';for(size_t i=0;i<pn;i++){if(pass[i]=='"'||pass[i]=='\\')quoted[at++]='\\';quoted[at++]=pass[i];}quoted[at++]='"';quoted[at]=0;snprintf(cmd,sizeof(cmd),"SET_NETWORK %d %s %s",id,!strcmp(sec,"WPA3")?"sae_password":"psk",quoted);int ok=ack(cmd);memset(quoted,0,sizeof(quoted));memset(cmd,0,sizeof(cmd));if(!ok)goto fail;}
 if(!strcmp(sec,"WPA3")){snprintf(cmd,sizeof(cmd),"SET_NETWORK %d ieee80211w 2",id);if(!ack(cmd))goto fail;}
 snprintf(cmd,sizeof(cmd),"SET_NETWORK %d freq_list %d",id,frequency);if(!ack(cmd))goto fail;stage="热点信道切换失败，已尝试恢复；请换用 2.4G 上游或原热点信道";if(!align_radio(frequency))goto fail;
 snprintf(cmd,sizeof(cmd),"SELECT_NETWORK %d",id);if(!ack(cmd))goto fail;
 stage="关联未完成，请核对密码、信号和上游加密方式";
 for(int n=0;n<CONNECT_TRIES;n++){char state[64];if(ctrl("STATUS",b,sizeof(b))&&keyval(b,"wpa_state",state,sizeof(state))&&!strcmp(state,"COMPLETED")){char actual[32];if(!keyval(b,"freq",actual,sizeof(actual))||atoi(actual)!=frequency)goto fail;if(!ack("SAVE_CONFIG"))goto fail;save_band(frequency);if(!helper("enable"))return result(0,"新网络已保存，但中继服务未启动，请重试开启");return result(1,"已连接并保存，正在获取上游地址；以中继状态为准");}sleep(1);}
 fail:memset(cmd,0,sizeof(cmd));if(id>=0){snprintf(cmd,sizeof(cmd),"REMOVE_NETWORK %d",id);ack(cmd);}helper(resume?"pause":"off");if(resume){int requested=helper("on");return result(0,requested?"新网络连接失败，已请求恢复原中继；请查看上游状态":"新网络连接失败，原中继恢复未确认；请重试开启");}return result(0,stage);
}
static int ipv4(const char*s,uint32_t*out){struct in_addr a;if(inet_pton(AF_INET,s,&a)!=1)return 0;*out=ntohl(a.s_addr);return 1;}
static cJSON *lease(const cJSON*a){
 uint32_t ip,mask,gw,lan,lmask;const char*i=str(a,"ip"),*m=str(a,"subnet"),*g=str(a,"router"),*l=str(a,"lan"),*lm=str(a,"lan_mask");
 if(!ipv4(i,&ip)||!ipv4(m,&mask)||!ipv4(g,&gw)||!ipv4(l,&lan)||!ipv4(lm,&lmask))return result(0,"INVALID_LEASE");
 uint32_t inv=~mask;if((inv&(inv+1))||mask==0||inv<3)return result(0,"INVALID_MASK");
 if((ip&mask)!=(gw&mask)||ip==gw||!(ip&inv)||(ip&inv)==inv||!(gw&inv)||(gw&inv)==inv||ip>>24==0||ip>>24==127||ip>>24>=224||gw>>24==0||gw>>24==127||gw>>24>=224)return result(0,"INVALID_GATEWAY");
 uint32_t low=ip&mask,hi=low|inv,ll=lan&lmask,lh=ll|~lmask;
 if((low<=lh&&hi>=ll)||(low<=0x647fffff&&hi>=0x64400000)||(low<=0xa9feffff&&hi>=0xa9fe0000))return result(0,"SUBNET_CONFLICT");
 int prefix=0;for(uint32_t x=mask;x&0x80000000u;x<<=1)prefix++;
 cJSON*j=result(1,"VALID_LEASE");cJSON_AddNumberToObject(j,"prefix",prefix);return j;
}
#ifndef RELAY_TEST
int main(int argc,char**argv){umask(077);struct rlimit lim={0,0};setrlimit(RLIMIT_CORE,&lim);if(argc!=2)return 2;cJSON*j=NULL,*a=NULL;char b[8192]={0};if(!strcmp(argv[1],"connect")||!strcmp(argv[1],"lease")){size_t n=fread(b,1,sizeof(b)-1,stdin);const char*end=NULL;if(n==sizeof(b)-1||!(a=cJSON_ParseWithOpts(b,&end,1))||!cJSON_IsObject(a))j=result(0,"无效请求");}
 if(!j){if(!strcmp(argv[1],"scan"))j=scan();else if(!strcmp(argv[1],"connect"))j=connect_ap(a);else if(!strcmp(argv[1],"coordinate"))j=coordinate();else if(!strcmp(argv[1],"lease"))j=lease(a);else j=result(0,"未知操作");}int code=!strcmp(argv[1],"coordinate")&&!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"ok"));char*out=cJSON_PrintUnformatted(j);puts(out?out:"{}");free(out);memset(b,0,sizeof(b));cJSON_Delete(a);cJSON_Delete(j);return code;}
#endif
