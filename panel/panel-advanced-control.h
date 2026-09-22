/* Firmware-verified native settings. Includes only allowlisted getters/fields.
 * API tokens verified against U60 Pro service_rpc.js + live ubus -v on 2026-09-20.
 * This module never passes credentials, SMS bodies or arbitrary commands. */
#ifndef PANEL_ADVANCED_CONTROL_H
#define PANEL_ADVANCED_CONTROL_H
#ifndef ADV_BUS
#define ADV_BUS ubus_call
#endif
struct adv_spec{const char*action,*section,*label,*obj,*get,*set,*readkey,*writekey;int kind;};
/* kind: 0 numeric bool; 1 string bool nested deviceInfoList; 2 token; 3 CSV */
static const struct adv_spec adv_specs[]={
 {"router.nat","router","NAT","zwrt_router.api","router_get_firewall_para","router_set_nat_switch","nat_enable","enable",0},
 {"router.firewall","router","防火墙","zwrt_router.api","router_get_firewall_para","router_set_firewall_switch","firewall_enable","enable",0},
 {"router.upnp","router","UPnP","zwrt_router.api","router_get_upnp","router_set_upnp_switch","enable_upnp","enable_upnp",0},
 {"router.portmapping","router","端口映射总开关","zwrt_router.api","router_get_firewall_para","router_set_portmapping_switch","portmapping_enable","portmapping_enable",0},
 {"router.portforward","router","端口转发总开关","zwrt_router.api","router_get_firewall_para","router_set_portforward_switch","portforward_enable","portforward_enable",0},
 {"system.fastboot","system","快速开机","zwrt_mc.device.manager","get_device_info","set_device_info","quicken_power_on","quicken_power_on",1},
 {"system.saver","system","省电模式","zwrt_mc.device.manager","get_device_info","set_device_info","power_saver_mode","power_saver_mode",1},
 {"cell.mode","cell","网络制式偏好","zte_nwinfo_api","nwinfo_get_netinfo","nwinfo_set_netselect","net_select","net_select",2},
 {"band.lte","band","LTE 允许频段","zte_nwinfo_api","nwinfo_get_netinfo","nwinfo_set_lte_ext_band","lte_band","lte_band",3},
 {"band.sa","band","5G SA 允许频段","zte_nwinfo_api","nwinfo_get_netinfo","nwinfo_set_nrbandlock","nr5g_sa_band_lock","nr5g_band",3},
 {"band.nsa","band","5G NSA 允许频段","zte_nwinfo_api","nwinfo_get_netinfo","nwinfo_set_nrbandlock","nr5g_nsa_band_lock","nr5g_band",3}
};
static cJSON*adv_section(cJSON*root,const char*id,const char*title){cJSON*s;cJSON_ArrayForEach(s,jget(root,"sections"))if(!strcmp(jstr(s,"id"),id))return s;return section(root,id,title);}
static cJSON*adv_read(const char*obj,const char*method){return ADV_BUS(obj,method,NULL);}
static cJSON*adv_read_spec(const struct adv_spec*s){
 if(s->kind!=1)return adv_read(s->obj,s->get);
 cJSON*a=cJSON_CreateObject(),*keys=cJSON_AddArrayToObject(a,"deviceInfoList");cJSON_AddItemToArray(keys,cJSON_CreateString(s->readkey));cJSON*r=ADV_BUS(s->obj,s->get,a);cJSON_Delete(a);return r;
}
static int adv_bool(const cJSON*v){if(cJSON_IsBool(v))return cJSON_IsTrue(v);if(cJSON_IsNumber(v)&&(v->valuedouble==0||v->valuedouble==1))return v->valueint;if(cJSON_IsString(v)){if(!strcmp(v->valuestring,"0"))return 0;if(!strcmp(v->valuestring,"1"))return 1;}return -1;}
static int adv_scalar_eq(const cJSON*v,const char*expect){char out[80];if(cJSON_IsString(v))return !strcmp(v->valuestring,expect);if(cJSON_IsNumber(v)){snprintf(out,sizeof(out),"%.0f",v->valuedouble);return !strcmp(out,expect);}return 0;}
static int adv_result_ok(const cJSON*r){return firmware_success(r);}
static int adv_ip(const char*s,int family,int optional){unsigned char b[16];return optional&&!*s?1:inet_pton(family,s,b)==1;}
static int adv_csv(const char*s){if(!*s||strlen(s)>220)return 0;int need=1,count=0;const char*p=s;while(*p){char*e;long n=strtol(p,&e,10);if(e==p||n<1||n>1024)return 0;if(*e&&*e!=',')return 0;if(++count>64)return 0;need=*e==',';p=*e?e+1:e;}return !need;}
static int adv_csv_eq(const char*a,const char*b){if(!adv_csv(a)||!adv_csv(b))return 0;int mask[1025]={0},i;char*e;while(*a){long n=strtol(a,&e,10);mask[n]|=1;a=*e?e+1:e;}while(*b){long n=strtol(b,&e,10);mask[n]|=2;b=*e?e+1:e;}for(i=1;i<=1024;i++)if(mask[i]&&mask[i]!=3)return 0;return 1;}
static void adv_wait(void){if(!fixture){struct timespec d={0,200000000};nanosleep(&d,NULL);}}
static cJSON*adv_field_choice(cJSON*i,const char*key,const char*label,const char*value,const char*labels,const char*values){
 field(i,key,label,value,"choice",1);cJSON*fs=jget(i,"fields"),*f=cJSON_GetArrayItem(fs,cJSON_GetArraySize(fs)-1),*choices=cJSON_AddArrayToObject(f,"choices");char *ls=strdup(labels),*vs=strdup(values),*lp=NULL,*vp=NULL,*l=strtok_r(ls,"|",&lp),*v=strtok_r(vs,"|",&vp);while(l&&v){cJSON*c=cJSON_CreateObject();cJSON_AddStringToObject(c,"label",l);cJSON_AddStringToObject(c,"value",v);cJSON_AddItemToArray(choices,c);l=strtok_r(NULL,"|",&lp);v=strtok_r(NULL,"|",&vp);}free(ls);free(vs);return f;
}
static cJSON*adv_generic_action(const struct adv_spec*s,const cJSON*args){
 int en=0;const char*value=jstr(args,"value");char boolval[2];
 if(s->kind<2){if(!boolarg(args,"enabled",&en))return reply(0,"开关参数必须为布尔值");boolval[0]=en?'1':'0';boolval[1]=0;value=boolval;}
 if(s->kind==2&&strcmp(value,"WL_AND_5G")&&strcmp(value,"Only_5G")&&strcmp(value,"Only_LTE"))return reply(0,"无效网络制式");
 if(s->kind==3&&!adv_csv(value))return reply(0,"频段请输入 1–1024 范围的逗号分隔数字");
 cJSON*before=adv_read_spec(s);cJSON*old=jget(before,s->readkey);int known=s->kind<2?adv_bool(old)>=0:cJSON_IsString(old)&&*old->valuestring;if(!known){cJSON_Delete(before);return reply(0,"当前值无法可靠读取，未修改");}cJSON_Delete(before);
 cJSON*body=cJSON_CreateObject();if(s->kind==0)cJSON_AddNumberToObject(body,s->writekey,en);else if(s->kind==1)cJSON_AddStringToObject(cJSON_AddObjectToObject(body,"deviceInfoList"),s->writekey,value);else cJSON_AddStringToObject(body,s->writekey,value);
 if(!strcmp(s->action,"band.sa")||!strcmp(s->action,"band.nsa"))cJSON_AddStringToObject(body,"nr5g_type",!strcmp(s->action,"band.sa")?"0":"1");
 cJSON*r=ADV_BUS(s->obj,s->set,body);cJSON_Delete(body);int accepted=adv_result_ok(r);cJSON_Delete(r);if(!accepted)return reply(0,"原厂接口拒绝或未确认请求");
 for(int n=0;n<6;n++){r=adv_read_spec(s);int ok=s->kind==3?adv_csv_eq(jstr(r,s->readkey),value):adv_scalar_eq(jget(r,s->readkey),value);cJSON_Delete(r);if(ok)return reply(1,"设置已保存并回读确认");adv_wait();}return reply(0,"已请求修改，但回读不一致，未确认成功");
}
static int adv_verify_map(const char*obj,const char*getter,cJSON*expect){for(int n=0;n<6;n++){cJSON*r=adv_read(obj,getter),*v;int ok=r!=NULL;cJSON_ArrayForEach(v,expect)if(!adv_scalar_eq(jget(r,v->string),v->valuestring))ok=0;cJSON_Delete(r);if(ok)return 1;adv_wait();}return 0;}
static cJSON*adv_router_form(const char*action,const cJSON*args){
 cJSON*body=cJSON_CreateObject(),*expect=cJSON_CreateObject(),*before=NULL;const char*method=NULL,*getter=NULL;const char*error="请检查地址、掩码、地址池及租期";
 if(!strcmp(action,"router.dns")){
  getter="router_get_dns_para";method="router_set_wan_dns";const char*mode=jstr(args,"dns_mode");if(strcmp(mode,"auto")&&strcmp(mode,"manual"))goto invalid;
  const char*keys[]={"prefer_dns_manual","standby_dns_manual","ipv6_prefer_dns_manual","ipv6_standby_dns_manual"};const char*readkeys[]={"wan_prefer_dns_manual","wan_standby_dns_manual","ipv6_wan_prefer_dns_manual","ipv6_wan_standby_dns_manual"};
  cJSON_AddStringToObject(body,"dns_mode",mode);cJSON_AddStringToObject(expect,"wan_dns_mode",mode);
  if(!strcmp(mode,"manual"))for(int n=0;n<4;n++){const char*v=jstr(args,keys[n]);if(!adv_ip(v,n<2?AF_INET:AF_INET6,n!=0)){error="DNS 地址无效，手动模式需首选 IPv4 DNS";goto invalid;}cJSON_AddStringToObject(body,keys[n],v);cJSON_AddStringToObject(expect,readkeys[n],v);}
 }else if(!strcmp(action,"router.lan")){
  getter="router_get_dhcp_router";method="router_set_lan_para";const char*ip=jstr(args,"ipaddr"),*mask=jstr(args,"netmask"),*ignore=jstr(args,"ignore");struct in_addr ia,ma,sa,ea;char*end;long lease=strtol(jstr(args,"leasetime"),&end,10);
  if(inet_pton(AF_INET,ip,&ia)!=1||inet_pton(AF_INET,mask,&ma)!=1||(strcmp(ignore,"0")&&strcmp(ignore,"1")))goto invalid;
  uint32_t m=ntohl(ma.s_addr),in=ntohl(ia.s_addr),inverse=~m;if(!m||(inverse&(inverse+1))||inverse<3||inverse>0xffffff||!(in&inverse)||(in&inverse)==inverse)goto invalid;
  cJSON_AddStringToObject(body,"ipaddr",ip);cJSON_AddStringToObject(body,"netmask",mask);cJSON_AddNumberToObject(body,"ignore",atoi(ignore));cJSON_AddStringToObject(expect,"lan_addr",ip);cJSON_AddStringToObject(expect,"lan_netmask",mask);cJSON_AddStringToObject(expect,"ignore",ignore);
  if(!strcmp(ignore,"0")){const char*start=jstr(args,"zte_start"),*finish=jstr(args,"zte_end");if(inet_pton(AF_INET,start,&sa)!=1||inet_pton(AF_INET,finish,&ea)!=1||*end||lease<60||lease>31536000)goto invalid;uint32_t st=ntohl(sa.s_addr),en=ntohl(ea.s_addr);if((st&m)!=(in&m)||(en&m)!=(in&m)||st>en||!(st&inverse)||(en&inverse)==inverse||(in>=st&&in<=en))goto invalid;
   const char*k[]={"zte_start","zte_end","leasetime"};for(int n=0;n<3;n++){cJSON_AddStringToObject(body,k[n],jstr(args,k[n]));cJSON_AddStringToObject(expect,k[n],jstr(args,k[n]));}}
 }else if(!strcmp(action,"router.dmz")){
  getter="router_get_firewall_para";method="router_set_dmz";const char*en=jstr(args,"enabled"),*ip=jstr(args,"ip");if(strcmp(en,"0")&&strcmp(en,"1"))goto invalid;if(!strcmp(en,"1")&&!adv_ip(ip,AF_INET,0))goto invalid;cJSON_AddNumberToObject(body,"dmz_enable",atoi(en));cJSON_AddStringToObject(expect,"dmz_enable",en);if(!strcmp(en,"1")){cJSON_AddStringToObject(body,"dmz_ip",ip);cJSON_AddStringToObject(expect,"dmz_ip",ip);}
 }else {cJSON_Delete(body);cJSON_Delete(expect);return NULL;}
 before=adv_read("zwrt_router.api",getter);cJSON*v;cJSON_ArrayForEach(v,expect)if(!jget(before,v->string))goto unavailable;cJSON_Delete(before);before=NULL;
 cJSON*r=ADV_BUS("zwrt_router.api",method,body);int accepted=adv_result_ok(r);cJSON_Delete(r);cJSON_Delete(body);if(!accepted){cJSON_Delete(expect);return reply(0,"原厂接口拒绝设置，未确认生效");}int ok=adv_verify_map("zwrt_router.api",getter,expect);cJSON_Delete(expect);return reply(ok,ok?"设置已保存并逐字段回读确认":"已请求修改，但回读不一致，未确认成功");
invalid:cJSON_Delete(body);cJSON_Delete(expect);return reply(0,error);
unavailable:cJSON_Delete(before);cJSON_Delete(body);cJSON_Delete(expect);return reply(0,"未能读取完整当前配置，已拒绝修改");
}
static cJSON*adv_apn_action(const cJSON*args){
 const char*id=jstr(args,"profile_id");int automatic=!strcmp(id,"auto");if(!*id)return reply(0,"请选择 APN");
 cJSON*list=NULL,*profile;int found=automatic;if(!automatic){list=adv_read("zwrt_apn_object","getManuApnList");cJSON_ArrayForEach(profile,jget(list,"apnListArray"))if(!strcmp(jstr(profile,"profileId"),id))found=1;}cJSON_Delete(list);if(!found)return reply(0,"所选 APN 不在设备现有配置中");
 cJSON*mode=adv_read("zwrt_apn_object","get_apn_mode");if(!jget(mode,"apn_mode")){cJSON_Delete(mode);return reply(0,"APN 状态不可读，未修改");}cJSON_Delete(mode);
 cJSON*body=cJSON_CreateObject();cJSON_AddNumberToObject(body,"apn_mode",automatic?0:1);cJSON*r=ADV_BUS("zwrt_apn_object","set_apn_mode",body);cJSON_Delete(body);int ok=adv_result_ok(r);cJSON_Delete(r);if(!ok)return reply(0,"APN 模式修改未获确认");
 if(!automatic){body=cJSON_CreateObject();cJSON_AddStringToObject(body,"profileId",id);r=ADV_BUS("zwrt_apn_object","enable_manu_apn_id",body);cJSON_Delete(body);ok=adv_result_ok(r);cJSON_Delete(r);if(!ok)return reply(0,"APN 模式可能已切换，但配置选择失败，请刷新检查");}
 for(int n=0;n<6;n++){mode=adv_read("zwrt_apn_object","get_apn_mode");ok=adv_scalar_eq(jget(mode,"apn_mode"),automatic?"0":"1");cJSON_Delete(mode);if(!automatic){r=adv_read("zwrt_apn_object","get_enabled_manu_apn_id");ok=ok&&!strcmp(jstr(r,"profileId"),id);cJSON_Delete(r);}if(ok)return reply(1,"APN 选择已回读确认，蜂窝连接可能重连");adv_wait();}return reply(0,"APN 请求已发出，但回读未确认");
}
static cJSON*control_advanced_action(const char*action,const cJSON*args){
 for(size_t n=0;n<sizeof(adv_specs)/sizeof(adv_specs[0]);n++)if(!strcmp(action,adv_specs[n].action))return adv_generic_action(&adv_specs[n],args);
 if(!strcmp(action,"cell.apn"))return adv_apn_action(args);
 return adv_router_form(action,args);
}
static void control_advanced_sections(cJSON*root){
 adv_section(root,"band","频段与小区");adv_section(root,"sim","SIM 卡状态");adv_section(root,"clients","已连接设备");adv_section(root,"diagnostics","网络诊断");
 for(size_t n=0;n<sizeof(adv_specs)/sizeof(adv_specs[0]);n++){
  const struct adv_spec*sp=&adv_specs[n];cJSON*s=adv_section(root,sp->section,sp->section),*r=adv_read_spec(sp),*v=jget(r,sp->readkey),*i;int current=adv_bool(v);char text[256];scalar_text(v,text,sizeof(text));
  if(sp->kind<2)i=toggle(s,sp->action,sp->label,sp->action,current==1,current>=0,current<0?"原厂未提供确定状态，不能安全切换":sp->kind==1?NULL:"修改可能影响当前连接");
  else if(sp->kind==2){i=item(s,sp->action,sp->label,"choice",text,sp->action,cJSON_IsString(v)&&*v->valuestring,cJSON_IsString(v)&&*v->valuestring?"切换制式可能短暂断网":"当前选网状态不可读");choice(i,"自动 4G / 5G","value","WL_AND_5G");choice(i,"仅 5G","value","Only_5G");choice(i,"仅 4G","value","Only_LTE");}
  else {i=item(s,sp->action,sp->label,"form",text,sp->action,cJSON_IsString(v)&&adv_csv(v->valuestring),"修改频段可能导致失联，请保留可用频段");field(i,"value","允许频段（逗号分隔）",cJSON_IsString(v)?v->valuestring:"","text",1);}
  if(sp->kind==1)cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(0));cJSON_Delete(r);
 }
 cJSON*s=adv_section(root,"router","路由与上网"),*r=adv_read("zwrt_router.api","router_get_dns_para"),*i=item(s,"dns.edit","WAN DNS","form",jstr(r,"wan_dns_mode"),"router.dns",r!=NULL,"修改后重新建立部分连接");
 adv_field_choice(i,"dns_mode","DNS 模式",jstr(r,"wan_dns_mode"),"自动获取|手动设置","auto|manual");const char*dk[]={"prefer_dns_manual","standby_dns_manual","ipv6_prefer_dns_manual","ipv6_standby_dns_manual"};const char*rk[]={"wan_prefer_dns_manual","wan_standby_dns_manual","ipv6_wan_prefer_dns_manual","ipv6_wan_standby_dns_manual"};const char*dl[]={"首选 IPv4 DNS","备用 IPv4 DNS","首选 IPv6 DNS","备用 IPv6 DNS"};for(int n=0;n<4;n++)field(i,dk[n],dl[n],jstr(r,rk[n]),"ip",0);cJSON_Delete(r);
 r=adv_read("zwrt_router.api","router_get_dhcp_router");i=item(s,"lan.edit","LAN 与 DHCP","form",jstr(r,"lan_addr"),"router.lan",r!=NULL,"变更 LAN 地址后需重新连接管理地址");field(i,"ipaddr","LAN IPv4 地址",jstr(r,"lan_addr"),"ip",1);field(i,"netmask","子网掩码",jstr(r,"lan_netmask"),"ip",1);adv_field_choice(i,"ignore","DHCP 服务",jstr(r,"ignore"),"开启|关闭","0|1");field(i,"zte_start","地址池起点",jstr(r,"zte_start"),"ip",0);field(i,"zte_end","地址池终点",jstr(r,"zte_end"),"ip",0);field(i,"leasetime","租期（秒）",jstr(r,"leasetime"),"number",0);info(s,"ipv6.mode","IPv6 地址分配",r,"dhcpv6_mode");cJSON_Delete(r);
 r=adv_read("zwrt_router.api","router_get_firewall_para");i=item(s,"dmz.edit","DMZ 主机","form",jstr(r,"dmz_enable"),"router.dmz",adv_bool(jget(r,"dmz_enable"))>=0,"开启后将外网入站交给指定主机");adv_field_choice(i,"enabled","DMZ",jstr(r,"dmz_enable"),"关闭|开启","0|1");field(i,"ip","主机 IPv4 地址",jstr(r,"dmz_ip"),"ip",0);info(s,"filter","MAC / IP 过滤",r,"macipport_filter_enable");info(s,"remote","远程 Web 管理",r,"remote_web_access_enable");cJSON_Delete(r);
 s=adv_section(root,"cell","蜂窝网络");r=adv_read("zwrt_apn_object","get_apn_mode");char val[32];scalar_text(jget(r,"apn_mode"),val,sizeof(val));i=item(s,"apn.select","APN 选择","choice",!strcmp(val,"0")?"自动":"手动","cell.apn",jget(r,"apn_mode")!=NULL,"切换 APN 会重新建立蜂窝连接");choice(i,"自动 APN","profile_id","auto");cJSON_Delete(r);r=adv_read("zwrt_apn_object","getManuApnList");cJSON*p;cJSON_ArrayForEach(p,jget(r,"apnListArray"))if(*jstr(p,"profileId"))choice(i,jstr(p,"profilename"),"profile_id",jstr(p,"profileId"));cJSON_Delete(r);
 s=adv_section(root,"band","频段与小区");r=adv_read("zte_nwinfo_api","nwinfo_get_netinfo");const char*bk[]={"wan_active_band","nr5g_action_band","nr5g_pci","nr5g_action_channel","lock_lte_cell","lock_nr_cell"};const char*bl[]={"当前 LTE 频段","当前 5G 频段","5G PCI","5G 信道","LTE 小区锁定","NR 小区锁定"};for(int n=0;n<6;n++)info(s,bk[n],bl[n],r,bk[n]);cJSON_Delete(r);
 s=adv_section(root,"sim","SIM 卡状态");r=adv_read("zwrt_zte_mdm.api","get_sim_info");const char*sk[]={"sim_states","current_sim_slot","pin_status","modem_main_state"};const char*sl[]={"SIM 状态","当前卡槽","PIN 状态","调制解调器"};for(int n=0;n<4;n++)info(s,sk[n],sl[n],r,sk[n]);cJSON_Delete(r);
 s=adv_section(root,"clients","已连接设备");r=adv_read("zwrt_router.api","router_lan_access_list");int count=0;cJSON_ArrayForEach(p,jget(r,"lan_access_list_info")){char id[32];snprintf(id,sizeof(id),"client.%d",count++);item(s,id,*jstr(p,"hostname")?jstr(p,"hostname"):"未命名设备","info",jstr(p,"ip_address"),NULL,1,NULL);}int lan_ok=r!=NULL;cJSON_Delete(r);
 cJSON*counts=adv_read("zwrt_router.api","router_get_user_list_num");cJSON*num=jget(counts,"wireless_num");int wireless_known=cJSON_IsNumber(num)||(cJSON_IsString(num)&&num->valuestring[0]>='0'&&num->valuestring[0]<='9');int total=cJSON_IsNumber(num)?num->valueint:atoi(jstr(counts,"wireless_num"));cJSON_Delete(counts);int wireless_ok=wireless_known;
 if(total>1024||total<0)wireless_ok=0;else for(int start=1;start<=total;start+=64){cJSON*qa=cJSON_CreateObject();cJSON_AddNumberToObject(qa,"start_id",start);cJSON_AddNumberToObject(qa,"end_id",start+63);r=ADV_BUS("zwrt_router.api","router_wireless_access_list",qa);cJSON_Delete(qa);if(!r){wireless_ok=0;break;}cJSON_ArrayForEach(p,jget(r,"wireless_access_list_info")){char id[32];snprintf(id,sizeof(id),"client.%d",count++);item(s,id,*jstr(p,"hostname")?jstr(p,"hostname"):"Wi-Fi 设备","info",jstr(p,"ip_address"),NULL,1,NULL);}cJSON_Delete(r);}
 if(!count)item(s,"empty","终端列表","info",lan_ok&&wireless_ok?"当前没有终端记录":"终端数据读取失败",NULL,1,NULL);
 s=adv_section(root,"diagnostics","网络诊断");r=adv_read("zwrt_sntp","get_systime");info(s,"clock","设备本地时间",r,"localtime");cJSON_Delete(r);r=adv_read("zwrt_sntp","get_systime_mode");info(s,"clock.mode","时间来源",r,"systime_mode");cJSON_Delete(r);r=adv_read("zwrt_sntp","get_sync_state");info(s,"clock.sync","时间同步状态",r,"sntp_syn_done");cJSON_Delete(r);
}
#endif
