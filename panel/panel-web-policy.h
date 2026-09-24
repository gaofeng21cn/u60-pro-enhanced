#ifndef U60_WEB_POLICY_H
#define U60_WEB_POLICY_H
#include <string.h>
#include "cJSON.h"
static int web_session_valid(const char *s) {
 if(!s||strlen(s)!=32)return 0;int nonzero=0;
 for(int i=0;i<32;i++){if(!strchr("0123456789abcdef",s[i]))return 0;if(s[i]!='0')nonzero=1;}
 return nonzero;
}
static int web_action_allowed(const char *s) {
 static const char *const names[]={"state",
 "web.clash.state","web.clash.diagnose","web.clash.subscription_save","web.clash.subscription_delete","web.clash.connection_close","web.clash.favorite","web.clash.mode","web.clash.autoselect","web.tailscale.state","web.tailscale.hostname","web.tailscale.routes","web.tailscale.netcheck",
 "wifi.ap","wifi.channel","wifi.password","wifi.power","wifi.relay.connect","wifi.relay.off","wifi.relay.on","wifi.relay.scan","wifi.relay.select","wifi.show_password","wifi.sleep","wifi.ssid",
 "tailscale.accept_dns","tailscale.accept_routes","tailscale.advertise_lan","tailscale.allow_lan","tailscale.connected","tailscale.exit","tailscale.lan_gateway","tailscale.offer_exit","tailscale.peer_test","network.tailscale_mode",
 "usb.power_role","usb.role","clash.close_connections","clash.delay","clash.diagnose","clash.favorite","clash.flush_dns","clash.global_target","clash.history","clash.mode","clash.provider","clash.rule_add","clash.rule_edit","clash.rule_provider","clash.select","clash.service",
 "usage.adjust","usage.allowance","usage.configure","usage.cycle","usage.refresh","usage.remaining","usage.warning","charge.policy","charge.manual","power.standby","system.saver",
 "diag.connections","diag.web","band.lte","band.nsa","band.sa","cell.apn","cell.mode","router.dmz","router.dns","router.firewall","router.lan","router.nat","router.portforward","router.portmapping","router.upnp",
 "signal.lock","signal.lock.lte","signal.lock.nr","signal.lock.neighbor","signal.neighbors","signal.reset","signal.scan","signal.serving","sms.delete","sms.list","sms.read"};
 if(!s)return 0;for(unsigned i=0;i<sizeof(names)/sizeof(*names);i++)if(!strcmp(s,names[i]))return 1;return 0;
}
static int web_request_valid(const cJSON *r) {
 if(!cJSON_IsObject(r))return 0;
 const cJSON *action=cJSON_GetObjectItemCaseSensitive(r,"action"),*args=cJSON_GetObjectItemCaseSensitive(r,"args");
 if(!cJSON_IsString(action)||!web_action_allowed(action->valuestring)||!cJSON_IsObject(args))return 0;
 int n=0;const cJSON *v;cJSON_ArrayForEach(v,r){if(strcmp(v->string,"action")&&strcmp(v->string,"args"))return 0;n++;}
 return n==2;
}
#endif
