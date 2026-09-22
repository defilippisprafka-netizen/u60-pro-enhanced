/* Screen orchestration only; credentials travel through stdin, never argv. */
static int relay_enabled(void){return fixture?cJSON_IsTrue(jget(jget(fixture,"relay.status"),"enabled")):access("/data/u60-panel/relay-private/enabled",F_OK)==0;}
static cJSON *relay_run(const char*command,const cJSON*args){
 if(fixture){if(!strcmp(command,"status"))return mock("relay.status");if(!strcmp(command,"scan"))return mock("relay.scan");fixture_write_count++;return reply(!cJSON_IsTrue(jget(fixture,"reject_writes")),"测试中继请求");}
 char out[32768];char*body=args?cJSON_PrintUnformatted(args):NULL;
 int binary=!strcmp(command,"connect")||!strcmp(command,"scan");const char*path=binary?"/data/u60-panel/panel-relay":"/data/u60-panel/wifi-relay.sh";char*v[]={(char*)path,(char*)command,NULL};int ok=run_cmd(path,v,body,out,sizeof(out));if(body){memset(body,0,strlen(body));free(body);}
 if(binary||!strcmp(command,"status")){cJSON*r=cJSON_Parse(out);if(ok&&cJSON_IsObject(r))return r;cJSON_Delete(r);return reply(0,"中继操作未确认，请查看当前上游状态");}
 return reply(ok,ok?(!strcmp(command,"off")?"中继已断开，恢复原出口；已保存的网络仍保留":"正在连接已保存的上游"):"操作未完成，请确认USB为LAN、5G热点开启且访客热点关闭且已保存网络");
}
static cJSON *relay_action(const char*action,const cJSON*args){
 if(!strcmp(action,"wifi.relay.off"))return relay_run("off",NULL);
 if(!strcmp(action,"wifi.relay.on"))return relay_run("on",NULL);
 if(!strcmp(action,"wifi.relay.connect"))return relay_run("connect",args);
 if(!strcmp(action,"wifi.relay.select")){
  const char*ssid=jstr(args,"ssid"),*security=jstr(args,"security"),*bssid=jstr(args,"bssid");
  const cJSON*fv=jget(args,"frequency");int freq=cJSON_IsNumber(fv)?fv->valueint:0;
  if(freq<2412||freq>5825||!*ssid||strlen(ssid)>32||strlen(bssid)!=17||(strcmp(security,"WPA2")&&strcmp(security,"WPA3")&&strcmp(security,"OPEN")))return reply(0,"该网络暂不支持，请选择个人Wi-Fi");
  cJSON*r=reply(1,"在屏幕输入上游密码"),*s=cJSON_CreateObject();cJSON_AddArrayToObject(s,"items");
  cJSON*i=item(s,"relay-connect",ssid,"form","输入密码","wifi.relay.connect",1,"连接并在U60保存上游凭据，重启自动重连；断线可能回退蜂窝。支持双频热点；同频热点信道会跟随上游，IPv6互联网暂停。");
  cJSON_AddStringToObject(jget(i,"args"),"ssid",ssid);cJSON_AddStringToObject(jget(i,"args"),"security",security);cJSON_AddStringToObject(jget(i,"args"),"bssid",bssid);cJSON_AddNumberToObject(jget(i,"args"),"frequency",freq);
  if(strcmp(security,"OPEN"))field(i,"password","上游 Wi-Fi 密码","","password",1);else field(i,"password","开放网络，无需密码","","password",0);
  cJSON_AddItemToObject(r,"picker",cJSON_DetachItemFromArray(jget(s,"items"),0));cJSON_Delete(s);return r;
 }
 if(!strcmp(action,"wifi.relay.scan")){
  cJSON*r=relay_run("scan",NULL);if(!r||!cJSON_IsTrue(jget(r,"ok")))return r?r:reply(0,"扫描不可用");
  cJSON*out=reply(1,"请选择上游Wi-Fi"),*picker=cJSON_AddObjectToObject(out,"picker"),*choices=cJSON_AddArrayToObject(picker,"choices");
  cJSON_AddStringToObject(picker,"label","选择上游 Wi-Fi");cJSON_AddStringToObject(picker,"type","choice");cJSON_AddStringToObject(picker,"action","wifi.relay.select");cJSON_AddBoolToObject(picker,"enabled",1);cJSON_AddBoolToObject(picker,"confirm",0);
  cJSON*n;cJSON_ArrayForEach(n,jget(r,"networks")){
   if(strcmp(jstr(n,"security"),"unsupported")==0)continue;
   int f=jget(n,"frequency")?jget(n,"frequency")->valueint:0;
   if(!((f>=2412&&f<=2472&&(f-2412)%5==0)||f==5180||f==5200||f==5220||f==5240||f==5745||f==5765||f==5785||f==5805||f==5825))continue;
   char label[128];snprintf(label,sizeof(label),"%s · %s",jstr(n,"ssid"),jget(n,"frequency")&&jget(n,"frequency")->valueint<3000?"2.4G":"5G");
   double signal=jget(n,"signal")?jget(n,"signal")->valuedouble:-150;
   int duplicate=-1,at=0;cJSON*old;
   cJSON_ArrayForEach(old,choices){if(!strcmp(jstr(old,"label"),label)&&!strcmp(jstr(jget(old,"args"),"security"),jstr(n,"security"))){duplicate=at;break;}at++;}
   if(duplicate>=0){if(jget(old,"_signal")->valuedouble>=signal)continue;cJSON_DeleteItemFromArray(choices,duplicate);}
   cJSON*c=cJSON_CreateObject();cJSON_AddStringToObject(c,"label",label);cJSON_AddNumberToObject(c,"_signal",signal);
   char desc[64];snprintf(desc,sizeof(desc),"%s · %.0f dBm",jstr(n,"security"),signal);cJSON_AddStringToObject(c,"description",desc);
   cJSON*args=cJSON_AddObjectToObject(c,"args");for(int k=0;k<3;k++){const char*key=(const char*[]){"ssid","bssid","security"}[k];cJSON_AddStringToObject(args,key,jstr(n,key));}
   cJSON_AddNumberToObject(args,"frequency",jget(n,"frequency")->valueint);
   at=0;cJSON_ArrayForEach(old,choices){if(jget(old,"_signal")->valuedouble<signal)break;at++;}cJSON_InsertItemInArray(choices,at,c);
  }
  cJSON*choice;cJSON_ArrayForEach(choice,choices)cJSON_DeleteItemFromObject(choice,"_signal");
  cJSON_Delete(r);if(!cJSON_GetArraySize(choices)){cJSON_Delete(out);return reply(0,"未找到支持的网络，请靠近上游后重试");}return out;
 }
 return reply(0,"未知中继操作");
}
static void relay_items(cJSON*s,cJSON*data){
 if(fixture&&!jget(fixture,"relay.status"))return;
 cJSON*r=relay_run("status",NULL);int known=cJSON_IsTrue(jget(r,"ok")),enabled=cJSON_IsTrue(jget(r,"enabled")),active=cJSON_IsTrue(jget(r,"active")),saved=cJSON_IsTrue(jget(r,"saved"));
 const char*state=jstr(r,"state"),*text=!known?"不可用":active?"Wi-Fi 上游":!enabled?"关闭":!strcmp(state,"CONFLICT")?"网段冲突 · 原出口":!strcmp(state,"POLICY")?"热点/USB冲突 · 原出口":!strcmp(state,"ERROR")?"故障 · 原出口":"未连通 · 原出口";
 cJSON*i=item(s,"relay","Wi-Fi 中继",enabled||saved?"action":"info",text,enabled?"wifi.relay.off":saved?"wifi.relay.on":"wifi.relay.scan",known,"中继可保留双频热点；掉线回退原出口，可能消耗蜂窝流量。开机自动重连已保存网络。");cJSON_ReplaceItemInObject(i,"confirm",cJSON_CreateBool(enabled||saved));
 item(s,"relay-scan",saved?"连接 / 更换上游 Wi-Fi":"连接上游 Wi-Fi","action","扫描附近网络","wifi.relay.scan",known,enabled?"扫描时保留当前中继；确认新网络后切换，失败尝试恢复原网络":"请开启5G热点、关闭访客热点，USB设为LAN");
 if(active){int freq=jget(r,"frequency")?jget(r,"frequency")->valueint:0;const char*label=freq>0&&freq<3000?"2.4G 上游中继":freq>=5000?"5G 上游中继":"Wi-Fi 上游中继";cJSON_ReplaceItemInObject(data,"wifi_status",cJSON_CreateString(label));cJSON_ReplaceItemInObject(i,"value",cJSON_CreateString(label));}
 cJSON_AddItemToObject(data,"wifi_relay",r?r:cJSON_CreateObject());
 // Front-load the relay controls without moving the existing hotspot controls apart.
 cJSON*items=jget(s,"items");if(enabled){cJSON*it;cJSON_ArrayForEach(it,items){const char*action=jstr(it,"action");if(!strncmp(action,"wifi.",5)&&strncmp(action,"wifi.relay.",11)&&strcmp(action,"wifi.power")&&strcmp(action,"wifi.sleep")&&strcmp(action,"wifi.show_password")&&!( !strcmp(action,"wifi.ap")&&!strcmp(jstr(jget(it,"args"),"section"),"main_2g"))){cJSON_ReplaceItemInObject(it,"enabled",cJSON_CreateBool(0));cJSON_ReplaceItemInObject(it,"reason",cJSON_CreateString("中继期间允许切换2.4G；其他热点设置请先断开中继"));}}}
 i=cJSON_DetachItemViaPointer(items,i);cJSON_InsertItemInArray(items,0,i);
 {int n=cJSON_GetArraySize(items);cJSON*scan=cJSON_DetachItemFromArray(items,n-1);cJSON_InsertItemInArray(items,1,scan);}
}
