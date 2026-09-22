/* Read actual power state locally; never infer charging from a selected role. */
#ifndef PANEL_BATTERY_STATUS_H
#define PANEL_BATTERY_STATUS_H
enum panel_battery_power { BP_UNKNOWN, BP_BATTERY, BP_CHARGING, BP_OUTPUT, BP_INPUT_IDLE, BP_FULL };
static enum panel_battery_power battery_power_state(const char *status,const char *role,int attached){
 if(attached<0)return BP_UNKNOWN;
 if(!attached)return !strcmp(status,"Discharging")||!strcmp(status,"Not charging")||!strcmp(status,"Full")?BP_BATTERY:BP_UNKNOWN;
 int sink=!strcmp(role,"sink")||!strcmp(role,"source [sink]"),source=!strcmp(role,"source")||!strcmp(role,"[source] sink");
 if(source)return !strcmp(status,"Charging")?BP_UNKNOWN:BP_OUTPUT;
 if(!sink)return BP_UNKNOWN;
 if(!strcmp(status,"Charging"))return BP_CHARGING;
 if(!strcmp(status,"Full"))return BP_FULL;
 if(!strcmp(status,"Discharging")||!strcmp(status,"Not charging"))return BP_INPUT_IDLE;
 return BP_UNKNOWN;
}
static int battery_read_text(const char *path,char *out,size_t n){
 out[0]=0;FILE*f=fopen(path,"r");if(!f)return 0;int ok=fgets(out,n,f)!=NULL;fclose(f);out[strcspn(out,"\r\n")]=0;return ok;
}
static int battery_fcc_bypass(void){
 char enabled[16],current[24],online[16];
 return battery_read_text("/sys/class/qcom-battery/restrict_chg",enabled,sizeof(enabled))&&!strcmp(enabled,"1")&&
  battery_read_text("/sys/class/qcom-battery/restrict_cur",current,sizeof(current))&&!strcmp(current,"0")&&
  battery_read_text("/sys/class/power_supply/usb/online",online,sizeof(online))&&!strcmp(online,"1");
}
static enum panel_battery_power battery_power_effective(const char*status,const char*role,int attached,int bypass){
 enum panel_battery_power p=battery_power_state(status,role,attached);
 return bypass&&(p==BP_CHARGING||p==BP_FULL)?BP_INPUT_IDLE:p;
}
static enum panel_battery_power battery_power_sample(void){
 char status[48],role[48];struct stat st;
 int attached=stat("/sys/class/typec/port0-partner",&st)==0?1:errno==ENOENT?0:-1;
 battery_read_text("/sys/class/power_supply/battery/status",status,sizeof(status));
 battery_read_text("/sys/class/typec/port0/power_role",role,sizeof(role));
 return battery_power_effective(status,role,attached,battery_fcc_bypass());
}
#endif
