#ifndef PANEL_LCD_NOTIFY_H
#define PANEL_LCD_NOTIFY_H
/* Mirror stock LCD events without changing the radio or proxy configuration.
 * One bounded child preserves off/on ordering; failure retries the latest state.
 * Repeating off events would reset the stock idle timer, so do not poll-send. */
#ifndef PANEL_LCD_UBUS
#define PANEL_LCD_UBUS "/bin/ubus"
#endif
#ifndef PANEL_LCD_TIMEOUT_MS
#define PANEL_LCD_TIMEOUT_MS 2500
#endif
#ifndef PANEL_LCD_RETRY_MS
#define PANEL_LCD_RETRY_MS 1000
#endif
struct panel_lcd_notice {pid_t pid;int desired,sent,applied;long started,retry_at;};
static void panel_lcd_notice_init(struct panel_lcd_notice*n,int on){memset(n,0,sizeof(*n));n->desired=!!on;n->applied=-1;}
static void panel_lcd_notice_request(struct panel_lcd_notice*n,int on){n->desired=!!on;n->retry_at=0;}
static void panel_lcd_notice_close(struct panel_lcd_notice*n){if(n->pid>0){kill(n->pid,SIGKILL);while(waitpid(n->pid,NULL,0)<0&&errno==EINTR){}}n->pid=0;}
/* Returns 1 after acknowledgement, -1 on failure, 0 while pending/idle. */
static int panel_lcd_notice_poll(struct panel_lcd_notice*n,long now){
 int result=0;
 if(n->pid){int status=0;pid_t p=waitpid(n->pid,&status,WNOHANG);
  if(p==n->pid){n->pid=0;if(WIFEXITED(status)&&WEXITSTATUS(status)==0){n->applied=n->sent;result=1;}else{n->applied=-1;n->retry_at=now+PANEL_LCD_RETRY_MS;result=-1;}}
  else if((p<0&&errno!=EINTR)||now-n->started>PANEL_LCD_TIMEOUT_MS){panel_lcd_notice_close(n);n->applied=-1;n->retry_at=now+PANEL_LCD_RETRY_MS;result=-1;}
 }
 if(!n->pid&&n->desired!=n->applied&&now>=n->retry_at){
  n->sent=n->desired;pid_t p=fork();
  if(p==0){int fd=open("/dev/null",O_RDWR);if(fd>=0){dup2(fd,0);dup2(fd,1);dup2(fd,2);}for(int i=3;i<1024;i++)close(i);
   execl(PANEL_LCD_UBUS,"ubus","-t","2","send","zwrt_deviceui_event.lcdstatus",n->sent?"{\"lcd_status\":\"on\"}":"{\"lcd_status\":\"off\"}",(char*)NULL);_exit(127);}
  if(p<0){n->retry_at=now+PANEL_LCD_RETRY_MS;return -1;}n->pid=p;n->started=now;
 }
 return result;
}
#endif
