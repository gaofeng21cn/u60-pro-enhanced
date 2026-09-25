/* Saved-network radio coordination. Never fetches a PSK or changes the saved
 * file during retries; successful interactive connect owns SAVE_CONFIG. */
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
static void save_band(int freq);
static cJSON* coordinate(void){
 if(access(PRIVATE "/enabled",F_OK))return result(0,"中继未启用");
 char list[2048],ssid_raw[256],ssid[33],key[64],freq[256],pin[64];int id=-1;
 if(!ctrl("LIST_NETWORKS",list,sizeof(list)))return result(0,"读取保存网络失败");
 char*line=strchr(list,'\n');if(!line)return result(0,"尚未保存上游");char*end;long n=strtol(line+1,&end,10);if(end==line+1||*end!='\t'||n<0||n>4096)return result(0,"保存网络无效");id=(int)n;
 if(!saved_value(id,"ssid",ssid_raw,sizeof(ssid_raw))||!saved_ssid(ssid_raw,ssid)||!saved_value(id,"key_mgmt",key,sizeof(key)))return result(0,"保存网络不可读");
 int chosen=0;if(saved_value(id,"freq_list",freq,sizeof(freq)))chosen=atoi(freq);
 int band=chosen?chosen<3000?2:5:0;
 if(!saved_value(id,"bssid",pin,sizeof(pin)))pin[0]=0;
 if(!fresh_scan())return result(0,"等待上游扫描");
 char scans[32768];if(!ctrl("SCAN_RESULTS",scans,sizeof(scans)))return result(0,"上游扫描不可读");
 cJSON*a=parse_scan(scans),*x,*best=NULL;int best_signal=-151;
 /* Older installations have no freq_list. Infer the band from the saved BSS
  * when visible; an interactive connection also saves a non-secret band marker. */
 if(!band){FILE*f=fopen(PRIVATE "/upstream-band","r");if(f){int v=0;if(fscanf(f,"%d",&v)==1&&(v==2||v==5))band=v;fclose(f);}}
 if(!band&&*pin)cJSON_ArrayForEach(x,a){if(!strcmp(str(x,"bssid"),pin)&&!strcmp(str(x,"ssid"),ssid)){band=cJSON_GetObjectItemCaseSensitive(x,"frequency")->valueint<3000?2:5;break;}}
 const char*sec=strstr(key,"SAE")?"WPA3":strstr(key,"WPA-PSK")?"WPA2":!strcmp(key,"NONE")?"OPEN":"unsupported";
 cJSON_ArrayForEach(x,a){int f=cJSON_GetObjectItemCaseSensitive(x,"frequency")->valueint,r=cJSON_GetObjectItemCaseSensitive(x,"signal")->valueint;
  if(!relay_frequency(f)||strcmp(str(x,"ssid"),ssid)||strcmp(str(x,"security"),sec)||(band&&band!=(f<3000?2:5)))continue;
  if(r>best_signal){best=x;best_signal=r;}
 }
 if(!best){cJSON_Delete(a);return result(0,"所选频段没有可用上游，使用原出口");}
 int f=cJSON_GetObjectItemCaseSensitive(best,"frequency")->valueint;char cmd[128];
 const char*stage="channel";int ok=ack("DISCONNECT")&&align_radio(f);
 if(ok){stage="bssid";snprintf(cmd,sizeof(cmd),"SET_NETWORK %d bssid %s",id,str(best,"bssid"));ok=ack(cmd);}
 if(ok){stage="frequency";snprintf(cmd,sizeof(cmd),"SET_NETWORK %d freq_list %d",id,f);ok=ack(cmd);}
 if(ok&&!access(PRIVATE "/enabled",F_OK)){stage="select";snprintf(cmd,sizeof(cmd),"SELECT_NETWORK %d",id);ok=ack(cmd);}else ok=0;
 if(ok){
  save_band(f);stage="association";ok=0;
  /* A fresh scan/SELECT during ASSOCIATING resets authentication on B28. */
  for(int attempt=0;attempt<CONNECT_TRIES&&!access(PRIVATE "/enabled",F_OK);attempt++){
   char status[2048],state[64],actual[32];
   if(ctrl("STATUS",status,sizeof(status))&&keyval(status,"wpa_state",state,sizeof(state))&&!strcmp(state,"COMPLETED")&&keyval(status,"freq",actual,sizeof(actual))&&atoi(actual)==f){ok=1;break;}
   sleep(1);
  }
 }
 cJSON_Delete(a);cJSON*r=result(ok,ok?"上游关联已完成":"信道协调未完成，使用原出口");cJSON_AddStringToObject(r,"stage",stage);cJSON_AddNumberToObject(r,"target_frequency",f);return r;
}
static void save_band(int freq){
 FILE*f=fopen(PRIVATE "/upstream-band.next","w");if(!f)return;
 int ok=fprintf(f,"%d\n",freq<3000?2:5)>0;if(fclose(f))ok=0;
 if(ok)rename(PRIVATE "/upstream-band.next",PRIVATE "/upstream-band");else unlink(PRIVATE "/upstream-band.next");
}
