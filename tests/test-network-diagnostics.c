/* Standalone, no external network:
 * cc -O1 -I panel -I panel/vendor tests/test-network-diagnostics.c panel/vendor/cJSON.c -lm -o /tmp/test-network-diagnostics
 */
#include <stddef.h>
#include "../panel/vendor/cJSON.h"
struct diag_https;
struct diag_dns;
struct diag_route;
struct diag_link;
static cJSON *test_config(const char *);
static struct diag_https test_https(const char *,const char *);
static struct diag_dns test_dns(const char *);
static struct diag_route test_route(void);
static struct diag_link test_link(const struct diag_route *);
static int test_legacy_probe(const char *,const char *,char *,size_t);
static const char *curl_path;
#define DIAG_GET test_config
#define DIAG_NETWORK_HTTPS test_https
#define DIAG_NETWORK_DNS test_dns
#define DIAG_NETWORK_ROUTE test_route
#define DIAG_NETWORK_LINK test_link
#define DIAG_CURL_PATH curl_path
#define DIAG_PROBE test_legacy_probe
#define main panel_program_main
#include "../panel/panel-control.c"
#undef main
#include <assert.h>

static int scenario,probes,default_probes,proxy_probes;
static cJSON *config;
static cJSON *test_config(const char *endpoint){assert(!strcmp(endpoint,"/configs"));return cJSON_Duplicate(config,1);}
static struct diag_route test_route(void){struct diag_route route={1,1,"wlan1","192.0.2.2","192.0.2.1"};if(scenario==5)route.found=0;return route;}
static struct diag_link test_link(const struct diag_route *route){struct diag_link link={1,1,1,1};if(!route->found)memset(&link,0,sizeof(link));if(scenario==6)link.address=0;return link;}
static struct diag_dns test_dns(const char *host){
 assert(!strcmp(host,"www.google.com")||!strcmp(host,"www.cloudflare.com"));struct diag_dns result={1,0,1,{0,0,0,0}};
 if(scenario==2){result.code=EAI_NONAME;result.addresses=0;}if(scenario==8){result.measured=0;result.process.timed_out=1;result.process.exit_code=-1;}return result;
}
static struct diag_https test_https(const char *url,const char *proxy){
 int google=!strcmp(url,"https://www.google.com/generate_204");assert(google||!strcmp(url,"https://www.cloudflare.com/"));
 probes++;if(proxy){assert(!strcmp(proxy,"http://127.0.0.1:17890"));proxy_probes++;}else default_probes++;
 struct diag_https result={{0,0,0,0},1,google?204:200,0,0.25};
 if(scenario==1){result.process.exit_code=60;result.tls=10;result.http=0;}
 if(scenario==2){result.process.exit_code=6;result.http=0;}
 if(scenario==3&&!proxy){result.process.exit_code=28;result.http=0;}
 if(scenario==4&&proxy){result.process.exit_code=7;result.http=0;}
 if(scenario==7)result.http=302;
 if(scenario==9){result.process.exit_code=0;result.tls=10;}
 if(scenario==10)result.parsed=0;
 if(scenario==11)result.seconds=NAN;
 if(scenario==12){result.process.exit_code=35;result.tls=1;result.http=0;}
 return result;
}
static int test_legacy_probe(const char *url,const char *proxy,char *out,size_t cap){
 assert(!strcmp(url,"https://www.google.com/")||!strcmp(url,"https://www.baidu.com/"));assert(!strcmp(proxy,"http://127.0.0.1:17890"));snprintf(out,cap,"200 0 0.25");return scenario==0;
}
static cJSON *find_check(cJSON *reply,const char *id){cJSON *check;cJSON_ArrayForEach(check,jget(jget(reply,"report"),"checks"))if(!strcmp(jstr(check,"id"),id))return check;assert(!"missing check");return NULL;}
static cJSON *summary(cJSON *reply){return jget(jget(reply,"report"),"summary");}
static cJSON *run_scenario(int value){scenario=value;probes=default_probes=proxy_probes=0;cJSON *result=diagnostics_action("diag.network",NULL);assert(result);assert(cJSON_IsTrue(jget(result,"ok")));assert(!strcmp(jstr(jget(result,"report"),"title"),"一键网络自检"));assert(cJSON_IsArray(jget(jget(result,"report"),"lines")));return result;}
static int arg_has(int argc,char **argv,const char *value){for(int n=1;n<argc;n++)if(!strcmp(argv[n],value))return 1;return 0;}
static const char *arg_after(int argc,char **argv,const char *value){for(int n=1;n+1<argc;n++)if(!strcmp(argv[n],value))return argv[n+1];return NULL;}
/* The test binary doubles as curl so it exercises the actual fork/exec/wait,
 * bounded output and exit-code paths while opening no sockets. */
