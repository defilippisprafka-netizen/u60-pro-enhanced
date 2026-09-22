#ifndef PANEL_DIAGNOSTICS_H
#define PANEL_DIAGNOSTICS_H
#include <ifaddrs.h>
#include <math.h>
#ifndef DIAG_GET
#define DIAG_GET cc_get
#endif
static cJSON *diag_report(const char *title){cJSON*r=reply(1,"诊断完成");cJSON*p=cJSON_AddObjectToObject(r,"report");cJSON_AddStringToObject(p,"title",title);cJSON_AddArrayToObject(p,"lines");return r;}
static void diag_line(cJSON*r,const char*text){cJSON_AddItemToArray(jget(jget(r,"report"),"lines"),cJSON_CreateString(text));}
static void diag_pair(cJSON*r,const char*label,const char*value){char text[768];snprintf(text,sizeof(text),"%s：%s",label,value&&*value?value:"未知");diag_line(r,text);}
static cJSON *diag_connections(void){
 cJSON *cfg=DIAG_GET("/configs"),*raw=DIAG_GET("/connections");
 if(!cJSON_IsObject(raw)){cJSON_Delete(cfg);cJSON_Delete(raw);return reply(0,"无法读取 Clash 连接，请检查服务状态");}
 cJSON*list=jget(raw,"connections");if(!cJSON_IsArray(list)&&!cJSON_IsNull(list)){cJSON_Delete(cfg);cJSON_Delete(raw);return reply(0,"Clash 连接格式异常，未生成结果");}
 cJSON*r=diag_report("实时分流连接");diag_pair(r,"当前模式",jstr(cfg,"mode"));cJSON_Delete(cfg);char line[768];int count=cJSON_GetArraySize(list),shown=count>40?40:count;
 snprintf(line,sizeof(line),"活跃 %d 条 · 展示前 %d 条",count,shown);diag_line(r,line);diag_line(r,"仅展示核心当前连接；非全网流量审计。");
 if(!count)diag_line(r,"当前无活跃连接，打开网站后刷新查看。");
 for(int n=0;n<shown;n++){cJSON*c=cJSON_GetArrayItem(list,n),*m=jget(c,"metadata");snprintf(line,sizeof(line),"—— 连接 %d ——",n+1);diag_line(r,line);
  diag_pair(r,"设备",jstr(m,"sourceIP"));diag_pair(r,"目标",*jstr(m,"host")?jstr(m,"host"):jstr(m,"destinationIP"));diag_pair(r,"协议",jstr(m,"network"));
  diag_pair(r,"命中规则",jstr(c,"rule"));if(*jstr(c,"rulePayload"))diag_pair(r,"匹配内容",jstr(c,"rulePayload"));
  cJSON*p;int path=0;cJSON_ArrayForEach(p,jget(c,"chains")){if(path++>=8)break;if(cJSON_IsString(p))diag_pair(r,"策略 / 节点",p->valuestring);}
  cJSON*u=jget(c,"upload"),*d=jget(c,"download");if(cJSON_IsNumber(u)&&cJSON_IsNumber(d)&&u->valuedouble>=0&&d->valuedouble>=0){snprintf(line,sizeof(line),"上行 %.1f KiB · 下行 %.1f KiB",u->valuedouble/1024,d->valuedouble/1024);diag_line(r,line);}
 }
 cJSON_Delete(raw);return r;
}
/* Only use the live core's port bound to an address owned by this device. */
static int diag_proxy_url(const cJSON *cfg,char *out,size_t cap){
 cJSON *p=jget(cfg,"mixed-port");if(!cJSON_IsNumber(p)||p->valuedouble==0)p=jget(cfg,"port");
 if(!cJSON_IsNumber(p)||!isfinite(p->valuedouble)||p->valuedouble<1||p->valuedouble>65535||p->valuedouble!=(int)p->valuedouble)return 0;
 const char *host=jstr(cfg,"bind-address");if(!*host||!strcmp(host,"*")||!strcmp(host,"0.0.0.0"))host="127.0.0.1";else if(!strcmp(host,"::"))host="::1";
 struct in_addr v4;struct in6_addr v6;int family=inet_pton(AF_INET,host,&v4)==1?AF_INET:inet_pton(AF_INET6,host,&v6)==1?AF_INET6:0;if(!family)return 0;
 struct ifaddrs *ifs=NULL;int own=0;if(getifaddrs(&ifs))return 0;
 for(struct ifaddrs *i=ifs;i;i=i->ifa_next){if(!i->ifa_addr||i->ifa_addr->sa_family!=family)continue;
  if(family==AF_INET&&!memcmp(&((struct sockaddr_in*)i->ifa_addr)->sin_addr,&v4,sizeof(v4)))own=1;
  if(family==AF_INET6&&!memcmp(&((struct sockaddr_in6*)i->ifa_addr)->sin6_addr,&v6,sizeof(v6)))own=1;
 }freeifaddrs(ifs);if(!own)return 0;
 snprintf(out,cap,family==AF_INET6?"http://[%s]:%d":"http://%s:%d",host,(int)p->valuedouble);return 1;
}
static int diag_real_probe(const char*url,const char*proxy,char*out,size_t cap){
 char*v[]={"curl","--silent","--show-error","--noproxy","","--proxy",(char*)proxy,"--connect-timeout","3","--max-time","6","--output","/dev/null","--write-out","%{http_code} %{ssl_verify_result} %{time_total}",(char*)url,NULL};
 return run_cmd("/usr/bin/curl",v,NULL,out,cap);
}
#ifndef DIAG_PROBE
#define DIAG_PROBE diag_real_probe
#endif
static cJSON *diag_web(void){
 char proxy[160];cJSON*cfg=DIAG_GET("/configs");int ready=diag_proxy_url(cfg,proxy,sizeof(proxy));cJSON_Delete(cfg);if(!ready)return reply(0,"无法确认本机 Clash HTTP 监听地址与端口，未发送请求");
 cJSON*r=diag_report("网站连通性");diag_line(r,"路径：本机 → 现有 Clash HTTP 代理");diag_line(r,"这是应用层测试，不代表所有客户端/协议。");int passed=0;
 const char*hosts[]={"Google","百度"},*urls[]={"https://www.google.com/","https://www.baidu.com/"};
 for(int n=0;n<2;n++){char out[256]={0},line[320];int code=0,tls=-1;double elapsed=-1;int executed=DIAG_PROBE(urls[n],proxy,out,sizeof(out));int parsed=sscanf(out,"%d %d %lf",&code,&tls,&elapsed)==3;
  int ok=executed&&parsed&&tls==0&&code>=200&&code<400;passed+=ok;
  snprintf(line,sizeof(line),"%s：%s",hosts[n],ok?"可访问":"未通过");diag_line(r,line);
  if(parsed){snprintf(line,sizeof(line),"HTTP %d · %.2f 秒",code,elapsed);diag_line(r,line);diag_line(r,tls==0&&executed?"TLS 证书校验通过":"TLS / 连接未完成或未通过");}else diag_line(r,"未取得有效响应；稍后重试。");
 }
 cJSON_ReplaceItemInObject(r,"message",cJSON_CreateString(passed==2?"Google 与百度访问通过":"诊断完成，存在未通过项"));return r;
}
static cJSON*diagnostics_action(const char*action,const cJSON*args){(void)args;if(!strcmp(action,"diag.connections"))return diag_connections();if(!strcmp(action,"diag.web"))return diag_web();return NULL;}
static void diagnostics_sections(cJSON*root){
 cJSON*s=adv_section(root,"diagnostics","网络诊断"),*i=item(s,"diag.connections","实时分流连接","action","查看规则与节点","diag.connections",1,NULL);cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(0));
 i=item(s,"diag.web","Google / 百度测试","action","TLS 与 HTTP 验证","diag.web",1,NULL);cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(0));
 s=adv_section(root,"clash","Clash");i=item(s,"diag.connections","查看实际分流","action","设备 · 规则 · 节点","diag.connections",1,NULL);cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(0));
}
#endif
