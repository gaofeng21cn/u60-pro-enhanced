#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <dirent.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include "panel-standby-policy.h"

/* No socket, ubus or periodic subprocess is used by the observer. Only a real
 * transition launches the service helper. The stock power manager owns sleep. */
static volatile sig_atomic_t stopped;
static void stop_signal(int sig) {(void)sig;stopped=1;}
static const char *base(void) {
#ifdef SB_TEST
 const char *p=getenv("SB_TEST_ROOT");return p?p:"/nonexistent-u60-test";
#else
 return "";
#endif
}
static void path(char *out,size_t n,const char *p) {snprintf(out,n,"%s%s",base(),p);}
static int exists(const char *p) {char b[512];path(b,sizeof(b),p);return access(b,F_OK)==0;}
static int read_text(const char *p,char *b,size_t n) {
 char f[512];path(f,sizeof(f),p);FILE *in=fopen(f,"r");if(!in)return 0;
 size_t k=fread(b,1,n-1,in);int ok=!ferror(in);fclose(in);b[k]=0;
 while(k&&(b[k-1]=='\n'||b[k-1]=='\r'||b[k-1]==' '))b[--k]=0;
 return ok&&k>0;
}
static int number(const char *p) {
 char b[64],*end;if(!read_text(p,b,sizeof(b)))return -1;
 errno=0;long v=strtol(b,&end,10);return errno||*end||v<0||v>1000000?-1:(int)v;
}
static void unquote(char *s) {
 size_t n=strlen(s);if(n>=2&&((s[0]=='\''&&s[n-1]=='\'')||(s[0]=='"'&&s[n-1]=='"'))){memmove(s,s+1,n-2);s[n-2]=0;}
}
/* Use the firmware's UCI loader: the sleep daemon saves transitions in the
 * pending delta directory and does not commit /etc/config on each transition.
 * Opaque handles + export avoid depending on private struct layout. A fresh
 * context per sample sees both commits and pending changes without subprocesses. */
