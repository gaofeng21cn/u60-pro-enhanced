/* Saved-network radio coordination. Never fetches a PSK or changes the saved
 * file during retries; interactive connect atomically merges credentials. */
#include "panel-relay-frequency.h"
static int align_radio(int frequency){
 if(!relay_frequency(frequency))return 0;
 char freq[16];snprintf(freq,sizeof(freq),"%d",frequency);
 pid_t p=fork();if(!p){int n=open("/dev/null",O_RDWR);dup2(n,0);dup2(n,1);dup2(n,2);if(n>2)close(n);execl(RELAY_HELPER,"wifi-relay.sh","align",freq,(char*)NULL);_exit(127);}
 int s=0;if(p<0)return 0;while(waitpid(p,&s,0)<0)if(errno!=EINTR)return 0;return WIFEXITED(s)&&!WEXITSTATUS(s);
}
static int saved_value(int id,const char*key,char*out,size_t n){
 char cmd[80];snprintf(cmd,sizeof(cmd),"GET_NETWORK %d %s",id,key);
 if(!ctrl(cmd,out,n)||!strncmp(out,"FAIL",4))return 0;
 out[strcspn(out,"\r\n")]=0;return 1;
}
static int saved_ssid(const char*encoded,char*out){
 size_t n=strlen(encoded);
 if(n>=2&&encoded[0]=='"'&&encoded[n-1]=='"'){
  char b[256];if(n-2>=sizeof(b))return 0;memcpy(b,encoded+1,n-2);b[n-2]=0;return decode_ssid(b,out);
 }
 if(n<2||n>64||n%2)return 0;
 for(size_t i=0;i<n;i+=2){int a=hexval(encoded[i]),b=hexval(encoded[i+1]);if(a<0||b<0)return 0;out[i/2]=(char)(a*16+b);if((unsigned char)out[i/2]<32||(unsigned char)out[i/2]==127)return 0;}
 out[n/2]=0;return 1;
}
#include "panel-relay-profiles.h"
static void save_band(int freq);
static long relay_uptime(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec;}
/* Failures remain excluded until every visible candidate has had a turn.
 * A new round starts only after the last failure has cooled down. */
