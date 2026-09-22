#ifndef PANEL_STANDBY_CONTROL_H
#define PANEL_STANDBY_CONTROL_H
#ifndef STANDBY_MODE_FILE
#define STANDBY_MODE_FILE "/data/u60-panel/standby-mode"
#endif
static const char *standby_mode_read(void) {
 char b[32]={0};FILE*f=fopen(STANDBY_MODE_FILE,"r");if(!f)return "normal";
 int ok=fgets(b,sizeof(b),f)!=NULL;fclose(f);b[strcspn(b,"\r\n")]=0;
 return ok&&!strcmp(b,"deep")?"deep":"normal";
}
static int standby_mode_save(const char *mode) {
 if(!strcmp(standby_mode_read(),mode))return 1;
 char p[512];snprintf(p,sizeof(p),"%s.XXXXXX",STANDBY_MODE_FILE);int fd=mkstemp(p);if(fd<0)return 0;
 size_t n=strlen(mode);int ok=!fchmod(fd,0600)&&write(fd,mode,n)==(ssize_t)n&&write(fd,"\n",1)==1&&!fsync(fd);
 if(close(fd))ok=0;if(ok&&rename(p,STANDBY_MODE_FILE))ok=0;if(!ok)unlink(p);
 return ok&&!strcmp(standby_mode_read(),mode);
}
static void standby_sections(cJSON *root) {
 const char*mode=standby_mode_read();char out[512]={0};char*v[]={"panel-standby","status",NULL};
 int available=access("/data/u60-panel/panel-standby",X_OK)==0;
 cJSON*status=available&&run_cmd("/data/u60-panel/panel-standby",v,NULL,out,sizeof(out))?cJSON_Parse(out):NULL;
 cJSON*s=adv_section(root,"battery","电池与充电");
 cJSON*i=item(s,"power.standby","待机省电","choice",!strcmp(mode,"deep")?"深度省电":"服务常驻","power.standby",available,
 "无连接并进入原厂休眠后，深度省电会暂停 Clash / Tailscale；亮屏恢复原状态，休眠时无法远程访问");
 choice(i,"深度省电","mode","deep");choice(i,"服务常驻","mode","normal");
 const char*p=jstr(status,"phase");const char*label=!strcmp(p,"asleep")?"服务已暂停":!strcmp(p,"entering")?"正在进入省电":!strcmp(p,"waking")?"正在恢复服务":!strcmp(p,"error")?"恢复未完成，自动重试":!strcmp(p,"awake")?"正常运行":"未知";
 item(s,"power.standby_state","待机状态","info",label,NULL,0,NULL);cJSON_Delete(status);
}
static cJSON *standby_action(const char *action,const cJSON *args) {
 if(strcmp(action,"power.standby"))return NULL;const char*mode=jstr(args,"mode");
 if(strcmp(mode,"deep")&&strcmp(mode,"normal"))return reply(0,"请选择深度省电或服务常驻");
 if(!standby_mode_save(mode))return reply(0,"省电设置未保存，原设置保留");
 return reply(1,!strcmp(mode,"deep")?"已开启深度省电，空闲时跟随原厂休眠":"已选择服务常驻，已暂停的服务将自动恢复");
}
#endif