struct uci_context;
struct uci_package;
static struct uci_context *(*uci_alloc)(void);
static void (*uci_free)(struct uci_context *);
static int (*uci_load_package)(struct uci_context *,const char *,struct uci_package **);
static int (*uci_export_package)(struct uci_context *,FILE *,struct uci_package *,bool);
#ifdef SB_TEST
static int (*uci_confdir)(struct uci_context *,const char *);
static int (*uci_savedir)(struct uci_context *,const char *);
#endif
static int load_uci(void) {
 static int loaded=0;if(loaded)return loaded>0;loaded=-1;
 const char *library="/lib/libuci.so";
#ifdef SB_TEST
 library=getenv("SB_TEST_UCI");if(!library)return 0;
#endif
 void *h=dlopen(library,RTLD_NOW|RTLD_LOCAL);if(!h)return 0;
#define SB_SYM(target,name) do {*(void **)(&target)=dlsym(h,name);if(!target)return 0;} while(0)
 SB_SYM(uci_alloc,"uci_alloc_context");SB_SYM(uci_free,"uci_free_context");
 SB_SYM(uci_load_package,"uci_load");SB_SYM(uci_export_package,"uci_export");
#ifdef SB_TEST
 SB_SYM(uci_confdir,"uci_set_confdir");SB_SYM(uci_savedir,"uci_set_savedir");
#endif
#undef SB_SYM
 loaded=1;return 1;
}
static int stock_sleep(void) {
 if(!load_uci())return -1;
 struct uci_context *ctx=uci_alloc();struct uci_package *pkg=NULL;
 if(!ctx)return -1;
#ifdef SB_TEST
 char cp[512],dp[512];path(cp,sizeof(cp),"/etc/config");path(dp,sizeof(dp),"/tmp/.uci");
 if(uci_confdir(ctx,cp)||uci_savedir(ctx,dp)){uci_free(ctx);return -1;}
#endif
 char *text=NULL;size_t len=0;FILE *stream=open_memstream(&text,&len);int result=-1;
 if(!stream){uci_free(ctx);return -1;}
 int ok=!uci_load_package(ctx,"zwrt_sleep",&pkg)&&!uci_export_package(ctx,stream,pkg,false);
 if(fclose(stream))ok=0;uci_free(ctx);
 if(ok&&text&&len<65536){
  FILE *f=fmemopen(text,len,"r");char line[512],kind[32],key[64],value[64];int section=0;
  if(f){while(fgets(line,sizeof(line),f)) {
   if(sscanf(line," %31s %63s %63s",kind,key,value)!=3)continue;
   unquote(key);unquote(value);
   if(!strcmp(kind,"config")){section=!strcmp(key,"ztmp_status")&&!strcmp(value,"ztmp_status");continue;}
   if(section&&!strcmp(kind,"option")&&!strcmp(key,"sleepStatus"))
    result=!strcmp(value,"sleep")?1:!strcmp(value,"wakeup")?0:-1;
  }if(ferror(f))result=-1;fclose(f);}
 }
 free(text);return result;
}
static struct sb_inputs sample(void) {
 struct sb_inputs s={0};char b[64];
 s.deep=read_text("/data/u60-panel/standby-mode",b,sizeof(b))&&!strcmp(b,"deep");
 s.stock_sleep=stock_sleep();s.lcd=number("/sys/class/leds/led:lcd/brightness");
 s.usb=number("/sys/class/power_supply/usb/online");s.external=0;s.radio=0;
 /* Any attached Ethernet adapter is conservatively in use; sleep never
  * changes LAN/WAN/relay roles or relies on a stale DHCP lease list. */
 if(exists("/sys/class/net/eth0"))s.external=1;
 char netdir[512];path(netdir,sizeof(netdir),"/sys/class/net");DIR *nets=opendir(netdir);
 if(nets){struct dirent *entry;while((entry=readdir(nets))){
  if(entry->d_name[0]=='.')continue;
  char nic[384];snprintf(nic,sizeof(nic),"/sys/class/net/%s/device/../idVendor",entry->d_name);
  if(exists(nic)){s.external=1;break;}
 }closedir(nets);}
 const char *g[]={"/sys/class/net/ecm0/carrier","/sys/class/net/rndis0/carrier","/sys/class/net/usb0/carrier"};
 for(size_t n=0;n<sizeof(g)/sizeof(*g);n++)if(exists(g[n])&&number(g[n])!=0)s.external=1;
 const char *r[]={"wlan0","wlan1","wlan2","wlan3","u60sta"};
 for(size_t n=0;n<sizeof(r)/sizeof(*r);n++){char p[128];snprintf(p,sizeof(p),"/sys/class/net/%s",r[n]);if(exists(p))s.radio=1;}
 return s;
}
static long long monotonic_ms(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static const char *reason(const struct sb_inputs *s) {
 if(!s->deep)return "normal";if(s->lcd<0||s->usb<0||s->stock_sleep<0)return "unknown";
 if(s->lcd>0)return "screen";if(s->usb!=0||s->external!=0)return "usb";
 if(s->stock_sleep!=1)return "stock_awake";if(s->radio!=0)return "radio";return "ready";
}
static const char *phase(void) {
 static char b[32];if(!read_text("/tmp/u60-standby/phase",b,sizeof(b)))return "awake";
 const char *v[]={"awake","entering","asleep","waking","error"};
 for(size_t n=0;n<sizeof(v)/sizeof(*v);n++)if(!strcmp(b,v[n]))return v[n];return "error";
}
static int trusted_run_dir(void) {
 char p[512];path(p,sizeof(p),"/tmp/u60-standby");if(mkdir(p,0700)&&errno!=EEXIST)return 0;
 struct stat st;return !lstat(p,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==getuid()&&(st.st_mode&0077)==0;
}
static pid_t launch(const char *action) {
 pid_t p=fork();if(p>0){setpgid(p,p);return p;}if(p<0)return p;
 setpgid(0,0);
 char helper[512];path(helper,sizeof(helper),"/data/u60-panel/standby-services.sh");
 int fd=open("/dev/null",O_RDWR);if(fd>=0){dup2(fd,0);dup2(fd,1);dup2(fd,2);if(fd>2)close(fd);}
 execl(helper,helper,action,(char*)NULL);_exit(127);
}
int main(int argc,char **argv) {
 if(argc!=2)return 2;struct sb_inputs s=sample();
 if(!strcmp(argv[1],"eligible"))return sb_eligible(&s)?0:1;
 if(!strcmp(argv[1],"blocked"))return sb_block_network(&s)?0:1;
 if(!strcmp(argv[1],"status")) {
  printf("{\"ok\":true,\"mode\":\"%s\",\"phase\":\"%s\",\"eligible\":%s,\"reason\":\"%s\"}\n",s.deep?"deep":"normal",phase(),sb_eligible(&s)?"true":"false",reason(&s));return 0;
 }
 if(strcmp(argv[1],"watch")||!trusted_run_dir())return 2;
 char p[512];path(p,sizeof(p),"/tmp/u60-standby/observer.lock");int lock=open(p,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
 if(lock<0||flock(lock,LOCK_EX|LOCK_NB))return 1;
 signal(SIGTERM,stop_signal);signal(SIGINT,stop_signal);signal(SIGPIPE,SIG_IGN);
 pid_t child=0;int resuming=0,force_resume=0,failures=0,term_sent=0;long long due=0,stable=0,started=0;
 while(!stopped) {
  long long now=monotonic_ms();
  if(child&&now-started>180000&&!term_sent){kill(-child,SIGTERM);term_sent=1;force_resume=1;}
  if(child){int status=0;pid_t got=waitpid(child,&status,WNOHANG);if(got==child){
   child=0;if(!WIFEXITED(status)||WEXITSTATUS(status)!=0){force_resume=1;failures++;due=now+(failures<6?5000:60000);}
   else {force_resume=0;failures=0;due=now+(resuming?30000:0);}stable=0;
  }}
  s=sample();int active=exists("/tmp/u60-standby/active"),eligible=sb_eligible(&s);
  if(!child&&now>=due) {
   if(active&&(!eligible||force_resume||strcmp(phase(),"asleep"))) {resuming=1;child=launch("resume");}
   else if(!active&&eligible){if(!stable)stable=now;else if(now-stable>=3000){resuming=0;child=launch("pause");}}
   else stable=0;
   if(child<0){child=0;force_resume=1;due=now+5000;}
   else if(child){started=now;term_sent=0;}
  }
  poll(NULL,0,1000);
 }
 /* A procd restart will read the journal. Do not start services during system
  * shutdown, and do not kill a child flushing the existing Tailscale identity. */
 close(lock);return 0;
}
