/* Original-web authenticated UBUS adapter. No HTTP listener or shell command.
 * Each job belongs to one live stock session, held only in process memory.
 * Jobs are polled to survive the stock HTTP request timeout for radio actions.
 * No credentials, requests or results are written to logs or temporary files. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <libubus.h>
#include <libubox/blobmsg_json.h>
#include "cJSON.h"
#include "panel-web-policy.h"
#define API_LIST(X) X(ubus_connect) X(ubus_lookup_id) X(ubus_invoke_fd) X(ubus_free) X(ubus_add_object) X(ubus_send_reply) X(blob_buf_init) X(blob_buf_free) X(blobmsg_add_json_from_string) X(blobmsg_format_json_with_cb) X(uloop_init) X(uloop_fd_add) X(uloop_timeout_set) X(uloop_process_add) X(uloop_process_delete) X(uloop_run_timeout) X(uloop_done)
#define DECLARE(f) static __typeof__(&f) p_##f;
API_LIST(DECLARE)
static int load_api(void){
 const char *libs[]={"/lib/libubox.so.20230523","/lib/libubus.so.20230605","/lib/libblobmsg_json.so.20230523"};
 for(unsigned i=0;i<sizeof(libs)/sizeof(*libs);i++)if(!dlopen(libs[i],RTLD_NOW|RTLD_GLOBAL))return 0;
#define LOAD(f) *(void **)(&p_##f)=dlsym(RTLD_DEFAULT,#f);if(!p_##f)return 0;
 API_LIST(LOAD)
 return 1;
}
static long long now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static const char *str(const cJSON *r,const char *key){cJSON*v=cJSON_GetObjectItemCaseSensitive(r,key);return cJSON_IsString(v)?v->valuestring:"";}
static cJSON *answer(int ok,const char *message){cJSON*r=cJSON_CreateObject();cJSON_AddBoolToObject(r,"ok",ok);if(message)cJSON_AddStringToObject(r,"message",message);return r;}
static int send_json(struct ubus_context *ctx,struct ubus_request_data *req,cJSON*r){
 struct blob_buf b={0};p_blob_buf_init(&b,0);char*s=cJSON_PrintUnformatted(r);int rc=UBUS_STATUS_UNKNOWN_ERROR;
 if(s&&p_blobmsg_add_json_from_string(&b,s))rc=p_ubus_send_reply(ctx,req,b.head);
 if(s){memset(s,0,strlen(s));free(s);}p_blob_buf_free(&b);cJSON_Delete(r);return rc;
}
static void access_reply(struct ubus_request*r,int type,struct blob_attr*msg){
 (void)type;char*s=msg?p_blobmsg_format_json_with_cb(msg,true,NULL,NULL,-1):NULL;cJSON*j=s?cJSON_Parse(s):NULL;
 *(int*)r->priv=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j,"access"));cJSON_Delete(j);free(s);
}
static int authenticated(const char *session){
 if(!web_session_valid(session))return 0;
 struct ubus_context *ctx=p_ubus_connect(NULL);if(!ctx)return 0;uint32_t id;int allowed=0;
 cJSON*j=cJSON_CreateObject();cJSON_AddStringToObject(j,"ubus_rpc_session",session);cJSON_AddStringToObject(j,"scope","ubus");cJSON_AddStringToObject(j,"object","zwrt_u60_panel");cJSON_AddStringToObject(j,"function","start");
 char*s=cJSON_PrintUnformatted(j);struct blob_buf b={0};p_blob_buf_init(&b,0);
 if(!p_ubus_lookup_id(ctx,"session",&id)&&s&&p_blobmsg_add_json_from_string(&b,s))
  if(p_ubus_invoke_fd(ctx,id,"access",b.head,access_reply,&allowed,1500,-1))allowed=0;
 if(s){memset(s,0,strlen(s));free(s);}cJSON_Delete(j);p_blob_buf_free(&b);p_ubus_free(ctx);return allowed;
}
#define OUTPUT_CAP (512*1024)
static struct {pid_t pid;int fd,done,delivered,exited,exit_status,read_only;long long began,finished;char id[33],owner[33];size_t used;char output[OUTPUT_CAP];} job;
static struct uloop_timeout tick;
static struct uloop_process child_watch;
static void child_exited(struct uloop_process *p,int status){(void)p;job.pid=0;job.exited=1;job.exit_status=status;}
static void job_fail(const char *message){
 if(job.pid){p_uloop_process_delete(&child_watch);kill(-job.pid,SIGKILL);waitpid(job.pid,NULL,0);job.pid=0;}
 if(job.fd>=0){close(job.fd);job.fd=-1;}
 memset(job.output,0,sizeof(job.output));cJSON*r=answer(0,message);char*s=cJSON_PrintUnformatted(r);
 snprintf(job.output,sizeof(job.output),"%s",s?s:"{}");free(s);cJSON_Delete(r);job.done=1;job.finished=now_ms();
}
static void job_tick(struct uloop_timeout *t){
 (void)t;if(job.done){if(now_ms()-job.finished>=120000){memset(&job,0,sizeof(job));job.fd=-1;}else p_uloop_timeout_set(&tick,1000);return;}
 if(!*job.id)return;
 while(job.fd>=0){ssize_t n=read(job.fd,job.output+job.used,sizeof(job.output)-1-job.used);
  if(n>0){job.used+=(size_t)n;if(job.used>=sizeof(job.output)-1){job_fail("Response too large; refresh state");break;}}
  else if(n==0){close(job.fd);job.fd=-1;break;}
  else {if(errno!=EAGAIN&&errno!=EINTR)job_fail("Controller pipe failed");break;}
 }
 if(!job.done){
  if(job.exited&&job.fd<0){job.output[job.used]=0;
   cJSON*r=cJSON_Parse(job.output);int valid=WIFEXITED(job.exit_status)&&WEXITSTATUS(job.exit_status)==0&&cJSON_IsObject(r);cJSON_Delete(r);
   if(!valid)job_fail("Controller failed; refresh state before retrying");else {job.done=1;job.finished=now_ms();}
  }else if(now_ms()-job.began>230000)job_fail("Operation timed out; refresh state before retrying");
 }
 p_uloop_timeout_set(&tick,job.done?1000:100);
}
static int launch(const char *input,const char *owner,int read_only,int advanced){
 int op[2];if(pipe(op))return 0;pid_t p=fork();if(p<0){close(op[0]);close(op[1]);return 0;}
 if(!p){
  setpgid(0,0);int ip[2];if(pipe(ip))_exit(127);pid_t writer=fork();if(writer<0)_exit(127);
  if(!writer){close(ip[0]);close(op[0]);close(op[1]);size_t sent=0,n=strlen(input);while(sent<n){ssize_t k=write(ip[1],input+sent,n-sent);if(k<=0)_exit(1);sent+=(size_t)k;}close(ip[1]);_exit(0);}
  close(ip[1]);dup2(ip[0],0);dup2(op[1],1);int nul=open("/dev/null",O_WRONLY);if(nul>=0)dup2(nul,2);
  for(int fd=3;fd<1024;fd++)close(fd);
  const char *program=advanced?"/data/u60-panel/panel-web-control":"/data/u60-panel/panel-control";
  char *av[]={(char *)program,NULL},*env[]={"PATH=/usr/sbin:/usr/bin:/sbin:/bin","LANG=C",NULL};
  execve(program,av,env);_exit(127);
 }
 setpgid(p,p);close(op[1]);memset(&job,0,sizeof(job));job.pid=p;job.fd=op[0];fcntl(job.fd,F_SETFL,O_NONBLOCK);fcntl(job.fd,F_SETFD,FD_CLOEXEC);
 unsigned char random[16];int rd=open("/dev/urandom",O_RDONLY);if(rd<0||read(rd,random,sizeof(random))!=sizeof(random)){if(rd>=0)close(rd);job_fail("Random source unavailable");return 0;}close(rd);
 for(int i=0;i<16;i++)snprintf(job.id+2*i,3,"%02x",random[i]);snprintf(job.owner,sizeof(job.owner),"%s",owner);job.began=now_ms();job.read_only=read_only;child_watch.pid=p;child_watch.cb=child_exited;p_uloop_process_add(&child_watch);tick.cb=job_tick;p_uloop_timeout_set(&tick,100);return 1;
}
static const struct blobmsg_policy policy[]={ {.name="session",.type=BLOBMSG_TYPE_STRING},{.name="request",.type=BLOBMSG_TYPE_STRING},{.name="id",.type=BLOBMSG_TYPE_STRING} };
static int handle(struct ubus_context *ctx,struct ubus_object *obj,struct ubus_request_data *req,const char *method,struct blob_attr *msg){
 (void)obj;if(!msg||blob_len(msg)>20000)return UBUS_STATUS_INVALID_ARGUMENT;
 char *body=p_blobmsg_format_json_with_cb(msg,true,NULL,NULL,-1);cJSON*r=body?cJSON_Parse(body):NULL;
 if(body){memset(body,0,strlen(body));free(body);}if(!cJSON_IsObject(r)){cJSON_Delete(r);return UBUS_STATUS_INVALID_ARGUMENT;}
 const char *session=str(r,"session");if(!authenticated(session)){cJSON_Delete(r);return UBUS_STATUS_PERMISSION_DENIED;}
 cJSON *out=NULL;
 if(!strcmp(method,"start")){
  const char *input=str(r,"request");size_t n=strlen(input);cJSON *command=n&&n<16384?cJSON_ParseWithOpts(input,NULL,1):NULL;
  if(!web_request_valid(command))out=answer(0,"Unsupported or invalid request");
  else if(*job.id&&job.read_only&&!strcmp(str(command,"action"),"state")&&!strcmp(job.owner,session)&&(!job.done||!job.delivered)){
   out=answer(1,NULL);cJSON_AddStringToObject(out,"id",job.id);
  }
  else if((*job.id&&!job.done)||(job.done&&!job.delivered&&now_ms()-job.finished<120000))out=answer(0,"Another web operation is in progress");
  else if(!launch(input,session,!strcmp(str(command,"action"),"state"),!strncmp(str(command,"action"),"web.",4)))out=answer(0,"Unable to start controller");
  else {out=answer(1,NULL);cJSON_AddStringToObject(out,"id",job.id);}
  cJSON_Delete(command);
 }else {
  if(!*job.id||strcmp(session,job.owner)||strcmp(str(r,"id"),job.id))out=answer(0,"Job unavailable or expired; refresh state");
  else {out=answer(1,NULL);cJSON_AddBoolToObject(out,"done",job.done);if(job.done){job.delivered=1;cJSON *payload=cJSON_Parse(job.output);cJSON_AddItemToObject(out,"payload",payload?payload:answer(0,"Invalid result"));}}
 }
 char *mutable_session=(char*)session;if(*mutable_session)memset(mutable_session,0,strlen(mutable_session));
 cJSON_Delete(r);return send_json(ctx,req,out);
}
static const struct ubus_method methods[]={UBUS_METHOD("start",handle,policy),UBUS_METHOD("result",handle,policy)};
static struct ubus_object_type type=UBUS_OBJECT_TYPE("u60.panel.web",methods);
static struct ubus_object object={.name="zwrt_u60_panel",.type=&type,.methods=methods,.n_methods=ARRAY_SIZE(methods)};
int main(void){
 if(getuid()!=0||!load_api())return 1;signal(SIGPIPE,SIG_IGN);job.fd=-1;
 if(p_uloop_init())return 1;struct ubus_context*ctx=p_ubus_connect(NULL);if(!ctx)return 1;
 if(p_ubus_add_object(ctx,&object)){p_ubus_free(ctx);return 1;}
 p_uloop_fd_add(&ctx->sock,ULOOP_BLOCKING|ULOOP_READ);p_uloop_run_timeout(-1);
 if(job.pid)job_fail("Service stopped");memset(&job,0,sizeof(job));p_ubus_free(ctx);p_uloop_done();return 0;
}
