#!/usr/bin/env python3
import pathlib,tempfile,subprocess,os,time
R=pathlib.Path(__file__).parents[1]
s=(R/'panel/wifi-relay.sh').read_text();fn=s[s.index('ap_state()'):s.index('allowed()')]
with tempfile.TemporaryDirectory() as td:
 d=pathlib.Path(td);p=d/'hostapd_cli';p.write_text('#!/bin/sh\nif [ "${HANG:-0}" = 1 ];then exec sleep 10;fi\necho state=ENABLED\n');p.chmod(0o755)
 env=dict(os.environ,PATH=td+':'+os.environ['PATH'])
 script=fn+'\nap_state wlan2\n'
 r=subprocess.run(['sh','-c',script],env=env,capture_output=True,text=True,timeout=6);assert r.stdout.strip()=='ENABLED',r
 start=time.monotonic();r=subprocess.run(['sh','-c',script],env=dict(env,HANG='1'),capture_output=True,text=True,timeout=6);assert time.monotonic()-start<5 and not r.stdout.strip()
 assert 'timeout ' not in fn
 print('PASS: AP state and bounded unresponsive control socket without a timeout executable')
