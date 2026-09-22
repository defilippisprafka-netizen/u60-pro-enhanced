/* B28 radio/SMS tools. Read paths never scan, mark read or write settings.
 * SMS bodies live only in the on-demand reply; callers must not persist/log it.
 * Include after panel-advanced-control.h. All mutation capabilities default off. */
#ifndef PANEL_RADIO_TOOLS_H
#define PANEL_RADIO_TOOLS_H
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#ifndef RADIO_BUS
#define RADIO_BUS ubus_call
#endif
#define RADIO_SMS_PAGE_SIZE 10
#define RADIO_TEXT_MAX 12288
#define RADIO_ROW_MAX 32
static int radio_sha_valid(const char *s){if(strlen(s)!=64)return 0;for(int i=0;i<64;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;return 1;}
#ifndef RADIO_RUN
#define RADIO_RUN run_cmd
#endif
static int radio_capability(const char *name){
 if(fixture)return 0;
 int sms=!strcmp(name,"sms-delete");if(!sms&&strcmp(name,"nr-scan")&&strcmp(name,"nr-lock")&&strcmp(name,"lte-lock")&&strcmp(name,"band-cell-reset"))return 0;
 char path[160],buf[1024];snprintf(path,sizeof(path),"/data/u60-panel/verified-b28-%s",name);
 int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return 0;struct stat st;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||(st.st_mode&022)||st.st_size<2||st.st_size>=(off_t)sizeof(buf)){close(fd);return 0;}
 ssize_t n=read(fd,buf,sizeof(buf)-1);close(fd);if(n!=st.st_size)return 0;buf[n]=0;
 cJSON *j=cJSON_ParseWithOpts(buf,NULL,1);int ok=cJSON_IsObject(j)&&cJSON_IsNumber(jget(j,"version"))&&jget(j,"version")->valuedouble==1&&cJSON_IsTrue(jget(j,"verified"))&&!strcmp(jstr(j,"capability"),name);
 const char *paths[]={"/etc/openwrt_release",sms?"/usr/bin/zte_topsw_wms":"/usr/bin/zte_topsw_nwinfo",sms?"/usr/lib/libzte_wms.so":"/usr/lib/libzte_nwinfo.so"};
 const char *keys[]={"firmware_sha256","daemon_sha256","library_sha256"};
 for(int k=0;k<3&&ok;k++){const char *want=jstr(j,keys[k]);if(!radio_sha_valid(want)){ok=0;break;}char out[256];char *av[]={"sha256sum",(char*)paths[k],NULL};const char *sha=access("/usr/bin/sha256sum",X_OK)==0?"/usr/bin/sha256sum":"/bin/sha256sum";ok=RADIO_RUN(sha,av,NULL,out,sizeof(out))&&!strncmp(out,want,64)&&(out[64]==' '||out[64]=='\t');}
 cJSON_Delete(j);return ok;
}
#ifndef RADIO_CAP
#define RADIO_CAP radio_capability
#endif
static void radio_wipe(void *p,size_t n){volatile unsigned char *b=p;while(n--)*b++=0;}
static void radio_private_delete(cJSON *r){if(!r)return;cJSON *v;for(v=r->child;v;v=v->next)radio_private_delete(v);if(cJSON_IsString(r)&&r->valuestring)radio_wipe(r->valuestring,strlen(r->valuestring));}
static void radio_sms_free(cJSON *r){radio_private_delete(r);cJSON_Delete(r);}
static int radio_int(const cJSON *v,int min,int max,int *out){
 double d;if(cJSON_IsNumber(v))d=v->valuedouble;else if(cJSON_IsString(v)){const char *s=v->valuestring;size_t len=strnlen(s,16);if(!len||len>=16)return 0;for(size_t n=0;n<len;n++)if(!isdigit((unsigned char)s[n]))return 0;char *end;d=strtod(s,&end);if(*end)return 0;}else return 0;
 if(!isfinite(d)||d<min||d>max||d!=(int)d)return 0;*out=(int)d;return 1;
}
static int radio_arg(const cJSON *a,const char *key,int min,int max,int fallback,int *out){cJSON *v=jget(a,key);if(!v&&fallback>=min&&fallback<=max){*out=fallback;return 1;}return radio_int(v,min,max,out);}
static int radio_success(const cJSON *r){
 if(!cJSON_IsObject(r)||jget(r,"error"))return 0;cJSON *v=jget(r,"error_code");if(v&&!((cJSON_IsNumber(v)&&v->valuedouble==0)||(cJSON_IsString(v)&&!strcmp(v->valuestring,"0"))))return 0;
 v=jget(r,"result");return !v||(cJSON_IsNumber(v)&&v->valuedouble==0)||(cJSON_IsString(v)&&(!strcmp(v->valuestring,"0")||!strcmp(v->valuestring,"success")))||cJSON_IsTrue(v);
}
static int radio_confirmed(const cJSON *a){return cJSON_IsTrue(jget(a,"confirmed"));}
static cJSON *radio_get(const char *method){return RADIO_BUS("zte_nwinfo_api",method,NULL);}
static cJSON *radio_report(const char *title){cJSON *r=reply(1,"读取完成"),*p=cJSON_AddObjectToObject(r,"report");cJSON_AddStringToObject(p,"title",title);cJSON_AddArrayToObject(p,"lines");return r;}
static void radio_line(cJSON *r,const char *fmt,...){cJSON *a=jget(jget(r,"report"),"lines");if(cJSON_GetArraySize(a)>=240)return;char line[768];va_list ap;va_start(ap,fmt);vsnprintf(line,sizeof(line),fmt,ap);va_end(ap);size_t n=strlen(line);if(n==sizeof(line)-1){size_t start=n-1;while(start&&((unsigned char)line[start]&0xc0)==0x80)start--;if((unsigned char)line[start]>=0x80)line[start]=0;}cJSON_AddItemToArray(a,cJSON_CreateString(line));radio_wipe(line,sizeof(line));}
static void radio_stamp(cJSON *r){time_t now=time(NULL);struct tm tm;char buf[48];time_t shown=now+28800;if(now>1609459200&&gmtime_r(&shown,&tm)&&strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S UTC+8",&tm))radio_line(r,"读取时间：%s",buf);else radio_line(r,"读取时间：设备时钟未确认");}
static void radio_text(const cJSON *v,char *out,size_t cap){
 if(cJSON_IsNumber(v)&&isfinite(v->valuedouble)){snprintf(out,cap,"%.2f",v->valuedouble);size_t n=strlen(out);while(n&&out[n-1]=='0')out[--n]=0;if(n&&out[n-1]=='.')out[--n]=0;return;}
 if(cJSON_IsString(v)&&*v->valuestring&&strnlen(v->valuestring,256)<256){size_t n=0;for(const unsigned char *s=(const unsigned char*)v->valuestring;*s&&n+1<cap;s++)out[n++]=*s<32||*s==127?' ':*s;out[n]=0;return;}snprintf(out,cap,"未知");
}
static void radio_value(cJSON *r,const cJSON *obj,const char *key,const char *label,const char *unit){char s[260];radio_text(jget(obj,key),s,sizeof(s));radio_line(r,"%s：%s%s",label,s,!strcmp(s,"未知")?"":unit);}
/* Split preserves empty fields, avoiding accidental index shifts on bad modem CSV. */
static int radio_split(char *s,char sep,char **out,int cap){int n=0;if(!s||!*s)return 0;out[n++]=s;for(;*s;s++)if(*s==sep){*s=0;if(n==cap)return -1;out[n++]=s+1;}return n;}
static int radio_csv(const char *s,char *buf,size_t cap,char **rows,int n){size_t len=strnlen(s,cap);if(len>=cap)return -1;memcpy(buf,s,len+1);return radio_split(buf,';',rows,n);}
static void radio_metric(cJSON *r,const char *label,const char *s,const char *unit){char *end;double v=strtod(s,&end);if(!*s||*end||!isfinite(v)||v < -200||v>200)radio_line(r,"%s：未知",label);else radio_line(r,"%s：%.2f %s",label,v,unit);}
static void radio_carriers(cJSON *r,const cJSON *net,int nr){
 const char *keys4[]={"wan_active_band","lte_pci","wan_active_channel","bandwidth","lte_rsrp","lte_rsrq","lte_snr","lte_rssi"};
 const char *keys5[]={"nr5g_action_band","nr5g_pci","nr5g_action_channel","nr5g_bandwidth","nr5g_rsrp","nr5g_rsrq","nr5g_snr","nr5g_rssi"};
 const char *labels[]={"频段","PCI","ARFCN","带宽","RSRP","RSRQ","SINR","RSSI"},*units[]={"","",""," MHz"," dBm"," dB"," dB"," dBm"};
 radio_line(r,"—— %s 主载波 ——",nr?"NR":"LTE");for(int j=0;j<8;j++)radio_value(r,net,nr?keys5[j]:keys4[j],labels[j],units[j]);
 char buf[4096],sigbuf[4096],*rows[17],*sigrows[17];int n=radio_csv(jstr(net,nr?"nrca":"lteca"),buf,sizeof(buf),rows,17),ns=nr?0:radio_csv(jstr(net,"ltecasig"),sigbuf,sizeof(sigbuf),sigrows,17);
 if(n<0||ns<0){radio_line(r,"辅载波数据超出上限或格式异常，未解析");return;}if(!n){radio_line(r,"辅载波：接口未提供");return;}
 for(int j=nr?0:1;j<n&&j<16;j++){char *p[13],*q[8];int np=radio_split(rows[j],',',p,13),nq=nr?0:j-1<ns?radio_split(sigrows[j-1],',',q,8):0;if(!*rows[j])continue;
  radio_line(r,"—— %s 辅载波 %d ——",nr?"NR":"LTE",nr?j+1:j);
  if(np<(nr?11:5)||np>13||(!nr&&nq<4)){radio_line(r,"字段不完整，保留未知");continue;}
  const int indexes5[]={3,1,4,5},indexes4[]={1,0,3,4};int valid=1;
  for(int k=0;k<4;k++){const char *v=p[nr?indexes5[k]:indexes4[k]];if(strnlen(v,40)>=40)valid=0;}
  if(!valid){radio_line(r,"字段过长，未解析");continue;}
  radio_line(r,"频段 %s · PCI %s",p[nr?3:1],p[nr?1:0]);radio_line(r,"ARFCN %s · %s MHz",p[nr?4:3],p[nr?5:4]);radio_line(r,"状态：%s",!strcmp(p[2],"2")?"激活":!strcmp(p[2],"1")?"未激活":"未知");
  for(int k=0;k<4;k++)radio_metric(r,labels[k+4],nr?p[k+7]:q[k],k==0||k==3?"dBm":"dB");
 }
}
static cJSON *radio_signal(void){cJSON *net=radio_get("nwinfo_get_netinfo");if(!radio_success(net)){cJSON_Delete(net);return reply(0,"信号接口不可用");}cJSON *r=radio_report("服务小区与载波信号");radio_stamp(r);radio_value(r,net,"network_type","网络制式","");radio_line(r,"来源：nwinfo_get_netinfo；空字段为未知。非当前制式的历史值可能仍由固件返回。");radio_carriers(r,net,0);radio_carriers(r,net,1);radio_value(r,net,"lock_lte_cell","LTE 锁定 PCI,ARFCN","");radio_value(r,net,"lock_nr_cell","NR 锁定 PCI,ARFCN,band","");cJSON_Delete(net);return r;}
static cJSON *radio_neighbors(void){cJSON *r=radio_report("邻区结果（只读缓存）");radio_stamp(r);radio_line(r,"未发起扫描；空串表示当前缓存无记录。五列字段语义尚未实机确认，不用于锁定。");
 const char *methods[]={"nwinfo_get_lte_nbr_contents","nwinfo_get_nr5g_nbr_contents"},*keys[]={"lte_nbr_contents","nr5g_nbr_contents"};
 for(int j=0;j<2;j++){cJSON *raw=radio_get(methods[j]),*v=jget(raw,keys[j]);radio_line(r,"—— %s 邻区 ——",j?"NR":"LTE");if(!cJSON_IsString(v)){radio_line(r,"接口未提供结果");cJSON_Delete(raw);continue;}char buf[8192],*rows[RADIO_ROW_MAX+1];int n=radio_csv(v->valuestring,buf,sizeof(buf),rows,RADIO_ROW_MAX+1);if(n<0)radio_line(r,"结果超出 32 行 / 8192 字节上限，未展示");else if(!n)radio_line(r,"缓存为空（不等于周边没有小区）");else for(int k=0;k<n&&k<RADIO_ROW_MAX;k++){if(!*rows[k])continue;int safe=1;for(const unsigned char *p=(unsigned char*)rows[k];*p;p++)if(!isdigit(*p)&&*p!=','&&*p!='-'&&*p!='.'&&*p!=' ')safe=0;if(!safe||strlen(rows[k])>220)radio_line(r,"第 %d 行格式未识别",k+1);else radio_line(r,"原始记录 %d：%s",k+1,rows[k]);}cJSON_Delete(raw);}
 return r;
}
static int radio_sms_args(const cJSON *a,int *page){return radio_arg(a,"page",0,999,0,page);}
static cJSON *radio_sms_page(int page){cJSON *a=cJSON_CreateObject();cJSON_AddNumberToObject(a,"page",page);cJSON_AddNumberToObject(a,"data_per_page",RADIO_SMS_PAGE_SIZE);cJSON_AddNumberToObject(a,"mem_store",1);cJSON_AddNumberToObject(a,"tags",10);cJSON_AddStringToObject(a,"order_by","order by id desc");cJSON *r=RADIO_BUS("zwrt_wms","zte_libwms_get_sms_data",a);cJSON_Delete(a);return r;}
static int radio_inbound(const cJSON *m){int tag;return cJSON_IsObject(m)&&radio_int(jget(m,"tag"),0,4,&tag)&&tag<2;}
static int radio_hex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static int radio_utf8_emit(unsigned cp,char *out,size_t cap,size_t *used){unsigned char s[4];size_t n;if(cp<0x80){n=1;s[0]=cp;}else if(cp<0x800){n=2;s[0]=0xc0|(cp>>6);s[1]=0x80|(cp&63);}else if(cp<0x10000){n=3;s[0]=0xe0|(cp>>12);s[1]=0x80|((cp>>6)&63);s[2]=0x80|(cp&63);}else{n=4;s[0]=0xf0|(cp>>18);s[1]=0x80|((cp>>12)&63);s[2]=0x80|((cp>>6)&63);s[3]=0x80|(cp&63);}if(*used+n>=cap)return 0;memcpy(out+*used,s,n);*used+=n;out[*used]=0;return 1;}
/* Default firmware content is UTF-16BE hex. Explicit UTF8/plain input is also
 * supported for test fixtures/future verified firmware, never guessed as hex. */
static int radio_decode(const char *s,const char *encoding,char *out,size_t cap){
 size_t len=strnlen(s,RADIO_TEXT_MAX+1),used=0;if(!cap||len>RADIO_TEXT_MAX)return 0;out[0]=0;int utf8=!strcmp(encoding,"UTF8")||!strcmp(encoding,"UTF-8");if(!*encoding)for(size_t n=0;n<len;n++)if(radio_hex(s[n])<0){utf8=1;break;}
 if(!utf8&&*encoding&&strcmp(encoding,"UCS2")&&strcmp(encoding,"UTF-16BE"))return 0;
 if(!utf8){if(len%4)return 0;for(size_t n=0;n<len;n++)if(radio_hex(s[n])<0)return 0;
  for(size_t n=0;n<len;n+=4){unsigned cp=0;for(int k=0;k<4;k++)cp=cp*16+radio_hex(s[n+k]);if(cp>=0xd800&&cp<=0xdbff){if(n+8>len)return 0;unsigned lo=0;for(int k=0;k<4;k++)lo=lo*16+radio_hex(s[n+4+k]);if(lo<0xdc00||lo>0xdfff)return 0;cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);n+=4;}else if(cp>=0xdc00&&cp<=0xdfff)return 0;
   if(cp==0||cp==0xfeff)continue;if(cp<32&&cp!='\n'&&cp!='\r'&&cp!='\t')cp=' ';if(!radio_utf8_emit(cp,out,cap,&used))return 0;
  }return 1;
 }
 for(size_t n=0;n<len;){unsigned char c=s[n];unsigned cp;size_t k,need;if(c<0x80){cp=c;need=1;}else if(c>=0xc2&&c<=0xdf){cp=c&31;need=2;}else if(c>=0xe0&&c<=0xef){cp=c&15;need=3;}else if(c>=0xf0&&c<=0xf4){cp=c&7;need=4;}else return 0;if(n+need>len)return 0;for(k=1;k<need;k++){unsigned char d=s[n+k];if((d&0xc0)!=0x80)return 0;cp=(cp<<6)|(d&63);}if((need==3&&cp<0x800)||(need==4&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return 0;if(cp<32&&cp!='\n'&&cp!='\r'&&cp!='\t')cp=' ';if(!radio_utf8_emit(cp,out,cap,&used))return 0;n+=need;}return 1;
}
static void radio_body_lines(cJSON *r,char *body){const char *s=body;while(*s){const char *e=s;size_t n=0;while(*e&&*e!='\n'&&n<600){size_t k=(*(unsigned char*)e<0x80)?1:(*(unsigned char*)e<0xe0)?2:(*(unsigned char*)e<0xf0)?3:4;if(n+k>600)break;e+=k;n+=k;}char line[604];memcpy(line,s,n);line[n]=0;radio_line(r,"%s",line);radio_wipe(line,sizeof(line));s=*e=='\n'?e+1:e;}}
/* Use the firmware date only as a local-looking label; timezone is unknown. */
static void radio_sms_short_time(const cJSON *m,char *out,size_t cap){
 const char *s=jstr(m,"date");char buf[64],*p[8];if(strnlen(s,sizeof(buf))>=sizeof(buf)){snprintf(out,cap,"时间未知");return;}strcpy(buf,s);int count=radio_split(buf,',',p,8),values[6],low[]={0,1,1,0,0,0},high[]={99,12,31,23,59,60};
 if(count<6){snprintf(out,cap,"时间未知");return;}for(int k=0;k<6;k++){cJSON *v=cJSON_CreateString(p[k]);int ok=radio_int(v,low[k],high[k],&values[k]);cJSON_Delete(v);if(!ok){snprintf(out,cap,"时间未知");return;}}
 snprintf(out,cap,"%02d-%02d %02d:%02d",values[1],values[2],values[3],values[4]);
}
static void radio_sms_picker_choice(cJSON *picker,const char *label,int page,int id,const char *action){
 cJSON *choice=cJSON_CreateObject(),*a=cJSON_AddObjectToObject(choice,"args");cJSON_AddStringToObject(choice,"label",label);cJSON_AddNumberToObject(a,"page",page);if(id>=0)cJSON_AddNumberToObject(a,"id",id);if(action)cJSON_AddStringToObject(choice,"action",action);cJSON_AddItemToArray(jget(picker,"choices"),choice);
}
static cJSON *radio_sms_read(const cJSON *a,int read_body){
 int page,id=-1;if(!radio_sms_args(a,&page)||(read_body&&!radio_arg(a,"id",0,2147483647,-1,&id)))return reply(0,"页码须为 0–999，短信 ID 须为非负整数");
 cJSON *raw=radio_sms_page(page),*messages=jget(raw,"messages");if(!radio_success(raw)||!cJSON_IsArray(messages)||cJSON_GetArraySize(messages)>RADIO_SMS_PAGE_SIZE){radio_sms_free(raw);return reply(0,"短信分页接口不可用或返回数量异常");}
 int count=cJSON_GetArraySize(messages);cJSON *r=radio_report(read_body?"短信内容":"收件短信（按需读取）"),*picker=NULL;
 if(!read_body){picker=cJSON_CreateObject();cJSON_AddStringToObject(picker,"type","choice");cJSON_AddStringToObject(picker,"label","短信收件箱");cJSON_AddStringToObject(picker,"action","sms.read");cJSON_AddBoolToObject(picker,"confirm",0);cJSON_AddArrayToObject(picker,"choices");}
 radio_stamp(r);radio_line(r,"原厂消息页 %d（每页 10 条）· 仅展示收件项",page);radio_line(r,"只在当前查看中保留内容，不修改已读状态。短信原厂时间的时区未知。");int found=0,seen[10],nseen=0;cJSON *m;
 cJSON_ArrayForEach(m,messages){
  int mid;if(!radio_inbound(m)||!radio_int(jget(m,"id"),0,2147483647,&mid))continue;int duplicate=0;for(int k=0;k<nseen;k++)if(seen[k]==mid)duplicate=1;if(duplicate){radio_line(r,"存在重复 ID，拒绝读取此页");cJSON_ReplaceItemInObject(r,"ok",cJSON_CreateBool(0));break;}seen[nseen++]=mid;if(read_body&&mid!=id)continue;found++;
  const char *status=adv_scalar_eq(jget(m,"tag"),"1")?"未读":"已读";radio_line(r,"—— ID %d · %s ——",mid,status);radio_value(r,m,"date","原厂时间（时区未知）","");
  if(!read_body){char date[32],label[64];radio_sms_short_time(m,date,sizeof(date));snprintf(label,sizeof(label),"%s · %s",date,status);radio_sms_picker_choice(picker,label,page,mid,NULL);}
  if(read_body){char decoded[RADIO_TEXT_MAX+1];const char *enc=jstr(m,"encoding");if(!cJSON_IsString(jget(m,"content"))||!radio_decode(jstr(m,"content"),enc,decoded,sizeof(decoded))){radio_line(r,"内容编码不受支持、损坏或过长，未展示");cJSON_ReplaceItemInObject(r,"ok",cJSON_CreateBool(0));}else{if(!*decoded)radio_line(r,"（空内容）");else radio_body_lines(r,decoded);}radio_wipe(decoded,sizeof(decoded));}
 }
 if(!found)radio_line(r,read_body?"该 ID 不在此页的收件项中，请刷新列表":"此页没有收件项；可能为空或只有已发送/草稿项");if(read_body&&!found)cJSON_ReplaceItemInObject(r,"ok",cJSON_CreateBool(0));
 if(picker){
  char label[64];if(page>0){snprintf(label,sizeof(label),"上一页 · 第 %d 页",page);radio_sms_picker_choice(picker,label,page-1,-1,"sms.list");}
  if(count==RADIO_SMS_PAGE_SIZE&&page<999){snprintf(label,sizeof(label),"下一页 · 第 %d 页",page+2);radio_sms_picker_choice(picker,label,page+1,-1,"sms.list");radio_line(r,"原厂本页已满 10 条，可点击下一页。");}
  if(found)radio_line(r,"点击收件项直接阅读。也可使用“按 ID 阅读”表单。");
  if(cJSON_IsTrue(jget(r,"ok"))&&cJSON_GetArraySize(jget(picker,"choices")))cJSON_AddItemToObject(r,"picker",picker);else cJSON_Delete(picker);
 }
 radio_sms_free(raw);return r;
}
/* A complete bounded page search is required both before and after delete;
 * inability to prove absence never reports deletion success. */
static int radio_sms_find(int id){for(int page=0;page<50;page++){cJSON *raw=radio_sms_page(page),*list=jget(raw,"messages");if(!radio_success(raw)||!cJSON_IsArray(list)||cJSON_GetArraySize(list)>RADIO_SMS_PAGE_SIZE){radio_sms_free(raw);return -1;}int count=cJSON_GetArraySize(list),found=0;cJSON *m;cJSON_ArrayForEach(m,list){int mid;if(!radio_int(jget(m,"id"),0,2147483647,&mid)){radio_sms_free(raw);return -1;}if(mid==id){if(!radio_inbound(m)){radio_sms_free(raw);return -1;}found++;}}radio_sms_free(raw);if(found>1)return -1;if(found==1)return 1;if(count<RADIO_SMS_PAGE_SIZE)return 0;}return -1;}
static cJSON *radio_sms_delete(const cJSON *a){int id;if(!radio_arg(a,"id",0,2147483647,-1,&id))return reply(0,"短信 ID 无效");if(!radio_confirmed(a))return reply(0,"删除需确认所选 ID");if(!RADIO_CAP("sms-delete"))return reply(0,"本固件删除尚未实机验收，未删除");int found=radio_sms_find(id);if(found!=1)return reply(0,found==0?"该 ID 已不存在，未删除":"无法确认 ID 唯一存在，未删除");cJSON *b=cJSON_CreateObject();char idstr[32];snprintf(idstr,sizeof(idstr),"%d;",id);cJSON_AddStringToObject(b,"id",idstr);cJSON *res=RADIO_BUS("zwrt_wms","zwrt_wms_delete_sms",b);cJSON_Delete(b);int accepted=radio_success(res);cJSON_Delete(res);if(!accepted)return reply(0,"短信删除未获原厂接口确认");for(int n=0;n<6;n++){found=radio_sms_find(id);if(found==0)return reply(1,"该 ID 已从消息库列表消失");if(found<0)break;adv_wait();}return reply(0,"已请求删除，但无法回读确认消失");}
static int radio_band(const cJSON *v,int *out){if(radio_int(v,1,1024,out))return 1;if(!cJSON_IsString(v))return 0;const char *s=v->valuestring;if(strnlen(s,32)>=32)return 0;if(!strncmp(s,"NR5G BAND ",10))s+=10;else if(!strncmp(s,"LTE BAND ",9))s+=9;else if(*s=='n'||*s=='N'||*s=='B'||*s=='b')s++;cJSON *t=cJSON_CreateString(s);int ok=radio_int(t,1,1024,out);cJSON_Delete(t);return ok;}
static int radio_lock_values(const cJSON *n,int nr,int *pci,int *arfcn,int *band){return radio_int(jget(n,nr?"nr5g_pci":"lte_pci"),0,nr?1007:503,pci)&&radio_int(jget(n,nr?"nr5g_action_channel":"wan_active_channel"),1,nr?3279165:262143,arfcn)&&(!nr||radio_band(jget(n,"nr5g_action_band"),band));}
static int radio_locked(const cJSON *net,int nr,int pci,int arfcn,int band){const char *s=jstr(net,nr?"lock_nr_cell":"lock_lte_cell");char buf[100],*p[5];if(strnlen(s,sizeof(buf))>=sizeof(buf))return 0;strcpy(buf,s);int n=radio_split(buf,',',p,5);if(n<(nr?3:2))return 0;int want[]={pci,arfcn,band};for(int k=0;k<(nr?3:2);k++){cJSON *v=cJSON_CreateString(p[k]);int x,ok=radio_int(v,0,3279165,&x)&&x==want[k];cJSON_Delete(v);if(!ok)return 0;}return 1;}
static cJSON *radio_lock(const cJSON *a){const char *rat=jstr(a,"rat"),*source=jstr(a,"source");int nr=!strcmp(rat,"nr"),pci,arfcn,band=0;if(!nr&&strcmp(rat,"lte"))return reply(0,"仅支持 LTE / NR");if(strcmp(source,"current"))return reply(0,"邻区列语义尚未实机确认，仅允许当前服务小区");if(!radio_arg(a,"pci",0,nr?1007:503,-1,&pci)||!radio_arg(a,"arfcn",1,nr?3279165:262143,-1,&arfcn)||(nr&&!radio_arg(a,"band",1,1024,-1,&band)))return reply(0,"小区参数范围无效");if(!radio_confirmed(a))return reply(0,"锁定需确认，可能中断蜂窝连接");if(!RADIO_CAP(nr?"nr-lock":"lte-lock"))return reply(0,"本固件锁定尚未完成写入及恢复验收");
 cJSON *before=radio_get("nwinfo_get_netinfo");int p,f,b=0;if(!radio_lock_values(before,nr,&p,&f,&b)||p!=pci||f!=arfcn||(nr&&b!=band)||!cJSON_IsString(jget(before,nr?"lock_nr_cell":"lock_lte_cell"))){cJSON_Delete(before);return reply(0,"服务小区已变化或锁定状态不可读，未修改");}cJSON_Delete(before);
 cJSON *payload=cJSON_CreateObject();char v[32];snprintf(v,sizeof(v),"%d",pci);cJSON_AddStringToObject(payload,nr?"lock_nr_pci":"lock_lte_pci",v);snprintf(v,sizeof(v),"%d",arfcn);cJSON_AddStringToObject(payload,nr?"lock_nr_earfcn":"lock_lte_earfcn",v);if(nr){snprintf(v,sizeof(v),"%d",band);cJSON_AddStringToObject(payload,"lock_nr_cell_band",v);}cJSON *res=RADIO_BUS("zte_nwinfo_api",nr?"nwinfo_lock_nr_cell":"nwinfo_lock_lte_cell",payload);cJSON_Delete(payload);int accepted=radio_success(res);cJSON_Delete(res);if(!accepted)return reply(0,"原厂接口拒绝或未确认锁定");for(int j=0;j<6;j++){res=radio_get("nwinfo_get_netinfo");int ok=radio_locked(res,nr,pci,arfcn,band);cJSON_Delete(res);if(ok)return reply(1,"锁定参数已回读一致；联网可用性需另行检查");adv_wait();}return reply(0,"已请求锁定，但回读不一致；请刷新状态");
}
static cJSON *radio_scan(const cJSON *a){if(!radio_confirmed(a))return reply(0,"主动扫描可能影响连接，需确认");if(!RADIO_CAP("nr-scan"))return reply(0,"本固件扫描尚未实机验收");cJSON *res=radio_get("nwinfo_scan_nbr");int ok=radio_success(res);cJSON_Delete(res);if(!ok)return reply(0,"扫描请求未获确认");for(int j=0;j<10;j++){adv_wait();res=radio_get("nwinfo_m_netselect_status");const char *s=jstr(res,"m_netselect_status");int fail=!strcmp(s,"manual_search_fail"),done=!strcmp(s,"manual_search_success");cJSON_Delete(res);if(fail)return reply(0,"固件报告扫描失败");if(done){cJSON *out=radio_neighbors();cJSON_ReplaceItemInObject(out,"message",cJSON_CreateString("扫描状态已结束，显示当前邻区缓存"));return out;}}return reply(0,"扫描已请求，结束状态未确认；稍后读取邻区缓存");}
static int radio_unlocked(const cJSON *n,const char *key){const cJSON *v=jget(n,key);if(!cJSON_IsString(v))return 0;const char *s=v->valuestring;if(!*s)return 1;for(;*s;s++)if(*s!='0'&&*s!=',')return 0;return 1;}
static cJSON *radio_reset(const cJSON *a){if(!radio_confirmed(a))return reply(0,"重置会同时恢复原厂频段与小区限制，需确认");if(!RADIO_CAP("band-cell-reset"))return reply(0,"本固件频段与小区重置尚未验收");cJSON *before=radio_get("nwinfo_get_netinfo");if(!jget(before,"lock_lte_cell")||!jget(before,"lock_nr_cell")){cJSON_Delete(before);return reply(0,"当前锁定状态不可读，未重置");}cJSON_Delete(before);cJSON *res=radio_get("nwinfo_reset_band_cell_setting");int ok=radio_success(res);cJSON_Delete(res);if(!ok)return reply(0,"原厂接口拒绝或未确认重置");for(int n=0;n<6;n++){res=radio_get("nwinfo_get_netinfo");ok=radio_unlocked(res,"lock_lte_cell")&&radio_unlocked(res,"lock_nr_cell");cJSON_Delete(res);if(ok)return reply(1,"小区解除已回读；原厂频段也已请求恢复，请检查频段页");adv_wait();}return reply(0,"已请求重置，但小区解除未确认");}
static cJSON *radio_tools_action(const char *action,const cJSON *args){if(!strcmp(action,"signal.serving"))return radio_signal();if(!strcmp(action,"signal.neighbors"))return radio_neighbors();if(!strcmp(action,"signal.scan"))return radio_scan(args);if(!strcmp(action,"signal.lock"))return radio_lock(args);if(!strcmp(action,"signal.reset"))return radio_reset(args);if(!strcmp(action,"signal.lock.neighbor"))return reply(0,"邻区字段映射未确认，不能锁定");if(!strcmp(action,"sms.list"))return radio_sms_read(args,0);if(!strcmp(action,"sms.read"))return radio_sms_read(args,1);if(!strcmp(action,"sms.delete"))return radio_sms_delete(args);return NULL;}
static cJSON *radio_button(cJSON *s,const char *id,const char *label,const char *type,const char *value,int enabled,const char *reason,int confirm){cJSON *i=item(s,id,label,type,value,id,enabled,reason);cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(confirm));if(confirm)cJSON_AddBoolToObject(jget(i,"args"),"confirmed",1);return i;}
static void radio_tools_sections(cJSON *root){
 cJSON *s=adv_section(root,"sms","短信收件箱"),*cap=RADIO_BUS("zwrt_wms","zwrt_wms_get_wms_capacity",NULL);info(s,"sms.unread","设备未读",cap,"sms_dev_unread_num");info(s,"sms.sim.unread","SIM 未读",cap,"sms_sim_unread_num");info(s,"sms.received","设备收件数",cap,"sms_nv_rev_total");cJSON_Delete(cap);
 radio_button(s,"sms.list","查看第一页","action","仅主动读取时取消息",1,"设备消息库；只显示收件项",0);cJSON *i=radio_button(s,"sms.list","短信分页","form","每页 10 条",1,"页码从 0 开始；不自动设为已读",0);cJSON_ReplaceItemInObject(i,"id",cJSON_CreateString("sms.page"));field(i,"page","原厂消息页码","0","number",1);
 i=radio_button(s,"sms.read","按 ID 阅读","form","内容仅在当前查看中保留",1,NULL,0);field(i,"page","列表页码","0","number",1);field(i,"id","收件短信 ID","","number",1);
 i=radio_button(s,"sms.delete","删除指定短信","form","需要确认；不可恢复",RADIO_CAP("sms-delete"),RADIO_CAP("sms-delete")?"删除后无法恢复":"本固件删除尚未验证，暂未开放",1);field(i,"id","要删除的短信 ID","","number",1);
 s=adv_section(root,"signal","信号与小区");radio_button(s,"signal.serving","载波信号详情","action","LTE / NR 主辅载波",1,NULL,0);radio_button(s,"signal.neighbors","查看邻区缓存","action","不发起扫描",1,NULL,0);cJSON *scan=radio_get("nwinfo_m_netselect_status");int scan_failed=!strcmp(jstr(scan,"m_netselect_status"),"manual_search_fail");radio_button(s,"signal.scan","主动扫描 NR 邻区","action",scan_failed?"本机扫描返回失败，待验证":"可能短暂影响连接",RADIO_CAP("nr-scan"),scan_failed?"实机返回 manual_search_fail；当前禁止扫描":"扫描需实机确认结束状态后启用",1);cJSON_Delete(scan);
 cJSON *net=radio_get("nwinfo_get_netinfo");info(s,"signal.lock.lte","当前 LTE 锁定",net,"lock_lte_cell");info(s,"signal.lock.nr","当前 NR 锁定",net,"lock_nr_cell");
 for(int nr=0;nr<2;nr++){int p,f,b=0;int ready=radio_lock_values(net,nr,&p,&f,&b)&&RADIO_CAP(nr?"nr-lock":"lte-lock");i=radio_button(s,"signal.lock",nr?"锁定当前 NR 小区":"锁定当前 LTE 小区","action",ready?"当前 PCI / ARFCN":"尚未验收或服务参数不完整",ready,ready?"锁定前再次核对当前小区，可能失联":"本机锁定验证未完成或小区参数不完整，暂未开放",1);cJSON_ReplaceItemInObject(i,"id",cJSON_CreateString(nr?"signal.lock.nr":"signal.lock.lte"));cJSON *a=jget(i,"args");cJSON_AddStringToObject(a,"source","current");cJSON_AddStringToObject(a,"rat",nr?"nr":"lte");if(ready){cJSON_AddNumberToObject(a,"pci",p);cJSON_AddNumberToObject(a,"arfcn",f);if(nr)cJSON_AddNumberToObject(a,"band",b);}}
 cJSON_Delete(net);radio_button(s,"signal.lock.neighbor","锁定邻区","action","邻区字段映射待实机确认",0,"不将未确认的原始列当作锁定参数",1);radio_button(s,"signal.reset","重置频段与小区限制","action","恢复原厂，可能失联",RADIO_CAP("band-cell-reset"),"原厂此接口同时重置频段；需实机验收后启用",1);
}
#endif
