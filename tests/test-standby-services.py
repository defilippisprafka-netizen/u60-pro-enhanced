#!/usr/bin/env python3
"""Isolated service lifecycle tests; no real device or host process is signalled."""
import json,os,pathlib,subprocess,tempfile,unittest
R=pathlib.Path(__file__).resolve().parents[1]
TOOL='''#!/usr/bin/env python3
import os,pathlib,sys,shutil
r=pathlib.Path(os.environ['U60_STANDBY_TEST_ROOT']);n=pathlib.Path(sys.argv[0]).name
def event(s):
 with (r/'events').open('a') as f:f.write(s+'\\n')
if n=='flock':sys.exit(1 if (r/'busy').exists() else 0)
if n=='panel-standby':sys.exit(0 if (r/'eligible').exists() else 1)
if n=='kill':
 event('term '+sys.argv[-1])
 if not (r/'stuck').exists():shutil.rmtree(r/'proc'/sys.argv[-1])
 sys.exit(0)
if n=='sleep':sys.exit(0)
if n in ('tailscale-mode.sh','network-profile.sh','tailscale-lan.sh'):
 event(n+' '+sys.argv[1]);sys.exit(1 if (r/('fail-'+n)).exists() else 0)
'''
class Standby(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.r=pathlib.Path(self.tmp.name)
  for d in ['bin','proc','tmp/u60-standby','data/u60-panel','data/tailscale/bin','data/u60-clash','sys/class/power_supply/battery']:(self.r/d).mkdir(parents=True,exist_ok=True)
  for n in ['flock','kill','sleep','panel-standby','tailscale-mode.sh','network-profile.sh','tailscale-lan.sh']:
   p=self.r/('bin' if n in ['flock','kill','sleep'] else 'data/u60-panel')/n;p.write_text(TOOL);p.chmod(0o700)
  (self.r/'data/u60-panel/network-profile').write_text('clash\n');(self.r/'eligible').touch()
  (self.r/'proc/uptime').write_text('1000 20\n');(self.r/'sys/class/power_supply/battery/capacity').write_text('88\n')
  self.env=dict(os.environ,U60_STANDBY_TEST_ROOT=str(self.r),PATH=str(self.r/'bin')+':'+os.environ['PATH'])
 def tearDown(self):self.tmp.cleanup()
 def owner(self,pid,name,wrong=False):
  p=self.r/'proc'/str(pid);p.mkdir();exe=self.r/('data/u60-clash/mihomo' if name=='clash' else 'data/tailscale/bin/tailscaled');(p/'exe').symlink_to(exe)
  args=[str(exe),'-f',str(self.r/'data/u60-clash/config.yaml')] if name=='clash' else [str(exe),'--state='+str(self.r/'data/tailscale/tailscaled.state'),'--socket='+str(self.r/'tmp/tailscale/tailscaled.sock')]
  if wrong:args[-1]='--socket=/unrelated'
  (p/'cmdline').write_bytes(b'\0'.join(x.encode() for x in args)+b'\0')
 def run_action(self,a):return subprocess.run(['sh',str(R/'panel/standby-services.sh'),a],env=self.env,capture_output=True,text=True,timeout=8)
 def events(self):return (self.r/'events').read_text().splitlines() if (self.r/'events').exists() else []
 def test_pause_preserves_profile_and_wake_restores_only_original_services(self):
  self.owner(801,'clash');self.owner(802,'tailscale');self.assertEqual(self.run_action('pause').returncode,0)
  self.assertTrue((self.r/'tmp/u60-standby/asleep').exists());self.assertEqual((self.r/'data/u60-panel/network-profile').read_text(),'clash\n')
  self.assertEqual(self.events(),['term 801','term 802'])
  self.assertEqual(self.run_action('resume').returncode,0);self.assertFalse((self.r/'tmp/u60-standby/active').exists())
  self.assertEqual(self.events()[-3:],['tailscale-mode.sh standby-resume','network-profile.sh standby-resume','tailscale-lan.sh reconcile'])
 def test_manually_disabled_service_is_not_started(self):
  self.owner(802,'tailscale');self.assertEqual(self.run_action('pause').returncode,0);self.assertEqual(self.run_action('resume').returncode,0)
  self.assertNotIn('network-profile.sh standby-resume',self.events())
 def test_usb_or_active_radio_recheck_refuses_pause(self):
  self.owner(801,'clash');(self.r/'eligible').unlink();self.assertNotEqual(self.run_action('pause').returncode,0);self.assertEqual(self.events(),[])
 def test_unknown_scope_is_never_signalled(self):
  self.owner(802,'tailscale',wrong=True);self.assertEqual(self.run_action('pause').returncode,0);self.assertEqual(self.events(),[])
 def test_failure_keeps_journal_and_next_restore_does_not_repeat_completed_service(self):
  self.owner(801,'clash');self.owner(802,'tailscale');self.assertEqual(self.run_action('pause').returncode,0)
  failure=self.r/'fail-network-profile.sh';failure.touch();self.assertNotEqual(self.run_action('resume').returncode,0)
  self.assertTrue((self.r/'tmp/u60-standby/active').exists());self.assertFalse((self.r/'tmp/u60-standby/asleep').exists())
  failure.unlink();self.assertEqual(self.run_action('resume').returncode,0);self.assertEqual(self.events().count('tailscale-mode.sh standby-resume'),1)
 def test_busy_lock_does_not_stop_anything(self):
  self.owner(801,'clash');(self.r/'busy').touch();self.assertNotEqual(self.run_action('pause').returncode,0);self.assertEqual(self.events(),[])
 def test_no_sigkill_for_daemon_flushing_state(self):
  self.owner(802,'tailscale');(self.r/'stuck').touch();self.assertNotEqual(self.run_action('pause').returncode,0)
  self.assertTrue((self.r/'proc/802').exists());self.assertEqual(self.events(),['term 802']);self.assertTrue((self.r/'tmp/u60-standby/active').exists())
 def test_corrupt_journal_is_not_sourced_or_executed(self):
  (self.r/'tmp/u60-standby/active').touch();(self.r/'tmp/u60-standby/resume').write_text('$(touch /tmp/not-allowed) 1 clash\n')
  self.assertNotEqual(self.run_action('resume').returncode,0);self.assertEqual(self.events(),[])
if __name__=='__main__':unittest.main()
