#!/usr/bin/env python3
"""Exercise the real shell watchdog with suspend-aware clock fixtures."""
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class WatchdogTests(unittest.TestCase):
    def run_samples(self, samples):
        source = (ROOT / 'panel/panel-run.sh').read_text()
        prefix = source.split('[ -x "$BIN" ]', 1)[0]
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            (root / 'samples').write_text(json.dumps(samples))
            (root / 'lock').write_text('424242\n')
            (root / 'clock-step.py').write_text(r'''
import json, os, pathlib
p=pathlib.Path(os.environ['TEST_ROOT'])
tick=p/'tick';n=int(tick.read_text()) if tick.exists() else 0
samples=json.loads((p/'samples').read_text());tick.write_text(str(n+1))
if n >= len(samples):
 (p/'lock').unlink(missing_ok=True)
else:
 uptime,hb=samples[n];(p/'uptime').write_text(str(uptime)+'.0 0\n')
 if hb is None:(p/'hb').unlink(missing_ok=True)
 else:(p/'hb').write_text(str(hb)+'\n')
''')
            script = root / 'watchdog.sh'
            script.write_text(prefix + '''
log() { printf '%s\\n' "$*" >> "$ROOT/log"; }
kill() { printf '%s\\n' "$*" >> "$ROOT/killed"; }
sleep() { python3 "$ROOT/clock-step.py"; }
start_watchdog
wait "$WD"
''')
            env = dict(os.environ, TEST_ROOT=td, ROOT=td,
                       PANEL_LOCK=str(root/'lock'), PANEL_HB=str(root/'hb'),
                       PANEL_WD_MARK=str(root/'watchdog'), UPTIME_FILE=str(root/'uptime'))
            result = subprocess.run(['sh',str(script)], env=env, capture_output=True,
                                    text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            killed = (root/'killed').read_text().splitlines() if (root/'killed').exists() else []
            return killed, (root/'watchdog').exists(), int((root/'tick').read_text())

    def test_healthy_ui_after_hours_of_suspend_is_not_killed(self):
        killed,marked,_ = self.run_samples([(16856+n*3,100+(n//2)*5) for n in range(18)])
        self.assertEqual(killed, [], 'healthy heartbeat must not be compared with suspend-inclusive uptime')
        self.assertFalse(marked)

    def test_resume_clock_jump_does_not_kill_healthy_ui(self):
        samples=[(100+n*3+(16756 if n>=4 else 0),100+(n//2)*5) for n in range(18)]
        killed,marked,_=self.run_samples(samples)
        self.assertEqual(killed, [])
        self.assertFalse(marked)

    def test_frozen_heartbeat_still_triggers_recovery(self):
        killed,marked,ticks=self.run_samples([(100+n*3,100) for n in range(20)])
        self.assertEqual(killed, ['424242'])
        self.assertTrue(marked)
        self.assertLessEqual(ticks,12)

    def test_missing_or_invalid_heartbeat_still_triggers_recovery(self):
        for value in [None,'invalid']:
            with self.subTest(value=value):
                killed,marked,ticks=self.run_samples([(100+n*3,value) for n in range(20)])
                self.assertEqual(killed,['424242'])
                self.assertTrue(marked)
                self.assertLessEqual(ticks,12)


if __name__ == '__main__':
    unittest.main()
