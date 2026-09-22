/* Charge policy. The controller must hold its shared mutation lock around
 * charge_action/charge_tick. State reads never reconcile or write files.
 * Legacy pause uses BSP; verified FCC-zero keeps USB input supplying the system.
 * FCC controls are bound to a physically verified firmware/kernel marker. */
#ifndef PANEL_CHARGE_H
#define PANEL_CHARGE_H
#include <ctype.h>
#include <math.h>
#include <sys/stat.h>
#ifndef CHARGE_DIR
#define CHARGE_DIR "/data/u60-panel"
#endif
#ifndef CHARGE_BUS
#define CHARGE_BUS ubus_call
#endif
#ifndef CHARGE_RUN
#define CHARGE_RUN run_cmd
#endif
#ifndef CHARGE_FIXTURE
#define CHARGE_FIXTURE (fixture != NULL)
#endif
#ifndef CHARGE_OWNER
#define CHARGE_OWNER 0
#endif
#define CHARGE_CONFIG CHARGE_DIR "/charge-policy.json"
#define CHARGE_MARKER CHARGE_DIR "/charge-verified"
#define CHARGE_HYSTERESIS 5
struct charge_config { int limit, original, last, pending, fault, restore; char boot[40]; };
struct charge_sample {
 int capacity, online, connected, mode, status;
 int temperature_known; double temperature;
 int current_known, voltage_known; double current, voltage;
};
/* mode: 1 = enable (pause); 0 = disable (allow charging); -1 = unknown. */
static int charge_mode(const char *s) { return !strcmp(s,"enable")?1:!strcmp(s,"disable")?0:-1; }
static const char *charge_mode_name(int mode) { return mode==1?"enable":mode==0?"disable":""; }
static int charge_int(const cJSON *v,int low,int high,int *out) {
 double n; if(cJSON_IsNumber(v)) n=v->valuedouble;
 else if(cJSON_IsString(v)) { const char *s=v->valuestring; char *end; if(!*s||isspace((unsigned char)*s))return 0; errno=0; n=strtod(s,&end);if(errno||*end)return 0; }
 else return 0;
 if(!isfinite(n)||n<low||n>high||n!=(int)n)return 0; *out=(int)n; return 1;
}
static int charge_read_file(const char *path,char *out,size_t cap,int trusted) {
 if(CHARGE_FIXTURE){errno=ENOENT;return 0;}
 int fd=open(path,O_RDONLY|O_NOFOLLOW);if(fd<0)return 0;struct stat st;
 if(fstat(fd,&st)||!S_ISREG(st.st_mode)||(trusted&&(st.st_uid!=CHARGE_OWNER||(st.st_mode&0022)))){close(fd);errno=EPERM;return 0;}
 size_t used=0;while(used+1<cap){ssize_t n=read(fd,out+used,cap-used-1);if(n<0&&errno==EINTR)continue;if(n<0){close(fd);return 0;}if(!n)break;used+=(size_t)n;}
 char extra;ssize_t n=read(fd,&extra,1);close(fd);out[used]=0;if(n!=0){errno=EFBIG;return 0;}return 1;
}
#ifndef CHARGE_READ
#define CHARGE_READ charge_read_file
#endif
static int charge_trusted_dir(void) {
 struct stat st;return !lstat(CHARGE_DIR,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==CHARGE_OWNER&&!(st.st_mode&0022);
}
static cJSON *charge_json(const char *s) { const char *end=NULL;return cJSON_ParseWithOpts(s,&end,1); }
/* Missing config is OFF. A malformed existing config is an error, never a reset. */
static int charge_load(struct charge_config *c) {
 *c=(struct charge_config){0,-1,-1,-1,0,0,{0}};char buf[2048];
 if(!CHARGE_READ(CHARGE_CONFIG,buf,sizeof(buf),1))return errno==ENOENT?1:0;
 cJSON *j=charge_json(buf);int version,ok=cJSON_IsObject(j)&&charge_int(jget(j,"version"),1,1,&version)&&
  charge_int(jget(j,"limit"),0,100,&c->limit)&&(c->limit==0||c->limit==80||c->limit==90||c->limit==100)&&
  charge_int(jget(j,"original_mode"),-1,1,&c->original)&&charge_int(jget(j,"last_mode"),-1,1,&c->last)&&
  charge_int(jget(j,"pending_mode"),-1,1,&c->pending)&&charge_int(jget(j,"fault"),0,1,&c->fault)&&charge_int(jget(j,"restore_pending"),0,1,&c->restore);
 if(ok&&((c->limit||c->restore)&&(c->original<0||c->last<0)))ok=0;
 if(ok&&*jstr(j,"boot_id")){if(strlen(jstr(j,"boot_id"))!=36)ok=0;else snprintf(c->boot,sizeof(c->boot),"%s",jstr(j,"boot_id"));}
 cJSON_Delete(j);return ok;
}
static int charge_boot(char *out,size_t cap){if(!CHARGE_READ("/proc/sys/kernel/random/boot_id",out,cap,0))return 0;out[strcspn(out,"\r\n")]=0;return strlen(out)==36;}
static int charge_save_file(const struct charge_config *c) {
 if(CHARGE_FIXTURE)return 0;
 if(mkdir(CHARGE_DIR,0700)&&errno!=EEXIST)return 0;if(!charge_trusted_dir())return 0;
 cJSON *j=cJSON_CreateObject();cJSON_AddNumberToObject(j,"version",1);cJSON_AddNumberToObject(j,"limit",c->limit);
 cJSON_AddNumberToObject(j,"original_mode",c->original);cJSON_AddNumberToObject(j,"last_mode",c->last);cJSON_AddNumberToObject(j,"pending_mode",c->pending);
 cJSON_AddNumberToObject(j,"fault",c->fault);cJSON_AddNumberToObject(j,"restore_pending",c->restore);
 char boot[40];if(charge_boot(boot,sizeof(boot)))cJSON_AddStringToObject(j,"boot_id",boot);
 char *body=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!body)return 0;
 char tmp[]=CHARGE_DIR "/.charge-policy-XXXXXX";int fd=mkstemp(tmp);if(fd<0){free(body);return 0;}size_t n=strlen(body),done=0;int ok=1;
 while(done<n){ssize_t r=write(fd,body+done,n-done);if(r<0&&errno==EINTR)continue;if(r<=0){ok=0;break;}done+=(size_t)r;}
 free(body);if(ok&&fsync(fd))ok=0;if(close(fd))ok=0;
 if(ok&&rename(tmp,CHARGE_CONFIG))ok=0;if(!ok){unlink(tmp);return 0;}
 fd=open(CHARGE_DIR,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(fd<0)return 0;ok=!fsync(fd);close(fd);return ok;
}
#ifndef CHARGE_SAVE
#define CHARGE_SAVE charge_save_file
#endif
static int charge_sha_valid(const char *s) { if(strlen(s)!=64)return 0;for(int n=0;n<64;n++)if(!((s[n]>='0'&&s[n]<='9')||(s[n]>='a'&&s[n]<='f')))return 0;return 1; }
static int charge_fcc_restore(void) {
 char buf[1536];if(!CHARGE_READ(CHARGE_MARKER,buf,sizeof(buf),1))return 0;
 cJSON*j=charge_json(buf);int value=0;
 if(strcmp(jstr(j,"backend"),"fcc-zero")||!charge_int(jget(j,"restore_current_ua"),1,5000000,&value))value=0;
 cJSON_Delete(j);return value;
}
static int charge_verified(void) {
 char buf[1536];if(!CHARGE_READ(CHARGE_MARKER,buf,sizeof(buf),1))return 0;
 cJSON *j=charge_json(buf);int v,ok=cJSON_IsObject(j)&&charge_int(jget(j,"version"),1,1,&v)&&cJSON_IsTrue(jget(j,"stop_resume_verified"))&&
  !strcmp(jstr(j,"pause_value"),"enable")&&!strcmp(jstr(j,"resume_value"),"disable");
 int fcc=!strcmp(jstr(j,"backend"),"fcc-zero");if(*jstr(j,"backend")&&!fcc)ok=0;
 if(fcc&&(!charge_fcc_restore()||!cJSON_IsTrue(jget(j,"usb_input_retained"))))ok=0;
 const char *paths[]={"/etc/openwrt_release","/usr/bin/zte_ubus_bsp_pm","/proc/sys/kernel/osrelease"},*keys[]={"firmware_sha256","bsp_sha256","kernel_sha256"};
 for(int n=0;n<(fcc?3:2)&&ok;n++) {const char *want=jstr(j,keys[n]);if(!charge_sha_valid(want)){ok=0;break;}char out[256];
  char *av[]={"sha256sum",(char *)paths[n],NULL};const char *sha=access("/usr/bin/sha256sum",X_OK)==0?"/usr/bin/sha256sum":"/bin/sha256sum";
  ok=CHARGE_RUN(sha,av,NULL,out,sizeof(out))&&!strncmp(out,want,64)&&(out[64]==' '||out[64]=='\t');}
 cJSON_Delete(j);return ok;
}
static int charge_sys_number(const char *path,int low,int high,double *out) {
 char buf[96];if(!CHARGE_READ(path,buf,sizeof(buf),0))return 0;size_t n=strlen(buf);while(n&&isspace((unsigned char)buf[n-1]))buf[--n]=0;
 cJSON *s=cJSON_CreateString(buf);int value,ok=charge_int(s,low,high,&value);cJSON_Delete(s);if(ok)*out=value;return ok;
}
static struct charge_sample charge_read(void) {
 struct charge_sample s={.capacity=-1,.online=-1,.connected=-1,.mode=-1,.status=-1};
 cJSON *b=CHARGE_BUS("zwrt_bsp.battery","list",NULL),*c=CHARGE_BUS("zwrt_bsp.charger","list",NULL);
 charge_int(jget(b,"battery_capacity"),0,100,&s.capacity);charge_int(jget(b,"battery_online"),0,1,&s.online);
 charge_int(jget(c,"charger_connect"),0,1,&s.connected);charge_int(jget(c,"charge_status"),0,255,&s.status);s.mode=charge_mode(jstr(c,"direct_power_supply_mode"));
 int temp;s.temperature_known=charge_int(jget(b,"battery_temperature"),-50,150,&temp);if(s.temperature_known)s.temperature=temp;
 cJSON_Delete(b);cJSON_Delete(c);
 s.current_known=charge_sys_number("/sys/class/power_supply/battery/current_now",-50000000,50000000,&s.current);if(s.current_known)s.current/=1000000.0;
 s.voltage_known=charge_sys_number("/sys/class/power_supply/battery/voltage_now",0,20000000,&s.voltage);if(s.voltage_known)s.voltage/=1000000.0;
 int restore=charge_fcc_restore();if(restore){double enabled,current;
  if(s.mode!=0||!charge_sys_number("/sys/class/qcom-battery/restrict_chg",0,1,&enabled)||!charge_sys_number("/sys/class/qcom-battery/restrict_cur",0,5000000,&current))s.mode=-1;
  else s.mode=enabled==1&&current==0?1:enabled==0&&current==restore?0:-1;
 }
 return s;
}
static int charge_ready(const struct charge_sample *s) {return s->capacity>=0&&s->online==1&&s->connected>=0&&s->mode>=0;}
/* {} means successful libubus transport; every explicit result must also pass.
 * Readback is mandatory. An empty reply alone never proves a state change. */
static int charge_accepted(const cJSON *r) {
 if(!cJSON_IsObject(r)||jget(r,"error"))return 0;int seen=0;cJSON *v;
 cJSON_ArrayForEach(v,r) {
  if(!strcmp(v->string,"error_code")){int code;if(!charge_int(v,0,0,&code))return 0;seen=1;}
  else if(!strcmp(v->string,"result")){if(!((cJSON_IsString(v)&&(!strcmp(v->valuestring,"success")||!strcmp(v->valuestring,"0")))||(cJSON_IsNumber(v)&&v->valuedouble==0)||cJSON_IsTrue(v)))return 0;seen=1;}
 }
 return seen||cJSON_GetArraySize(r)==0;
}
static int charge_sys_write(const char *path,int value) {
 int fd=open(path,O_WRONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 0;
 char b[32];int n=snprintf(b,sizeof(b),"%d\n",value);int ok=write(fd,b,(size_t)n)==n;close(fd);return ok;
}
#ifndef CHARGE_SYS_WRITE
#define CHARGE_SYS_WRITE charge_sys_write
#endif
static int charge_set(int mode) {
 int restore=charge_fcc_restore();if(restore){
  /* Never alter USB current, thermal limits, charging voltage or the ship mode. */
  struct charge_sample before=charge_read();if(before.mode<0)return 0;
  int ok=mode?(CHARGE_SYS_WRITE("/sys/class/qcom-battery/restrict_cur",0)&&CHARGE_SYS_WRITE("/sys/class/qcom-battery/restrict_chg",1)):
    (CHARGE_SYS_WRITE("/sys/class/qcom-battery/restrict_chg",0)&&CHARGE_SYS_WRITE("/sys/class/qcom-battery/restrict_cur",restore));
  struct charge_sample after=charge_read();double online=-1;
  if(ok&&after.mode==mode&&(!mode||(charge_sys_number("/sys/class/power_supply/usb/online",0,1,&online)&&online==1)))return 1;
  /* Failed writes/readback restore the previous verified setting. */
  if(before.mode){CHARGE_SYS_WRITE("/sys/class/qcom-battery/restrict_cur",0);CHARGE_SYS_WRITE("/sys/class/qcom-battery/restrict_chg",1);}
  else {CHARGE_SYS_WRITE("/sys/class/qcom-battery/restrict_chg",0);CHARGE_SYS_WRITE("/sys/class/qcom-battery/restrict_cur",restore);}
  return 0;
 }
 cJSON *a=cJSON_CreateObject();cJSON_AddStringToObject(a,"direct_power_supply_mode",charge_mode_name(mode));
 cJSON *r=CHARGE_BUS("zwrt_bsp.charger","set",a);cJSON_Delete(a);int ok=charge_accepted(r);cJSON_Delete(r);if(!ok)return 0;
 for(int n=0;n<4;n++){r=CHARGE_BUS("zwrt_bsp.charger","list",NULL);ok=charge_mode(jstr(r,"direct_power_supply_mode"))==mode;cJSON_Delete(r);if(ok)return 1;
  if(!CHARGE_FIXTURE){struct timespec d={0,100000000};nanosleep(&d,NULL);}}
 return 0;
}
static cJSON *charge_result(int ok,const char *msg,const struct charge_config *c) {
 cJSON *r=reply(ok,msg);cJSON_AddNumberToObject(r,"limit",c->limit);cJSON_AddBoolToObject(r,"restore_pending",c->restore);return r;
}
/* Journaling prevents retry storms after a rejected/ambiguous setter or a
 * power loss between setter and final config commit. User must re-arm. */
static cJSON *charge_apply(struct charge_config *c,int target) {
 c->pending=target;if(!CHARGE_SAVE(c))return charge_result(0,"充电策略写入失败，未请求充电模式变更",c);
 if(!charge_set(target)){c->fault=1;CHARGE_SAVE(c);return charge_result(0,"充电模式未获接口及回读确认，自动控制已暂停",c);}
 c->last=target;c->pending=-1;c->fault=0;
 if(!CHARGE_SAVE(c)){c->fault=1;c->pending=target;CHARGE_SAVE(c);return charge_result(0,"充电模式已回读，但策略保存失败；请检查并重新选择策略",c);}
 return charge_result(1,"充电模式已回读并保存",c);
}
static int charge_target(const struct charge_config *c,const struct charge_sample *s) {
 if(s->capacity>=c->limit)return 1;if(s->capacity<=c->limit-CHARGE_HYSTERESIS)return 0;return s->mode;
}
static cJSON *charge_tick(void) {
 struct charge_config c;if(!charge_load(&c))return reply(0,"充电策略文件无效，未修改设备");
 if(!c.limit)return charge_result(!c.restore,c.restore?"策略已关闭，原模式恢复尚待处理":"充电策略关闭，保留现有模式",&c);
 if(c.fault||c.pending>=0)return charge_result(0,"上次充电操作未完成，自动控制暂停；请重新选择策略",&c);
 if(!charge_verified())return charge_result(0,"当前固件未通过充电停止/恢复验收，未修改",&c);
 struct charge_sample s=charge_read();if(!charge_ready(&s))return charge_result(0,"电池或充电器状态不完整，未修改",&c);
 /* FCC votes reset at boot. Re-arm only after a proven boot change and
  * a fully recognized default mode; never forgive an interrupted transaction. */
 char boot[40];if(charge_fcc_restore()&&*c.boot&&charge_boot(boot,sizeof(boot))&&strcmp(c.boot,boot)&&s.mode==0){
  c.last=s.mode;snprintf(c.boot,sizeof(c.boot),"%s",boot);
  if(!CHARGE_SAVE(&c))return charge_result(0,"重启后的充电策略恢复记录保存失败",&c);
 }
 if(s.mode!=c.last){c.fault=1;int saved=CHARGE_SAVE(&c);return charge_result(0,saved?"充电模式被外部修改，自动控制已暂停":"检测到外部模式变更，暂停记录保存失败",&c);}
 if(s.connected!=1)return charge_result(1,"未连接充电器，保留现有模式",&c);
 int target=charge_target(&c,&s);if(target==s.mode)return charge_result(1,"充电策略保持当前模式",&c);return charge_apply(&c,target);
}
static cJSON *charge_action(const char *action,const cJSON *args) {
 if(!strcmp(action,"charge.manual")){
  const char*mode=jstr(args,"mode");int target=!strcmp(mode,"direct")?1:!strcmp(mode,"normal")?0:-1;
  if(target<0)return reply(0,"请选择正常充电或直供电");
  if(!charge_verified()||!charge_fcc_restore())return reply(0,"本机直供电尚未通过实测，未修改");
  struct charge_config c;if(!charge_load(&c))return reply(0,"充电策略文件无效，未修改");
  struct charge_sample s=charge_read();if(!charge_ready(&s))return reply(0,"充电状态不完整，未修改");
  if(target&&s.connected!=1)return reply(0,"请先接入充电器，再开启直供电");
  /* Manual operation durably disables the automatic limit first. */
  c=(struct charge_config){0,-1,s.mode,-1,0,0,{0}};
  cJSON*r=charge_apply(&c,target);
  if(cJSON_IsTrue(jget(r,"ok")))cJSON_ReplaceItemInObject(r,"message",cJSON_CreateString(target?"已开启直供电，USB 输入保持在线；自动上限已关闭":"已恢复正常充电；自动上限已关闭"));
  return r;
 }
 if(strcmp(action,"charge.policy"))return NULL;
 const char *v=jstr(args,"limit");int limit=!strcmp(v,"off")?0:!strcmp(v,"80")?80:!strcmp(v,"90")?90:!strcmp(v,"100")?100:-1;
 if(limit<0)return reply(0,"充电上限仅支持关闭、80%、90% 或 100%");
 struct charge_config c;if(!charge_load(&c))return reply(0,"充电策略文件无效，未覆盖原文件或修改设备");
 if(!limit) {
  if(!c.limit&&!c.restore&&!c.fault&&c.pending<0)return charge_result(1,"策略已关闭，保留现有模式",&c);
  /* Disable durably before any restore. A failed restore never re-enables policy. */
  c.limit=0;c.restore=c.original>=0;if(!CHARGE_SAVE(&c))return charge_result(0,"无法保存关闭状态，未修改充电模式",&c);
  if(!c.restore)return charge_result(1,"策略已关闭，保留现有模式",&c);
  if(!charge_verified())return charge_result(0,"策略已关闭；固件未验收，原模式尚未恢复",&c);
  struct charge_sample s=charge_read();if(!charge_ready(&s))return charge_result(0,"策略已关闭；状态不完整，原模式尚未恢复",&c);
  /* Preserve an external/manual change instead of overwriting it. */
  if(s.mode!=c.original&&s.mode!=c.last&&s.mode!=c.pending){c.original=s.mode;}
  if(s.mode!=c.original){if(s.connected!=1)return charge_result(0,"策略已关闭；充电器未连接，原模式尚未恢复",&c);
   cJSON *r=charge_apply(&c,c.original);if(!cJSON_IsTrue(jget(r,"ok")))return r;cJSON_Delete(r);}
  c=(struct charge_config){0,-1,-1,-1,0,0,{0}};if(!CHARGE_SAVE(&c))return charge_result(0,"充电模式已恢复，但关闭状态收尾保存失败",&c);
  return charge_result(1,"策略已关闭，原有或手动充电模式已保留",&c);
 }
 if(!charge_verified())return reply(0,"待接充电器验证停止/恢复，当前不能启用自动策略");
 struct charge_sample s=charge_read();if(!charge_ready(&s))return reply(0,"电池或充电器状态不完整，未启用策略");
 if(c.original<0||(!c.limit&&!c.restore)||(c.pending<0&&s.mode!=c.last))c.original=s.mode;
 c.limit=limit;c.last=s.mode;c.pending=-1;c.fault=0;c.restore=0;
 if(!CHARGE_SAVE(&c))return charge_result(0,"充电策略保存失败，未请求充电模式变更",&c);
 return charge_tick();
}
static void charge_value(cJSON *o,const char *key,int known,double value) {if(known)cJSON_AddNumberToObject(o,key,value);else cJSON_AddNullToObject(o,key);}
static void charge_sections(cJSON *root) {
 struct charge_config c;int loaded=charge_load(&c),verified=charge_verified();struct charge_sample b=charge_read();
 cJSON *d=jget(root,"data");if(!d)d=cJSON_AddObjectToObject(root,"data");cJSON_DeleteItemFromObject(d,"charge");cJSON *out=cJSON_AddObjectToObject(d,"charge");
 charge_value(out,"capacity_percent",b.capacity>=0,b.capacity);charge_value(out,"battery_online",b.online>=0,b.online);charge_value(out,"charger_connected",b.connected>=0,b.connected);
 charge_value(out,"temperature_c",b.temperature_known,b.temperature);charge_value(out,"current_a",b.current_known,b.current);charge_value(out,"voltage_v",b.voltage_known,b.voltage);
 charge_value(out,"charge_status_raw",b.status>=0,b.status);if(b.mode>=0)cJSON_AddStringToObject(out,"direct_power_supply_mode",charge_mode_name(b.mode));else cJSON_AddNullToObject(out,"direct_power_supply_mode");
 charge_value(out,"limit_percent",loaded,c.limit);cJSON_AddNumberToObject(out,"hysteresis_percent",CHARGE_HYSTERESIS);cJSON_AddBoolToObject(out,"verified",verified);cJSON_AddBoolToObject(out,"config_ok",loaded);
 cJSON_AddBoolToObject(out,"paused_on_error",loaded&&(c.fault||c.pending>=0));cJSON_AddBoolToObject(out,"restore_pending",loaded&&c.restore);cJSON_AddNumberToObject(out,"sampled_at",(double)time(NULL));
 const char *status=!loaded?"策略文件损坏，禁止修改":c.restore?"策略关闭，原模式恢复待处理":(c.fault||c.pending>=0)?"上次操作异常，自动控制暂停":!c.limit?"关闭，保留原有模式":!verified?"固件未验收，自动控制暂停":!charge_ready(&b)?"状态不完整，自动控制等待":b.mode!=c.last?"检测到外部模式变更，自动控制暂停":b.connected==0?"等待连接充电器":"策略运行中";
 cJSON_AddStringToObject(out,"status",status);cJSON_AddStringToObject(out,"source","zwrt_bsp battery/charger + battery sysfs");
 cJSON *s=adv_section(root,"battery","电池与充电");char value[128];
 if(b.capacity>=0)snprintf(value,sizeof(value),"%d%%",b.capacity);else snprintf(value,sizeof(value),"未知");item(s,"charge.capacity","电池电量","info",value,NULL,0,NULL);
 item(s,"charge.connected","充电器连接","info",b.connected<0?"未知":b.connected?"已连接":"未连接",NULL,0,NULL);
 int fcc=charge_fcc_restore();cJSON_AddStringToObject(out,"backend",fcc?"fcc-zero":"bsp-pause");
 double input_online=-1,input_current=0,input_voltage=0;
 int input_known=charge_sys_number("/sys/class/power_supply/usb/online",0,1,&input_online);
 charge_value(out,"usb_input_online",input_known,input_online);
 int input_current_known=charge_sys_number("/sys/class/power_supply/usb/current_now",0,50000000,&input_current);
 int input_voltage_known=charge_sys_number("/sys/class/power_supply/usb/voltage_now",0,50000000,&input_voltage);
 charge_value(out,"usb_input_current_a",input_current_known,input_current/1000000.0);
 charge_value(out,"usb_input_voltage_v",input_voltage_known,input_voltage/1000000.0);
 const char*actual=b.mode<0?"未知":!verified?"保留原厂设置 · 待验证":b.mode?(fcc?(input_known&&input_online==1?"直供电 · 外电输入在线":"直供电设置 · 等待外电"):"暂停充电"):"正常充电";
 cJSON *manual=item(s,"charge.manual","充电与直供","choice",actual,"charge.manual",loaded&&verified&&fcc&&charge_ready(&b),"手动选择会关闭自动上限；手动模式本次开机有效，重启后以实际状态为准");
 if(b.connected==1)choice(manual,"直供电 · 保持外电输入","mode","direct");
 choice(manual,"恢复正常充电","mode","normal");cJSON_AddBoolToObject(manual,"confirm",1);
 if(b.temperature_known)snprintf(value,sizeof(value),"%.0f °C",b.temperature);else snprintf(value,sizeof(value),"未知");item(s,"charge.temp","电池温度","info",value,NULL,0,NULL);
 if(b.voltage_known)snprintf(value,sizeof(value),"%.3f V",b.voltage);else snprintf(value,sizeof(value),"未知");item(s,"charge.voltage","电池电压","info",value,NULL,0,NULL);
 if(b.current_known)snprintf(value,sizeof(value),"%+.3f A（驱动符号）",b.current);else snprintf(value,sizeof(value),"未知");item(s,"charge.current","电池电流","info",value,NULL,0,NULL);
 if(!loaded)snprintf(value,sizeof(value),"配置错误");else if(!c.limit)snprintf(value,sizeof(value),"关闭");else snprintf(value,sizeof(value),"%d%% / %d%% 恢复",c.limit,c.limit-CHARGE_HYSTERESIS);
 cJSON *i=item(s,"charge.policy","充电上限","choice",value,"charge.policy",loaded,fcc?"达到上限转直供，低于上限 5 个百分点恢复充电；关闭恢复策略启用前模式":"达到上限暂停，低于上限 5 个百分点恢复；关闭恢复策略启用前的模式");
 choice(i,"关闭，保留原有模式","limit","off");if(verified&&charge_ready(&b)){choice(i,"80%（75% 恢复）","limit","80");choice(i,"90%（85% 恢复）","limit","90");choice(i,"100%（95% 恢复）","limit","100");}
 item(s,"charge.policy_status","策略状态","info",status,NULL,0,NULL);item(s,"charge.capability","固件充电验收","info",verified?"已绑定当前固件":"待接充电器验证，自动上限不可用",NULL,0,NULL);
 /* Both surfaces put the two charging controls before read-only telemetry. */
 cJSON *items=jget(s,"items");cJSON_DetachItemViaPointer(items,i);cJSON_InsertItemInArray(items,0,i);
 cJSON_DetachItemViaPointer(items,manual);cJSON_InsertItemInArray(items,0,manual);
}
#endif
