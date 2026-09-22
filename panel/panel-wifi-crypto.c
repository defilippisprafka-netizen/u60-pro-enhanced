#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include "cJSON.h"
/* Original B28 libztecrypto ABI verified against zte_topsw_wlan.
 * Secrets travel only through stdin/stdout pipes to panel-control, never argv.
 * Library derives keys in memory from existing device material; this helper
 * does not create or save credentials. Refuse firmware crypto debug logging. */
static void wipe(void *p,size_t n){volatile unsigned char *v=p;while(n--)*v++=0;}
static void b64(const unsigned char *p,size_t n,char *out){const char *a="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";size_t k=0;for(size_t i=0;i<n;i+=3){unsigned v=(unsigned)p[i]<<16;if(i+1<n)v|=(unsigned)p[i+1]<<8;if(i+2<n)v|=p[i+2];out[k++]=a[v>>18];out[k++]=a[(v>>12)&63];out[k++]=i+1<n?a[(v>>6)&63]:'=';out[k++]=i+2<n?a[v&63]:'=';}out[k]=0;}
static const char *str(cJSON *o,const char *k){cJSON *v=cJSON_GetObjectItemCaseSensitive(o,k);return cJSON_IsString(v)?v->valuestring:"";}
int main(void){char input[4096],encoded[256]={0},cipher[2048]={0},plain[2048]={0};size_t n=fread(input,1,sizeof(input)-1,stdin);input[n]=0;cJSON *req=cJSON_Parse(input),*res=cJSON_CreateObject();cJSON_AddBoolToObject(res,"ok",0);const char *action=str(req,"action"),*password=str(req,"password"),*key=str(req,"key");int test=!strcmp(action,"selftest"),type=0,ok=0;void *lib=NULL;
 if(access("/cache/ztecrypto_log_switch",F_OK)==0)goto done;
 if(test)password="U60-public-selftest-2026";else if(strcmp(action,"encode")&&strcmp(action,"verify"))goto done;
 size_t len=strlen(password);if(len<8||len>63)goto done;for(size_t i=0;i<len;i++)if((unsigned char)password[i]<32||(unsigned char)password[i]>126)goto done;
 cJSON *tv=cJSON_GetObjectItemCaseSensitive(req,"type");if(tv){if(!cJSON_IsNumber(tv)||(tv->valuedouble!=0&&tv->valuedouble!=1))goto done;type=tv->valueint;}
 /* Suppress firmware printf/debug chatter even on error. */
 fflush(stdout);int saved=dup(STDOUT_FILENO),nullfd=open("/dev/null",O_WRONLY);if(saved<0||nullfd<0)goto done;dup2(nullfd,STDOUT_FILENO);dup2(nullfd,STDERR_FILENO);close(nullfd);
 lib=dlopen("/usr/lib/libztecrypto.so",RTLD_NOW|RTLD_LOCAL);if(lib){typedef int (*init_fn)(void);typedef int (*crypt_fn)(int,const char*,int,char*,int*);init_fn init=(init_fn)dlsym(lib,"MBB_cipher_init");crypt_fn enc=(crypt_fn)dlsym(lib,"MBB_encryption"),dec=(crypt_fn)dlsym(lib,"MBB_decryption");
  if(init&&enc&&dec){int initialized=init();if(initialized==0||initialized==-1){b64((const unsigned char*)password,len,encoded);int outlen=sizeof(cipher),plainlen=sizeof(plain);if(test||!strcmp(action,"encode")){if(enc(type,encoded,(int)strlen(encoded)+1,cipher,&outlen)==0&&outlen>0&&outlen<(int)sizeof(cipher))key=cipher;else key="";}
   if(*key&&strlen(key)<sizeof(cipher)&&dec(type,key,(int)strlen(key),plain,&plainlen)==0&&plainlen>0&&plainlen<(int)sizeof(plain)){plain[plainlen]=0;ok=!strcmp(plain,encoded);}
  }}dlclose(lib);lib=NULL;}
 fflush(stdout);dup2(saved,STDOUT_FILENO);close(saved);if(ok){cJSON_ReplaceItemInObject(res,"ok",cJSON_CreateBool(1));if(!test&&!strcmp(action,"encode"))cJSON_AddStringToObject(res,"key",cipher);}
done:if(lib)dlclose(lib);char *out=cJSON_PrintUnformatted(res);puts(out?out:"{\"ok\":false}");if(out){wipe(out,strlen(out));free(out);}wipe(input,sizeof(input));wipe(encoded,sizeof(encoded));wipe(cipher,sizeof(cipher));wipe(plain,sizeof(plain));cJSON_Delete(req);cJSON_Delete(res);return 0;
}
