/* cc -I panel/vendor tests/test-radio-tools.c panel/vendor/cJSON.c -o /tmp/test-radio-tools */
#include "../panel/vendor/cJSON.h"
static cJSON *radio_mock(const char *,const char *,cJSON *);
static int radio_test_cap(const char *);
#define RADIO_BUS radio_mock
#define RADIO_CAP radio_test_cap
#define main controller_main
#include "../panel/panel-control.c"
#undef main
#include "../panel/panel-radio-tools.h"
#include <assert.h>
static cJSON *net,*pages,*last;
static int writes,reads,sms_reads,caps,reject,stale,broken,scan_state;
static int radio_test_cap(const char *name){(void)name;return caps;}
static cJSON *radio_mock(const char *obj,const char *method,cJSON *args){
 (void)obj;
 if(!strcmp(method,"nwinfo_lock_nr_cell")||!strcmp(method,"nwinfo_lock_lte_cell")){
  writes++;cJSON_Delete(last);last=cJSON_Duplicate(args,1);if(reject)return cJSON_Parse("{\"error_code\":7}");if(!stale){int nr=!strcmp(method,"nwinfo_lock_nr_cell");char text[80];if(nr)snprintf(text,sizeof(text),"%s,%s,%s,30",jstr(args,"lock_nr_pci"),jstr(args,"lock_nr_earfcn"),jstr(args,"lock_nr_cell_band"));else snprintf(text,sizeof(text),"%s,%s",jstr(args,"lock_lte_pci"),jstr(args,"lock_lte_earfcn"));cJSON_ReplaceItemInObject(net,nr?"lock_nr_cell":"lock_lte_cell",cJSON_CreateString(text));}return cJSON_CreateObject();
 }
 if(!strcmp(method,"nwinfo_reset_band_cell_setting")){writes++;if(reject)return NULL;if(!stale){cJSON_ReplaceItemInObject(net,"lock_nr_cell",cJSON_CreateString("0,0,0,0"));cJSON_ReplaceItemInObject(net,"lock_lte_cell",cJSON_CreateString("0,0"));}return cJSON_CreateObject();}
 if(!strcmp(method,"nwinfo_scan_nbr")){writes++;return reject?NULL:cJSON_CreateObject();}
 if(!strcmp(method,"zwrt_wms_delete_sms")){writes++;cJSON_Delete(last);last=cJSON_Duplicate(args,1);if(reject)return NULL;if(!stale){cJSON *p=cJSON_GetArrayItem(pages,0);cJSON_DeleteItemFromArray(jget(p,"messages"),0);}return cJSON_CreateObject();}
 reads++;if(broken)return cJSON_Parse("[]");
 if(!strcmp(method,"nwinfo_get_netinfo"))return cJSON_Duplicate(net,1);
 if(!strcmp(method,"nwinfo_get_lte_nbr_contents"))return cJSON_Parse("{\"lte_nbr_contents\":\"100,20,3,-90,-8;\"}");
 if(!strcmp(method,"nwinfo_get_nr5g_nbr_contents"))return cJSON_Parse("{\"nr5g_nbr_contents\":\"\"}");
 if(!strcmp(method,"nwinfo_m_netselect_status"))return cJSON_Parse(scan_state==1?"{\"m_netselect_status\":\"manual_selecting\"}":scan_state==2?"{\"m_netselect_status\":\"manual_search_fail\"}":"{\"m_netselect_status\":\"manual_search_success\"}");
 if(!strcmp(method,"zwrt_wms_get_wms_capacity"))return cJSON_Parse("{\"sms_dev_unread_num\":0,\"sms_sim_unread_num\":null,\"sms_nv_rev_total\":1}");
 if(!strcmp(method,"zte_libwms_get_sms_data")){sms_reads++;assert(cJSON_IsNumber(jget(args,"page")));assert(adv_scalar_eq(jget(args,"mem_store"),"1"));assert(adv_scalar_eq(jget(args,"tags"),"10"));assert(adv_scalar_eq(jget(args,"data_per_page"),"10"));assert(!strcmp(jstr(args,"order_by"),"order by id desc"));int page=jget(args,"page")->valueint;cJSON *r=cJSON_GetArrayItem(pages,page);return r?cJSON_Duplicate(r,1):cJSON_Parse("{\"messages\":[]}");}
 assert(!"unexpected radio method");return NULL;
}
static void setup(void){cJSON_Delete(net);cJSON_Delete(pages);cJSON_Delete(last);last=NULL;writes=reads=sms_reads=reject=stale=broken=scan_state=0;caps=1;
 net=cJSON_Parse("{\"network_type\":\"SA\",\"lte_pci\":0,\"wan_active_channel\":1650,\"wan_active_band\":\"B3\",\"bandwidth\":20,\"lte_rsrp\":-90,\"lte_rsrq\":-8,\"lte_snr\":0,\"lte_rssi\":-65,\"nr5g_pci\":42,\"nr5g_action_channel\":627264,\"nr5g_action_band\":\"n78\",\"nr5g_rsrp\":-99,\"nr5g_rsrq\":-9,\"nr5g_snr\":\"0.0\",\"nr5g_rssi\":-60,\"lock_lte_cell\":\"0,0\",\"lock_nr_cell\":\"\",\"lteca\":\"0,3,2,1650,20;7,1,2,100,10\",\"ltecasig\":\"-91,-8,0,-66,1\",\"nrca\":\"0,44,2,78,627360,100,1,-92,-9,12,-70\"}");
 pages=cJSON_Parse("[{\"messages\":[{\"id\":12,\"tag\":1,\"date\":\"26,09,20,12,00,00\",\"number\":\"synthetic-private-number\",\"content\":\"4F60597DD83DDE00\"}]}]");}
