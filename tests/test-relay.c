#define RELAY_TEST
#include "../panel/panel-relay.c"
#include <assert.h>
static void check(const char*ip,const char*mask,const char*gw,int ok){cJSON*a=cJSON_CreateObject();cJSON_AddStringToObject(a,"ip",ip);cJSON_AddStringToObject(a,"subnet",mask);cJSON_AddStringToObject(a,"router",gw);cJSON_AddStringToObject(a,"lan","192.168.0.1");cJSON_AddStringToObject(a,"lan_mask","255.255.255.0");cJSON*r=lease(a);assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok"))==ok);cJSON_Delete(r);cJSON_Delete(a);}
int main(void){
 check("192.168.50.40","255.255.255.0","192.168.50.1",1);
 check("192.168.0.40","255.255.255.0","192.168.0.1",0);
 check("192.168.1.40","255.255.0.0","192.168.1.1",0);
 check("100.100.1.2","255.255.255.0","100.100.1.1",0);
 check("169.254.1.2","255.255.0.0","169.254.1.1",0);
 check("192.168.50.0","255.255.255.0","192.168.50.1",0);
 check("192.168.50.255","255.255.255.0","192.168.50.1",0);
 check("192.168.50.4","255.255.255.0","192.168.51.1",0);
 check("192.168.50.4;id","255.255.255.0","192.168.50.1",0);
 check("192.168.50.4","255.0.255.0","192.168.50.1",0);
 check("127.0.0.4","255.255.255.0","127.0.0.1",0);
 check("224.1.1.4","255.255.255.0","224.1.1.1",0);
 char b[]="bssid / frequency / signal level / flags / ssid\n00:11:22:33:44:55\t5220\t-35\t[WPA2-PSK-CCMP][ESS]\tTest\\x20WiFi\n00:11:22:33:44:56\t2412\t-40\t[WPA2-EAP-CCMP][ESS]\tCorp\n00:11:22:33:44:57\t5220\t-41\t[WPA3-SAE-CCMP][ESS]\tSAE\n00:11:22:33:44:58\t5220\t-45\t[ESS]\tOpen\n00:11:22:33:44:59\t5220\t-45\t[ESS]\tbad\\x00name\n";
 cJSON*r=parse_scan(b);assert(cJSON_GetArraySize(r)==4);assert(!strcmp(str(cJSON_GetArrayItem(r,0),"ssid"),"Test WiFi"));assert(!strcmp(str(cJSON_GetArrayItem(r,1),"security"),"unsupported"));assert(!strcmp(str(cJSON_GetArrayItem(r,2),"security"),"WPA3"));assert(!strcmp(str(cJSON_GetArrayItem(r,3),"security"),"OPEN"));cJSON_Delete(r);
 assert(!ssid_valid(""));assert(!ssid_valid("bad\nname"));assert(mac_valid("aa:bb:cc:dd:ee:ff"));assert(!mac_valid("aa:bb:cc:dd:ee;ff"));
 puts("PASS: relay scan parsing, personal security types, DHCP subnet/tailnet conflicts, hostile scalars, gateway and mask validation");return 0;
}
