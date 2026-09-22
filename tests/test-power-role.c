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
static cJSON *test_bus(const char *,const char *,cJSON *);
static int test_status(char *,size_t);
#define PR_BUS test_bus
#define PR_STATUS test_status
#define PR_PAUSE() ((void)0)
#include "../panel/panel-power-role.h"
static char role[16],data_role[16],status[32],requested[16];
static int attached,writes,delay,refuse,change_data;
static cJSON *test_bus(const char *o,const char *method,cJSON *a){
 assert(!strcmp(o,"zwrt_bsp.typec"));
 if(!strcmp(method,"set")){assert(cJSON_GetArraySize(a)==1);assert(jget(a,"PR_Swap")&&!jget(a,"DR_Swap"));writes++;snprintf(requested,sizeof(requested),"%s",jstr(a,"PR_Swap"));return cJSON_Parse(refuse?"{\"error_code\":1}":"{}");}
 assert(!strcmp(method,"list")&&!a);if(writes&&!refuse&&delay--<=0){snprintf(role,sizeof(role),"%s",requested);if(change_data)strcpy(data_role,"host");}
 cJSON*r=cJSON_CreateObject();cJSON_AddStringToObject(r,"power_role",role);cJSON_AddStringToObject(r,"data_role",data_role);cJSON_AddNumberToObject(r,"cc_attch_state",attached);return r;
}
static int test_status(char *b,size_t n){snprintf(b,n,"%s",status);return 1;}
static void reset(void){strcpy(role,"source");strcpy(data_role,"device");strcpy(status,"Discharging");requested[0]=0;attached=1;writes=delay=refuse=change_data=0;}
static void act(const char *want,int ok,int count){cJSON*a=cJSON_CreateObject();cJSON_AddStringToObject(a,"role",want);cJSON*r=power_role_action("usb.power_role",a);assert(cJSON_IsTrue(jget(r,"ok"))==ok);assert(writes==count);cJSON_Delete(a);cJSON_Delete(r);}
int main(void){
 reset();cJSON*r=cJSON_CreateObject();cJSON_AddArrayToObject(r,"sections");power_role_sections(r);assert(!writes);cJSON*i=cJSON_GetArrayItem(jget(cJSON_GetArrayItem(jget(r,"sections"),0),"items"),0);assert(!strcmp(jstr(i,"action"),"usb.power_role"));assert(cJSON_GetArraySize(jget(i,"choices"))==2);cJSON_Delete(r);
 act("auto",0,0);act("sink;reboot",0,0);act("source",1,0);act("sink",1,1);assert(!strcmp(data_role,"device"));
 reset();attached=0;act("sink",0,0);reset();strcpy(role,"unknown");act("sink",0,0);
 reset();refuse=1;act("sink",0,1);reset();delay=100;act("sink",0,1);reset();delay=3;act("sink",1,1);
 reset();change_data=1;act("sink",0,1);reset();strcpy(role,"sink");act("source",1,1);
 reset();strcpy(status,"Charging");act("sink",1,1);
 puts("PASS: power role read-only state, validation, detach, idempotence, PR-only setter, rejection, delayed/stale readback, data-role changes, bidirectional choices");return 0;
}
