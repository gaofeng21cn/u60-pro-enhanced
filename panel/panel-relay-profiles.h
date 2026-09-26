/* The supplicant file is the only credential store. Public projections contain
 * identities and preferences only; raw blocks never enter a reply or log. */
#define PROFILE_MAX 8
#define PROFILE_BYTES 32768
struct relay_profile {char id[80],ssid[33],security[8];int band,priority;size_t begin,end;};
struct relay_profiles {char text[PROFILE_BYTES+1];size_t size;int count;struct relay_profile entries[PROFILE_MAX];};
static void profile_identity(struct relay_profile*p){
 size_t n=strlen(p->ssid);for(size_t i=0;i<n;i++)snprintf(p->id+i*2,3,"%02x",(unsigned char)p->ssid[i]);
 snprintf(p->id+n*2,sizeof(p->id)-n*2,"-%s-%d",p->security,p->band);
}
static int profile_field(const char*block,const char*key,char*out,size_t cap){
 for(const char*p=block;*p;){const char*e=strchr(p,'\n');if(!e)e=p+strlen(p);while(p<e&&(*p==' '||*p=='\t'))p++;
  size_t k=strlen(key);if((size_t)(e-p)>k&&p[k]=='='&&!strncmp(p,key,k)){size_t n=e-p-k-1;while(n&&(p[k+n]=='\r'||p[k+n]==' '||p[k+n]=='\t'))n--;if(n>=cap)return 0;memcpy(out,p+k+1,n);out[n]=0;return 1;}p=*e?e+1:e;
 }out[0]=0;return 0;
}
static int profiles_parse(struct relay_profiles*s){
 s->count=0;int opened=0;size_t begin=0;
 for(size_t at=0;at<s->size;){size_t end=at;while(end<s->size&&s->text[end]!='\n')end++;size_t next=end<s->size?end+1:end;
  size_t first=at,last=end;while(first<last&&(s->text[first]==' '||s->text[first]=='\t'))first++;while(last>first&&(s->text[last-1]=='\r'||s->text[last-1]==' '||s->text[last-1]=='\t'))last--;
  if(last-first==9&&!memcmp(s->text+first,"network={",9)){if(opened||s->count>=PROFILE_MAX)return 0;opened=1;begin=at;}
  else if(opened&&last-first==1&&s->text[first]=='}'){
   struct relay_profile*p=&s->entries[s->count];memset(p,0,sizeof(*p));p->begin=begin;p->end=next;
   char block[PROFILE_BYTES+1],value[256];size_t n=next-begin;memcpy(block,s->text+begin,n);block[n]=0;
   int ok=profile_field(block,"ssid",value,sizeof(value))&&saved_ssid(value,p->ssid);
   if(!profile_field(block,"key_mgmt",value,sizeof(value)))snprintf(value,sizeof(value),"WPA-PSK");
   snprintf(p->security,sizeof(p->security),"%s",strstr(value,"SAE")?"WPA3":strstr(value,"WPA-PSK")?"WPA2":!strcmp(value,"NONE")?"OPEN":"UNKNOWN");
   if(profile_field(block,"id_str",value,sizeof(value))){if(!strcmp(value,"\"u60-band-2\""))p->band=2;else if(!strcmp(value,"\"u60-band-5\""))p->band=5;}
   if(!p->band&&profile_field(block,"freq_list",value,sizeof(value))){int f=atoi(value);p->band=f>0?(f<3000?2:5):0;}
   if(profile_field(block,"priority",value,sizeof(value))){char*tail;long rank=strtol(value,&tail,10);if(*tail||rank<0||rank>1000)ok=0;else p->priority=(int)rank;}
   memset(block,0,sizeof(block));if(!ok||!strcmp(p->security,"UNKNOWN"))return 0;profile_identity(p);
   for(int i=0;i<s->count;i++)if(!strcmp(s->entries[i].id,p->id))return 0;
   s->count++;opened=0;
  }
  at=next;
 }return !opened;
}
static int profiles_load(struct relay_profiles*s){
 memset(s,0,sizeof(*s));int fd=open(PRIVATE "/wpa.conf",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return errno==ENOENT;
 struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size>PROFILE_BYTES){close(fd);return 0;}
 ssize_t n=read(fd,s->text,PROFILE_BYTES+1);close(fd);if(n<0||n>PROFILE_BYTES)return 0;s->size=(size_t)n;s->text[n]=0;if(!profiles_parse(s))return 0;
 if(s->count==1&&!s->entries[0].band){FILE*f=fopen(PRIVATE "/upstream-band","r");if(f){int b=0;if(fscanf(f,"%d",&b)==1&&(b==2||b==5)){s->entries[0].band=b;profile_identity(&s->entries[0]);}fclose(f);}}
 return 1;
}
static int profiles_write(const char*data,size_t size){
 char path[256];snprintf(path,sizeof(path),PRIVATE "/wpa.conf.next-%ld",(long)getpid());
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return 0;
 size_t at=0;while(at<size){ssize_t n=write(fd,data+at,size-at);if(n<=0){close(fd);unlink(path);return 0;}at+=(size_t)n;}
 int ok=!fsync(fd);if(close(fd))ok=0;if(ok)ok=!rename(path,PRIVATE "/wpa.conf");if(!ok)unlink(path);return ok;
}
static int profile_append(char*out,size_t*used,const char*s,size_t n){if(*used+n>PROFILE_BYTES)return 0;memcpy(out+*used,s,n);*used+=n;out[*used]=0;return 1;}
static int profile_block(char*out,size_t*used,const struct relay_profiles*s,int index,int rank){
 const struct relay_profile*p=&s->entries[index];size_t at=p->begin;int has_band=0;
 while(at<p->end){size_t end=at;while(end<p->end&&s->text[end]!='\n')end++;size_t next=end<p->end?end+1:end;size_t first=at;while(first<end&&(s->text[first]==' '||s->text[first]=='\t'))first++;
  if(!strncmp(s->text+first,"id_str=",7))has_band=1;
  if(rank>=0&&!strncmp(s->text+first,"priority=",9)){}
  else {if(!has_band&&p->band&&s->text[first]=='}'){char b[40];int n=snprintf(b,sizeof(b),"\tid_str=\"u60-band-%d\"\n",p->band);if(!profile_append(out,used,b,n))return 0;}
   if(rank>=0&&s->text[first]=='}'){char b[40];int n=snprintf(b,sizeof(b),"\tpriority=%d\n",rank);if(!profile_append(out,used,b,n))return 0;}
   if(!profile_append(out,used,s->text+at,next-at))return 0;}
  at=next;
 }return 1;
}
static cJSON*profiles_public(void){
 struct relay_profiles*s=calloc(1,sizeof(*s));if(!s)return result(0,"内存不足");
 if(!profiles_load(s)){memset(s,0,sizeof(*s));free(s);return result(0,"保存网络不可读；未修改凭据");}
 cJSON*r=result(1,"已读取保存网络"),*a=cJSON_AddArrayToObject(r,"networks");
 int order[PROFILE_MAX];for(int i=0;i<s->count;i++){int j=i;while(j>0&&s->entries[order[j-1]].priority<s->entries[i].priority){order[j]=order[j-1];j--;}order[j]=i;}
 for(int i=0;i<s->count;i++){struct relay_profile*p=&s->entries[order[i]];cJSON*x=cJSON_CreateObject();cJSON_AddStringToObject(x,"id",p->id);cJSON_AddStringToObject(x,"ssid",p->ssid);cJSON_AddStringToObject(x,"security",p->security);cJSON_AddNumberToObject(x,"band",p->band);cJSON_AddNumberToObject(x,"priority",p->priority);cJSON_AddItemToArray(a,x);}
 memset(s,0,sizeof(*s));free(s);return r;
}
static int profiles_merge(const struct relay_profiles*old,const char*new_config){
 struct relay_profiles*added=calloc(1,sizeof(*added));char*out=calloc(1,PROFILE_BYTES+1);size_t used=0;int ok=added&&out&&strlen(new_config)<=PROFILE_BYTES;
 if(ok){added->size=strlen(new_config);memcpy(added->text,new_config,added->size+1);ok=profiles_parse(added)&&added->count==1;}
 if(ok){struct relay_profile*n=&added->entries[0];int same=-1;for(int i=0;i<old->count;i++)if(!strcmp(old->entries[i].id,n->id))same=i;
  if(old->count>=PROFILE_MAX&&same<0)ok=0;
  if(ok){size_t head=old->count?old->entries[0].begin:added->entries[0].begin;ok=profile_append(out,&used,old->count?old->text:added->text,head);
   for(int i=0;i<old->count&&ok;i++)if(i!=same)ok=profile_block(out,&used,old,i,-1);
   if(ok)ok=profile_block(out,&used,added,0,same>=0?old->entries[same].priority:0);
   if(ok)ok=profiles_write(out,used);
  }
 }
 if(added){memset(added,0,sizeof(*added));free(added);}if(out){memset(out,0,PROFILE_BYTES+1);free(out);}return ok;
}
static int selected_profile(const char*id){
 int fd=open(RUN "/selected-profile",O_CREAT|O_TRUNC|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return 0;int ok=write(fd,id,strlen(id))==(ssize_t)strlen(id);if(close(fd))ok=0;return ok;
}
static cJSON*profile_action(const char*command,const cJSON*a){
 struct relay_profiles*s=calloc(1,sizeof(*s));if(!s)return result(0,"内存不足");
 int ok=profiles_load(s),index=-1;for(int i=0;ok&&i<s->count;i++)if(!strcmp(s->entries[i].id,str(a,"id")))index=i;
 cJSON*r=NULL;if(!ok||index<0){r=result(0,"保存网络已变化，请重新打开列表");goto end;}
 if(!strcmp(command,"profile-connect")){
  int was_enabled=!access(PRIVATE "/enabled",F_OK);
  ok=helper("pause")&&selected_profile(s->entries[index].id)&&helper("on");
  if(!ok){unlink(RUN "/selected-profile");if(was_enabled)helper("on");}
  r=result(ok,ok?"正在连接所选网络；失败后按优先级尝试其他网络":"连接请求未完成，请查看中继状态");goto end;
 }
 if(!strcmp(command,"profile-prefer")&&s->entries[index].priority==1000){
  int highest=1;for(int i=0;i<s->count;i++)if(i!=index&&s->entries[i].priority>=1000)highest=0;
  if(highest){r=result(1,"已是首选；当前连接保持不变");goto end;}
 }
 if(!strcmp(command,"profile-forget")&&!access(PRIVATE "/enabled",F_OK)){r=result(0,"请先停止中继再忘记网络");goto end;}
 char*out=calloc(1,PROFILE_BYTES+1);size_t used=0;if(!out){r=result(0,"内存不足");goto end;}
 ok=profile_append(out,&used,s->text,s->entries[0].begin);
 for(int i=0;i<s->count&&ok;i++){
  if(!strcmp(command,"profile-forget")&&i==index)continue;
  int rank=!strcmp(command,"profile-prefer")?(i==index?1000:s->entries[i].priority>0?s->entries[i].priority-1:0):-1;
  ok=profile_block(out,&used,s,i,rank);
 }
 if(ok)ok=profiles_write(out,used);memset(out,0,PROFILE_BYTES+1);free(out);
 r=result(ok,ok?(!strcmp(command,"profile-prefer")?"已设为首选；当前连接保持不变":"已忘记所选网络，其他网络保留"):"保存失败，原网络保持不变");
 end:memset(s,0,sizeof(*s));free(s);return r;
}
