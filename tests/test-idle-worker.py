#!/usr/bin/env python3
"""Exercise the production worker scheduler: blanked reads stop, actions finish."""
import pathlib,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).parents[1]
class IdleWorker(unittest.TestCase):
 def test_sleep_wake_and_inflight_user_action(self):
  source=(ROOT/'panel/panel-worker.h').read_text()
  body='static int worker_poll(struct app *a){'+source.split('static int worker_poll(struct app *a){',1)[1].split('/* Fast local telemetry',1)[0]
  stub=r'''
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include "cJSON.h"
#define WORKER_CAP 4096
struct app {int blanked; struct {int busy;long status_until;char status[256];cJSON*snapshot;}shell;};
struct {int pid,fd,action;long started,timeout;char*buf;size_t used;}pw;
struct {int pid;}screen_notice;
static long next_snapshot,clock_ms=100000;
static int starts;
static long now_ms(void){return clock_ms;}
static const char*jstr(cJSON*r,const char*k){cJSON*v=cJSON_GetObjectItem(r,k);return cJSON_IsString(v)?v->valuestring:"";}
static void sh_open_item(struct app*a,cJSON*r){(void)a;(void)r;}
static void sh_show_report(struct app*a,cJSON*r){(void)a;(void)r;}
static void sh_action_result(struct app*a,cJSON*r){a->shell.busy=0;snprintf(a->shell.status,sizeof(a->shell.status),"%s",jstr(r,"message"));}
static void append_screen_controls(struct app*a){(void)a;}
static void worker_clear(int fail){(void)fail;if(pw.fd>=0)close(pw.fd);free(pw.buf);memset(&pw,0,sizeof(pw));pw.fd=-1;}
static int worker_start(cJSON*r,int action){assert(!strcmp(jstr(r,"action"),"state"));assert(!action);starts++;return 1;}
'''
  main=r'''
int main(void){
 struct app a={.blanked=1};
 for(int n=0;n<100;n++){clock_ms+=1000;worker_poll(&a);}assert(starts==0);
 a.blanked=0;next_snapshot=0;worker_poll(&a);assert(starts==1);
 clock_ms+=1000;worker_poll(&a);assert(starts==1);
 a.blanked=1;clock_ms+=60000;worker_poll(&a);assert(starts==1);
 /* A user write already running at blanking may complete normally. */
 int fds[2];assert(!pipe(fds));const char*reply="{\"message\":\"done\"}";assert(write(fds[1],reply,strlen(reply))==(ssize_t)strlen(reply));close(fds[1]);
 pw.pid=123;pw.fd=fds[0];pw.action=1;pw.started=clock_ms;pw.timeout=45000;pw.buf=malloc(WORKER_CAP+1);a.shell.busy=1;
 assert(worker_poll(&a)==1);assert(!a.shell.busy&&!pw.pid);assert(!strcmp(a.shell.status,"done"));assert(starts==1);
 a.blanked=0;next_snapshot=0;worker_poll(&a);assert(starts==2);

 /* Slow backend state never owns fast local route/traffic telemetry. */
 a.shell.snapshot=cJSON_Parse("{\"data\":{\"physical_iface\":\"u60sta\",\"download_bps\":12345,\"cpu_percent\":12.5,\"wifi_status\":\"old\"},\"sections\":[]}");
 a.blanked=1;
 for(int turn=0;turn<3;turn++){
  if(turn==1)cJSON_ReplaceItemInObject(cJSON_GetObjectItem(a.shell.snapshot,"data"),"physical_iface",cJSON_CreateString("rmnet_data0"));
  assert(!pipe(fds));const char*state="{\"data\":{\"wifi_status\":\"fresh\",\"download_bps\":0,\"cpu_percent\":0},\"sections\":[]}";
  assert(write(fds[1],state,strlen(state))==(ssize_t)strlen(state));close(fds[1]);
  pw.pid=123;pw.fd=fds[0];pw.action=0;pw.started=clock_ms;pw.timeout=45000;pw.buf=malloc(WORKER_CAP+1);
  assert(worker_poll(&a)==1);cJSON*d=cJSON_GetObjectItem(a.shell.snapshot,"data");
  assert(!strcmp(jstr(d,"physical_iface"),turn==0?"u60sta":"rmnet_data0"));
  assert(cJSON_GetObjectItem(d,"download_bps")->valuedouble==12345);
  assert(cJSON_GetObjectItem(d,"cpu_percent")->valuedouble==12.5);
  assert(!strcmp(jstr(d,"wifi_status"),"fresh"));
 }
 cJSON_Delete(a.shell.snapshot);
 puts("PASS: blanked reads, in-flight action, local route/traffic survives snapshots, real cellular fallback remains visible");
}
'''
  with tempfile.TemporaryDirectory() as td:
   p=pathlib.Path(td);(p/'t.c').write_text(stub+body+main)
   subprocess.run(['cc','-O1','-fsanitize=address,undefined','-I',str(ROOT/'panel/vendor'),str(p/'t.c'),str(ROOT/'panel/vendor/cJSON.c'),'-o',str(p/'t')],check=True)
   subprocess.run([str(p/'t')],check=True,timeout=5)
if __name__=='__main__':unittest.main()
