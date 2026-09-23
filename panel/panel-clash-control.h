/* Mihomo control adapter. Secrets stay in process memory, never in argv or output. */
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/file.h>
#ifndef CC_ROOT
#define CC_ROOT "/data/u60-clash"
#endif
#ifndef CC_CONFIG
#define CC_CONFIG CC_ROOT "/config.yaml"
#endif
#ifndef CC_API_PORT
#define CC_API_PORT 19090
#endif
static char cc_secret[128];
static int cc_load_secret(void)
{
	char line[512];
	FILE *f = fopen(CC_CONFIG, "r");
	if (!f)
		return -1;
	while (fgets(line, sizeof(line), f)) {
		char *p = !strncmp(line, "secret:", 7) ? line : NULL;
		if (!p)
			continue;
		p += 7;
		while (*p == ' ' || *p == '"' || *p == '\'')
			p++;
		snprintf(cc_secret, sizeof(cc_secret), "%s", p);
		p = cc_secret + strlen(cc_secret);
		while (p > cc_secret && (p[-1] == '\n' || p[-1] == '\r' || p[-1] == '"' || p[-1] == '\'' || p[-1] == ' '))
			*--p = 0;
		fclose(f);
		return 0;
	}
	fclose(f);
	return -1;
}
static int cc_http_to(const char *method,const char *path,const char *body,char *out,int outsz,int timeout_sec)
{
 int fd=-1,result=-1,status=0;size_t used=0,sent=0;char *req=NULL,*wire=NULL;
 struct sockaddr_in addr={0};struct timeval tv={.tv_sec=timeout_sec<2?2:timeout_sec};
 if(out && outsz>0)out[0]=0;
 fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;
 setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
 addr.sin_family=AF_INET;addr.sin_port=htons(CC_API_PORT);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
 if(connect(fd,(struct sockaddr *)&addr,sizeof(addr))<0)goto done;
 if(asprintf(&req,"%s %s HTTP/1.0\r\nHost: 127.0.0.1\r\nAuthorization: Bearer %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",method,path,cc_secret,body?strlen(body):0,body?body:"")<0)goto done;
 size_t length=strlen(req);
 while(sent<length){ssize_t n=send(fd,req+sent,length-sent,0);if(n<0&&errno==EINTR)continue;if(n<=0)goto done;sent+=(size_t)n;}
 wire=malloc(524289);if(!wire)goto done;
 while(used<524288){ssize_t n=read(fd,wire+used,524288-used);if(n<0&&errno==EINTR)continue;if(n<0)goto done;if(!n)break;used+=(size_t)n;}
 if(used==524288)goto done;wire[used]=0;
 char *sep=strstr(wire,"\r\n\r\n");if(!sep || sscanf(wire,"HTTP/%*s %d",&status)!=1 || status<200 || status>=300)goto done;
 *sep=0;char *data=sep+4;size_t count=used-(size_t)(data-wire);
 if(strcasestr(wire,"Transfer-Encoding: chunked")){
  char *r=data,*w=data,*end=data+count;int complete=0;
  while(r<end){char *tail;unsigned long n=strtoul(r,&tail,16);if(tail==r)goto done;char *nl=strstr(tail,"\r\n");if(!nl)goto done;r=nl+2;if(!n){complete=1;break;}if(n>(unsigned long)(end-r)||end-r-(long)n<2)goto done;memmove(w,r,n);w+=n;r+=n;if(r[0]!='\r'||r[1]!='\n')goto done;r+=2;}
  if(!complete)goto done;count=(size_t)(w-data);
 }
 const char *cl=strcasestr(wire,"\r\nContent-Length:");if(cl && !strcasestr(wire,"Transfer-Encoding: chunked")){char *tail;unsigned long declared=strtoul(cl+17,&tail,10);if(tail==cl+17||declared!=count)goto done;}
 if(out && outsz>0){if(count>=(size_t)outsz)goto done;memcpy(out,data,count);out[count]=0;}
 result=(int)count;
 done:free(req);free(wire);close(fd);return result;
}
static int cc_http(const char *method, const char *path, const char *body, char *out, int outsz)
{
	return cc_http_to(method, path, body, out, outsz, 2);
}
static void cc_urlenc(const char *in, char *out, int outsz)
{
	static const char *hex = "0123456789ABCDEF";
	int o = 0;
	while (*in && o < outsz - 4) {
		unsigned char c = (unsigned char)*in++;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
			out[o++] = (char)c;
		else {
			out[o++] = '%';
			out[o++] = hex[c >> 4];
			out[o++] = hex[c & 15];
		}
	}
	out[o] = 0;
}

