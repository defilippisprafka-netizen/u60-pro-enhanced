/* Subscription quota only. URL stays in memory and curl's stdin, never argv. */
static unsigned long long cc_header_value(const char *s,const char *name){char pat[32];snprintf(pat,sizeof(pat),"%s=",name);const char*p=strstr(s,pat);return p?strtoull(p+strlen(pat),NULL,10):0;}
/* Resolve the existing listener, including LAN-only binding; never change it. */
static int cc_quota_proxy(char*out,size_t cap){
 FILE*f=fopen(CC_CONFIG,"r");if(!f)return 0;char line[2048],bind[128]="127.0.0.1";long port=0;
 while(fgets(line,sizeof(line),f)){
  if(!strncmp(line,"mixed-port:",11)){char*end;port=strtol(line+11,&end,10);while(*end==' '||*end=='\r'||*end=='\n')end++;if(*end&&*end!='#')port=0;}
  if(!strncmp(line,"bind-address:",13)){char*v=line+13;while(*v==' ')v++;char q=(*v=='\"'||*v=='\'')?*v++:0;char*end=v;if(q){while(*end&&*end!=q)end++;}else while(*end&&*end!=' '&&*end!='\r'&&*end!='\n'&&*end!='#')end++;size_t n=(size_t)(end-v);if(!n||n>=sizeof(bind)){fclose(f);return 0;}memcpy(bind,v,n);bind[n]=0;}
 }
 fclose(f);if(port<1||port>65535)return 0;
 if(!strcmp(bind,"*")||!strcmp(bind,"0.0.0.0")||!strcmp(bind,"::"))snprintf(bind,sizeof(bind),"127.0.0.1");
 struct in_addr ip4;struct in6_addr ip6;if(inet_pton(AF_INET,bind,&ip4)==1)snprintf(out,cap,"http://%s:%ld",bind,port);else if(inet_pton(AF_INET6,bind,&ip6)==1)snprintf(out,cap,"http://[%s]:%ld",bind,port);else return 0;return 1;
}
static cJSON *cc_quota(void){
 char proxy[180];if(!cc_quota_proxy(proxy,sizeof(proxy)))return cJSON_CreateObject();
 const char*cache="/tmp/u60-panel-quota.json";char*saved=cc_readfile(cache);cJSON*q=saved?cJSON_Parse(saved):NULL;free(saved);
 if(q&&!strcmp(jstr(q,"proxy"),proxy)&&time(NULL)-cc_num(q,"checked")<900&&time(NULL)>=cc_num(q,"checked"))return q;cJSON_Delete(q);q=cJSON_CreateObject();cJSON_AddNumberToObject(q,"checked",time(NULL));cJSON_AddStringToObject(q,"proxy",proxy);
 FILE*f=fopen(CC_CONFIG,"r");char line[2048],url[1536]="";int providers=0;
 if(f){while(fgets(line,sizeof(line),f)){if(!strncmp(line,"proxy-providers:",16)){providers=1;continue;}if(providers&&line[0]!=' '&&line[0]!='\n'&&line[0]!='#')break;if(!providers)continue;char*p=line;while(*p==' ')p++;if(strncmp(p,"url:",4))continue;p+=4;while(*p==' ')p++;char quote=(*p=='\''||*p=='"')?*p++:0;char*e=p+strcspn(p,"\r\n");while(e>p&&e[-1]==' ')e--;if(quote&&e>p&&e[-1]==quote)e--;if((size_t)(e-p)>=sizeof(url))break;memcpy(url,p,(size_t)(e-p));url[e-p]=0;break;}fclose(f);}
 if(!strncmp(url,"https://",8)||!strncmp(url,"http://",7)){
  char cfg[3200];size_t n=0;n+=(size_t)snprintf(cfg,sizeof(cfg),"url = \"");for(const char*p=url;*p&&n+4<sizeof(cfg);p++){if(*p=='"'||*p=='\\')cfg[n++]='\\';if((unsigned char)*p<32){n=0;break;}cfg[n++]=*p;}if(n){snprintf(cfg+n,sizeof(cfg)-n,"\"\n");char headers[16384];char*argv[]={"curl","--silent","--head","--max-time","4","--proxy",proxy,"--config","-",NULL};
   if(run_cmd("/usr/bin/curl",argv,cfg,headers,sizeof(headers))){char*p=strcasestr(headers,"subscription-userinfo:");if(p){char*e=strpbrk(p,"\r\n");if(e)*e=0;unsigned long long total=cc_header_value(p,"total"),used=cc_header_value(p,"upload")+cc_header_value(p,"download");if(total){cJSON_AddNumberToObject(q,"total",(double)total);cJSON_AddNumberToObject(q,"remaining",(double)(used>total?0:total-used));cJSON_AddNumberToObject(q,"expire",(double)cc_header_value(p,"expire"));}}}memset(headers,0,sizeof(headers));}memset(cfg,0,sizeof(cfg));
 }memset(url,0,sizeof(url));char*out=cJSON_PrintUnformatted(q);if(out){cc_atomic(cache,out);free(out);}return q;
}
