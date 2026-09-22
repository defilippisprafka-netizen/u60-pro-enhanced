/* Private stdin/stdout UBUS bridge: credentials never enter process arguments.
 * Caller is the root-only panel-control executable; no network listener. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libubus.h>
#include <libubox/blobmsg_json.h>
#include "cJSON.h"
#include <dlfcn.h>

__typeof__(&ubus_connect) dyn_ubus_connect;
__typeof__(&ubus_lookup_id) dyn_ubus_lookup_id;
__typeof__(&ubus_invoke_fd) dyn_ubus_invoke_fd;
__typeof__(&ubus_free) dyn_ubus_free;
__typeof__(&blob_buf_init) dyn_blob_buf_init;
__typeof__(&blob_buf_free) dyn_blob_buf_free;
__typeof__(&blobmsg_add_json_from_string) dyn_blobmsg_add_json_from_string;
__typeof__(&blobmsg_format_json_with_cb) dyn_blobmsg_format_json_with_cb;
static int load_api(void){
 const char *libs[]={"/lib/libubox.so.20230523","/lib/libubus.so.20230605","/lib/libblobmsg_json.so.20230523"};
 for(unsigned i=0;i<sizeof(libs)/sizeof(libs[0]);i++)if(!dlopen(libs[i],RTLD_NOW|RTLD_GLOBAL))return 0;
 *(void**)(&dyn_ubus_connect)=dlsym(RTLD_DEFAULT,"ubus_connect");if(!dyn_ubus_connect)return 0;
 *(void**)(&dyn_ubus_lookup_id)=dlsym(RTLD_DEFAULT,"ubus_lookup_id");if(!dyn_ubus_lookup_id)return 0;
 *(void**)(&dyn_ubus_invoke_fd)=dlsym(RTLD_DEFAULT,"ubus_invoke_fd");if(!dyn_ubus_invoke_fd)return 0;
 *(void**)(&dyn_ubus_free)=dlsym(RTLD_DEFAULT,"ubus_free");if(!dyn_ubus_free)return 0;
 *(void**)(&dyn_blob_buf_init)=dlsym(RTLD_DEFAULT,"blob_buf_init");if(!dyn_blob_buf_init)return 0;
 *(void**)(&dyn_blob_buf_free)=dlsym(RTLD_DEFAULT,"blob_buf_free");if(!dyn_blob_buf_free)return 0;
 *(void**)(&dyn_blobmsg_add_json_from_string)=dlsym(RTLD_DEFAULT,"blobmsg_add_json_from_string");if(!dyn_blobmsg_add_json_from_string)return 0;
 *(void**)(&dyn_blobmsg_format_json_with_cb)=dlsym(RTLD_DEFAULT,"blobmsg_format_json_with_cb");if(!dyn_blobmsg_format_json_with_cb)return 0;
return 1;
}
static int received;
static void result(struct ubus_request *r,int type,struct blob_attr *msg) {
 (void)r;(void)type;if(!msg)return;
 char *json=dyn_blobmsg_format_json_with_cb(msg,true,NULL,NULL,-1);if(json){puts(json);fflush(stdout);free(json);received=1;}
}
static int name_valid(const char *s){if(!s||!*s||strlen(s)>120)return 0;for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||strchr("._-",*s)))return 0;return 1;}
int main(void){
 if(!load_api())return 5;
 char input[65536];size_t n=fread(input,1,sizeof(input)-1,stdin);if(!feof(stdin)||!n)return 2;input[n]=0;
 cJSON *req=cJSON_Parse(input);memset(input,0,sizeof(input));if(!req)return 2;
 cJSON *o=cJSON_GetObjectItem(req,"object"),*m=cJSON_GetObjectItem(req,"method"),*a=cJSON_GetObjectItem(req,"args");
 if(!cJSON_IsString(o)||!cJSON_IsString(m)||!name_valid(o->valuestring)||!name_valid(m->valuestring)||!cJSON_IsObject(a)){cJSON_Delete(req);return 2;}
 struct ubus_context *ctx=dyn_ubus_connect(NULL);if(!ctx){cJSON_Delete(req);return 3;}uint32_t id=0;int rc=dyn_ubus_lookup_id(ctx,o->valuestring,&id);
 struct blob_buf b={0};dyn_blob_buf_init(&b,0);char *json=cJSON_PrintUnformatted(a);
 if(!json||!dyn_blobmsg_add_json_from_string(&b,json))rc=2;
 if(json){memset(json,0,strlen(json));free(json);}
 if(!rc)rc=dyn_ubus_invoke_fd(ctx,id,m->valuestring,b.head,result,NULL,5000,-1);
 if(!rc&&!received)puts("{}");dyn_blob_buf_free(&b);dyn_ubus_free(ctx);cJSON_Delete(req);return rc?4:0;
}