static cJSON *cc_get(const char *path) {
 char *buf=malloc(1048576); if(!buf)return NULL;
 if(!cc_secret[0])cc_load_secret();
 int n=cc_http("GET",path,NULL,buf,1048576);
 cJSON *j=n>0?cJSON_Parse(buf):NULL; free(buf); return j;
}
static double cc_num(const cJSON *j,const char *key) {
 const cJSON *v=jget(j,key);return cJSON_IsNumber(v)?v->valuedouble:0;
}
static int cc_write(const char *method,const char *path,const cJSON *j) {
 char *body=j?cJSON_PrintUnformatted(j):NULL;char resp[8192];
 if(!cc_secret[0])cc_load_secret();
 int n=cc_http_to(method,path,body,resp,sizeof(resp),8);free(body);return n>=0;
}
static cJSON *cc_item(cJSON *items,const char *id,const char *label,const char *value,const char *type,const char *action) {
 cJSON *i=cJSON_CreateObject();cJSON_AddStringToObject(i,"id",id);cJSON_AddStringToObject(i,"label",label);
 cJSON_AddStringToObject(i,"value",value?value:"");cJSON_AddStringToObject(i,"type",type);
 cJSON_AddStringToObject(i,"action",action?action:"");cJSON_AddBoolToObject(i,"enabled",1);cJSON_AddItemToArray(items,i);return i;
}
static void cc_arg(cJSON *i,const char *key,const char *value) {
 cJSON *a=jget(i,"args");if(!a){a=cJSON_CreateObject();cJSON_AddItemToObject(i,"args",a);}cJSON_AddStringToObject(a,key,value);
}
static cJSON *cc_choice(cJSON *choices,const char *label,const char *key,const char *value) {
 cJSON *c=cJSON_CreateObject(),*a=cJSON_CreateObject();cJSON_AddStringToObject(c,"label",label);cJSON_AddStringToObject(a,key,value);cJSON_AddItemToObject(c,"args",a);cJSON_AddItemToArray(choices,c);return c;
}
static cJSON *cc_section(cJSON *root,const char *id,const char *title) {
 cJSON *sections=jget(root,"sections");if(!sections){sections=cJSON_CreateArray();cJSON_AddItemToObject(root,"sections",sections);}
 cJSON *s=cJSON_CreateObject(),*items=cJSON_CreateArray();cJSON_AddStringToObject(s,"id",id);cJSON_AddStringToObject(s,"title",title);cJSON_AddItemToObject(s,"items",items);cJSON_AddItemToArray(sections,s);return items;
}
static cJSON *cc_field(cJSON *fields,const char *key,const char *label,const char *value,const char *kind) {
 cJSON *f=cJSON_CreateObject();cJSON_AddStringToObject(f,"key",key);cJSON_AddStringToObject(f,"label",label);cJSON_AddStringToObject(f,"value",value);cJSON_AddStringToObject(f,"kind",kind);cJSON_AddBoolToObject(f,"required",1);cJSON_AddItemToArray(fields,f);return f;
}
static void cc_option(cJSON *choices,const char *label,const char *value) {
 cJSON *o=cJSON_CreateObject();cJSON_AddStringToObject(o,"label",label);cJSON_AddStringToObject(o,"value",value);cJSON_AddItemToArray(choices,o);
}
static void cc_rule_fields(cJSON *item,const char *rule,cJSON *proxies) {
 char copy[512];snprintf(copy,sizeof(copy),"%s",rule?rule:"DOMAIN-SUFFIX,example.com,DIRECT");
 char *type=copy,*pattern=strchr(copy,','),*policy=NULL;
 if(pattern){*pattern++=0;policy=strchr(pattern,',');if(policy)*policy++=0;}
 if(!pattern||!policy)return;
 cJSON *fields=cJSON_CreateArray();cJSON_AddItemToObject(item,"fields",fields);
 cJSON *f=cc_field(fields,"type","规则类型",type,"choice"),*choices=cJSON_CreateArray();cJSON_AddItemToObject(f,"choices",choices);
 cc_option(choices,"域名后缀","DOMAIN-SUFFIX");cc_option(choices,"完整域名","DOMAIN");cc_option(choices,"IPv4 网段","IP-CIDR");cc_option(choices,"IPv6 网段","IP-CIDR6");
 cc_field(fields,"pattern","域名或网段",pattern,"text");
 f=cc_field(fields,"policy","处理方式",policy,"choice");choices=cJSON_CreateArray();cJSON_AddItemToObject(f,"choices",choices);
 cc_option(choices,"直连","DIRECT");cc_option(choices,"拒绝","REJECT");cJSON *p;
 cJSON_ArrayForEach(p,proxies)if(jget(p,"all")&&p->string)cc_option(choices,p->string,p->string);
}
/* Local rules are stored as a clearly marked block in the existing rules list.
 * The source configuration is never copied to stdout or a workstation. */
