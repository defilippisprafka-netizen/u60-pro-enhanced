/* Exact route hashes enumerated from the connected factory menu. */
struct web_setting { int sec; const char *title,*route; };
static const struct web_setting web_settings[] = {
 {SEC_DEV,"系统设置","system_setting"},{SEC_DEV,"配置备份与恢复","configuration"},{SEC_DEV,"系统升级","ota_update"},{SEC_DEV,"时间同步","SNTP"},{SEC_DEV,"管理密码","password_management"},{SEC_DEV,"开发者选项","developer_options"},{SEC_DEV,"NFC 设置","nfc_setting"},
 {SEC_CELL,"移动网络","mobile_network"},{SEC_CELL,"APN 设置","apn_setting"},{SEC_CELL,"网络信息","network_info"},{SEC_CELL,"IMS 设置","ims_settings"},
 {SEC_DATA,"流量提醒","traffic_alert"},{SEC_DATA,"在线终端","station_info"},{SEC_DATA,"离线终端","offline_info"},{SEC_DATA,"智能守护","intelligent_guardianship"},
 {SEC_SIM,"双卡切换","dual_sim_switch"},{SEC_SIM,"PIN 管理","pin_management"},{SEC_SIM,"短信列表","smslist"},{SEC_SIM,"SIM 短信","sim_messages"},{SEC_SIM,"短信设置","sms_setting"},
 {SEC_ROUTER,"路由设置","router_setting"},{SEC_ROUTER,"DNS 设置","dns_settings"},{SEC_ROUTER,"地址绑定","bind_addr_lan"},{SEC_ROUTER,"系统安全","system_security"},{SEC_ROUTER,"更多防火墙设置","firewall_more"},{SEC_ROUTER,"端口过滤","port_filter"},{SEC_ROUTER,"端口转发","port_forward"},{SEC_ROUTER,"端口映射","port_map"},{SEC_ROUTER,"网址过滤","url_filter"},{SEC_ROUTER,"VPN 客户端","vpn_client"},{SEC_ROUTER,"动态 DNS","ddns_settings"},
 {SEC_USB,"路由与网口设置","router_setting"},{SEC_USB,"网络信息","network_info"},
 {SEC_POWER,"供电设置","power_supply"},{SEC_POWER,"省电设置","power_save"},{SEC_POWER,"快速开机","fastboot"},{SEC_POWER,"休眠设置","sleep_mode"},
 {SEC_WIFI,"主 Wi-Fi","wifi_main"},{SEC_WIFI,"访客 Wi-Fi","wifi_guest"},{SEC_WIFI,"Wi-Fi 信道","wifiChannel"},{SEC_WIFI,"高级 Wi-Fi","wifi_advance"},{SEC_WIFI,"无线过滤","wireless_filter"},{SEC_WIFI,"WPS","wps"},
 {SEC_BAND,"开发者频段选项","developer_options"},{SEC_BAND,"网络信息","network_info"},
 {SEC_DIAG,"网络诊断","diagnosis"},{SEC_DIAG,"Ping 日志","ping_log"},{SEC_DIAG,"抓包工具","tcpdump_menu"},{SEC_DIAG,"基带日志","modem_log"},{SEC_DIAG,"系统日志等级","syslog_level"},{SEC_DIAG,"追踪工具","TracingTool"},
 {SEC_TS,"原厂 VPN 设置","vpn_client"},{SEC_POLICY,"原厂路由设置","router_setting"}
};
static int web_matches(struct app *a,int *indices) {
 int n=0;for(size_t i=0;i<sizeof(web_settings)/sizeof(web_settings[0]);i++)if(a->more_sec==SEC_MENU||a->more_sec==web_settings[i].sec)indices[n++]=(int)i;return n;
}
static void draw_web_settings(struct drm_buf *b,struct app *a) {
 int ids[64],n=web_matches(a,ids);char line[128];
 hit_reset(a);fill(b,COL_BG);draw_text(b,16,14,"完整后台",21,COL_TEXT);
 draw_text(b,16,43,"手机连接设备 Wi-Fi 后扫码",12,COL_MUTED);
 if(a->web_selected>=0 && a->web_selected<(int)(sizeof(web_settings)/sizeof(web_settings[0]))) {
  const struct web_setting *w=&web_settings[a->web_selected];
  draw_text_clip(b,16,76,w->title,19,COL_TEXT,288);
  if(a->lan_address[0]){
   uint8_t tmp[qrcodegen_BUFFER_LEN_MAX],qr[qrcodegen_BUFFER_LEN_MAX];char url[200];
   snprintf(url,sizeof(url),"http://%s/#%s",a->lan_address,w->route);
   if(qrcodegen_encodeText(url,tmp,qr,qrcodegen_Ecc_MEDIUM,1,10,qrcodegen_Mask_AUTO,true)) {
    int size=qrcodegen_getSize(qr),scale=200/(size+8),whole=(size+8)*scale,x=(W-whole)/2,y=112;
    fill_rect(b,x,y,x+whole,y+whole,0xffff);
    for(int row=0;row<size;row++)for(int col=0;col<size;col++)if(qrcodegen_getModule(qr,col,row))fill_rect(b,x+(col+4)*scale,y+(row+4)*scale,x+(col+5)*scale,y+(row+5)*scale,0);
   }
   draw_text_center(b,12,308,322,a->lan_address,16,COL_ACCENT);
  }else draw_text(b,28,182,"LAN 地址未读取，暂不可扫码",14,COL_MUTED);
  draw_text(b,16,354,"进入原厂网页，可能需要登录",13,COL_MUTED);
  draw_text(b,16,378,"这里不会自动修改任何设置",12,COL_MUTED);
  chip(b,a,12,428,156,474,"返回列表",HID_WEB_LIST,0);chip(b,a,164,428,308,474,"回到屏幕",HID_WEB_CLOSE,1);
 }else{
  if(a->web_off>=n)a->web_off=0;
  for(int i=0;i<5 && a->web_off+i<n;i++){int k=ids[a->web_off+i];setting_row(b,a,74+i*59,web_settings[k].title,"扫码进入原厂设置",HID_WEB_ITEM0+k);}
  snprintf(line,sizeof(line),"%d / %d",a->web_off/5+1,(n+4)/5);
  chip(b,a,12,378,86,420,"上一页",HID_WEB_UP,0);draw_text_center(b,90,230,391,line,12,COL_MUTED);chip(b,a,234,378,308,420,"下一页",HID_WEB_DN,0);
  chip(b,a,12,428,308,474,"回到屏幕",HID_WEB_CLOSE,1);
 }
}
