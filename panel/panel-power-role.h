/* Manual USB-C power direction. Never change the USB data role or persist a
 * role across cable reconnections; the partner negotiates each attachment. */
#ifndef PANEL_POWER_ROLE_H
#define PANEL_POWER_ROLE_H
#ifndef PR_BUS
#define PR_BUS ubus_call
#endif
#ifndef PR_PAUSE
#define PR_PAUSE() do { struct timespec d={0,200000000};nanosleep(&d,NULL); } while(0)
#endif
#ifndef PR_STATUS
static int pr_status_file(char *b,size_t n){FILE*f=fopen("/sys/class/power_supply/battery/status","r");if(!f)return 0;int ok=fgets(b,n,f)!=NULL;fclose(f);if(ok)b[strcspn(b,"\r\n")]=0;return ok;}
#define PR_STATUS pr_status_file
#endif
static int pr_valid(const char*s){return !strcmp(s,"sink")||!strcmp(s,"source");}
static int pr_data_valid(const char*s){return !strcmp(s,"device")||!strcmp(s,"host");}
static int pr_attached(const cJSON*r){cJSON*v=jget(r,"cc_attch_state");return cJSON_IsNumber(v)&&v->valuedouble==1;}
static const char *pr_label(const char*s){return !strcmp(s,"sink")?"电脑 / 充电器 → U60":!strcmp(s,"source")?"U60 → 外部设备":"未知";}
static const char *pr_battery_status(void){
#ifdef PANEL_CHARGE_H
 double e,c,u;if(charge_sys_number("/sys/class/qcom-battery/restrict_chg",0,1,&e)&&e==1&&charge_sys_number("/sys/class/qcom-battery/restrict_cur",0,5000000,&c)&&c==0&&charge_sys_number("/sys/class/power_supply/usb/online",0,1,&u)&&u==1)return "外接直供 · 电池暂停充电";
#endif
 char b[48]={0};if(!PR_STATUS(b,sizeof(b)))return "未知";return !strcmp(b,"Charging")?"正在充电":!strcmp(b,"Discharging")?"正在放电":!strcmp(b,"Full")?"电量已满":!strcmp(b,"Not charging")?"已暂停充电":"未知";}
static int pr_accepted(const cJSON*r){
 if(!cJSON_IsObject(r)||jget(r,"error"))return 0;
 cJSON*v;cJSON_ArrayForEach(v,r){
  if(!strcmp(v->string,"error_code")){if(!cJSON_IsNumber(v)||v->valuedouble!=0)return 0;}
  else if(!strcmp(v->string,"result")){if(!(cJSON_IsTrue(v)||(cJSON_IsNumber(v)&&v->valuedouble==0)||(cJSON_IsString(v)&&(!strcmp(v->valuestring,"success")||!strcmp(v->valuestring,"0")))))return 0;}
  else return 0;
 }return 1;
}
static void power_role_sections(cJSON*root){
 cJSON*r=PR_BUS("zwrt_bsp.typec","list",NULL);const char*role=jstr(r,"power_role");
 int ready=pr_attached(r)&&pr_valid(role)&&pr_data_valid(jstr(r,"data_role"));
 cJSON*s=adv_section(root,"battery","电池与充电");
 cJSON*i=item(s,"usb.power_role","USB-C 供电方向","choice",pr_label(role),"usb.power_role",ready,ready?"接线后手动切换；重插会重新协商，USB 连接可能短暂中断":"请先接入设备，等待供电状态确认");
 choice(i,"给 U60 充电","role","sink");choice(i,"U60 对外供电","role","source");cJSON_AddBoolToObject(i,"confirm",1);
 item(s,"usb.charge_state","电池实际状态","info",pr_battery_status(),NULL,0,NULL);
 cJSON*d=jget(root,"data");if(!d)d=cJSON_AddObjectToObject(root,"data");cJSON*p=cJSON_AddObjectToObject(d,"usb_power");cJSON_AddStringToObject(p,"role",pr_valid(role)?role:"unknown");cJSON_AddStringToObject(p,"data_role",jstr(r,"data_role"));cJSON_AddBoolToObject(p,"connected",pr_attached(r));cJSON_Delete(r);
}
static cJSON *power_role_action(const char*action,const cJSON*args){
 if(strcmp(action,"usb.power_role"))return NULL;
 const char*want=jstr(args,"role");if(!pr_valid(want))return reply(0,"请选择受电或对外供电");
 cJSON*r=PR_BUS("zwrt_bsp.typec","list",NULL);char original_data[16];snprintf(original_data,sizeof(original_data),"%s",jstr(r,"data_role"));
 int ready=pr_attached(r)&&pr_valid(jstr(r,"power_role"))&&pr_data_valid(original_data),same=!strcmp(jstr(r,"power_role"),want);cJSON_Delete(r);
 if(!ready)return reply(0,"USB-C 未连接或状态未知，未切换");
 if(same)return reply(1,!strcmp(want,"sink")?"当前已是受电端，请看电池实际状态":"当前已在对外供电");
 cJSON*a=cJSON_CreateObject();cJSON_AddStringToObject(a,"PR_Swap",want);r=PR_BUS("zwrt_bsp.typec","set",a);cJSON_Delete(a);int accepted=pr_accepted(r);cJSON_Delete(r);
 if(!accepted)return reply(0,"切换请求未确认，请查看当前供电方向");
 for(int n=0;n<8;n++){
  r=PR_BUS("zwrt_bsp.typec","list",NULL);int attached=pr_attached(r),data_ok=!strcmp(jstr(r,"data_role"),original_data),done=!strcmp(jstr(r,"power_role"),want);cJSON_Delete(r);
  if(!attached)return reply(0,"USB-C 连接已变化，请重连后查看状态");
  if(!data_ok)return reply(0,"USB 数据角色已变化，请检查连接状态");
  if(done){char msg[160];snprintf(msg,sizeof(msg),!strcmp(want,"sink")?"已切为受电端；%s":"已切为对外供电；%s",pr_battery_status());return reply(1,msg);}
  if(n<7)PR_PAUSE();
 }
 return reply(0,"对端尚未完成供电切换，请稍后查看当前方向");
}
#endif
