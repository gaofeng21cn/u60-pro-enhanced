#ifndef PANEL_DIAGNOSTICS_H
#define PANEL_DIAGNOSTICS_H
#include <ifaddrs.h>
#include <math.h>
#include <net/if.h>
#include <netdb.h>
#ifndef DIAG_GET
#define DIAG_GET cc_get
#endif
static cJSON *diag_report(const char *title){cJSON*r=reply(1,"诊断完成");cJSON*p=cJSON_AddObjectToObject(r,"report");cJSON_AddStringToObject(p,"title",title);cJSON_AddArrayToObject(p,"lines");return r;}
static void diag_line(cJSON*r,const char*text){cJSON_AddItemToArray(jget(jget(r,"report"),"lines"),cJSON_CreateString(text));}
static void diag_pair(cJSON*r,const char*label,const char*value){char text[768];snprintf(text,sizeof(text),"%s：%s",label,value&&*value?value:"未知");diag_line(r,text);}
static cJSON *diag_connections(void){
 cJSON *cfg=DIAG_GET("/configs"),*raw=DIAG_GET("/connections");
 if(!cJSON_IsObject(raw)){cJSON_Delete(cfg);cJSON_Delete(raw);return reply(0,"无法读取 Clash 连接，请检查服务状态");}
 cJSON*list=jget(raw,"connections");if(!cJSON_IsArray(list)&&!cJSON_IsNull(list)){cJSON_Delete(cfg);cJSON_Delete(raw);return reply(0,"Clash 连接格式异常，未生成结果");}
 cJSON*r=diag_report("实时分流连接");diag_pair(r,"当前模式",jstr(cfg,"mode"));cJSON_Delete(cfg);char line[768];int count=cJSON_GetArraySize(list),shown=count>40?40:count;
 snprintf(line,sizeof(line),"活跃 %d 条 · 展示前 %d 条",count,shown);diag_line(r,line);diag_line(r,"仅展示核心当前连接；非全网流量审计。");
 if(!count)diag_line(r,"当前无活跃连接，打开网站后刷新查看。");
 for(int n=0;n<shown;n++){cJSON*c=cJSON_GetArrayItem(list,n),*m=jget(c,"metadata");snprintf(line,sizeof(line),"—— 连接 %d ——",n+1);diag_line(r,line);
  diag_pair(r,"设备",jstr(m,"sourceIP"));diag_pair(r,"目标",*jstr(m,"host")?jstr(m,"host"):jstr(m,"destinationIP"));diag_pair(r,"协议",jstr(m,"network"));
  diag_pair(r,"命中规则",jstr(c,"rule"));if(*jstr(c,"rulePayload"))diag_pair(r,"匹配内容",jstr(c,"rulePayload"));
  cJSON*p;int path=0;cJSON_ArrayForEach(p,jget(c,"chains")){if(path++>=8)break;if(cJSON_IsString(p))diag_pair(r,"策略 / 节点",p->valuestring);}
  cJSON*u=jget(c,"upload"),*d=jget(c,"download");if(cJSON_IsNumber(u)&&cJSON_IsNumber(d)&&u->valuedouble>=0&&d->valuedouble>=0){snprintf(line,sizeof(line),"上行 %.1f KiB · 下行 %.1f KiB",u->valuedouble/1024,d->valuedouble/1024);diag_line(r,line);}
 }
 cJSON_Delete(raw);return r;
}
/* Only use the live core's port bound to an address owned by this device. */
static int diag_proxy_url(const cJSON *cfg,char *out,size_t cap){
 cJSON *p=jget(cfg,"mixed-port");if(!cJSON_IsNumber(p)||p->valuedouble==0)p=jget(cfg,"port");
 if(!cJSON_IsNumber(p)||!isfinite(p->valuedouble)||p->valuedouble<1||p->valuedouble>65535||p->valuedouble!=(int)p->valuedouble)return 0;
 const char *host=jstr(cfg,"bind-address");if(!*host||!strcmp(host,"*")||!strcmp(host,"0.0.0.0"))host="127.0.0.1";else if(!strcmp(host,"::"))host="::1";
 struct in_addr v4;struct in6_addr v6;int family=inet_pton(AF_INET,host,&v4)==1?AF_INET:inet_pton(AF_INET6,host,&v6)==1?AF_INET6:0;if(!family)return 0;
 struct ifaddrs *ifs=NULL;int own=0;if(getifaddrs(&ifs))return 0;
 for(struct ifaddrs *i=ifs;i;i=i->ifa_next){if(!i->ifa_addr||i->ifa_addr->sa_family!=family)continue;
  if(family==AF_INET&&!memcmp(&((struct sockaddr_in*)i->ifa_addr)->sin_addr,&v4,sizeof(v4)))own=1;
  if(family==AF_INET6&&!memcmp(&((struct sockaddr_in6*)i->ifa_addr)->sin6_addr,&v6,sizeof(v6)))own=1;
 }freeifaddrs(ifs);if(!own)return 0;
 snprintf(out,cap,family==AF_INET6?"http://[%s]:%d":"http://%s:%d",host,(int)p->valuedouble);return 1;
}
static int diag_real_probe(const char*url,const char*proxy,char*out,size_t cap){
 char*v[]={"curl","--disable","--silent","--show-error","--proto","=https","--noproxy","","--proxy",(char*)proxy,"--connect-timeout","3","--max-time","6","--output","/dev/null","--write-out","%{http_code} %{ssl_verify_result} %{time_total}",(char*)url,NULL};
 return run_cmd("/usr/bin/curl",v,NULL,out,cap);
}
#ifndef DIAG_PROBE
#define DIAG_PROBE diag_real_probe
#endif
static cJSON *diag_web(void){
 char proxy[160];cJSON*cfg=DIAG_GET("/configs");int ready=diag_proxy_url(cfg,proxy,sizeof(proxy));cJSON_Delete(cfg);if(!ready)return reply(0,"无法确认本机 Clash HTTP 监听地址与端口，未发送请求");
 cJSON*r=diag_report("网站连通性");diag_line(r,"路径：本机 → 现有 Clash HTTP 代理");diag_line(r,"这是应用层测试，不代表所有客户端/协议。");int passed=0;
 const char*hosts[]={"Google","百度"},*urls[]={"https://www.google.com/","https://www.baidu.com/"};
 for(int n=0;n<2;n++){char out[256]={0},line[320];int code=0,tls=-1;double elapsed=-1;int executed=DIAG_PROBE(urls[n],proxy,out,sizeof(out));int parsed=sscanf(out,"%d %d %lf",&code,&tls,&elapsed)==3;
  int ok=executed&&parsed&&tls==0&&code>=200&&code<400;passed+=ok;
  snprintf(line,sizeof(line),"%s：%s",hosts[n],ok?"可访问":"未通过");diag_line(r,line);
  if(parsed){snprintf(line,sizeof(line),"HTTP %d · %.2f 秒",code,elapsed);diag_line(r,line);diag_line(r,tls==0&&executed?"TLS 证书校验通过":"TLS / 连接未完成或未通过");}else diag_line(r,"未取得有效响应；稍后重试。");
 }
 cJSON_ReplaceItemInObject(r,"message",cJSON_CreateString(passed==2?"Google 与百度访问通过":"诊断完成，存在未通过项"));return r;
}
/* Read-only, bounded measurements. The report keeps operation success separate
 * from network success; producing a report does not mean its checks passed. */