static int profile_cooldown(const char*id,int record){char p[256];snprintf(p,sizeof(p),RUN "/retry-%s",id);long now=relay_uptime(),last=0;
 if(record){FILE*f=fopen(p,"w");if(f){fprintf(f,"%ld",now);fclose(f);}return 0;}
 FILE*f=fopen(p,"r");if(f){if(fscanf(f,"%ld",&last)!=1)last=0;fclose(f);}return last>0;
}
static cJSON* coordinate(void){
 if(access(PRIVATE "/enabled",F_OK))return result(0,"中继未启用");
 char current[4096],state[64];
 if(ctrl("STATUS",current,sizeof(current))&&keyval(current,"wpa_state",state,sizeof(state))&&!strcmp(state,"COMPLETED"))return result(1,"当前上游已连接，保持连接");
 struct relay_profiles*saved=calloc(1,sizeof(*saved));if(!saved)return result(0,"内存不足");
 if(!profiles_load(saved)){free(saved);return result(0,"保存网络不可读");}
 if(!ack("RECONFIGURE")||!ack("DISCONNECT")){memset(saved,0,sizeof(*saved));free(saved);return result(0,"保存网络载入失败");}
 char list[4096];if(!ctrl("LIST_NETWORKS",list,sizeof(list))){memset(saved,0,sizeof(*saved));free(saved);return result(0,"读取保存网络失败");}
 struct {int id,rank,band;char ssid[33],sec[8],identity[80];} candidates[PROFILE_MAX];int count=0;
 char*save=NULL;strtok_r(list,"\n",&save);char*line;
 while((line=strtok_r(NULL,"\n",&save))&&count<PROFILE_MAX){char*end;long n=strtol(line,&end,10);if(end==line||*end!='\t'||n<0||n>4096)continue;
  char encoded[256],ssid[33],key[64],freq[256],priority[32];
  if(!saved_value((int)n,"ssid",encoded,sizeof(encoded))||!saved_ssid(encoded,ssid)||!saved_value((int)n,"key_mgmt",key,sizeof(key)))continue;
  struct relay_profile p={0};snprintf(p.ssid,sizeof(p.ssid),"%s",ssid);snprintf(p.security,sizeof(p.security),"%s",strstr(key,"SAE")?"WPA3":strstr(key,"WPA-PSK")?"WPA2":!strcmp(key,"NONE")?"OPEN":"UNKNOWN");
  if(saved_value((int)n,"id_str",freq,sizeof(freq))){if(!strcmp(freq,"\"u60-band-2\""))p.band=2;else if(!strcmp(freq,"\"u60-band-5\""))p.band=5;}
  if(!p.band&&saved_value((int)n,"freq_list",freq,sizeof(freq))&&atoi(freq)>0)p.band=atoi(freq)<3000?2:5;
  if(saved_value((int)n,"priority",priority,sizeof(priority)))p.priority=atoi(priority);
  if(!p.band){FILE*f=fopen(PRIVATE "/upstream-band","r");if(f){int b=0;if(fscanf(f,"%d",&b)==1&&(b==2||b==5))p.band=b;fclose(f);}}
  profile_identity(&p);
  for(int i=0;i<saved->count;i++)if(!strcmp(saved->entries[i].id,p.id))p.priority=saved->entries[i].priority;
  candidates[count].id=(int)n;candidates[count].rank=p.priority;candidates[count].band=p.band;
  snprintf(candidates[count].ssid,33,"%s",p.ssid);snprintf(candidates[count].sec,8,"%s",p.security);snprintf(candidates[count].identity,80,"%s",p.id);count++;
 }
 memset(saved,0,sizeof(*saved));free(saved);
 if(!count)return result(0,"尚未保存上游");
 char requested[80]="";FILE*fp=fopen(RUN "/selected-profile","r");if(fp){fgets(requested,sizeof(requested),fp);fclose(fp);}
 if(!fresh_scan())return result(0,"等待上游扫描");
 char scans[32768];if(!ctrl("SCAN_RESULTS",scans,sizeof(scans)))return result(0,"上游扫描不可读");
 cJSON*a=parse_scan(scans),*x,*best=NULL;int chosen=-1,best_rank=-2147483647,best_signal=-151;int visible[PROFILE_MAX]={0},remaining=0;long latest_failure=0;
 for(int i=0;i<count;i++){
  int manual=*requested&&!strcmp(requested,candidates[i].identity);
  int failed=!manual&&profile_cooldown(candidates[i].identity,0);
  int rank=manual?2000:candidates[i].rank;
  cJSON_ArrayForEach(x,a){int f=cJSON_GetObjectItemCaseSensitive(x,"frequency")->valueint,r=cJSON_GetObjectItemCaseSensitive(x,"signal")->valueint;
   if(!relay_frequency(f)||strcmp(str(x,"ssid"),candidates[i].ssid)||strcmp(str(x,"security"),candidates[i].sec)||(candidates[i].band&&candidates[i].band!=(f<3000?2:5)))continue;
   visible[i]=1;
   if(failed)continue;
   remaining=1;
   if(rank>best_rank||(rank==best_rank&&r>best_signal)){best=x;chosen=i;best_rank=rank;best_signal=r;}
  }
 }
 unlink(RUN "/selected-profile");
 if(!best){
  if(!remaining){for(int i=0;i<count;i++)if(visible[i]){char p[256];snprintf(p,sizeof(p),RUN "/retry-%s",candidates[i].identity);FILE*f=fopen(p,"r");long last=0;if(f){if(fscanf(f,"%ld",&last)!=1)last=0;fclose(f);}if(last>latest_failure)latest_failure=last;}
   if(latest_failure&&relay_uptime()-latest_failure>=60)for(int i=0;i<count;i++)if(visible[i]){char p[256];snprintf(p,sizeof(p),RUN "/retry-%s",candidates[i].identity);unlink(p);}
  }
  cJSON_Delete(a);return result(0,"保存网络暂不可用；按断线策略等待");
 }
 int id=candidates[chosen].id,f=cJSON_GetObjectItemCaseSensitive(best,"frequency")->valueint;char cmd[128];
 const char*stage="channel";int ok=ack("DISCONNECT")&&align_radio(f);
 if(ok){stage="bssid";snprintf(cmd,sizeof(cmd),"SET_NETWORK %d bssid %s",id,str(best,"bssid"));ok=ack(cmd);}
 if(ok){stage="frequency";snprintf(cmd,sizeof(cmd),"SET_NETWORK %d freq_list %d",id,f);ok=ack(cmd);}
 if(ok&&!access(PRIVATE "/enabled",F_OK)){stage="select";snprintf(cmd,sizeof(cmd),"SELECT_NETWORK %d",id);ok=ack(cmd);}else ok=0;
 if(ok){
  stage="association";ok=0;
  for(int attempt=0;attempt<CONNECT_TRIES&&!access(PRIVATE "/enabled",F_OK);attempt++){
   char status[2048],st[64],actual[32],actual_id[32];
   if(ctrl("STATUS",status,sizeof(status))&&keyval(status,"wpa_state",st,sizeof(st))&&!strcmp(st,"COMPLETED")&&keyval(status,"freq",actual,sizeof(actual))&&atoi(actual)==f&&keyval(status,"id",actual_id,sizeof(actual_id))&&atoi(actual_id)==id){ok=1;break;}
   sleep(1);
  }
 }
 if(ok){save_band(f);for(int i=0;i<count;i++){char p[256];snprintf(p,sizeof(p),RUN "/retry-%s",candidates[i].identity);unlink(p);}}else {profile_cooldown(candidates[chosen].identity,1);ack("DISCONNECT");}
 cJSON_Delete(a);cJSON*r=result(ok,ok?"上游关联已完成":"所选网络未连通，稍后尝试其他保存网络");cJSON_AddStringToObject(r,"stage",stage);cJSON_AddNumberToObject(r,"target_frequency",f);return r;
}
static void save_band(int freq){
 FILE*f=fopen(PRIVATE "/upstream-band.next","w");if(!f)return;
 int ok=fprintf(f,"%d\n",freq<3000?2:5)>0;if(fclose(f))ok=0;
 if(ok)rename(PRIVATE "/upstream-band.next",PRIVATE "/upstream-band");else unlink(PRIVATE "/upstream-band.next");
}