static int fake_curl(int argc,char **argv){
 assert(argc>10&&!strcmp(argv[1],"--disable"));assert(arg_has(argc,argv,"--head"));assert(arg_has(argc,argv,"--ipv4"));assert(!arg_has(argc,argv,"-k"));assert(!arg_has(argc,argv,"--insecure"));assert(!arg_has(argc,argv,"--location"));
 assert(!strcmp(arg_after(argc,argv,"--proto"),"=https"));assert(!strcmp(arg_after(argc,argv,"--connect-timeout"),"4"));assert(!strcmp(arg_after(argc,argv,"--max-time"),"8"));
 assert(!getenv("HTTPS_PROXY")&&!getenv("HTTP_PROXY")&&!getenv("ALL_PROXY")&&!getenv("CURL_CA_BUNDLE")&&!getenv("HOME"));
 const char *proxy=arg_after(argc,argv,"--proxy"),*url=argv[argc-1];assert(proxy);assert(!strcmp(arg_after(argc,argv,"--noproxy"),*proxy?"":"*"));
 if(strstr(url,"/tls")){fputs("000 20 0.125",stdout);return 60;}
 if(strstr(url,"/dns")){fputs("000 0 0.050",stdout);return 6;}
 if(strstr(url,"/nan")){fputs("204 0 nan",stdout);return 0;}
 if(strstr(url,"/trailing")){fputs("204 0 0.2 junk",stdout);return 0;}
 if(strstr(url,"/nooutput"))return 0;
 fputs("204 0 0.125",stdout);return 0;
}
int main(int argc,char **argv){
 if(!strcmp(argv[0],"curl"))return fake_curl(argc,argv);
 if(argc>1&&!strcmp(argv[1],"--sleep")){struct timespec delay={3,0};nanosleep(&delay,NULL);return 0;}
 if(argc>1&&!strcmp(argv[1],"--flood")){for(int n=0;n<1000;n++)putchar('x');return 0;}
 if(argc>1&&!strcmp(argv[1],"--exit"))return 42;
 char resolved[4096];assert(realpath(argv[0],resolved));curl_path=resolved;
 fixture=cJSON_CreateObject();config=cJSON_Parse("{\"mixed-port\":17890,\"bind-address\":\"127.0.0.1\",\"secret\":\"never-print-this\"}");
 cJSON *result=run_scenario(0);assert(probes==4&&default_probes==2&&proxy_probes==2);assert(cJSON_IsTrue(jget(summary(result),"all_checks_passed")));assert(!strcmp(jstr(summary(result),"status"),"passed"));
 char *json=cJSON_PrintUnformatted(result);assert(strstr(json,"透明代理")&&strstr(json,"默认出口固定 IPv4"));assert(!strstr(json,"never-print-this"));free(json);cJSON_Delete(result);
 for(int value=1;value<=12;value++){result=run_scenario(value);assert(!cJSON_IsTrue(jget(summary(result),"all_checks_passed")));assert(strcmp(jstr(summary(result),"status"),"passed"));cJSON_Delete(result);}
 result=run_scenario(1);assert(!strcmp(jstr(find_check(result,"https.default.0"),"status"),"failed"));assert(jget(find_check(result,"https.default.0"),"curl_exit_code")->valueint==60);assert(strstr(jstr(find_check(result,"https.default.0"),"hint"),"设备时间"));cJSON_Delete(result);
 result=run_scenario(2);assert(!strcmp(jstr(find_check(result,"dns.0"),"status"),"failed"));assert(!strcmp(jstr(summary(result),"status"),"failed"));cJSON_Delete(result);
 result=run_scenario(3);assert(!cJSON_IsTrue(jget(summary(result),"default_path_passed")));assert(cJSON_IsTrue(jget(summary(result),"proxy_path_passed")));cJSON_Delete(result);
 result=run_scenario(4);assert(cJSON_IsTrue(jget(summary(result),"default_path_passed")));assert(!cJSON_IsTrue(jget(summary(result),"proxy_path_passed")));cJSON_Delete(result);
 result=run_scenario(8);assert(cJSON_IsTrue(jget(find_check(result,"dns.0"),"timed_out")));assert(!cJSON_IsTrue(jget(summary(result),"default_path_passed")));cJSON_Delete(result);
 result=run_scenario(12);assert(strstr(jstr(find_check(result,"https.clash.0"),"hint"),"Clash 当前节点"));assert(!strstr(jstr(find_check(result,"https.clash.0"),"hint"),"核对设备时间"));assert(strstr(jstr(find_check(result,"https.default.0"),"hint"),"TLS 握手未完成"));cJSON_Delete(result);
 const char *unsafe[]={"198.51.100.1","example.com","127.0.0.1;bad","127.0.0.1/path"};char proxy[160];
 for(size_t n=0;n<sizeof(unsafe)/sizeof(unsafe[0]);n++){cJSON_ReplaceItemInObject(config,"bind-address",cJSON_CreateString(unsafe[n]));assert(!diag_proxy_url(config,proxy,sizeof(proxy)));result=run_scenario(0);assert(probes==2&&proxy_probes==0);assert(!cJSON_IsTrue(jget(summary(result),"all_checks_passed")));assert(!cJSON_IsTrue(jget(summary(result),"proxy_path_tested")));assert(!strcmp(jstr(find_check(result,"clash.listener"),"status"),"skipped"));cJSON_Delete(result);}
 cJSON_ReplaceItemInObject(config,"bind-address",cJSON_CreateString("127.0.0.1"));
 const char *badports[]={"0","65536","2.5","-1","\"17890\""};for(size_t n=0;n<sizeof(badports)/sizeof(badports[0]);n++){cJSON_ReplaceItemInObject(config,"mixed-port",cJSON_Parse(badports[n]));assert(!diag_proxy_url(config,proxy,sizeof(proxy)));}
 cJSON_ReplaceItemInObject(config,"mixed-port",cJSON_CreateNumber(17890));assert(diag_proxy_url(config,proxy,sizeof(proxy))&&!strcmp(proxy,"http://127.0.0.1:17890"));
 scenario=0;result=diagnostics_action("diag.web",NULL);assert(!strcmp(jstr(result,"message"),"Google 与百度访问通过"));cJSON_Delete(result);scenario=1;result=diagnostics_action("diag.web",NULL);assert(!strcmp(jstr(result,"message"),"诊断完成，存在未通过项"));cJSON_Delete(result);
 cJSON *root=reply(1,"");cJSON_AddArrayToObject(root,"sections");diagnostics_sections(root);cJSON *section,*entry;int seen=0;cJSON_ArrayForEach(section,jget(root,"sections"))cJSON_ArrayForEach(entry,jget(section,"items"))if(!strcmp(jstr(entry,"action"),"diag.network")){seen++;assert(!cJSON_IsTrue(jget(entry,"confirm")));}assert(seen==1);cJSON_Delete(root);
 char route_text[]="1.1.1.1 via 192.0.2.1 dev wlan1 src 192.0.2.2 uid 0\n    cache";struct diag_route route=diag_route_parse(1,route_text);assert(route.found&&!strcmp(route.iface,"wlan1")&&!strcmp(route.source,"192.0.2.2"));
 char denied[]="unreachable 1.1.1.1";route=diag_route_parse(1,denied);assert(!route.found);char bad_iface[]="1.1.1.1 dev bad;name src 192.0.2.2";route=diag_route_parse(1,bad_iface);assert(!route.found);
 setenv("HTTPS_PROXY","must-not-leak",1);setenv("CURL_CA_BUNDLE","must-not-leak",1);
 struct diag_https https=diag_https_probe("https://fixture.invalid/ok",NULL);assert(https.parsed&&https.process.exit_code==0&&https.http==204&&https.tls==0);
 https=diag_https_probe("https://fixture.invalid/tls","http://127.0.0.1:17890");assert(https.parsed&&https.process.exit_code==60&&https.tls==20);
 https=diag_https_probe("https://fixture.invalid/dns",NULL);assert(https.parsed&&https.process.exit_code==6);
 https=diag_https_probe("https://fixture.invalid/nan",NULL);assert(!https.parsed);
 https=diag_https_probe("https://fixture.invalid/trailing",NULL);assert(!https.parsed);
 https=diag_https_probe("https://fixture.invalid/nooutput",NULL);assert(!https.parsed);
 char out[128];char *exit_args[]={resolved,"--exit",NULL};struct diag_process process=diag_exec(resolved,exit_args,out,sizeof(out),500);assert(process.exit_code==42&&!process.timed_out);
 process=diag_exec("/nonexistent/u60-diagnostic-test",exit_args,out,sizeof(out),500);assert(process.exit_code==127&&process.error_number==ENOENT);
 char *sleep_args[]={resolved,"--sleep",NULL};long long started=ms();process=diag_exec(resolved,sleep_args,out,sizeof(out),75);assert(process.timed_out&&ms()-started<1000);
 char *flood_args[]={resolved,"--flood",NULL};process=diag_exec(resolved,flood_args,out,8,500);assert(process.truncated);
 struct diag_dns dns=diag_system_dns("127.0.0.1");assert(dns.measured&&dns.code==0&&dns.addresses>0);
 assert(diagnostics_action("unknown",NULL)==NULL);assert(fixture_write_count==0);cJSON_Delete(config);cJSON_Delete(fixture);
 puts("PASS: layered diagnostics, partial/no-proxy results, DNS/TLS/HTTP failures, local-bind guards, actual process exit/errno/timeout/overflow and curl argument isolation (no external network)");return 0;
}
