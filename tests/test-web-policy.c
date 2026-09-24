#include <assert.h>
#include "panel-web-policy.h"
int main(void){
 assert(!web_session_valid(NULL));assert(!web_session_valid("00000000000000000000000000000000"));
 assert(!web_session_valid("123"));assert(!web_session_valid("fffffffffffffffffffffffffffffffZ"));
 assert(web_session_valid("0123456789abcdef0123456789abcdef"));
 const char *good[]={"{\"action\":\"state\",\"args\":{}}","{\"action\":\"web.clash.diagnose\",\"args\":{}}","{\"action\":\"clash.diagnose\",\"args\":{}}","{\"action\":\"wifi.relay.connect\",\"args\":{\"password\":\"not-a-real-password\"}}"};
 for(unsigned i=0;i<sizeof(good)/sizeof(*good);i++){cJSON*r=cJSON_Parse(good[i]);assert(web_request_valid(r));cJSON_Delete(r);}
 const char *bad[]={"{}","[]","{\"action\":\"system.power\",\"args\":{}}","{\"action\":\"system.fastboot\",\"args\":{}}","{\"action\":\"exec\",\"args\":{}}","{\"action\":\"state\",\"args\":{},\"fixture\":\"/tmp/x\"}","{\"action\":\"state\",\"args\":{},\"action\":\"state\"}"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){cJSON*r=cJSON_Parse(bad[i]);assert(!web_request_valid(r));cJSON_Delete(r);}
 return 0;
}