static char *cc_readfile(const char *path) {
 FILE *f=fopen(path,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
 if(n<0||n>2097152){fclose(f);return NULL;}char *s=calloc((size_t)n+1,1);if(s && fread(s,1,(size_t)n,f)!=(size_t)n){free(s);s=NULL;}fclose(f);return s;
}
static int cc_atomic(const char *path,const char *s) {
 char tmp[256];snprintf(tmp,sizeof(tmp),"%s.panel-%ld",path,(long)getpid());
 int fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return 0;size_t n=strlen(s),done=0;
 while(done<n){ssize_t w=write(fd,s+done,n-done);if(w<0&&errno==EINTR)continue;if(w<=0)break;done+=(size_t)w;}
 int ok=done==n && fsync(fd)==0;close(fd);if(ok)ok=rename(tmp,path)==0;if(!ok)unlink(tmp);return ok;
}
static cJSON *cc_local_rules(void) {
 cJSON *a=cJSON_CreateArray();char *config=cc_readfile(CC_CONFIG);if(!config)return a;
 char *begin=strstr(config,"  # U60-PANEL-RULES-BEGIN\n"),*end=begin?strstr(begin,"  # U60-PANEL-RULES-END"):NULL;
 if(begin&&end){char *save,*ln=strtok_r(begin,"\n",&save);while(ln&&ln<end){int enabled=!strncmp(ln,"  - ",4);int disabled=!strncmp(ln,"  # off - ",10);if(enabled||disabled){const char *p=ln+(enabled?4:10);cJSON *r=cJSON_CreateObject();cJSON_AddStringToObject(r,"rule",p);cJSON_AddBoolToObject(r,"enabled",enabled);cJSON_AddItemToArray(a,r);}ln=strtok_r(NULL,"\n",&save);}}
 free(config);return a;
}
static int cc_persist_mode(const char *mode) {
 char *old=cc_readfile(CC_CONFIG);if(!old)return 0;
 char *line=!strncmp(old,"mode:",5)?old:strstr(old,"\nmode:");if(!line){free(old);return 0;}if(*line=='\n')line++;
 char *end=strchr(line,'\n');if(!end)end=old+strlen(old);
 size_t cap=strlen(old)+40;char *next=malloc(cap);if(!next){free(old);return 0;}
 size_t prefix=(size_t)(line-old);memcpy(next,old,prefix);snprintf(next+prefix,cap-prefix,"mode: %s%s",mode,end);
 int ok=cc_atomic(CC_CONFIG,next);char value[24];snprintf(value,sizeof(value),"%s\n",mode);
 if(ok&&!cc_atomic(CC_ROOT "/mode",value)){cc_atomic(CC_CONFIG,old);ok=0;}free(next);free(old);return ok;
}
static int cc_reload(void) {
 cJSON *b=cJSON_CreateObject();cJSON_AddStringToObject(b,"path",CC_CONFIG);int ok=cc_write("PUT","/configs?force=true",b);cJSON_Delete(b);return ok;
}
static int cc_save_rules(cJSON *rules) {
 char *old=cc_readfile(CC_CONFIG);if(!old)return 0;
 char *rules_start=!strncmp(old,"rules:\n",7)?old:strstr(old,"\nrules:\n");if(!rules_start){free(old);return 0;}if(*rules_start=='\n')rules_start++;
 char *insert=rules_start+7,*begin=strstr(insert,"  # U60-PANEL-RULES-BEGIN\n"),*end=begin?strstr(begin,"  # U60-PANEL-RULES-END\n"):NULL;
 if((begin&&!end)||(!begin&&end)){free(old);return 0;}
 char *tail=insert;if(begin){if(begin!=insert){free(old);return 0;}tail=end+strlen("  # U60-PANEL-RULES-END\n");}
 size_t cap=strlen(old)+cJSON_GetArraySize(rules)*600+128;char *next=calloc(cap,1);if(!next){free(old);return 0;}
 size_t used=(size_t)(insert-old);memcpy(next,old,used);used+=snprintf(next+used,cap-used,"  # U60-PANEL-RULES-BEGIN\n");
 cJSON *r;cJSON_ArrayForEach(r,rules)used+=snprintf(next+used,cap-used,"  %s%s\n",cJSON_IsTrue(jget(r,"enabled"))?"- ":"# off - ",jstr(r,"rule"));
 snprintf(next+used,cap-used,"  # U60-PANEL-RULES-END\n%s",tail);
 const char *tmp=CC_ROOT "/.panel-check.yaml";int ok=cc_atomic(tmp,next);
 char *argv[]={CC_ROOT "/mihomo","-t","-d",CC_ROOT,"-f",(char*)tmp,NULL};
 /* Validation success is the exit status, not the length of progress output.
  * Drain without retaining logs, which may contain subscription details. */
 if(ok)ok=run_cmd(argv[0],argv,NULL,NULL,1);
 if(ok)ok=cc_atomic(CC_CONFIG,next);
 if(ok&&!cc_reload()){cc_atomic(CC_CONFIG,old);cc_reload();ok=0;}
 unlink(tmp);free(next);free(old);return ok;
}
static int cc_safe_token(const char *s,size_t max) {
 if(!s||!*s||strlen(s)>max)return 0;for(const unsigned char*p=(const unsigned char*)s;*p;p++)if(*p<32||*p==','||*p=='#'||*p=='\''||*p=='"'||*p=='\\')return 0;return 1;
}
#include "panel-clash-quota.h"
/* Provider leaves can be absent from the top-level /proxies object. */
static const cJSON *cc_provider_leaf(const cJSON *providers,const char *name) {
 const cJSON *provider,*leaf;cJSON_ArrayForEach(provider,jget(providers,"providers")){
  const cJSON *nodes=jget(provider,"proxies");if(!cJSON_IsArray(nodes))continue;
  cJSON_ArrayForEach(leaf,nodes)if(!strcmp(jstr(leaf,"name"),name)&&!jget(leaf,"now")&&!jget(leaf,"all"))return leaf;
 }
 return NULL;
}
/* Resolve the active/default policy, never an unrelated group's array order.
 * A valid chain visits at most all top-level objects plus one provider leaf. */
static const char *cc_resolve_node(const cJSON *proxies,const cJSON *providers,const char *name) {
 if(!cJSON_IsObject(proxies)||!name||!*name)return NULL;
 int remaining=cJSON_GetArraySize(proxies)+1;
 while(remaining--&&*name){
  if(!strcmp(name,"DIRECT")||!strcmp(name,"REJECT")||!strcmp(name,"REJECT-DROP"))return name;
  const cJSON *p=jget(proxies,name);if(!p)p=cc_provider_leaf(providers,name);
  const cJSON *next=jget(p,"now");if(!cJSON_IsObject(p))return NULL;
  if(next){if(!cJSON_IsString(next)||!next->valuestring[0])return NULL;name=next->valuestring;continue;}
  const char *type=jstr(p,"type");
  if(jget(p,"all")||!*type||!strcmp(type,"Selector")||!strcmp(type,"URLTest")||!strcmp(type,"Fallback")||!strcmp(type,"LoadBalance")||!strcmp(type,"Relay"))return NULL;
  return name;
 }
 return NULL;
}
static const char *cc_active_node(const cJSON *cfg,const cJSON *proxies,const cJSON *providers,const cJSON *rules) {
 const char *mode=jstr(cfg,"mode");
 if(!strcmp(mode,"direct"))return "DIRECT";
 if(!strcmp(mode,"global"))return cc_resolve_node(proxies,providers,"GLOBAL");
 if(!strcmp(mode,"rule")&&cJSON_IsArray(jget(rules,"rules"))){
  const cJSON *r;cJSON_ArrayForEach(r,jget(rules,"rules"))if(!strcasecmp(jstr(r,"type"),"Match"))return cc_resolve_node(proxies,providers,jstr(r,"proxy"));
 }
 return NULL;
}
/* Only expose the active mode's selectable root; keep rule policies intact. */
static int cc_builtin(const char *name) {
 return !strcmp(name,"DIRECT")||!strcmp(name,"REJECT")||!strcmp(name,"REJECT-DROP")||!strcmp(name,"PASS")||!strcmp(name,"PASS-RULE")||!strcmp(name,"COMPATIBLE");
}
static const char *cc_node_group(const cJSON *cfg,const cJSON *proxies,const cJSON *rules) {
 const char *root="";const cJSON *r;
 if(!strcmp(jstr(cfg,"mode"),"global"))root="GLOBAL";
 else if(!strcmp(jstr(cfg,"mode"),"rule"))cJSON_ArrayForEach(r,jget(rules,"rules"))if(!strcasecmp(jstr(r,"type"),"Match")){root=jstr(r,"proxy");break;}
 for(int depth=0;depth<16&&*root;depth++){
  const cJSON *group=jget(proxies,root);if(strcmp(jstr(group,"type"),"Selector")||!cJSON_IsArray(jget(group,"all")))return "";
  const char *selected=jstr(group,"now");const cJSON *nested=jget(proxies,selected);
  if(strcmp(jstr(nested,"type"),"Selector")||!cJSON_IsArray(jget(nested,"all")))return root;
  root=selected;
 }
 return *root?"":"";
}
static void control_clash_sections(cJSON *root) {
 cJSON *items=cc_section(root,"clash","Clash"),*data=jget(root,"data"),*state=cJSON_CreateObject();
 if(!data){data=cJSON_CreateObject();cJSON_AddItemToObject(root,"data",data);}cJSON_AddItemToObject(data,"clash",state);
 cJSON *cfg=cc_get("/configs"),*all=cc_get("/proxies"),*proxies=jget(all,"proxies");int online=cfg&&proxies;
 cJSON_AddBoolToObject(state,"online",online);cJSON_AddStringToObject(state,"mode",jstr(cfg,"mode"));
 cJSON *providers=cc_get("/providers/proxies");
 cJSON *live_rules=!strcmp(jstr(cfg,"mode"),"rule")?cc_get("/rules"):NULL;const char *node=cc_active_node(cfg,proxies,providers,live_rules);
 /* Explicit unknown prevents the UI from reviving a legacy cached node. */
 cJSON_AddStringToObject(state,"node",node?node:"未知");
 int active=online&&!strcmp(jstr(data,"network_profile"),"clash");
 cJSON *i=cc_item(items,"service","代理开关",active?"开启":online?"未接管上网":"关闭","choice","clash.service"),*choices=cJSON_AddArrayToObject(i,"choices");
 cc_choice(choices,"开启代理","operation","start");cc_choice(choices,"关闭代理 · 直连","operation","stop");cJSON_AddBoolToObject(i,"confirm",1);
 i=cc_item(items,"mode","分流模式",!cfg?"服务未启动":!strcmp(jstr(cfg,"mode"),"rule")?"规则分流":!strcmp(jstr(cfg,"mode"),"global")?"全局代理":"旧直连模式 · 请选择","choice","clash.mode");choices=cJSON_AddArrayToObject(i,"choices");
 cc_choice(choices,"规则分流","mode","rule");cc_choice(choices,"全局代理","mode","global");cJSON_ReplaceItemInObject(i,"enabled",cJSON_CreateBool(online));
 const char *group=cc_node_group(cfg,proxies,live_rules);cJSON *g=jget(proxies,group),*p;
 i=cc_item(items,"node","专线节点",node?node:"服务未启动或策略未知","choice","clash.select");cc_arg(i,"group",group);choices=cJSON_AddArrayToObject(i,"choices");
 cJSON *n;cJSON_ArrayForEach(n,jget(g,"all"))if(cJSON_IsString(n)&&!cc_builtin(n->valuestring))cc_choice(choices,n->valuestring,"name",n->valuestring);
 cJSON_ReplaceItemInObject(i,"enabled",cJSON_CreateBool(!strcmp(jstr(g,"type"),"Selector")&&cJSON_GetArraySize(choices)>0));
 cJSON_Delete(live_rules);
 cc_item(items,"scope","代理范围","IPv4 TCP 与 DNS；其他流量不保证代理","info",NULL);
 i=cc_item(items,"delay","节点测速","选择专线测速","choice","clash.delay");choices=cJSON_AddArrayToObject(i,"choices");
 /* Use the same choices as the node selector, never its parent group. */
 cJSON_ArrayForEach(n,jget(g,"all"))if(cJSON_IsString(n)&&!cc_builtin(n->valuestring)){
  int exists=0;cJSON *v;cJSON_ArrayForEach(v,choices)if(!strcmp(jstr(jget(v,"args"),"name"),n->valuestring))exists=1;
  if(!exists)cc_choice(choices,n->valuestring,"name",n->valuestring);
 }
 cJSON_ReplaceItemInObject(i,"enabled",cJSON_CreateBool(cJSON_GetArraySize(choices)>0));
 double quota_total=0,quota_remaining=0;int quota_known=0;
 cJSON_ArrayForEach(p,jget(providers,"providers")){if(!p->string||!strcmp(jstr(p,"vehicleType"),"Compatible"))continue;
  i=cc_item(items,p->string,"刷新订阅",p->string,"action","clash.provider");cc_arg(i,"name",p->string);
  cJSON *q=jget(p,"subscriptionInfo");if(cc_num(q,"Total")>0){quota_known=1;double total=cc_num(q,"Total"),used=cc_num(q,"Upload")+cc_num(q,"Download");quota_total+=total;quota_remaining+=total>used?total-used:0;}
 }
 if(!quota_known){cJSON *q=cc_quota();if(cc_num(q,"total")>0){quota_known=1;quota_total=cc_num(q,"total");quota_remaining=cc_num(q,"remaining");cJSON_AddNumberToObject(state,"quota_expire",cc_num(q,"expire"));}cJSON_Delete(q);}if(quota_known){cJSON_AddNumberToObject(state,"quota_remaining",quota_remaining);cJSON_AddNumberToObject(state,"quota_total",quota_total);}
 cJSON *rp=cc_get("/providers/rules");
 i=cc_item(items,"rule-providers","规则集","选择需要更新的规则","choice","clash.rule_provider");choices=cJSON_AddArrayToObject(i,"choices");
 cJSON_ArrayForEach(p,jget(rp,"providers")){if(!p->string)continue;const char *label=!strcmp(p->string,"u60-cn-domain")?"国内域名 · 直连":!strcmp(p->string,"u60-cn-ip")?"国内 IP · 直连":!strcmp(p->string,"u60-foreign-domain")?"国外域名 · 代理":p->string;cc_choice(choices,label,"name",p->string);}
 cJSON_ReplaceItemInObject(i,"enabled",cJSON_CreateBool(cJSON_GetArraySize(choices)>0));
 cJSON *con=cc_get("/connections");if(con){cJSON_AddNumberToObject(state,"upload",cc_num(con,"uploadTotal"));cJSON_AddNumberToObject(state,"download",cc_num(con,"downloadTotal"));}cJSON_AddNumberToObject(state,"connections",cJSON_GetArraySize(jget(con,"connections")));
 char val[100];snprintf(val,sizeof(val),"%d 个连接 · 点按清理",cJSON_GetArraySize(jget(con,"connections")));i=cc_item(items,"connections","当前连接",val,"action","clash.close_connections");cJSON_AddBoolToObject(i,"confirm",1);
 cc_item(items,"dns","清理 DNS 缓存","下次请求重新解析","action","clash.flush_dns");
 i=cc_item(items,"add-rule","添加分流规则","域名 / IP → 直连、代理或拒绝","form","clash.rule_add");cc_rule_fields(i,NULL,proxies);
 cJSON *rules=cc_local_rules();int index=0;cJSON *r;
 cJSON_ArrayForEach(r,rules){char id[48];snprintf(id,sizeof(id),"local-rule-%d",index);i=cc_item(items,id,cJSON_IsTrue(jget(r,"enabled"))?"本地规则 · 已启用":"本地规则 · 已停用",jstr(r,"rule"),"form","clash.rule_edit");cJSON *args=cJSON_CreateObject();cJSON_AddNumberToObject(args,"index",index);cJSON_AddStringToObject(args,"expected_rule",jstr(r,"rule"));cJSON_AddItemToObject(i,"args",args);cc_rule_fields(i,jstr(r,"rule"),proxies);
  cJSON *fields=jget(i,"fields"),*f=cc_field(fields,"operation","操作","save","choice");choices=cJSON_CreateArray();cJSON_AddItemToObject(f,"choices",choices);cc_option(choices,"保存修改","save");cc_option(choices,cJSON_IsTrue(jget(r,"enabled"))?"停用规则":"启用规则","toggle");cc_option(choices,"向前移动","up");cc_option(choices,"向后移动","down");cc_option(choices,"删除规则","delete");index++;
 }
 cJSON_Delete(rules);cJSON_Delete(con);cJSON_Delete(rp);cJSON_Delete(providers);cJSON_Delete(all);cJSON_Delete(cfg);
}
static cJSON *control_clash_action(const char *action,const cJSON *args) {
 if(strncmp(action,"clash.",6))return NULL;
 if(!cc_secret[0])cc_load_secret();
 int ok=0;char path[2400],enc[1800];cJSON *j=NULL,*result=NULL;
 if(!strcmp(action,"clash.mode")){
  const char *mode=jstr(args,"mode");if(strcmp(mode,"rule")&&strcmp(mode,"global")&&strcmp(mode,"direct"))return reply(0,"无效的代理模式");
  j=cc_get("/configs");char previous[24];snprintf(previous,sizeof(previous),"%s",jstr(j,"mode"));cJSON_Delete(j);if(strcmp(previous,"rule")&&strcmp(previous,"global")&&strcmp(previous,"direct"))return reply(0,"无法读取当前模式，未修改");
  j=cJSON_CreateObject();cJSON_AddStringToObject(j,"mode",mode);ok=cc_write("PATCH","/configs",j);cJSON_Delete(j);j=cc_get("/configs");ok=ok&&!strcmp(jstr(j,"mode"),mode);cJSON_Delete(j);
  if(ok&&!cc_persist_mode(mode)){j=cJSON_CreateObject();cJSON_AddStringToObject(j,"mode",previous);int restored=cc_write("PATCH","/configs",j);cJSON_Delete(j);j=cc_get("/configs");restored=restored&&!strcmp(jstr(j,"mode"),previous);cJSON_Delete(j);return reply(0,restored?"模式保存失败，已恢复原运行模式":"模式保存失败，运行模式回退未确认，请刷新核对");}
 }else if(!strcmp(action,"clash.select")){
  const char *group=jstr(args,"group"),*name=jstr(args,"name");if(!*group||strlen(group)>512||strlen(name)>512)return reply(0,"节点名称无效");
  cc_urlenc(group,enc,sizeof(enc));snprintf(path,sizeof(path),"/proxies/%s",enc);j=cc_get(path);cJSON *n;int found=0;cJSON_ArrayForEach(n,jget(j,"all"))if(cJSON_IsString(n)&&!strcmp(n->valuestring,name))found=1;
  int selector=!strcmp(jstr(j,"type"),"Selector");cJSON_Delete(j);if(!found||!selector)return reply(0,"节点已变化，请刷新后重试");
  j=cJSON_CreateObject();cJSON_AddStringToObject(j,"name",name);ok=cc_write("PUT",path,j);cJSON_Delete(j);j=cc_get(path);ok=ok&&!strcmp(jstr(j,"now"),name);cJSON_Delete(j);
 }else if(!strcmp(action,"clash.delay")){
  const char *name=jstr(args,"name");if(!*name||strlen(name)>512)return reply(0,"请选择节点");cc_urlenc(name,enc,sizeof(enc));snprintf(path,sizeof(path),"/proxies/%s/delay?timeout=5000&url=https%%3A%%2F%%2Fwww.gstatic.com%%2Fgenerate_204",enc);char buf[512];int n=cc_http_to("GET",path,NULL,buf,sizeof(buf),7);j=n>0?cJSON_Parse(buf):NULL;int ms=(int)cc_num(j,"delay");cJSON_Delete(j);char msg[80];snprintf(msg,sizeof(msg),ms>0?"延迟 %d ms":"测速失败或超时",ms);return reply(ms>0,msg);
 }else if(!strcmp(action,"clash.provider")||!strcmp(action,"clash.rule_provider")){
  const char *name=jstr(args,"name");if(!*name||strlen(name)>512)return reply(0,"订阅名称无效");cc_urlenc(name,enc,sizeof(enc));snprintf(path,sizeof(path),"/providers/%s/%s",!strcmp(action,"clash.provider")?"proxies":"rules",enc);ok=cc_write("PUT",path,NULL);if(ok){j=cc_get(path);ok=j!=NULL;cJSON_Delete(j);}
 }else if(!strcmp(action,"clash.close_connections")){
  ok=cc_write("DELETE","/connections",NULL);
 }else if(!strcmp(action,"clash.flush_dns")){
  ok=cc_write("POST","/cache/dns/flush",NULL);
 }else if(!strcmp(action,"clash.service")){
  const char *op=jstr(args,"operation");if(strcmp(op,"start")&&strcmp(op,"stop"))return reply(0,"操作无效");char *argv[]={"/data/u60-panel/network-profile.sh",!strcmp(op,"start")?"clash-enable":"clash-stop",NULL};char out[4096];ok=run_cmd(argv[0],argv,NULL,out,sizeof(out));cJSON*receipt=cJSON_Parse(out);if(!ok||!cJSON_IsTrue(jget(receipt,"ok"))){if(cJSON_IsObject(receipt)&&cJSON_IsFalse(jget(receipt,"ok"))){cJSON_AddStringToObject(receipt,"message",cJSON_IsTrue(jget(receipt,"fail_closed"))?"服务操作失败，已阻断公网转发；请检查出口":"代理服务操作失败，请检查出口状态");return receipt;}cJSON_Delete(receipt);return reply(0,"代理服务操作失败，请检查出口状态");}cJSON_Delete(receipt);
  j=cc_get("/version");ok=ok&&(!strcmp(op,"start")?j!=NULL:j==NULL);cJSON_Delete(j);
 }else if(!strcmp(action,"clash.rule_add")||!strcmp(action,"clash.rule_edit")){
  cJSON *rules=cc_local_rules();int count=cJSON_GetArraySize(rules),idx=(int)cc_num(args,"index");int edit=!strcmp(action,"clash.rule_edit");const char *op=jstr(args,"operation");
  if(count>=100&&!edit){cJSON_Delete(rules);return reply(0,"最多保存 100 条本地规则");}
  if(edit&&(!cJSON_IsNumber(jget(args,"index"))||idx<0||idx>=count||strcmp(jstr(args,"expected_rule"),jstr(cJSON_GetArrayItem(rules,idx),"rule")))){cJSON_Delete(rules);return reply(0,"规则已变化，请刷新");}
  if(edit&&!strcmp(op,"delete"))cJSON_DeleteItemFromArray(rules,idx);
  else if(edit&&!strcmp(op,"toggle")){j=cJSON_GetArrayItem(rules,idx);int enabled=cJSON_IsTrue(jget(j,"enabled"));cJSON_ReplaceItemInObject(j,"enabled",cJSON_CreateBool(!enabled));}
  else if(edit&&(!strcmp(op,"up")||!strcmp(op,"down"))){int dst=idx+(!strcmp(op,"up")?-1:1);if(dst>=0&&dst<count){j=cJSON_DetachItemFromArray(rules,idx);cJSON_InsertItemInArray(rules,dst,j);}}
  else {
   const char *type=jstr(args,"type"),*pattern=jstr(args,"pattern"),*policy=jstr(args,"policy");
   if((strcmp(type,"DOMAIN")&&strcmp(type,"DOMAIN-SUFFIX")&&strcmp(type,"IP-CIDR")&&strcmp(type,"IP-CIDR6"))||!cc_safe_token(pattern,240)||!cc_safe_token(policy,160)){cJSON_Delete(rules);return reply(0,"规则格式无效");}
   char line[512];snprintf(line,sizeof(line),"%s,%s,%s",type,pattern,policy);j=cJSON_CreateObject();cJSON_AddStringToObject(j,"rule",line);cJSON_AddBoolToObject(j,"enabled",edit?cJSON_IsTrue(jget(cJSON_GetArrayItem(rules,idx),"enabled")):1);
   if(edit)cJSON_ReplaceItemInArray(rules,idx,j);else cJSON_AddItemToArray(rules,j);
  }
  ok=cc_save_rules(rules);cJSON_Delete(rules);
 }else return reply(0,"暂不支持该代理操作");
 result=reply(ok,ok?"已应用并检查":"操作未完成，请检查服务状态");return result;
}