static cJSON *call(const char *action,const char *text,int ok,int count){cJSON *a=cJSON_Parse(text),*r=radio_tools_action(action,a);assert(r);assert(cJSON_IsTrue(jget(r,"ok"))==ok);assert(writes==count);cJSON_Delete(a);return r;}
static void check(const char *action,const char *text,int ok,int count){cJSON_Delete(call(action,text,ok,count));}
static int contains(cJSON *r,const char *needle){char *text=cJSON_PrintUnformatted(r);int found=strstr(text,needle)!=NULL;free(text);return found;}
static void test_sms_picker(void){
 setup();cJSON *r=call("sms.list","{}",1,0),*p=jget(r,"picker"),*choices=jget(p,"choices"),*first=cJSON_GetArrayItem(choices,0);
 assert(!strcmp(jstr(p,"type"),"choice"));assert(!strcmp(jstr(p,"label"),"短信收件箱"));assert(!strcmp(jstr(p,"action"),"sms.read"));assert(cJSON_IsFalse(jget(p,"confirm")));assert(cJSON_GetArraySize(choices)==1);assert(!strcmp(jstr(first,"label"),"09-20 12:00 · 未读"));assert(adv_scalar_eq(jget(jget(first,"args"),"page"),"0"));assert(adv_scalar_eq(jget(jget(first,"args"),"id"),"12"));assert(!jget(first,"action"));assert(!contains(p,"synthetic-private-number"));assert(!contains(p,"content"));assert(contains(r,"UTC+8"));assert(!contains(r,"下一页"));
 cJSON *body=radio_tools_action(jstr(p,"action"),jget(first,"args"));assert(cJSON_IsTrue(jget(body,"ok")));assert(contains(body,"你好😀"));assert(!jget(body,"picker"));cJSON_Delete(body);cJSON_Delete(r);
 setup();cJSON *page0=cJSON_GetArrayItem(pages,0),*list=jget(page0,"messages");for(int n=1;n<10;n++){cJSON *msg=cJSON_Duplicate(cJSON_GetArrayItem(list,0),1);cJSON_ReplaceItemInObject(msg,"id",cJSON_CreateNumber(12+n));cJSON_AddItemToArray(list,msg);}r=call("sms.list","{}",1,0);p=jget(r,"picker");choices=jget(p,"choices");assert(cJSON_GetArraySize(choices)==11);cJSON *next=cJSON_GetArrayItem(choices,10);assert(!strcmp(jstr(next,"action"),"sms.list"));assert(adv_scalar_eq(jget(jget(next,"args"),"page"),"1"));assert(!jget(jget(next,"args"),"id"));assert(contains(next,"下一页"));cJSON_Delete(r);
 cJSON *page1=cJSON_CreateObject(),*list1=cJSON_AddArrayToObject(page1,"messages"),*msg=cJSON_Duplicate(cJSON_GetArrayItem(list,0),1);cJSON_ReplaceItemInObject(msg,"tag",cJSON_CreateNumber(0));cJSON_AddItemToArray(list1,msg);cJSON_AddItemToArray(pages,page1);
 r=call("sms.list","{\"page\":1}",1,0);p=jget(r,"picker");choices=jget(p,"choices");assert(cJSON_GetArraySize(choices)==2);assert(contains(cJSON_GetArrayItem(choices,0),"已读"));cJSON *prev=cJSON_GetArrayItem(choices,1);assert(!strcmp(jstr(prev,"action"),"sms.list"));assert(adv_scalar_eq(jget(jget(prev,"args"),"page"),"0"));assert(!contains(r,"下一页"));assert(contains(prev,"上一页"));cJSON_Delete(r);
 /* Full source pages containing only sent items still need next-page navigation. */
 for(int n=0;n<10;n++)cJSON_ReplaceItemInObject(cJSON_GetArrayItem(list,n),"tag",cJSON_CreateNumber(2));r=call("sms.list","{}",1,0);assert(cJSON_GetArraySize(jget(jget(r,"picker"),"choices"))==1);assert(contains(jget(r,"picker"),"下一页"));cJSON_Delete(r);
 setup();list=jget(cJSON_GetArrayItem(pages,0),"messages");cJSON_DeleteItemFromArray(list,0);r=call("sms.list","{}",1,0);assert(!jget(r,"picker"));assert(contains(r,"此页没有收件项"));cJSON_Delete(r);
 setup();list=jget(cJSON_GetArrayItem(pages,0),"messages");cJSON_AddItemToArray(list,cJSON_Duplicate(cJSON_GetArrayItem(list,0),1));r=call("sms.list","{}",0,0);assert(!jget(r,"picker"));cJSON_Delete(r);
 setup();msg=cJSON_GetArrayItem(jget(cJSON_GetArrayItem(pages,0),"messages"),0);cJSON_ReplaceItemInObject(msg,"date",cJSON_CreateString("99,99,99,99,99,99"));r=call("sms.list","{}",1,0);assert(contains(jget(r,"picker"),"时间未知"));cJSON_Delete(r);assert(!writes);
}
int main(void){fixture=cJSON_CreateObject();char text[200];test_sms_picker();
 assert(radio_decode("4F60597DD83DDE00","UCS2",text,sizeof(text)));assert(!strcmp(text,"你好😀"));
 assert(radio_decode("hello 你好😀","UTF8",text,sizeof(text)));assert(!strcmp(text,"hello 你好😀"));assert(radio_decode("你好","",text,sizeof(text)));assert(!strcmp(text,"你好"));
 assert(!radio_decode("D8000041","UCS2",text,sizeof(text)));assert(!radio_decode("DC00","UCS2",text,sizeof(text)));assert(!radio_decode("123","UCS2",text,sizeof(text)));assert(!radio_decode("ZZZZ","UCS2",text,sizeof(text)));assert(!radio_decode("hello","unknown",text,sizeof(text)));assert(!radio_decode("\xc0\xaf","UTF8",text,sizeof(text)));assert(!radio_decode("\xed\xa0\x80","UTF8",text,sizeof(text)));assert(!radio_decode("4F60597D","UCS2",text,3));
 cJSON *bad=cJSON_Parse("{\"error_code\":{}}");assert(!radio_success(bad));cJSON_Delete(bad);bad=cJSON_Parse("{\"result\":null}");assert(!radio_success(bad));cJSON_Delete(bad);
 setup();cJSON *r=call("signal.serving","{}",1,0);assert(contains(r,"SINR：0 dB"));assert(contains(r,"LTE 辅载波 1"));assert(contains(r,"NR 辅载波 1"));cJSON_Delete(r);
 setup();r=call("sms.list","{}",1,0);assert(!contains(r,"synthetic-private-number"));assert(!contains(r,"4F60597D"));assert(!contains(r,"你好"));cJSON_Delete(r);r=call("sms.read","{\"id\":12,\"page\":0}",1,0);assert(contains(r,"你好😀"));cJSON_Delete(r);check("sms.read","{\"id\":99}",0,0);check("sms.list","{\"page\":-1}",0,0);check("sms.list","{\"page\":1.5}",0,0);check("sms.read","{\"id\":\"12;99;\"}",0,0);
 setup();broken=1;check("sms.list","{}",0,0);check("signal.serving","{}",0,0);
 setup();cJSON *list=jget(cJSON_GetArrayItem(pages,0),"messages");cJSON_AddItemToArray(list,cJSON_Duplicate(cJSON_GetArrayItem(list,0),1));check("sms.read","{\"id\":12}",0,0);
 setup();list=jget(cJSON_GetArrayItem(pages,0),"messages");for(int i=0;i<10;i++)cJSON_AddItemToArray(list,cJSON_CreateObject());check("sms.list","{}",0,0);
 setup();check("sms.delete","{\"id\":99,\"confirmed\":true}",0,0);check("sms.delete","{\"id\":12}",0,0);caps=0;check("sms.delete","{\"id\":12,\"confirmed\":true}",0,0);caps=1;reject=1;check("sms.delete","{\"id\":12,\"confirmed\":true}",0,1);
 setup();stale=1;check("sms.delete","{\"id\":12,\"confirmed\":true}",0,1);
 setup();check("sms.delete","{\"id\":12,\"confirmed\":true}",1,1);assert(!strcmp(jstr(last,"id"),"12;"));
 const char *lock="{\"rat\":\"nr\",\"source\":\"current\",\"pci\":42,\"arfcn\":627264,\"band\":78,\"confirmed\":true}";
 setup();check("signal.lock",lock,1,1);assert(cJSON_IsString(jget(last,"lock_nr_pci")));assert(!strcmp(jstr(last,"lock_nr_earfcn"),"627264"));
 setup();reject=1;check("signal.lock",lock,0,1);setup();stale=1;check("signal.lock",lock,0,1);setup();caps=0;check("signal.lock",lock,0,0);
 setup();check("signal.lock","{\"rat\":\"nr\",\"source\":\"current\",\"pci\":1008,\"arfcn\":627264,\"band\":78,\"confirmed\":true}",0,0);check("signal.lock","{\"rat\":\"nr\",\"source\":\"current\",\"pci\":41,\"arfcn\":627264,\"band\":78,\"confirmed\":true}",0,0);check("signal.lock","{\"rat\":\"nr\",\"source\":\"neighbor\",\"pci\":42,\"arfcn\":627264,\"band\":78,\"confirmed\":true}",0,0);
 setup();check("signal.lock","{\"rat\":\"lte\",\"source\":\"current\",\"pci\":0,\"arfcn\":1650,\"confirmed\":true}",1,1);assert(!strcmp(jstr(last,"lock_lte_pci"),"0"));
 setup();caps=0;check("signal.scan","{\"confirmed\":true}",0,0);caps=1;check("signal.scan","{}",0,0);check("signal.neighbors","{}",1,0);check("signal.scan","{\"confirmed\":true}",1,1);
 setup();scan_state=1;check("signal.scan","{\"confirmed\":true}",0,1);setup();scan_state=2;check("signal.scan","{\"confirmed\":true}",0,1);
 setup();check("signal.lock",lock,1,1);check("signal.reset","{\"confirmed\":true}",1,2);setup();check("signal.lock",lock,1,1);stale=1;check("signal.reset","{\"confirmed\":true}",0,2);
 setup();r=reply(1,"");cJSON_AddObjectToObject(r,"data");cJSON_AddArrayToObject(r,"sections");radio_tools_sections(r);assert(writes==0&&sms_reads==0);assert(contains(r,"设备未读"));assert(!contains(r,"synthetic-private-number"));assert(!contains(r,"content"));cJSON_Delete(r);
 setup();char huge[9000];memset(huge,'1',sizeof(huge)-1);huge[sizeof(huge)-1]=0;cJSON_ReplaceItemInObject(net,"nrca",cJSON_CreateString(huge));r=call("signal.serving","{}",1,0);assert(contains(r,"超出上限"));cJSON_Delete(r);
 setup();scan_state=2;caps=0;r=reply(1,"");cJSON_AddArrayToObject(r,"sections");radio_tools_sections(r);assert(contains(r,"本机扫描返回失败，待验证"));assert(!writes);cJSON_Delete(r);
 assert(radio_tools_action("unknown",NULL)==NULL);cJSON_Delete(net);cJSON_Delete(pages);cJSON_Delete(last);cJSON_Delete(fixture);puts("PASS: radio/SMS decode, bounds, malformed reads, no state writes, capabilities, stale/rejected setters, ID checks, click-to-read picker and pagination");return 0;
}
