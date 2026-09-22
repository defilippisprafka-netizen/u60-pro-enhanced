/* cc -I panel/vendor tests/test-charge.c panel/vendor/cJSON.c -o /tmp/test-charge */
#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <sys/stat.h>
#include "../panel/vendor/cJSON.h"
static cJSON *jget(const cJSON *o,const char *k){return cJSON_GetObjectItemCaseSensitive(o,k);}
static const char *jstr(const cJSON *o,const char *k){cJSON*v=jget(o,k);return cJSON_IsString(v)?v->valuestring:"";}
static cJSON *reply(int ok,const char *m){cJSON*r=cJSON_CreateObject();cJSON_AddBoolToObject(r,"ok",ok);cJSON_AddStringToObject(r,"message",m);return r;}
static cJSON *adv_section(cJSON *r,const char *id,const char *title){cJSON*s;cJSON_ArrayForEach(s,jget(r,"sections"))if(!strcmp(jstr(s,"id"),id))return s;s=cJSON_CreateObject();cJSON_AddStringToObject(s,"id",id);cJSON_AddStringToObject(s,"title",title);cJSON_AddArrayToObject(s,"items");cJSON_AddItemToArray(jget(r,"sections"),s);return s;}
static cJSON *item(cJSON*s,const char*id,const char*label,const char*type,const char*value,const char*action,int enabled,const char*reason){cJSON*i=cJSON_CreateObject();cJSON_AddStringToObject(i,"id",id);cJSON_AddStringToObject(i,"label",label);cJSON_AddStringToObject(i,"type",type);cJSON_AddStringToObject(i,"value",value);cJSON_AddStringToObject(i,"action",action?action:"");cJSON_AddBoolToObject(i,"enabled",enabled);cJSON_AddStringToObject(i,"reason",reason?reason:"");cJSON_AddItemToArray(jget(s,"items"),i);return i;}
static void choice(cJSON*i,const char*label,const char*key,const char*value){cJSON*a=jget(i,"choices");if(!a)a=cJSON_AddArrayToObject(i,"choices");cJSON*c=cJSON_CreateObject();cJSON_AddStringToObject(c,"label",label);cJSON_AddStringToObject(cJSON_AddObjectToObject(c,"args"),key,value);cJSON_AddItemToArray(a,c);}
struct charge_config;
static cJSON *test_bus(const char *,const char *,cJSON *);
static int test_run(const char *,char *const[],const char *,char *,size_t);
static int test_read(const char *,char *,size_t,int);
static int test_save(const struct charge_config *);
static int test_sys_write(const char *,int);
#define CHARGE_SYS_WRITE test_sys_write
#define CHARGE_DIR "/tmp/u60-charge-module-test"
#define CHARGE_OWNER getuid()
#define CHARGE_FIXTURE 0
#define CHARGE_BUS test_bus
#define CHARGE_RUN test_run
#define CHARGE_READ test_read
#define CHARGE_SAVE test_save
#include "../panel/panel-charge.h"
static cJSON *battery,*charger,*response;
static int writes,stale,fail_save,save_calls,hash_mismatch,missing_sysfs;
static int fcc_enabled,fcc_current,usb_online,fail_sys,new_boot;
static int test_sys_write(const char*path,int value){writes++;if(fail_sys)return 0;if(strstr(path,"restrict_chg"))fcc_enabled=value;else{assert(strstr(path,"restrict_cur"));fcc_current=value;}return 1;}
static const char *hash="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
static cJSON *test_bus(const char *object,const char *method,cJSON *args){
 if(!strcmp(method,"list")){assert(!args);return cJSON_Duplicate(!strcmp(object,"zwrt_bsp.battery")?battery:charger,1);}
 assert(!strcmp(object,"zwrt_bsp.charger")&&!strcmp(method,"set"));assert(cJSON_GetArraySize(args)==1);assert(charge_mode(jstr(args,"direct_power_supply_mode"))>=0);writes++;
 if(!stale&&charge_accepted(response))cJSON_ReplaceItemInObject(charger,"direct_power_supply_mode",cJSON_Duplicate(jget(args,"direct_power_supply_mode"),1));
 return cJSON_Duplicate(response,1);
}
static int test_run(const char *path,char *const args[],const char *input,char *out,size_t cap){
 assert(strstr(path,"sha256sum"));assert(!strcmp(args[0],"sha256sum"));assert(!strcmp(args[1],"/etc/openwrt_release")||!strcmp(args[1],"/usr/bin/zte_ubus_bsp_pm")||!strcmp(args[1],"/proc/sys/kernel/osrelease"));assert(!args[2]&&!input);
 snprintf(out,cap,"%s  %s\n",hash,args[1]);if(hash_mismatch)out[0]='b';return 1;
}
static int test_read(const char *path,char *out,size_t cap,int trusted){
 if(!strcmp(path,"/proc/sys/kernel/random/boot_id")){snprintf(out,cap,"00000000-0000-0000-0000-%012d",new_boot);return 1;}
 if(!strncmp(path,"/sys/",5)){assert(!trusted);if(missing_sysfs){errno=ENOENT;return 0;}
  if(strstr(path,"restrict_chg"))snprintf(out,cap,"%d\n",fcc_enabled);
  else if(strstr(path,"restrict_cur"))snprintf(out,cap,"%d\n",fcc_current);
  else if(strstr(path,"usb/online"))snprintf(out,cap,"%d\n",usb_online);
  else snprintf(out,cap,"%s",strstr(path,"current_now")?"-2305354\n":"3921000\n");return 1;}
 assert(!strcmp(path,CHARGE_CONFIG)||!strcmp(path,CHARGE_MARKER));return charge_read_file(path,out,cap,trusted);
}
static int test_save(const struct charge_config *c){save_calls++;if(fail_save&&save_calls>=fail_save)return 0;return charge_save_file(c);}
static void put(const char *path,const char *data){int fd=open(path,O_CREAT|O_TRUNC|O_WRONLY,0600);assert(fd>=0);assert(write(fd,data,strlen(data))==(ssize_t)strlen(data));assert(!close(fd));assert(!chmod(path,0600));}
static void marker(void){char s[700];snprintf(s,sizeof(s),"{\"version\":1,\"firmware_sha256\":\"%s\",\"bsp_sha256\":\"%s\",\"pause_value\":\"enable\",\"resume_value\":\"disable\",\"stop_resume_verified\":true}",hash,hash);put(CHARGE_MARKER,s);}
static void fcc_marker(void){marker();char b[1536];assert(charge_read_file(CHARGE_MARKER,b,sizeof(b),1));cJSON*j=charge_json(b);cJSON_AddStringToObject(j,"backend","fcc-zero");cJSON_AddStringToObject(j,"kernel_sha256",hash);cJSON_AddNumberToObject(j,"restore_current_ua",1000000);cJSON_AddBoolToObject(j,"usb_input_retained",1);char*s=cJSON_PrintUnformatted(j);put(CHARGE_MARKER,s);free(s);cJSON_Delete(j);}
static void number(cJSON *o,const char *k,int n){cJSON_ReplaceItemInObject(o,k,cJSON_CreateNumber(n));}
static void mode(int n){cJSON_ReplaceItemInObject(charger,"direct_power_supply_mode",cJSON_CreateString(charge_mode_name(n)));}
static void setup(void){
 assert(!mkdir(CHARGE_DIR,0700)||errno==EEXIST);assert(!chmod(CHARGE_DIR,0700));unlink(CHARGE_CONFIG);unlink(CHARGE_MARKER);
 cJSON_Delete(battery);cJSON_Delete(charger);cJSON_Delete(response);battery=cJSON_Parse("{\"battery_capacity\":70,\"battery_online\":1,\"battery_temperature\":42}");charger=cJSON_Parse("{\"charger_connect\":1,\"charge_status\":2,\"direct_power_supply_mode\":\"disable\"}");response=cJSON_CreateObject();writes=stale=fail_save=save_calls=hash_mismatch=missing_sysfs=0;new_boot=fcc_enabled=fail_sys=0;fcc_current=1000000;usb_online=1;
}
static void check_result(cJSON *r,int ok,int count){assert(r);if(cJSON_IsTrue(jget(r,"ok"))!=ok)fprintf(stderr,"Unexpected result: %s\n",jstr(r,"message"));assert(cJSON_IsTrue(jget(r,"ok"))==ok);assert(writes==count);cJSON_Delete(r);}
static void action(const char *limit,int ok,int count){cJSON *a=cJSON_CreateObject();cJSON_AddStringToObject(a,"limit",limit);check_result(charge_action("charge.policy",a),ok,count);cJSON_Delete(a);}
static void tick(int ok,int count){check_result(charge_tick(),ok,count);}
static struct charge_config config(void){struct charge_config c;assert(charge_load(&c));return c;}
int main(void){
 /* Default off, getter read-only, preserve the actual reverse-power state. */
 setup();mode(1);number(charger,"charger_connect",0);tick(1,0);action("off",1,0);assert(access(CHARGE_CONFIG,F_OK));
 cJSON *root=cJSON_CreateObject();cJSON_AddArrayToObject(root,"sections");charge_sections(root);cJSON *data=jget(jget(root,"data"),"charge");assert(jget(data,"charger_connected")->valueint==0);assert(!strcmp(jstr(data,"direct_power_supply_mode"),"enable"));assert(jget(data,"current_a")->valuedouble<0);assert(!cJSON_IsTrue(jget(data,"verified")));assert(!writes&&!save_calls);cJSON_Delete(root);
 action("80",0,0);marker();hash_mismatch=1;action("80",0,0);hash_mismatch=0;assert(charge_verified());assert(!chmod(CHARGE_MARKER,0666));assert(!charge_verified());assert(!chmod(CHARGE_MARKER,0600));
 /* State machine: exact threshold, hysteresis band, unplug, resume, restore. */
 setup();marker();action("80",1,0);assert(config().original==0);number(battery,"battery_capacity",80);tick(1,1);assert(config().last==1);number(battery,"battery_capacity",78);tick(1,1);number(battery,"battery_capacity",75);number(charger,"charger_connect",0);tick(1,1);number(charger,"charger_connect",1);tick(1,2);number(battery,"battery_capacity",95);action("90",1,3);assert(config().original==0);action("100",1,4);assert(config().limit==100);number(battery,"battery_capacity",100);tick(1,5);action("off",1,6);assert(config().limit==0);assert(charge_mode(jstr(charger,"direct_power_supply_mode"))==0);tick(1,6);
 /* Manual pause before enrollment is restored when policy is disabled. */
 setup();marker();mode(1);action("80",1,1);assert(config().original==1);action("off",1,2);assert(charge_mode(jstr(charger,"direct_power_supply_mode"))==1);
 /* Unplugged enrollment records configuration only; reads never write. */
 setup();marker();number(charger,"charger_connect",0);number(battery,"battery_capacity",90);action("80",1,0);assert(config().limit==80);tick(1,0);number(charger,"charger_connect",1);tick(1,1);
 /* External manual override suspends policy and is not undone on disable. */
 setup();marker();action("80",1,0);mode(1);tick(0,0);assert(config().fault);action("off",1,0);assert(charge_mode(jstr(charger,"direct_power_supply_mode"))==1);
 /* Reject ambiguous, rejected, malformed and stale setters; no retry loop. */
 const char *bad[]={"null","{\"error_code\":3}","{\"result\":false}","{\"result\":{}}","{\"unexpected\":true}","{\"error\":null}"};
 for(unsigned n=0;n<sizeof(bad)/sizeof(bad[0]);n++){setup();marker();number(battery,"battery_capacity",90);cJSON_Delete(response);response=cJSON_Parse(bad[n]);action("80",0,1);assert(config().fault);tick(0,1);}
 setup();marker();number(battery,"battery_capacity",90);stale=1;action("80",0,1);assert(config().pending==1);tick(0,1);
 setup();marker();number(battery,"battery_capacity",90);cJSON_Delete(response);response=cJSON_Parse("{\"result\":\"success\",\"error_code\":0}");action("80",1,1);
 /* Missing/invalid telemetry never becomes a zero or causes a setter. */
 setup();marker();cJSON_DeleteItemFromObject(battery,"battery_capacity");action("80",0,0);
 const char *invalid[]={"null","\"\"","\"80x\"","80.5","101","-1","true","\"NaN\""};
 for(unsigned n=0;n<sizeof(invalid)/sizeof(invalid[0]);n++){setup();marker();cJSON_ReplaceItemInObject(battery,"battery_capacity",cJSON_Parse(invalid[n]));action("80",0,0);}
 setup();marker();number(battery,"battery_online",0);action("80",0,0);
 setup();marker();number(charger,"charger_connect",2);action("80",0,0);
 setup();marker();cJSON_ReplaceItemInObject(charger,"direct_power_supply_mode",cJSON_CreateString("unknown"));action("80",0,0);
 setup();marker();action("80",1,0);cJSON_DeleteItemFromObject(battery,"battery_capacity");tick(0,0);missing_sysfs=1;root=cJSON_CreateObject();cJSON_AddArrayToObject(root,"sections");charge_sections(root);data=jget(jget(root,"data"),"charge");assert(cJSON_IsNull(jget(data,"capacity_percent")));assert(cJSON_IsNull(jget(data,"current_a")));assert(!writes);cJSON_Delete(root);
 /* Config commit before setter; journal surviving finalize failure is safe. */
 setup();marker();fail_save=1;action("80",0,0);assert(config().limit==0);
 setup();marker();number(battery,"battery_capacity",90);fail_save=2;action("80",0,0);assert(config().last==0);
 setup();marker();number(battery,"battery_capacity",90);fail_save=3;action("80",0,1);assert(config().pending==1);tick(0,1);fail_save=0;action("off",1,2);
 setup();marker();number(battery,"battery_capacity",90);fail_save=3;action("80",0,1);fail_save=0;action("90",1,1);assert(config().original==0);action("off",1,2);
 /* Off is durable even if restore cannot proceed; tick never restores OFF. */
 setup();marker();number(battery,"battery_capacity",90);action("80",1,1);unlink(CHARGE_MARKER);action("off",0,1);assert(!config().limit&&config().restore);tick(0,1);marker();action("off",1,2);
 setup();marker();number(battery,"battery_capacity",90);action("80",1,1);number(charger,"charger_connect",0);action("off",0,1);tick(0,1);number(charger,"charger_connect",1);action("off",1,2);
 /* Corrupt, oversized, unsafe and trailing-garbage files are never replaced. */
 setup();marker();put(CHARGE_CONFIG,"{broken");tick(0,0);action("off",0,0);action("80",0,0);assert(!save_calls);
 setup();marker();action("80",1,0);assert(!chmod(CHARGE_CONFIG,0666));tick(0,0);assert(!chmod(CHARGE_CONFIG,0600));
 setup();put(CHARGE_MARKER,"{} trailing");assert(!charge_verified());char oversized[2300];memset(oversized,'x',sizeof(oversized)-1);oversized[sizeof(oversized)-1]=0;put(CHARGE_CONFIG,oversized);tick(0,0);
 setup();marker();action("81",0,0);action("",0,0);cJSON *a=cJSON_Parse("{\"limit\":80}");check_result(charge_action("charge.policy",a),0,0);cJSON_Delete(a);assert(charge_action("other",NULL)==NULL);
 /* FCC-zero bypass retains USB input, manual mode takes priority over limit. */
 setup();fcc_marker();assert(charge_verified());assert(charge_read().mode==0);
 cJSON *manual=cJSON_Parse("{\"mode\":\"direct\"}");check_result(charge_action("charge.manual",manual),1,2);assert(fcc_enabled==1&&fcc_current==0&&!config().limit);tick(1,2);
 number(battery,"battery_capacity",70);action("80",1,4);assert(!fcc_enabled&&fcc_current==1000000);
 number(battery,"battery_capacity",80);tick(1,6);assert(fcc_enabled&&fcc_current==0);
 cJSON_ReplaceItemInObject(manual,"mode",cJSON_CreateString("normal"));check_result(charge_action("charge.manual",manual),1,8);assert(!config().limit&&!fcc_enabled);tick(1,8);
 setup();fcc_marker();number(charger,"charger_connect",0);cJSON_ReplaceItemInObject(manual,"mode",cJSON_CreateString("direct"));check_result(charge_action("charge.manual",manual),0,0);
 setup();fcc_marker();usb_online=0;check_result(charge_action("charge.manual",manual),0,4);assert(!fcc_enabled&&fcc_current==1000000&&config().fault);
 setup();fcc_marker();fcc_enabled=1;fcc_current=500000;check_result(charge_action("charge.manual",manual),0,0);
 setup();marker();check_result(charge_action("charge.manual",manual),0,0);cJSON_Delete(manual);
 /* Real reboot reset is re-armed, same-boot external override still faults. */
 setup();fcc_marker();number(battery,"battery_capacity",90);action("80",1,2);new_boot=1;fcc_enabled=0;fcc_current=1000000;tick(1,4);assert(!config().fault&&config().last==1);
 fcc_enabled=0;fcc_current=1000000;tick(0,4);assert(config().fault);
 setup();fcc_marker();number(battery,"battery_capacity",90);action("80",1,2);struct charge_config interrupted=config();interrupted.pending=1;assert(charge_save_file(&interrupted));new_boot=1;fcc_enabled=0;fcc_current=1000000;tick(0,2);

 setup();cJSON_Delete(battery);cJSON_Delete(charger);cJSON_Delete(response);assert(!rmdir(CHARGE_DIR));
 puts("PASS: charge telemetry, capability binding, durable journal, 80/90/100 hysteresis, off/manual restore, unplug, invalid/missing state, setter reject/stale, persistence failures");return 0;
}
