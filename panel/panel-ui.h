/* Native 320x480 navigation and audited field mapping. Included after draw helpers. */
static const char *section_names[] = {"全部功能", "设备信息", "蜂窝网络", "流量与终端", "SIM 状态", "路由与安全", "USB 与网口", "电源与屏幕", "Wi-Fi", "频段与小区", "网络诊断", "Tailscale", "代理分流"};
static const char *section_notes[] = {"", "固件 / 电池 / 温度", "选网 / 信号质量", "在线设备 / 订阅用量", "卡槽 / 注册状态", "NAT / DHCP / DNS", "接口 / 当前角色", "亮度 / 省电 / 重启", "双频 / MLO / 状态", "锁定状态 / 恢复自动", "连通性 / 重载服务", "组网 / 登录 / 断开", "策略组 / 路由选择"};
struct field_spec { int sec; const char *obj, *method, *key, *label; };
static const struct field_spec fields[] = {
 {SEC_CELL,"zte_nwinfo_api","nwinfo_get_netinfo","network_type","网络制式"},
 {SEC_CELL,"zte_nwinfo_api","nwinfo_get_netinfo","net_select","选网模式"},
 {SEC_CELL,"zte_nwinfo_api","nwinfo_get_netinfo","network_provider_fullname","运营商"},
 {SEC_CELL,"zte_nwinfo_api","nwinfo_get_netinfo","nr5g_rsrp","5G RSRP"},
 {SEC_CELL,"zte_nwinfo_api","nwinfo_get_netinfo","nr5g_rsrq","5G RSRQ"},
 {SEC_CELL,"zte_nwinfo_api","nwinfo_get_netinfo","nr5g_snr","5G SNR"},
 {SEC_CELL,"zte_nwinfo_api","nwinfo_get_netinfo","lte_rsrp","LTE RSRP"},
 {SEC_CELL,"zte_nwinfo_api","nwinfo_get_netinfo","lte_snr","LTE SNR"},
 {SEC_DATA,"zwrt_router.api","router_get_user_list_num","access_total_num","在线终端"},
 {SEC_DATA,"zwrt_router.api","router_get_user_list_num","wireless_num","Wi-Fi 终端"},
 {SEC_DATA,"zwrt_router.api","router_get_user_list_num","lan_num","有线终端"},
 {SEC_DATA,"zwrt_router.api","router_get_user_list_num","offline_num","离线记录"},
 {SEC_SIM,"zwrt_zte_mdm.api","get_sim_info","sim_states","SIM 状态"},
 {SEC_SIM,"zwrt_zte_mdm.api","get_sim_info","current_sim_slot","当前卡槽"},
 {SEC_SIM,"zwrt_zte_mdm.api","get_sim_info","pin_status","PIN 状态"},
 {SEC_SIM,"zwrt_zte_mdm.api","get_sim_info","modem_main_state","调制解调器"},
 {SEC_ROUTER,"zwrt_router.api","router_get_firewall_para","firewall_enable","防火墙"},
 {SEC_ROUTER,"zwrt_router.api","router_get_firewall_para","nat_enable","NAT"},
 {SEC_ROUTER,"zwrt_router.api","router_get_firewall_para","dmz_enable","DMZ"},
 {SEC_ROUTER,"zwrt_router.api","router_get_firewall_para","portforward_enable","端口转发"},
 {SEC_ROUTER,"zwrt_router.api","router_get_firewall_para","portmapping_enable","端口映射"},
 {SEC_ROUTER,"zwrt_router.api","router_get_firewall_para","macipport_filter_enable","MAC/IP 过滤"},
 {SEC_ROUTER,"zwrt_router.api","router_get_firewall_para","remote_web_access_enable","远程管理"},
 {SEC_ROUTER,"zwrt_router.api","router_get_firewall_para","wan_ping_enable","WAN Ping"},
 {SEC_ROUTER,"zwrt_router.api","router_get_upnp","enable_upnp","UPnP"},
 {SEC_ROUTER,"zwrt_router.api","router_get_dhcp_router","lan_addr","LAN 地址"},
 {SEC_ROUTER,"zwrt_router.api","router_get_dhcp_router","lan_netmask","子网掩码"},
 {SEC_ROUTER,"zwrt_router.api","router_get_dhcp_router","ignore","DHCP ignore"},
 {SEC_ROUTER,"zwrt_router.api","router_get_dhcp_router","zte_start","地址池起点"},
 {SEC_ROUTER,"zwrt_router.api","router_get_dhcp_router","zte_end","地址池终点"},
 {SEC_ROUTER,"zwrt_router.api","router_get_dhcp_router","dhcpv6_mode","IPv6 分配"},
 {SEC_ROUTER,"zwrt_router.api","router_get_dns_para","wan_dns_mode","DNS 模式"},
 {SEC_ROUTER,"zwrt_router.api","router_get_dns_para","wan_prefer_dns_manual","首选 DNS"},
 {SEC_ROUTER,"zwrt_router.api","router_get_dns_para","wan_standby_dns_manual","备用 DNS"},
 {SEC_ROUTER,"zwrt_router.api","router_get_qos","qos_smart_switch","智能 QoS"},
 {SEC_USB,"zwrt_bsp.usb","list","mode","USB 模式"},
 {SEC_USB,"zwrt_bsp.usb","list","connect","USB 连接"},
 {SEC_USB,"zwrt_bsp.usb","list","usb2rj45","USB 网卡"},
 {SEC_USB,"zwrt_bsp.usb","list","typec_cc","Type-C 方向"},
 {SEC_USB,"zwrt_router.api","router_get_wan_mode_para","opms_wan_mode","WAN 工作模式"},
 {SEC_USB,"zwrt_router.api","router_get_dhcp_router","cable_opms_wan_mode","有线工作模式"},
 {SEC_WIFI,"zwrt_wlan","report","wifi_onoff","Wi-Fi 开关"},
 {SEC_WIFI,"zwrt_wlan","report","main2g_ssid","2.4 GHz 名称"},
 {SEC_WIFI,"zwrt_wlan","report","main5g_ssid","5 GHz 名称"},
 {SEC_WIFI,"zwrt_wlan","report","main2g_authmode","2.4 GHz 安全"},
 {SEC_WIFI,"zwrt_wlan","report","main5g_authmode","5 GHz 安全"},
 {SEC_WIFI,"zwrt_wlan","report","mlo_enable","MLO"},
 {SEC_WIFI,"zwrt_wlan","report","lbd_enable","双频引导"},
 {SEC_WIFI,"zwrt_wlan","report","dfs_status","DFS 状态"},
 {SEC_BAND,"zte_nwinfo_api","nwinfo_get_netinfo","nr5g_action_band","5G 当前频段"},
 {SEC_BAND,"zte_nwinfo_api","nwinfo_get_netinfo","wan_active_band","LTE 当前频段"},
 {SEC_BAND,"zte_nwinfo_api","nwinfo_get_netinfo","nr5g_pci","5G PCI"},
 {SEC_BAND,"zte_nwinfo_api","nwinfo_get_netinfo","nr5g_action_channel","5G 信道"},
 {SEC_BAND,"zte_nwinfo_api","nwinfo_get_netinfo","nr5g_bandwidth","5G 带宽"},
 {SEC_BAND,"zte_nwinfo_api","nwinfo_get_netinfo","lte_band_lock","LTE 锁频"},
 {SEC_BAND,"zte_nwinfo_api","nwinfo_get_netinfo","nr5g_sa_band_lock","SA 锁频"},
 {SEC_BAND,"zte_nwinfo_api","nwinfo_get_netinfo","nr5g_nsa_band_lock","NSA 锁频"},
 {SEC_BAND,"zte_nwinfo_api","nwinfo_get_netinfo","lock_lte_cell","LTE 锁小区"},
 {SEC_BAND,"zte_nwinfo_api","nwinfo_get_netinfo","lock_nr_cell","NR 锁小区"}
};
static void detail_add(struct app *a, const char *label, const char *value) {
 if(a->ndetail>=64)return;
 snprintf(a->detail[a->ndetail].label,48,"%s",label);
 snprintf(a->detail[a->ndetail++].value,120,"%s",value&&*value?value:"未提供");
}
static const char *switch_label(const char *s) { return !strcmp(s,"1")?"已开启":!strcmp(s,"0")?"已关闭":"未知"; }
static void refresh_detail(struct app *a) {
 char out[16384],v[120]; cJSON *root=NULL; const char *last=""; size_t i;
 a->ndetail=0;
 for(i=0;i<sizeof(fields)/sizeof(fields[0]);i++) {
  const struct field_spec *f=&fields[i]; if(f->sec!=a->more_sec)continue;
  if(strcmp(last,f->method)) {cJSON_Delete(root);root=NULL;last=f->method;if(ubus_json(f->obj,f->method,"{}",out,sizeof(out)))root=cJSON_Parse(out);}
  cJSON *item=cJSON_GetObjectItemCaseSensitive(root,f->key);
  if(cJSON_IsString(item))snprintf(v,sizeof(v),"%s",item->valuestring);
  else if(cJSON_IsNumber(item))snprintf(v,sizeof(v),"%.6g",item->valuedouble);
  else snprintf(v,sizeof(v),"%s",root?"未提供":"读取失败");
  if(strstr(f->key,"enable") || !strcmp(f->key,"wifi_onoff"))snprintf(v,sizeof(v),"%s",switch_label(v));
  detail_add(a,f->label,v);
 }
 cJSON_Delete(root);
 if(a->more_sec==SEC_DEV) {
  detail_add(a,"固件",a->fwver);snprintf(v,sizeof(v),"%d%%",a->bat);detail_add(a,"电量",v);
  snprintf(v,sizeof(v),"%d（原始值）",a->temp);detail_add(a,"温度读数",v);
  detail_add(a,"WAN 状态",a->wan);detail_add(a,"代理核心",a->clash_online?"已连接":"不可达");
 }
 if(a->more_sec==SEC_POWER) {
  snprintf(v,sizeof(v),"%d / 255",a->bl);detail_add(a,"屏幕亮度",v);
  detail_add(a,"省电模式",switch_label(a->saver));detail_add(a,"快速开机",switch_label(a->fastboot));
  if(!a->blank_sec)snprintf(v,sizeof(v),"关闭");
  else if(a->blank_sec<60)snprintf(v,sizeof(v),"%d 秒",a->blank_sec);
  else snprintf(v,sizeof(v),"%d 分钟",a->blank_sec/60);
  detail_add(a,"自动熄屏",v);
 }
 if(a->more_sec==SEC_DATA) {
  if(a->quota_ok){snprintf(v,sizeof(v),"%.1f / %.0f GiB",a->quota_remain_gb,a->quota_total_gb);detail_add(a,"订阅剩余 / 总量",v);}
  else detail_add(a,"订阅用量","暂不可用");
  {
   FILE *lf=fopen("/tmp/dhcp.leases","r");
   char line[192],mac[32],ip[32],host[64]; unsigned exp;
   int shown=0;
   if(lf){
    while(fgets(line,sizeof(line),lf) && shown<8){
     if(sscanf(line,"%u %31s %31s %63s",&exp,mac,ip,host)>=3){
      if(!strcmp(host,"*")||!host[0])snprintf(host,sizeof(host),"未命名");
      detail_add(a,host,ip);shown++;
     }
    }
    fclose(lf);
   }
   if(!shown)detail_add(a,"在线终端","当前租约表为空");
  }
 }
 if(a->more_sec==SEC_CELL) {
  char apnbuf[4096]; cJSON *apn=NULL;
  if(ubus_json("zwrt_apn_object","get_apn_at_cid","{\"cid\":1}",apnbuf,sizeof(apnbuf))) apn=cJSON_Parse(apnbuf);
  if(apn){
   const char *an=jstr(apn,"wanapn"); if(!an[0])an=jstr(apn,"apn");
   const char *pn=jstr(apn,"profilename");
   if(pn[0])detail_add(a,"APN 名称",pn);
   if(an[0])detail_add(a,"APN",an);
   cJSON_Delete(apn);
  }
 }
 if(a->more_sec==SEC_TS){detail_add(a,"连接状态",a->ts_state);detail_add(a,"组网地址",a->ts_ip);detail_add(a,"登录方式","授权链接在电脑端打开");}
 if(a->more_sec==SEC_DIAG){detail_add(a,"Mihomo API",a->clash_online?"可用":"不可达");detail_add(a,"Google 连通性",a->google);detail_add(a,"当前模式",mode_label(a->mode));}
 if(a->more_sec==SEC_USB)detail_add(a,"USB 网卡","插网卡后由原厂 WAN/LAN 自适应；Mac 电脑请在后台启用 ECM");
 if(a->more_sec==SEC_SIM)detail_add(a,"卡管理","PIN 修改请用完整后台扫码，避免小屏误操作");
 if(a->more_sec==SEC_WIFI)detail_add(a,"Wi-Fi","名称与密码不在此修改");
 if(a->detail_off>=a->ndetail)a->detail_off=0;
}
static void section_open(struct app *a,int sec) {a->more_sec=sec;a->detail_off=0;a->pending_action=0;refresh_detail(a);}
static void setting_row(struct drm_buf *b,struct app *a,int y,const char *label,const char *value,int id) {
 fill_round(b,12,y,308,y+53,10,COL_CARD);
 draw_text_clip(b,24,y+7,label,12,COL_MUTED,256);
 draw_text_clip(b,24,y+25,value,text_width(value,15)>260?12:15,COL_TEXT,260);
 if(id)hit_add(a,12,y,308,y+53,id);
}
static void draw_more(struct drm_buf *b,struct app *a) {
 int i,y; char info[80];
 if(a->more_sec==SEC_MENU) {
  int start=a->more_off?7:1;
  draw_text(b,16,64,"常用设置与完整状态",13,COL_MUTED);
  for(i=0;i<6;i++) {
   int sec=start+i,x=12+(i%2)*152;y=91+(i/2)*91;
   fill_round(b,x,y,x+144,y+81,12,COL_CARD);
   snprintf(info,sizeof(info),"%02d",sec);draw_text(b,x+12,y+9,info,12,COL_ACCENT);
   draw_text(b,x+12,y+29,section_names[sec],17,COL_TEXT);
   draw_text_clip(b,x+12,y+55,section_notes[sec],10,COL_MUTED,124);
   hit_add(a,x,y,x+144,y+81,HID_MENU0+sec);
  }
  chip(b,a,12,373,154,417,"常用功能",HID_SCROLL_UP,!a->more_off);
  chip(b,a,166,373,308,417,"更多功能",HID_SCROLL_DN,a->more_off);
  return;
 }
 draw_text(b,16,63,section_names[a->more_sec],20,COL_TEXT);
 if(a->more_sec==SEC_POLICY) {
  if(!a->ngroups){draw_text(b,20,140,"策略组暂不可用",16,COL_MUTED);return;}
  if(a->group_i<0 || a->group_i>=a->ngroups)a->group_i=0;
  struct group *g=&a->groups[a->group_i];
  chip(b,a,12,94,62,138,"‹",HID_GROUP_UP,0);
  draw_text_clip(b,72,107,g->name,15,COL_ACCENT,176);
  chip(b,a,258,94,308,138,"›",HID_GROUP_DN,0);
  if(a->detail_off>=g->nall)a->detail_off=0;
  for(i=0;i<4 && i+a->detail_off<g->nall;i++) {
   int k=i+a->detail_off;setting_row(b,a,150+i*55,g->all[k],!strcmp(g->now,g->all[k])?"当前选择":"轻点选择",HID_MEMBER0+k);
  }
  snprintf(info,sizeof(info),"%d / %d",a->detail_off/4+1,(g->nall+3)/4);
 } else {
  int pages=(a->ndetail+3)/4;if(pages<1)pages=1;
  draw_text(b,16,91,"实时读取 · 未知状态不视为关闭",11,COL_MUTED);
  for(i=0;i<4 && a->detail_off+i<a->ndetail;i++)setting_row(b,a,112+i*55,a->detail[a->detail_off+i].label,a->detail[a->detail_off+i].value,0);
  snprintf(info,sizeof(info),"%d / %d",a->detail_off/4+1,pages);
  y=337;
  if(a->more_sec==SEC_CELL){chip(b,a,12,y,106,y+40,"自动",HID_NET_AUTO,!strcmp(a->net_select,"WL_AND_5G"));chip(b,a,113,y,207,y+40,"仅 5G",HID_NET_5G,!strcmp(a->net_select,"Only_5G"));chip(b,a,214,y,308,y+40,"仅 4G",HID_NET_4G,!strcmp(a->net_select,"Only_LTE"));}
  else if(a->more_sec==SEC_ROUTER){chip(b,a,12,y,106,y+40,"NAT",HID_NAT,!strcmp(a->nat_on,"1"));chip(b,a,113,y,207,y+40,"UPnP",HID_UPNP,!strcmp(a->upnp_on,"1"));chip(b,a,214,y,308,y+40,"防火墙",HID_FW,!strcmp(a->fw_on,"1"));}
  else if(a->more_sec==SEC_POWER){
   chip(b,a,12,y,106,y+40,"省电",HID_SAVER,!strcmp(a->saver,"1"));
   chip(b,a,113,y,207,y+40,"快启",HID_FASTBOOT,!strcmp(a->fastboot,"1"));
   chip(b,a,214,y,308,y+40,"亮度",HID_BL,0);
   y=382;
   chip(b,a,12,y,64,y+36,"关",HID_BLANK_OFF,a->blank_sec==0);
   chip(b,a,70,y,124,y+36,"15秒",HID_BLANK_15,a->blank_sec==15);
   chip(b,a,130,y,184,y+36,"30秒",HID_BLANK_30,a->blank_sec==30);
   chip(b,a,190,y,244,y+36,"1分",HID_BLANK_60,a->blank_sec==60);
   chip(b,a,250,y,308,y+36,"5分",HID_BLANK_300,a->blank_sec==300);
  }
  else if(a->more_sec==SEC_DIAG){chip(b,a,12,y,154,y+40,"测 Google",HID_GOOGLE,0);chip(b,a,166,y,308,y+40,"重载 Clash",HID_CLASH_RELOAD,0);}
  else if(a->more_sec==SEC_TS){chip(b,a,12,y,154,y+40,"登录 / 连接",HID_TS_LOGIN,0);chip(b,a,166,y,308,y+40,"断开",HID_TS_DOWN,0);}
  else if(a->more_sec==SEC_BAND)chip(b,a,12,y,308,y+40,"解除锁频与锁小区",HID_BAND_RESET,0);
  else if(a->more_sec==SEC_DEV)chip(b,a,12,y,308,y+40,"重启设备",HID_REBOOT,0);
  else chip(b,a,12,y,308,y+40,"刷新本页",HID_REFRESH,0);
 }
 chip(b,a,12,385,86,426,"上一页",HID_SCROLL_UP,0);
 draw_text_center(b,90,230,398,info,12,COL_MUTED);
 chip(b,a,234,385,308,426,"下一页",HID_SCROLL_DN,0);
}
static const char *action_name(int id) {
 switch(id){case HID_NAT:return "切换 NAT";case HID_FW:return "切换防火墙";case HID_UPNP:return "切换 UPnP";case HID_REBOOT:return "重启设备";case HID_NET_AUTO:return "自动选网";case HID_NET_5G:return "仅使用 5G";case HID_NET_4G:return "仅使用 4G";case HID_BAND_RESET:return "解除频段与小区锁定";case HID_SAVER:return "切换省电";case HID_FASTBOOT:return "切换快速开机";case HID_TS_DOWN:return "断开 Tailscale";default:return "应用设置";}
}
static void draw_confirmation(struct drm_buf *b,struct app *a) {
 if(!a->pending_action)return;
 if(time(NULL)>a->confirm_until){a->pending_action=0;return;}
 hit_reset(a);fill_round_border(b,12,134,308,354,18,COL_BG2,COL_ACCENT);
 draw_text(b,28,154,"确认操作",20,COL_TEXT);
 draw_text_clip(b,28,191,action_name(a->pending_action),17,COL_ACCENT,260);
 draw_text(b,28,226,"可能短暂中断网络或设备服务",13,COL_MUTED);
 draw_text(b,28,249,"30 秒内确认；取消不改变设置",12,COL_MUTED);
 chip(b,a,28,292,153,336,"取消",HID_CANCEL,0);chip(b,a,165,292,291,336,"确认",HID_CONFIRM,1);
}
