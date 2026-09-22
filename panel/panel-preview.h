/* Host-only render/test entry. No device writes; preview data is explicitly simulated. */
#include <assert.h>
static void preview_fixture(struct app *a) {
 memset(a,0,sizeof(*a));a->bat=86;a->temp=38;a->clash_online=1;a->delay_ms=82;
 strcpy(a->mode,"rule");strcpy(a->operator,"中国电信");strcpy(a->node,"示例节点 A01");
 a->quota_ok=1;a->quota_remain_gb=168.4;a->quota_total_gb=200;
 a->npick=4;for(int i=0;i<4;i++)snprintf(a->pick_name[i],80,"%s",(const char *[]){"示例节点 A01","日本 · 东京 02","香港 · 优选 01","DIRECT"}[i]);
 strcpy(a->nat_on,"1");strcpy(a->fw_on,"1");strcpy(a->upnp_on,"0");strcpy(a->saver,"0");strcpy(a->fastboot,"1");a->bl=160;
 strcpy(a->net_select,"WL_AND_5G");strcpy(a->ts_state,"Running");strcpy(a->ts_ip,"100.64.0.2");
 a->ngroups=2;for(int i=0;i<2;i++){struct group *g=&a->groups[i];strcpy(g->name,i?"视频媒体":"默认代理");strcpy(g->now,a->node);g->nall=4;for(int j=0;j<4;j++)strcpy(g->all[j],a->pick_name[j]);}
}
static void assert_hits(struct app *a) {
 for(int i=0;i<a->nhits;i++){assert(a->hits[i].x0>=0 && a->hits[i].y0>=0 && a->hits[i].x1<=W && a->hits[i].y1<=H);assert(a->hits[i].x1>a->hits[i].x0 && a->hits[i].y1>a->hits[i].y0);
 for(int j=0;j<i;j++)assert(!(a->hits[i].x0<a->hits[j].x1 && a->hits[i].x1>a->hits[j].x0 && a->hits[i].y0<a->hits[j].y1 && a->hits[i].y1>a->hits[j].y0));}
}
static int preview_main(const char *dir) {
 struct qpic_ctx d={0};struct app a;char path[1024];
 if(font_load()<0){fprintf(stderr,"Set U60_FONT to a local TTF font\n");return 1;}
 d.bufs[0].pitch=W*2;d.bufs[0].size=W*H*2;d.bufs[0].map=calloc(W*H,2);
 for(int page=0;page<SEC_COUNT+2;page++) {
  preview_fixture(&a);
  if(page){a.page=PAGE_MORE;a.more_sec=page==1||page==2?SEC_MENU:page-2;a.more_off=page==2;
   if(a.more_sec!=SEC_MENU){for(int i=0;i<8;i++)detail_add(&a,(const char *[]){"当前状态","工作模式","实时读数","连接质量","地址分配","自动恢复","上次刷新","功能状态"}[i],(const char *[]){"已连接","自动","—","良好","自动获取","已开启","刚刚","可用"}[i]);}}
  render(&d,&a);assert_hits(&a);
  snprintf(path,sizeof(path),"%s/%02d.ppm",dir,page);FILE *f=fopen(path,"wb");if(!f)return 1;fprintf(f,"P6\n320 480\n255\n");
  for(int i=0;i<W*H;i++){uint16_t p=d.bufs[0].map[i];unsigned char rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,f);}fclose(f);
  if(page>2 && a.more_sec!=SEC_POLICY){handle_hit(&a,HID_SCROLL_DN);assert(a.detail_off==4);handle_hit(&a,HID_SCROLL_UP);assert(a.detail_off==0);}
 }
 preview_fixture(&a);a.page=PAGE_MORE;a.more_sec=SEC_ROUTER;
 handle_hit(&a,HID_FW);assert(a.pending_action==HID_FW);render(&d,&a);assert(a.nhits==2);assert_hits(&a);handle_hit(&a,HID_CANCEL);assert(a.pending_action==0);
 handle_hit(&a,HID_REBOOT);assert(a.pending_action==HID_REBOOT);handle_hit(&a,HID_CANCEL);
 handle_hit(&a,HID_GROUP_DN);assert(a.group_i==1);handle_hit(&a,HID_GROUP_UP);assert(a.group_i==0);
 assert(!strcmp(switch_label(""),"未知"));assert(!strcmp(switch_label("1"),"已开启"));
 free(d.bufs[0].map);puts("PASS: 15 native render states; hit bounds/no overlap; page navigation; confirmation cancel; unknown state");return 0;
}
#include "panel-shell-preview.h"
int main(int argc,char **argv) {const char*theme=getenv("U60_PREVIEW_THEME");if(theme&&panel_theme_id(theme)>=0)sh_theme=panel_theme_id(theme);signal(SIGPIPE,SIG_IGN);if(argc>1 && !strcmp(argv[1],"--http-test")){char b[256];int r=http_do("GET","/test",NULL,b,sizeof(b));printf("%d\n",r);return r<0?2:0;}return shell_preview_main(argc>1?argv[1]:".");}
