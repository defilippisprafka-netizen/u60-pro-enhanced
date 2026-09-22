/* Standalone: cc -I panel/vendor tests/test-advanced-control.c panel/vendor/cJSON.c -o /tmp/test-advanced */
#include "../panel/vendor/cJSON.h"
static cJSON*adv_mock(const char*,const char*,cJSON*);
#define ADV_BUS adv_mock
#define main controller_main
#include "../panel/panel-control.c"
#undef main
#include <assert.h>
#include "../panel/panel-advanced-control.h"
static int writes,reject,stale;
static cJSON *db,*last_payload;
static cJSON*adv_mock(const char*obj,const char*method,cJSON*args){
 char key[200];snprintf(key,sizeof(key),"%s.%s",obj,method);
 for(size_t n=0;n<sizeof(adv_specs)/sizeof(adv_specs[0]);n++){
  const struct adv_spec*s=&adv_specs[n];if(strcmp(obj,s->obj)||strcmp(method,s->set))continue;
  if(!jget(args,s->writekey)&&s->kind!=1)continue;
  if(s->kind==1&&!jget(jget(args,"deviceInfoList"),s->writekey))continue;
  if(!strcmp(method,"nwinfo_set_nrbandlock")&&strcmp(jstr(args,"nr5g_type"),!strcmp(s->action,"band.sa")?"0":"1"))continue;
  writes++;cJSON_Delete(last_payload);last_payload=cJSON_Duplicate(args,1);if(reject)return cJSON_Parse("{\"error_code\":7}");
  snprintf(key,sizeof(key),"%s.%s",obj,s->get);cJSON*d=jget(db,key),*v=jget(s->kind==1?jget(args,"deviceInfoList"):args,s->writekey);if(!stale)cJSON_ReplaceItemInObject(d,s->readkey,cJSON_Duplicate(v,1));return cJSON_CreateObject();
 }
 if(!strcmp(obj,"zwrt_apn_object")&&(!strcmp(method,"set_apn_mode")||!strcmp(method,"enable_manu_apn_id"))){writes++;if(reject)return NULL;if(!stale){const char*g=!strcmp(method,"set_apn_mode")?"zwrt_apn_object.get_apn_mode":"zwrt_apn_object.get_enabled_manu_apn_id";cJSON*v=jget(args,!strcmp(method,"set_apn_mode")?"apn_mode":"profileId");cJSON_DeleteItemFromObject(db,g);cJSON*d=cJSON_AddObjectToObject(db,g);cJSON_AddItemToObject(d,v->string,cJSON_Duplicate(v,1));}return cJSON_CreateObject();}
 const char*get=NULL;
 if(!strcmp(method,"router_set_lan_para"))get="router_get_dhcp_router";
 if(!strcmp(method,"router_set_wan_dns"))get="router_get_dns_para";
 if(!strcmp(method,"router_set_dmz"))get="router_get_firewall_para";
 if(get){writes++;cJSON_Delete(last_payload);last_payload=cJSON_Duplicate(args,1);if(reject)return NULL;snprintf(key,sizeof(key),"%s.%s",obj,get);if(!stale){cJSON*d=jget(db,key),*v;cJSON_ArrayForEach(v,args){const char*k=v->string;if(!strcmp(k,"ipaddr"))k="lan_addr";else if(!strcmp(k,"netmask"))k="lan_netmask";else if(!strcmp(k,"dns_mode"))k="wan_dns_mode";else if(!strcmp(k,"prefer_dns_manual"))k="wan_prefer_dns_manual";else if(!strcmp(k,"standby_dns_manual"))k="wan_standby_dns_manual";else if(!strcmp(k,"ipv6_prefer_dns_manual"))k="ipv6_wan_prefer_dns_manual";else if(!strcmp(k,"ipv6_standby_dns_manual"))k="ipv6_wan_standby_dns_manual";cJSON_DeleteItemFromObject(d,k);cJSON_AddItemToObject(d,k,cJSON_Duplicate(v,1));}}return cJSON_CreateObject();}
 return cJSON_Duplicate(jget(db,key),1);
}
static void setup(void){cJSON_Delete(db);cJSON_Delete(last_payload);last_payload=NULL;writes=reject=stale=0;db=cJSON_Parse("{\"zwrt_router.api.router_get_firewall_para\":{\"nat_enable\":\"1\",\"firewall_enable\":\"1\",\"portmapping_enable\":\"0\",\"portforward_enable\":\"0\",\"dmz_enable\":\"0\",\"dmz_ip\":\"\"},\"zwrt_router.api.router_get_upnp\":{\"enable_upnp\":\"0\"},\"zwrt_mc.device.manager.get_device_info\":{\"quicken_power_on\":\"0\",\"power_saver_mode\":\"1\"},\"zte_nwinfo_api.nwinfo_get_netinfo\":{\"net_select\":\"WL_AND_5G\",\"lte_band\":\"1,3,5,7\",\"nr5g_sa_band_lock\":\"41,78,79\",\"nr5g_nsa_band_lock\":\"41,78,79\"},\"zwrt_router.api.router_get_dhcp_router\":{\"lan_addr\":\"192.168.0.1\",\"lan_netmask\":\"255.255.255.0\",\"ignore\":\"0\",\"zte_start\":\"192.168.0.2\",\"zte_end\":\"192.168.0.253\",\"leasetime\":\"86400\"},\"zwrt_router.api.router_get_dns_para\":{\"wan_dns_mode\":\"auto\",\"wan_prefer_dns_manual\":\"1.1.1.1\",\"wan_standby_dns_manual\":\"\",\"ipv6_wan_prefer_dns_manual\":\"\",\"ipv6_wan_standby_dns_manual\":\"\"}}");}
static void check(const char*action,const char*json,int success,int expected_writes){cJSON*args=cJSON_Parse(json),*r=control_advanced_action(action,args);assert(r);assert(cJSON_IsTrue(jget(r,"ok"))==success);assert(writes==expected_writes);cJSON_Delete(args);cJSON_Delete(r);}
int main(void){fixture=cJSON_CreateObject();
 for(size_t n=0;n<sizeof(adv_specs)/sizeof(adv_specs[0]);n++){
  const struct adv_spec*s=&adv_specs[n];const char*args=s->kind<2?"{\"enabled\":true}":s->kind==2?"{\"value\":\"Only_LTE\"}":"{\"value\":\"41,78\"}";
  setup();check(s->action,args,1,1);if(s->kind==0)assert(cJSON_IsNumber(jget(last_payload,s->writekey)));if(s->kind==1)assert(cJSON_IsString(jget(jget(last_payload,"deviceInfoList"),s->writekey)));
  setup();reject=1;check(s->action,args,0,1);
  setup();/* Ensure changed value for stale case. */cJSON*a=cJSON_Parse(args);if(s->kind<2){char key[200];snprintf(key,sizeof(key),"%s.%s",s->obj,s->get);cJSON_ReplaceItemInObject(jget(db,key),s->readkey,cJSON_CreateString("0"));}cJSON_Delete(a);stale=1;check(s->action,args,0,1);
  setup();check(s->action,s->kind<2?"{\"enabled\":\"1\"}":"{\"value\":\"x;bad\"}",0,0);
 }
 setup();check("router.dns","{\"dns_mode\":\"manual\",\"prefer_dns_manual\":\"8.8.8.8\",\"standby_dns_manual\":\"1.1.1.1\",\"ipv6_prefer_dns_manual\":\"2606:4700:4700::1111\",\"ipv6_standby_dns_manual\":\"\"}",1,1);
 setup();check("router.dns","{\"dns_mode\":\"manual\",\"prefer_dns_manual\":\"not-an-ip\"}",0,0);
 const char*lan="{\"ipaddr\":\"192.168.9.1\",\"netmask\":\"255.255.255.0\",\"ignore\":\"0\",\"zte_start\":\"192.168.9.2\",\"zte_end\":\"192.168.9.250\",\"leasetime\":\"7200\"}";
 setup();check("router.lan",lan,1,1);setup();stale=1;check("router.lan",lan,0,1);
 setup();check("router.lan","{\"ipaddr\":\"192.168.9.1\",\"netmask\":\"255.0.255.0\",\"ignore\":\"1\"}",0,0);
 setup();check("router.lan","{\"ipaddr\":\"192.168.9.1\",\"netmask\":\"255.255.255.0\",\"ignore\":\"0\",\"zte_start\":\"192.168.9.1\",\"zte_end\":\"192.168.9.10\",\"leasetime\":\"7200\"}",0,0);
 setup();check("router.dmz","{\"enabled\":\"1\",\"ip\":\"192.168.0.20\"}",1,1);
 setup();cJSON*root=reply(1,"");cJSON_AddArrayToObject(root,"sections");section(root,"router","路由");control_advanced_sections(root);int count=0;cJSON*s;cJSON_ArrayForEach(s,jget(root,"sections"))if(!strcmp(jstr(s,"id"),"router"))count++;assert(count==1);char*out=cJSON_PrintUnformatted(root);assert(!strstr(out,"password"));free(out);cJSON_Delete(root);
 setup();cJSON_AddItemToObject(db,"zwrt_apn_object.getManuApnList",cJSON_Parse("{\"apnListArray\":[{\"profileId\":\"manu1\",\"profilename\":\"Demo\",\"password\":\"do-not-output\"}]}"));cJSON_AddItemToObject(db,"zwrt_apn_object.get_apn_mode",cJSON_Parse("{\"apn_mode\":0}"));check("cell.apn","{\"profile_id\":\"manu1\"}",1,2);check("cell.apn","{\"profile_id\":\"invalid\"}",0,2);
 root=reply(1,"");cJSON_AddArrayToObject(root,"sections");control_advanced_sections(root);out=cJSON_PrintUnformatted(root);assert(!strstr(out,"do-not-output"));assert(!strstr(out,"password"));free(out);cJSON_Delete(root);
 assert(adv_csv_eq("1,3,5","5,3,1"));assert(!adv_csv("1,"));assert(!adv_csv(""));assert(!adv_csv("0,3"));assert(!adv_csv_eq("1,3","1,5"));assert(control_advanced_action("unknown",NULL)==NULL);
 cJSON_Delete(db);cJSON_Delete(last_payload);cJSON_Delete(fixture);puts("PASS: 11 advanced descriptors valid/reject/stale/invalid; DNS/LAN/DMZ validation and readback; schema merge; CSV set comparison");return 0;}
