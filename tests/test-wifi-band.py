#!/usr/bin/env python3
"""Run actual Wi-Fi helper against isolated firmware/AP processes, never a device."""
import json,os,pathlib,subprocess,tempfile
R=pathlib.Path(__file__).resolve().parents[1]
FAKE=r'''#!/usr/bin/env python3
import json,os,pathlib,sys
p=pathlib.Path(os.environ['TEST_DIR'])/'firmware.json';d=json.loads(p.read_text());tool=pathlib.Path(sys.argv[0]).name;a=sys.argv[1:]
def save():p.write_text(json.dumps(d))
if tool=='pgrep':sys.exit(0 if d.get('factory_busy') else 1)
if tool=='uci':
 key=a[-1].split('.')[-2:];key='.'.join(key)
 print(d['config'].get(key,''));sys.exit(0)
if tool=='jsonfilter':
 try:v=json.load(sys.stdin).get(a[-1].split('.')[-1]);print(str(v).lower() if isinstance(v,bool) else v if v is not None else '')
 except Exception:sys.exit(1)
 sys.exit(0)
if tool=='ubus':
 method=a[a.index('call')+2]
 if method=='wifi_set_notify':
  if not d.get('adapter_supported'):print('{"error_code":1}');sys.exit(0)
  assert json.loads(a[-1])=={'wifi_opt':'1'}
  d['writes'].append('resume');d['busy']=2;d['starting']=True;save();print('{}');sys.exit(0)
 if method=='status':
  if d.get('busy',0)>0:d['busy']-=1;save();print(json.dumps({'app_status':'busy','driver_status':'running','pending':0}));sys.exit(0)
  if d.get('starting') and not d.get('start_failure'):
   d['states']={'wlan0':'ENABLED','wlan2':'ENABLED'};d['starting']=False;save()
  print(json.dumps({'app_status':'idle','driver_status':'idle','pending':0}));sys.exit(0)
 assert method=='set';body=json.loads(a[-1]);assert set(body)<=set(['source_module','zte_mbb','main_2g','main_5g','wifi0','wifi1'])
 if d.get('reject'):print('{"error_code":1}');sys.exit(0)
 for section,values in body.items():
  if isinstance(values,dict):
   for k,v in values.items():assert k in ['wifi_onoff','lbd','disabled'];d['config'][section+'.'+k]=v
 on=body['zte_mbb']['wifi_onoff']=='1';d['writes'].append('power-'+str(int(on)));d['busy']=2;d['states']={'wlan0':'MISSING','wlan2':'MISSING'};d['starting']=on;save();print('{}');sys.exit(0)
assert tool=='hostapd';iface=a[a.index('-i')+1];cmd=a[-1]
if cmd=='status':
 state=d['states'].get(iface,'MISSING')
 if d.get('acs',{}).get(iface,0)>0:d['acs'][iface]-=1;state='ACS';save()
 if state=='MISSING':sys.exit(1)
 print('state='+state);sys.exit(0)
assert cmd in ['enable','disable']
if d.get('reject'):print('FAIL');sys.exit(0)
d['writes'].append(iface+'-'+cmd);d['states'][iface]='ENABLED' if cmd=='enable' else 'DISABLED'
if cmd=='enable' and d.get('delay'):d.setdefault('acs',{})[iface]=3
save();print('OK')
'''
with tempfile.TemporaryDirectory() as tmp:
 d=pathlib.Path(tmp)
 for name in ['uci','ubus','jsonfilter','hostapd','pgrep']:(d/name).write_text(FAKE);(d/name).chmod(0o755)
 env=dict(os.environ,ROOT=str(d),UCI=str(d/'uci'),UBUS=str(d/'ubus'),JSONFILTER=str(d/'jsonfilter'),HOSTAPD=str(d/'hostapd'),BAND_PGREP=str(d/'pgrep'),TEST_DIR=str(d),BAND_WAIT_SECONDS='8',BAND_POLL_SECONDS='0.01',LCD_BL=str(d/'brightness'))
 (d/'brightness').write_text('0')
 def initial(power='1',missing=False):
  x={'config':{'zte_mbb.wifi_onoff':power,'zte_mbb.lbd':'0','main_2g.disabled':'0','main_5g.disabled':'0','main_2g.ifname':'wlan0','main_5g.ifname':'wlan2','wifi0.disabled':'0','wifi1.disabled':'0'},'states':{'wlan0':'MISSING' if missing else 'ENABLED','wlan2':'MISSING' if missing else 'ENABLED'},'writes':[]}
  (d/'firmware.json').write_text(json.dumps(x))
  for b in ['2g','5g']:(d/('wifi-'+b+'-policy')).unlink(missing_ok=True)
 def data():return json.loads((d/'firmware.json').read_text())
 def patch(**kw):x=data();x.update(kw);(d/'firmware.json').write_text(json.dumps(x))
 def run(action):
  p=subprocess.run(['sh',str(R/'panel/wifi-band.sh'),action],env=env,capture_output=True,text=True,timeout=20)
  return json.loads(p.stdout) if p.stdout.strip() else None
 initial(missing=True)
 assert run('status')['available'], 'missing AP must remain recoverable from the screen'
 result=run('locked-on');assert result['ok'],(result,data());assert data()['states']['wlan0']=='ENABLED';assert data()['writes']==['power-0','power-1'],data()
 assert run('locked-off')['ok'];assert data()['states']['wlan0']=='DISABLED' and data()['states']['wlan2']=='ENABLED'
 patch(delay=True);assert run('locked-on')['ok'];assert data()['states']['wlan0']=='ENABLED'
 assert run('locked-off-5g')['ok'];assert data()['states']['wlan2']=='DISABLED' and data()['states']['wlan0']=='ENABLED'
 assert run('locked-on-5g')['ok'];assert data()['states']['wlan2']=='ENABLED'
 assert run('locked-off')['ok'];assert run('locked-power-off')['ok'];assert data()['config']['zte_mbb.wifi_onoff']=='0'
 assert run('locked-power-on')['ok'];assert data()['states']=={'wlan0':'DISABLED','wlan2':'ENABLED'}
 assert run('locked-off-5g')['ok'];assert run('locked-power-on')['ok'];assert data()['states']=={'wlan0':'ENABLED','wlan2':'ENABLED'}
 initial(power='0',missing=True);assert run('locked-on-5g')['ok'];assert data()['states']['wlan2']=='ENABLED'
 initial();(d/'wifi-2g-policy').write_text('off\n');patch(busy=3);before=data()['writes'];run('locked-reconcile');assert data()['writes']==before,'reconcile cannot stop an AP during firmware startup'
 patch(busy=0);run('locked-reconcile');assert data()['states']['wlan0']=='DISABLED'
 patch(states={'wlan0':'MISSING','wlan2':'MISSING'});before=data()['writes'];run('locked-reconcile');assert data()['writes']==before,'background must not wake stock sleep'
 initial();patch(reject=True);assert not run('locked-off')['ok'];assert (d/'wifi-2g-policy').read_text().strip()=='on'
 initial(missing=True);patch(start_failure=True);assert not run('locked-power-on')['ok'],'saved on configuration is not a started AP'
 initial(missing=True);assert run('locked-wake-recover')['ok'];assert not data()['writes'],'dark screen cannot recover radio'
 (d/'brightness').write_text('160');initial(power='0',missing=True);assert run('locked-wake-recover')['ok'];assert not data()['writes'],'manual master off wins'
 initial(missing=True);(d/'wifi-2g-policy').write_text('off');(d/'wifi-5g-policy').write_text('off');assert run('locked-wake-recover')['ok'];assert not data()['writes'],'manual bands off win'
 initial(missing=True);(d/'wifi-2g-policy').write_text('off');assert run('locked-wake-recover')['ok'];assert data()['states']=={'wlan0':'DISABLED','wlan2':'ENABLED'}
 initial(missing=True);patch(adapter_supported=True);(d/'wifi-2g-policy').write_text('off')
 assert run('locked-wake-recover')['ok']
 assert data()['writes']==['resume','wlan0-disable'],data()['writes']
 assert data()['config']['zte_mbb.wifi_onoff']=='1','resume must not rewrite the saved master switch'
 initial();patch(acs={'wlan2':3})
 assert run('locked-on-5g')['ok']
 assert not data()['writes'],'an AP still starting must finish without a disruptive OFF/ON cycle'
 initial();(d/'wifi-2g-policy').write_text('off');patch(factory_busy=True)
 run('locked-reconcile');assert not data()['writes'],'idle adapter does not override a running factory start'
 patch(factory_busy=False,states={'wlan0':'ENABLED','wlan2':'MISSING'})
 run('locked-reconcile');assert data()['states']['wlan0']=='DISABLED','a failed other band must not keep an unwanted hotspot broadcasting'
 print('PASS: screen-wake recovery respects darkness, master OFF and independent band preferences')
 print('PASS: missing-AP recovery, both-band isolation, ACS wait, master off/on, policy reapply, both-off recovery, startup race, no sleep wake, rejected writes, real-runtime success gate')