struct diag_process { int exit_code, timed_out, error_number, truncated; };
struct diag_https { struct diag_process process; int parsed, http, tls; double seconds; };
struct diag_dns { int measured, code, addresses; struct diag_process process; };
struct diag_route { int measured, found; char iface[IFNAMSIZ], source[INET_ADDRSTRLEN], gateway[INET_ADDRSTRLEN]; };
struct diag_link { int measured, up, running, address; };

static struct diag_process diag_collect(pid_t pid,int fd,char *out,size_t cap,int timeout_ms){
 struct diag_process result={-1,0,0,0};int status=0,done=0,eof=0;size_t used=0;
 long long end=ms()+timeout_ms;out[0]=0;int flags=fcntl(fd,F_GETFL,0);
 if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0)result.error_number=errno;
 while(!result.error_number&&!result.truncated&&(!done||!eof)){
  long long remaining=end-ms();if(remaining<=0){result.timed_out=1;break;}
  struct pollfd p={fd,eof?0:POLLIN,0};int rc=poll(&p,1,remaining>25?25:(int)remaining);
  if(rc<0&&errno!=EINTR){result.error_number=errno;break;}
  if(!eof&&(p.revents&(POLLIN|POLLHUP))){char buffer[256];ssize_t n=read(fd,buffer,sizeof(buffer));
   if(n>0){if(used+(size_t)n>=cap){result.truncated=1;break;}memcpy(out+used,buffer,(size_t)n);used+=(size_t)n;out[used]=0;}
   else if(n==0)eof=1;else if(errno!=EAGAIN&&errno!=EINTR){result.error_number=errno;break;}
  }
  if(!done){pid_t got=waitpid(pid,&status,WNOHANG);if(got==pid)done=1;else if(got<0&&errno!=EINTR){result.error_number=errno;break;}}
 }
 if(!done){kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){};}
 close(fd);if(done&&WIFEXITED(status))result.exit_code=WEXITSTATUS(status);
 else if(done&&WIFSIGNALED(status))result.exit_code=128+WTERMSIG(status);
 return result;
}
static struct diag_process diag_exec(const char *path,char *const argv[],char *out,size_t cap,int timeout_ms){
 struct diag_process failed={-1,0,0,0};int fd[2];if(!out||cap<2){failed.error_number=EINVAL;return failed;}out[0]=0;
 if(pipe(fd)){failed.error_number=errno;return failed;}pid_t pid=fork();
 if(pid<0){failed.error_number=errno;close(fd[0]);close(fd[1]);return failed;}
 if(!pid){
  close(fd[0]);if(dup2(fd[1],STDOUT_FILENO)<0)_exit(126);close(fd[1]);
  int nul=open("/dev/null",O_RDWR);if(nul<0)_exit(126);dup2(nul,STDIN_FILENO);dup2(nul,STDERR_FILENO);if(nul>2)close(nul);
  char *env[]={"PATH=/usr/sbin:/usr/bin:/sbin:/bin","LANG=C",NULL};execve(path,argv,env);
  int saved=errno;char error[48];int length=snprintf(error,sizeof(error),"exec_errno=%d",saved);(void)write(STDOUT_FILENO,error,(size_t)length);_exit(127);
 }
 close(fd[1]);struct diag_process result=diag_collect(pid,fd[0],out,cap,timeout_ms);
 if(result.exit_code==127){int error=0;if(sscanf(out,"exec_errno=%d",&error)==1&&error>0)result.error_number=error;}
 return result;
}
#ifndef DIAG_CURL_PATH
#define DIAG_CURL_PATH "/usr/bin/curl"
#endif
static struct diag_https diag_https_probe(const char *url,const char *proxy){
 struct diag_https result;memset(&result,0,sizeof(result));result.http=0;result.tls=-1;result.seconds=-1;char out[256];
 /* --disable must be first: local curlrc must not disable TLS verification.
  * An empty explicit proxy and a clean environment do not bypass transparent
  * routing, which is deliberately stated in the report. No redirects/retries. */
 char *argv[]={"curl","--disable","--silent","--head","--ipv4","--proto","=https","--noproxy",proxy?"":"*","--proxy",(char*)(proxy?proxy:""),"--connect-timeout","4","--max-time","8","--output","/dev/null","--write-out","%{http_code} %{ssl_verify_result} %{time_total}",(char*)url,NULL};
 result.process=diag_exec(DIAG_CURL_PATH,argv,out,sizeof(out),8500);
 char extra;result.parsed=sscanf(out,"%d %d %lf %c",&result.http,&result.tls,&result.seconds,&extra)==3&&result.http>=0&&result.http<=599&&result.tls>=0&&isfinite(result.seconds)&&result.seconds>=0&&result.seconds<=8.5;
 return result;
}
static struct diag_dns diag_system_dns(const char *host){
 struct diag_dns result;memset(&result,0,sizeof(result));result.process.exit_code=-1;int fd[2];
 if(pipe(fd)){result.process.error_number=errno;return result;}pid_t pid=fork();
 if(pid<0){result.process.error_number=errno;close(fd[0]);close(fd[1]);return result;}
 if(!pid){
  close(fd[0]);struct addrinfo hints,*addresses=NULL;memset(&hints,0,sizeof(hints));hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
  int rc=getaddrinfo(host,NULL,&hints,&addresses),saved=rc==EAI_SYSTEM?errno:0,count=0;for(struct addrinfo *a=addresses;a;a=a->ai_next)if(a->ai_family==AF_INET)count++;
  if(addresses)freeaddrinfo(addresses);char out[80];int length=snprintf(out,sizeof(out),"%d %d %d",rc,count,saved);(void)write(fd[1],out,(size_t)length);close(fd[1]);_exit(0);
 }
 close(fd[1]);char out[80],extra;result.process=diag_collect(pid,fd[0],out,sizeof(out),2000);
 int error=0;result.measured=result.process.exit_code==0&&!result.process.timed_out&&!result.process.error_number&&!result.process.truncated&&sscanf(out,"%d %d %d %c",&result.code,&result.addresses,&error,&extra)==3&&result.addresses>=0&&error>=0;
 if(result.measured&&error)result.process.error_number=error;
 return result;
}
static int diag_iface_valid(const char *iface){
 if(!*iface||strlen(iface)>=IFNAMSIZ)return 0;for(const unsigned char *p=(const unsigned char*)iface;*p;p++)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='_'||*p=='-'||*p=='.'||*p==':'))return 0;return 1;
}
static struct diag_route diag_route_parse(int executed,char *out){
 struct diag_route route;memset(&route,0,sizeof(route));route.measured=executed;
 if(!executed||strstr(out,"unreachable")||strstr(out,"blackhole")||strstr(out,"prohibit"))return route;
 char *save=NULL,*key=strtok_r(out," \t\r\n",&save);while(key){
  if(!strcmp(key,"dev")||!strcmp(key,"src")||!strcmp(key,"via")){char *value=strtok_r(NULL," \t\r\n",&save);if(!value)break;
   struct in_addr ip;if(!strcmp(key,"dev")){if(diag_iface_valid(value))snprintf(route.iface,sizeof(route.iface),"%s",value);}
   else if(inet_pton(AF_INET,value,&ip)==1)snprintf(!strcmp(key,"src")?route.source:route.gateway,INET_ADDRSTRLEN,"%s",value);
  }key=strtok_r(NULL," \t\r\n",&save);
 }
 route.found=*route.iface&&strcmp(route.iface,"lo");return route;
}
static struct diag_route diag_current_route(void){
 char out[1024];char *argv[]={"ip","-4","route","get","1.1.1.1",NULL};
 const char *path=access("/sbin/ip",X_OK)==0?"/sbin/ip":access("/usr/sbin/ip",X_OK)==0?"/usr/sbin/ip":access("/bin/ip",X_OK)==0?"/bin/ip":"/usr/bin/ip";
 struct diag_process process=diag_exec(path,argv,out,sizeof(out),1000);
 return diag_route_parse(process.exit_code==0&&!process.timed_out&&!process.error_number&&!process.truncated,out);
}
static struct diag_link diag_current_link(const struct diag_route *route){
 struct diag_link result={0,0,0,0};if(!route->found)return result;struct ifaddrs *ifs=NULL;if(getifaddrs(&ifs))return result;
 for(struct ifaddrs *i=ifs;i;i=i->ifa_next){if(!i->ifa_name||strcmp(i->ifa_name,route->iface))continue;
  result.measured=1;result.up=!!(i->ifa_flags&IFF_UP);result.running=!!(i->ifa_flags&IFF_RUNNING);
  if(i->ifa_addr&&i->ifa_addr->sa_family==AF_INET){struct in_addr addr=((struct sockaddr_in*)i->ifa_addr)->sin_addr;uint32_t h=ntohl(addr.s_addr);
   char ip[INET_ADDRSTRLEN];if(h&&h<0xe0000000&&(h>>24)!=127&&(h>>16)!=0xa9fe&&inet_ntop(AF_INET,&addr,ip,sizeof(ip))&&(!*route->source||!strcmp(route->source,ip)))result.address=1;
  }
 }freeifaddrs(ifs);return result;
}
#ifndef DIAG_NETWORK_HTTPS
#define DIAG_NETWORK_HTTPS diag_https_probe
#endif
#ifndef DIAG_NETWORK_DNS
#define DIAG_NETWORK_DNS diag_system_dns
#endif
#ifndef DIAG_NETWORK_ROUTE
#define DIAG_NETWORK_ROUTE diag_current_route
#endif
#ifndef DIAG_NETWORK_LINK
#define DIAG_NETWORK_LINK diag_current_link
#endif
static cJSON *diag_check(cJSON *r,const char *id,const char *label,const char *status,const char *detail,const char *hint){
 cJSON *report=jget(r,"report"),*checks=jget(report,"checks");if(!checks)checks=cJSON_AddArrayToObject(report,"checks");cJSON *check=cJSON_CreateObject();
 cJSON_AddStringToObject(check,"id",id);cJSON_AddStringToObject(check,"label",label);cJSON_AddStringToObject(check,"status",status);cJSON_AddStringToObject(check,"detail",detail);cJSON_AddStringToObject(check,"hint",hint?hint:"");cJSON_AddItemToArray(checks,check);
 char line[768];snprintf(line,sizeof(line),"%s：%s",label,!strcmp(status,"passed")?"通过":!strcmp(status,"failed")?"未通过":"未检测");diag_line(r,line);if(*detail)diag_line(r,detail);if(hint&&*hint){snprintf(line,sizeof(line),"建议：%s",hint);diag_line(r,line);}return check;
}
static const char *diag_https_reason(const struct diag_https *probe,int expected){
 if(probe->process.timed_out)return "检测超过时限";
 if(probe->process.error_number||probe->process.exit_code==127||probe->process.exit_code==126)return "探测程序未能执行";
 if(probe->process.truncated)return "探测输出异常";
 switch(probe->process.exit_code){
  case 5:return "代理地址解析失败";case 6:return "目标域名解析失败";case 7:return "连接未建立";case 28:return "DNS、连接或传输超时";case 35:return "TLS 握手失败";case 60:return "TLS 证书校验失败";case 77:return "本机 CA 证书无法读取";case 63:return "响应超过探测大小限制";
 }
 if(probe->process.exit_code!=0)return "请求未完成";
 if(!probe->parsed)return "未取得完整测量结果";
 if(probe->tls!=0)return "TLS 证书校验失败";
 if(probe->http!=expected)return probe->http>=300&&probe->http<400?"收到重定向，未跟随跳转":"HTTP 状态与探测端点不符";
 return "TLS 证书校验及 HTTP 响应均通过";
}
static const char *diag_https_hint(const struct diag_https *probe,int proxied){
 if(probe->process.exit_code==35)return proxied?"TLS 握手未完成；先检查 Clash 当前节点、远端连接与分流规则，不能仅据此判定设备时间或 CA 错误。":"TLS 握手未完成；检查上游网络、透明代理和目标连接，证书错误需单独确认。";
 if(probe->process.exit_code==60||probe->process.exit_code==77||(probe->process.exit_code==0&&probe->parsed&&probe->tls!=0))return "核对设备时间与 CA 证书；保留证书校验，不要跳过 TLS。";
 if(probe->process.exit_code==6)return "先查看 DNS 检测；代理请求还需检查节点的 DNS。";
 if(probe->process.error_number||probe->process.exit_code==127)return "探测组件不可用，请核对安装包和设备上的 curl。";
 return proxied?"确认 Clash 已启动、所选节点可用；查看实际分流和节点检测。":"检查上游连接、蜂窝回退和认证门户；默认出口可能受到分流规则影响。";
}
static int diag_https_check(cJSON *r,const char *id,const char *label,const char *url,int expected,const char *proxy){
 struct diag_https probe=DIAG_NETWORK_HTTPS(url,proxy);probe.parsed=probe.parsed&&isfinite(probe.seconds)&&probe.seconds>=0&&probe.seconds<=8.5&&probe.http>=0&&probe.http<=599&&probe.tls>=0;
 int ok=probe.process.exit_code==0&&!probe.process.timed_out&&!probe.process.error_number&&!probe.process.truncated&&probe.parsed&&probe.tls==0&&probe.http==expected;
 char detail[512];const char *why=diag_https_reason(&probe,expected);
 if(probe.parsed)snprintf(detail,sizeof(detail),"%s · curl %d · HTTP %d · TLS %d · %.2f 秒",why,probe.process.exit_code,probe.http,probe.tls,probe.seconds);
 else if(probe.process.error_number)snprintf(detail,sizeof(detail),"%s · curl %d · errno %d",why,probe.process.exit_code,probe.process.error_number);
 else snprintf(detail,sizeof(detail),"%s · curl %d",why,probe.process.exit_code);
 cJSON *check=diag_check(r,id,label,ok?"passed":"failed",detail,ok?NULL:diag_https_hint(&probe,proxy!=NULL));
 cJSON_AddStringToObject(check,"path",proxy?"clash_http_proxy":"default_ipv4");cJSON_AddStringToObject(check,"url",url);cJSON_AddNumberToObject(check,"curl_exit_code",probe.process.exit_code);cJSON_AddBoolToObject(check,"timed_out",probe.process.timed_out);cJSON_AddNumberToObject(check,"errno",probe.process.error_number);
 if(probe.parsed){cJSON_AddNumberToObject(check,"http_status",probe.http);cJSON_AddNumberToObject(check,"ssl_verify_result",probe.tls);cJSON_AddNumberToObject(check,"seconds",probe.seconds);}return ok;
}
static cJSON *diag_network(void){
 cJSON *r=diag_report("一键网络自检"),*report=jget(r,"report");long long start=ms();
 diag_line(r,"只读检查设备本机；默认出口固定 IPv4。热点客户端、完整 IPv6 与 UDP 需另行验证。");
 diag_line(r,"默认出口不指定应用代理，仍可能经过透明代理；不能据此认定直连。");
 struct diag_route route=DIAG_NETWORK_ROUTE();struct diag_link link=DIAG_NETWORK_LINK(&route);char detail[384];
 if(route.found)snprintf(detail,sizeof(detail),"到 1.1.1.1：接口 %s%s%s%s%s",route.iface,*route.source?" · 源地址 ":"",route.source,*route.gateway?" · 网关 ":"",route.gateway);
 else snprintf(detail,sizeof(detail),"%s",route.measured?"未找到可用 IPv4 出口":"未能读取 IPv4 路由");
 diag_check(r,"route","出口路由",route.found?"passed":"failed",detail,route.found?NULL:"在网络页面确认上游或蜂窝连接，再重新检测。");
 int linked=link.measured&&link.up&&link.running, addressed=link.measured&&link.address;
 diag_check(r,"link","出口接口",linked?"passed":link.measured?"failed":"skipped",linked?"内核报告接口已启用且链路就绪。":link.measured?"内核未报告出口链路就绪。":"出口接口未确认。",linked?NULL:"检查 Wi-Fi 关联状态、USB 网线或蜂窝信号。");
 diag_check(r,"address","出口地址",addressed?"passed":link.measured?"failed":"skipped",addressed?"出口接口具有与路由一致的有效 IPv4 地址。":link.measured?"尚无与路由一致的有效 IPv4 地址。":"没有可检查的出口接口。",addressed?NULL:"Wi-Fi 接力请确认已经获取地址；持续等待时检查上游 DHCP。");
 const char *hosts[]={"www.google.com","www.cloudflare.com"},*names[]={"Google","Cloudflare"},*urls[]={"https://www.google.com/generate_204","https://www.cloudflare.com/"};const int expected[]={204,200};int dns_passed=0,default_passed=0,proxy_passed=0;
 for(int n=0;n<2;n++){
  struct diag_dns probe=DIAG_NETWORK_DNS(hosts[n]);int ok=probe.measured&&probe.code==0&&probe.addresses>0;dns_passed+=ok;char id[64],label[96];snprintf(id,sizeof(id),"dns.%d",n);snprintf(label,sizeof(label),"系统 DNS / %s",names[n]);
  if(ok)snprintf(detail,sizeof(detail),"系统解析器取得 IPv4 地址；不代表远端已可访问。");
  else if(probe.process.timed_out)snprintf(detail,sizeof(detail),"系统域名解析超过 2 秒时限。");
  else if(probe.measured)snprintf(detail,sizeof(detail),"系统解析未返回可用 IPv4 地址（resolver %d）。",probe.code);
  else snprintf(detail,sizeof(detail),"系统解析未完成（errno %d）。",probe.process.error_number);
  cJSON *check=diag_check(r,id,label,ok?"passed":"failed",detail,ok?NULL:"检查上游联网和原厂 DNS 设置；此项不会自动改写 DNS。");cJSON_AddNumberToObject(check,"resolver_code",probe.code);cJSON_AddBoolToObject(check,"measured",probe.measured);cJSON_AddBoolToObject(check,"timed_out",probe.process.timed_out);
 }
 for(int n=0;n<2;n++){char id[64],label[96];snprintf(id,sizeof(id),"https.default.%d",n);snprintf(label,sizeof(label),"默认出口 / %s",names[n]);default_passed+=diag_https_check(r,id,label,urls[n],expected[n],NULL);}
 char proxy[160];cJSON *cfg=DIAG_GET("/configs");int ready=diag_proxy_url(cfg,proxy,sizeof(proxy));cJSON_Delete(cfg);
 if(ready){
  diag_line(r,"Clash 路径：使用已确认的本机 HTTP 监听；远端地址族由 Clash 决定，仍按当前策略，不能据此证明使用远端代理节点。");
  for(int n=0;n<2;n++){char id[64],label[96];snprintf(id,sizeof(id),"https.clash.%d",n);snprintf(label,sizeof(label),"Clash HTTP / %s",names[n]);proxy_passed+=diag_https_check(r,id,label,urls[n],expected[n],proxy);}
 }else diag_check(r,"clash.listener","Clash HTTP 路径","skipped","未能确认本机 Clash HTTP 监听，未向未经确认的地址发送请求。","如需检查代理，启动 Clash 后重新自检；默认出口结果独立显示。");
 int baseline=route.found&&linked&&addressed&&dns_passed==2,all=baseline&&default_passed==2&&ready&&proxy_passed==2;
 const char *overall=all?"passed":default_passed||proxy_passed?"partial":"failed";cJSON *summary=cJSON_AddObjectToObject(report,"summary");cJSON_AddStringToObject(summary,"status",overall);cJSON_AddBoolToObject(summary,"all_checks_passed",all);cJSON_AddBoolToObject(summary,"default_path_passed",baseline&&default_passed==2);cJSON_AddBoolToObject(summary,"proxy_path_tested",ready);cJSON_AddBoolToObject(summary,"proxy_path_passed",ready&&proxy_passed==2);cJSON_AddNumberToObject(summary,"duration_ms",ms()-start);
 snprintf(detail,sizeof(detail),"结果：默认出口 HTTPS %d/2；Clash HTTPS %s。",default_passed,ready?(proxy_passed==2?"2/2":proxy_passed==1?"1/2":"0/2"):"未检测");diag_line(r,detail);
 if(default_passed<2&&proxy_passed==2)diag_line(r,"Clash 请求通过，默认出口仍有失败；请检查分流规则与默认出口，不能报告整机联网全部通过。");
 if(default_passed==2&&ready&&proxy_passed<2)diag_line(r,"默认出口请求通过，Clash 路径仍有失败；优先检查当前节点和策略。");
 cJSON_ReplaceItemInObject(r,"message",cJSON_CreateString(all?"自检完成：所有检测项通过":"自检完成：存在未通过或未检测项"));return r;
}
static cJSON*diagnostics_action(const char*action,const cJSON*args){(void)args;if(!strcmp(action,"diag.connections"))return diag_connections();if(!strcmp(action,"diag.web"))return diag_web();if(!strcmp(action,"diag.network"))return diag_network();return NULL;}
static void diagnostics_sections(cJSON*root){
 cJSON*s=adv_section(root,"diagnostics","网络诊断"),*i=item(s,"diag.network","一键网络自检","action","链路 · DNS · HTTPS · Clash","diag.network",1,"只读检测，最多约 45 秒");cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(0));
 i=item(s,"diag.connections","实时分流连接","action","查看规则与节点","diag.connections",1,NULL);cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(0));
 i=item(s,"diag.web","Google / 百度测试","action","TLS 与 HTTP 验证","diag.web",1,NULL);cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(0));
 s=adv_section(root,"clash","Clash");i=item(s,"diag.connections","查看实际分流","action","设备 · 规则 · 节点","diag.connections",1,NULL);cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(0));
}
#endif
