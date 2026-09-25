#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "../panel/panel-menu-layout.h"
int main(void){
 cJSON *r=cJSON_Parse("{\"sections\":[{\"id\":\"system\",\"items\":[{\"id\":\"cpu\",\"type\":\"info\"},{\"id\":\"system.saver\",\"type\":\"toggle\",\"enabled\":false,\"label\":\"省电模式\",\"action\":\"system.saver\"},{\"id\":\"blank\",\"type\":\"choice\",\"action\":\"screen.blank\"},{\"id\":\"theme\",\"type\":\"choice\",\"action\":\"screen.theme\"},{\"id\":\"brightness\",\"type\":\"choice\",\"action\":\"screen.brightness\"}]},{\"id\":\"battery\",\"title\":\"电池与充电\",\"items\":[{\"id\":\"capacity\",\"type\":\"info\"},{\"id\":\"power.standby\",\"type\":\"choice\",\"label\":\"待机省电\",\"value\":\"深度省电\",\"action\":\"power.standby\",\"choices\":[{\"label\":\"深度省电\",\"args\":{\"mode\":\"deep\"}}]},{\"id\":\"charge.manual\",\"type\":\"choice\",\"action\":\"charge.manual\"}]},{\"id\":\"router\",\"items\":[{\"id\":\"status\",\"type\":\"info\",\"enabled\":false},{\"id\":\"unavailable\",\"type\":\"form\",\"enabled\":false,\"action\":\"blocked\"},{\"id\":\"lan\",\"type\":\"form\",\"action\":\"router.lan\",\"args\":{\"sentinel\":42}},{\"id\":\"dns\",\"type\":\"form\",\"action\":\"router.dns\"}]}]}");
 assert(r);cJSON *items=cJSON_GetObjectItem(menu_section(r,"router"),"items"),*lan=cJSON_GetArrayItem(items,2);char *before=cJSON_PrintUnformatted(lan);
 panel_menu_layout(r);
 assert(cJSON_GetArrayItem(items,0)==lan);assert(!strcmp(menu_str(cJSON_GetArrayItem(items,1),"id"),"dns"));
 assert(!strcmp(menu_str(cJSON_GetArrayItem(items,2),"id"),"status"));assert(!strcmp(menu_str(cJSON_GetArrayItem(items,3),"id"),"menu.unavailable"));assert(!*menu_str(cJSON_GetArrayItem(items,3),"action"));
 char *after=cJSON_PrintUnformatted(lan);assert(!strcmp(before,after));cJSON_free(before);cJSON_free(after);
 items=cJSON_GetObjectItem(menu_section(r,"system"),"items");assert(cJSON_GetArraySize(items)==4);
 const char *ids[]={"theme","brightness","blank","cpu"};for(int n=0;n<4;n++)assert(!strcmp(menu_str(cJSON_GetArrayItem(items,n),"id"),ids[n]));
 items=cJSON_GetObjectItem(menu_section(r,"battery"),"items");assert(cJSON_GetArraySize(items)==3);cJSON *standby=cJSON_GetArrayItem(items,0),*detail=cJSON_GetArrayItem(items,2);
 assert(!strcmp(menu_str(standby,"label"),"待机服务策略"));assert(!strcmp(menu_str(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(standby,"choices"),0),"args"),"mode"),"deep"));
 assert(!strcmp(menu_str(detail,"id"),"menu.battery_details"));assert(strstr(menu_str(detail,"detail"),"原厂节能开关"));assert(!*menu_str(detail,"action"));
 before=cJSON_PrintUnformatted(r);panel_menu_layout(r);after=cJSON_PrintUnformatted(r);assert(!strcmp(before,after));cJSON_free(before);cJSON_free(after);cJSON_Delete(r);
 /* Proxy daily actions stay on the first page; management has a real child
  * menu and retains the original form, args and confirmation boundaries. */
 r=cJSON_Parse("{\"sections\":[{\"id\":\"clash\",\"title\":\"Clash\",\"items\":[{\"id\":\"add-rule\",\"type\":\"form\",\"label\":\"添加分流规则\",\"action\":\"clash.rule_add\",\"confirm\":true,\"fields\":[{\"key\":\"pattern\"}]},{\"id\":\"coverage\",\"type\":\"info\",\"label\":\"代理覆盖\",\"value\":\"IPv4 TCP 与 DNS 已接管\"},{\"id\":\"service\",\"type\":\"choice\",\"label\":\"代理开关\",\"action\":\"clash.service\"},{\"id\":\"node\",\"type\":\"choice\",\"label\":\"专线节点\",\"action\":\"clash.select\",\"args\":{\"group\":\"G\"}},{\"id\":\"mode\",\"type\":\"choice\",\"label\":\"分流模式\",\"action\":\"clash.mode\"}]}]}");
 items=cJSON_GetObjectItem(menu_section(r,"clash"),"items");cJSON *rule=cJSON_GetArrayItem(items,0);before=cJSON_PrintUnformatted(rule);
 panel_menu_layout(r);
 const char *clash_ids[]={"coverage","service","mode","node","menu.clash-more"};for(int n=0;n<5;n++)assert(!strcmp(menu_str(cJSON_GetArrayItem(items,n),"id"),clash_ids[n]));
 assert(!strcmp(menu_str(cJSON_GetArrayItem(items,0),"type"),"report"));
 cJSON *more=menu_section(r,"clash-more");assert(!strcmp(menu_str(more,"parent"),"clash"));
 assert(cJSON_GetArrayItem(cJSON_GetObjectItem(more,"items"),0)==rule);after=cJSON_PrintUnformatted(rule);assert(!strcmp(before,after));cJSON_free(before);cJSON_free(after);
 before=cJSON_PrintUnformatted(r);panel_menu_layout(r);after=cJSON_PrintUnformatted(r);assert(!strcmp(before,after));cJSON_free(before);cJSON_free(after);cJSON_Delete(r);
 r=cJSON_Parse("{\"sections\":[{\"id\":\"tailscale\",\"items\":[{\"id\":\"mode\",\"type\":\"choice\",\"action\":\"network.tailscale_mode\",\"confirm\":true},{\"id\":\"backend\",\"type\":\"info\",\"value\":\"Stopped\"},{\"id\":\"connected\",\"type\":\"toggle\",\"action\":\"tailscale.connected\"},{\"id\":\"peer_0\",\"type\":\"info\"}]}]}");
 panel_menu_layout(r);items=cJSON_GetObjectItem(menu_section(r,"tailscale"),"items");assert(cJSON_GetArraySize(items)==3);assert(!strcmp(menu_str(cJSON_GetArrayItem(items,0),"id"),"backend"));more=menu_section(r,"tailscale-more");assert(!strcmp(menu_str(more,"parent"),"tailscale"));assert(cJSON_GetArraySize(cJSON_GetObjectItem(more,"items"))==2);assert(!strcmp(menu_str(cJSON_GetArrayItem(items,0),"value"),"已停止"));cJSON *peers=cJSON_GetArrayItem(cJSON_GetObjectItem(more,"items"),1);assert(strstr(menu_str(peers,"detail"),"不能据此判断"));before=cJSON_PrintUnformatted(r);panel_menu_layout(r);after=cJSON_PrintUnformatted(r);assert(!strcmp(before,after));cJSON_free(before);cJSON_free(after);cJSON_Delete(r);
 r=cJSON_Parse("{\"sections\":[{\"id\":\"battery\",\"items\":[]}]}");panel_menu_layout(r);cJSON_Delete(r);
 r=cJSON_Parse("{\"sections\":[{\"id\":\"battery\",\"items\":[{\"id\":\"charge.policy_status\",\"label\":\"策略状态\",\"type\":\"info\",\"value\":\"上次操作异常，自动控制暂停\"},{\"id\":\"usb.charge_state\",\"label\":\"实际状态\",\"type\":\"info\",\"value\":\"正在充电\"}]}]}");panel_menu_layout(r);
 detail=cJSON_GetArrayItem(cJSON_GetObjectItem(menu_section(r,"battery"),"items"),0);assert(strstr(menu_str(detail,"value"),"暂停"));assert(strstr(menu_str(detail,"detail"),"正在充电"));cJSON_Delete(r);
 r=cJSON_Parse("{\"data\":{\"clash\":{\"connections\":0}},\"sections\":[{\"id\":\"clash\",\"items\":[{\"id\":\"connections\",\"type\":\"action\",\"action\":\"clash.close_connections\",\"value\":\"0 个连接\",\"enabled\":true}]}]}");panel_menu_layout(r);items=cJSON_GetObjectItem(menu_section(r,"clash-more"),"items");assert(!strcmp(menu_str(cJSON_GetArrayItem(items,0),"type"),"info"));assert(!strcmp(menu_str(cJSON_GetArrayItem(items,0),"value"),"暂无活动连接"));cJSON_Delete(r);
 puts("PASS: controls precede status/unavailable; stable payloads; distinct power controls; screen priorities; idempotence; partial snapshot");
}
