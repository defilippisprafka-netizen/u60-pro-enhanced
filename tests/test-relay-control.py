#!/usr/bin/env python3
import pathlib,subprocess,tempfile,json
R=pathlib.Path(__file__).parents[1]
with tempfile.TemporaryDirectory() as td:
 d=pathlib.Path(td);binary=d/'control'
 subprocess.run(['cc','-O1','-Wno-deprecated-declarations','-I',str(R/'panel/vendor'),str(R/'panel/panel-control.c'),str(R/'panel/vendor/cJSON.c'),'-lm','-o',str(binary)],check=True)
 # Synthetic public fixture; never read a captured device snapshot.
 data={'zwrt_wlan.status':{'app_status':'idle','driver_status':'idle','pending':0},
       'wifi.zte_mbb':{'wifi_onoff':'1','lbd':'0'},
       'wifi.main_2g':{'ssid':'Demo','disabled':'0'},
       'wifi.main_5g':{'ssid':'Demo_5G','disabled':'0'},
       'usb.role.status':{'ok':True,'requested':'LAN','state':'NO_ADAPTER','badge':''}}
 data['relay.status']={'ok':True,'enabled':False,'active':False,'saved':False,'state':'OFF','frequency':0}
 data['wifi_runtime']={'ok':True,'enabled':False,'enabled_5g':True,'available':True,'active':True,'power_configured':True,'state_2g':'DISABLED','state_5g':'ENABLED'}
 data['relay.scan']={'ok':True,'networks':[{'ssid':'Test WiFi','bssid':'aa:bb:cc:dd:ee:ff','security':'WPA2','signal':-40,'frequency':5220},{'ssid':'Test WiFi','bssid':'aa:bb:cc:dd:ee:fd','security':'WPA2','signal':-80,'frequency':5180},{'ssid':'Enterprise','bssid':'aa:bb:cc:dd:ee:fe','security':'unsupported','frequency':2412}]}
 f=d/'fixture.json'
 def call(action,args={}):
  f.write_text(json.dumps(data));p=subprocess.run([str(binary),'--fixture',str(f)],input=json.dumps({'action':action,'args':args}),text=True,capture_output=True,check=True);return json.loads(p.stdout)
 r=call('wifi.relay.scan');assert len(r['picker']['choices'])==1;args=r['picker']['choices'][0]['args'];assert args['bssid']=='aa:bb:cc:dd:ee:ff' and args['frequency']==5220;r=call('wifi.relay.select',args)
 assert r['picker']['action']=='wifi.relay.connect' and r['picker']['fields'][0]['kind']=='password' and r['picker']['fields'][0]['value']==''
 assert not call('wifi.relay.select',dict(args,security='unsupported'))['ok']
 data['relay.scan']['networks'].reverse();r=call('wifi.relay.scan');assert len(r['picker']['choices'])==1 and r['picker']['choices'][0]['args']['bssid']=='aa:bb:cc:dd:ee:ff'
 data['relay.scan']['networks'].append({'ssid':'Test WiFi','bssid':'aa:bb:cc:dd:ee:fc','security':'WPA2','signal':-30,'frequency':2412})
 r=call('wifi.relay.scan');assert len(r['picker']['choices'])==2 and r['picker']['choices'][0]['label'].endswith('2.4G') and r['picker']['choices'][1]['label'].endswith('5G')
 r=call('state');s=next(x for x in r['sections']if x['id']=='wifi');assert s['items'][0]['id']=='relay' and s['items'][1]['id']=='relay-scan';assert s['items'][0]['type']=='info'
 data['relay.status'].update(enabled=True,active=True,saved=True,state='CONNECTED',frequency=2437);r=call('state');s=next(x for x in r['sections']if x['id']=='wifi');assert s['items'][0]['action']=='wifi.relay.off';assert s['items'][1]['id']=='relay-scan' and s['items'][1]['enabled'];assert call('wifi.relay.scan')['ok'];assert r['data']['wifi_status']=='2.4G 上游中继'
 assert 'password' not in json.dumps(r['data']['wifi_relay'])
 assert call('wifi.ap',{'section':'main_2g','enabled':True})['ok']
 assert not call('usb.role',{'role':'AUTO'})['ok']
 assert not next(x for x in next(z for z in r['sections']if z['id']=='usb')['items']if x['id']=='role')['enabled']
 print('PASS: relay scan-picker-password flow, unsupported networks excluded, active/off entry, home status and no secret readback')
