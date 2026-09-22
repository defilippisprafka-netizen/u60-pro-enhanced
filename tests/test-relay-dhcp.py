#!/usr/bin/env python3
"""Execute the actual DHCP callback with fake net commands; no device/network writes."""
import pathlib,tempfile,subprocess,os,json
R=pathlib.Path(__file__).parents[1]
with tempfile.TemporaryDirectory() as td:
 d=pathlib.Path(td);root=d/'root';run=d/'run';bin=d/'bin'
 for p in(root,run,bin,root/'relay-private'):p.mkdir(exist_ok=True)
 (root/'relay-private/enabled').touch();(run/'enabled').touch();(run/'dhcp-generation').write_text('generation-1');(d/'ifindex').write_text('47')
 subprocess.run(['cc','-O1','-Wno-deprecated-declarations','-I',str(R/'panel/vendor'),str(R/'panel/panel-relay.c'),str(R/'panel/vendor/cJSON.c'),'-o',str(root/'panel-relay')],check=True)
 fake='''#!/usr/bin/env python3
import pathlib,os,sys,json
p=pathlib.Path(os.environ['TEST_DIR']);a=sys.argv[1:];name=pathlib.Path(sys.argv[0]).name
if name=='uci':print('255.255.255.0' if a[-1].endswith('netmask') else '192.168.0.1')
elif name=='jsonfilter':
 v=json.load(sys.stdin).get(a[-1][2:]);print(str(v).lower() if isinstance(v,bool) else v)
elif name=='flock':pass
else:
 with (p/'calls').open('a') as f:f.write(name+' '+ ' '.join(a)+'\\n')
 if name=='wifi-relay.sh' and (p/'rules-fail').exists():sys.exit(1)
'''
 for p in [bin/x for x in ['uci','jsonfilter','flock','ip']]+[root/'wifi-relay.sh']:
  p.write_text(fake);p.chmod(0o755)
 s=(R/'panel/wifi-relay-dhcp.sh').read_text().replace('ROOT=/data/u60-panel',f'ROOT={root}').replace('RUN=/tmp/u60-wifi-relay',f'RUN={run}').replace('/sys/class/net/u60sta/ifindex',str(d/'ifindex'))
 script=d/'event.sh';script.write_text(s)
 env=dict(os.environ,PATH=str(bin)+':'+os.environ['PATH'],TEST_DIR=td,interface='u60sta',U60_RELAY_IFINDEX='47',U60_RELAY_GENERATION='generation-1',ip='192.168.50.40',subnet='255.255.255.0',router='192.168.50.1')
 def call(kind='bound',**kw):
  (d/'calls').write_text('');p=subprocess.run(['sh',str(script),kind],env=dict(env,**kw),capture_output=True,text=True,timeout=10);return p,(d/'calls').read_text()
 p,c=call();assert p.returncode==0,(p,c);assert c.index('wifi-relay.sh rules')<c.index('route replace default via');assert (run/'lease-ready').exists()
 for kw in [{'ip':'192.168.0.40','router':'192.168.0.1'},{'ip':'100.100.0.2','router':'100.100.0.1'},{'ip':'1.2.3.4;touch x'},{'U60_RELAY_IFINDEX':'46'},{'interface':'eth0'},{'U60_RELAY_GENERATION':'expired-generation'}]:
  p,c=call(**kw);assert p.returncode!=0;assert 'addr add' not in c and 'route replace' not in c,(kw,c)
 (d/'rules-fail').touch();p,c=call();assert p.returncode!=0 and 'route replace default' not in c
 (d/'rules-fail').unlink();p,c=call('deconfig');assert p.returncode==0 and 'route del default dev u60sta' in c and not (run/'lease-ready').exists()
 (run/'enabled').unlink();p,c=call();assert p.returncode!=0 and not c
 print('PASS: actual DHCP callback rejects stale sessions, LAN/Tailnet overlap, hostile values, and firewall failures; scoped route withdrawal')
