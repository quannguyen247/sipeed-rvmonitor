# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Nguyen Dong Quan
"""Host checks of current rendering and PID code; no board required."""
from pathlib import Path
import json,os,shlex,subprocess,tempfile,unittest
from vt import Screen

ROOT=Path(__file__).resolve().parent.parent

class HostSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='rvmonitor-test-')
        cls.work=Path(cls.tmp.name)
        source=(ROOT/'src/rvmonitor.c').read_text()
        # Compile the actual current pure functions, excluding the RISC-V rdtime
        # sampler and privileged hardware/lifecycle paths. Anchor changes fail.
        snapshot=source[source.index('struct snapshot {'):source.index('static uint64_t now_ns')]
        pure=source[source.index('static double percent('):source.index('static int compare_u64(')]
        (cls.work/'monitor_render.h').write_text(snapshot+pure)
        compiler=shlex.split(os.environ.get('TEST_CC','cc'))
        flags=['-std=gnu11','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),'-I'+str(cls.work)]
        for name in ['render_probe','pid_oracle']:
            subprocess.run([*compiler,*flags,str(ROOT/'tests'/(name+'.c')),'-o',str(cls.work/name)],check=True)

    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()

    def probe(self,*args):
        return subprocess.check_output([str(self.work/'render_probe'),*args])

    def test_current_render_erases_states_and_settings(self):
        # The probe writes to a pipe; emulate the TTY's normal ONLCR processing.
        screen=Screen();screen.feed(self.probe().replace(b'\n',b'\r\n'))
        self.assertEqual(screen.pending,b'')
        self.assertEqual(len(screen.frames),3)
        expected=[['Running']*3,['Idle']*3,['N/A']*3]
        for frame,states in zip(screen.frames,expected):
            rows=[next(line for line in frame if line.startswith(name))
                  for name in ['CPU0','CPU1','NPU']]
            for line,state in zip(rows,states):
                self.assertEqual(line[66:],state)
                self.assertEqual((line[24],line[45]),('[',']'))
            self.assertNotRegex('\n'.join(frame),r'Idleve|Sleepng|CPU2')
        self.assertIn('59.1 C','\n'.join(screen.frames[0]))
        self.assertIn('RAM USED','\n'.join(screen.frames[0]))
        self.assertIn('Sampling settings','\n'.join(screen.frames[0]))
        self.assertFalse(any('settings (this session)' in line or '1-6:' in line
                             for frame in screen.frames[1:] for line in frame))

    def test_current_json_nulls_and_canonical_states(self):
        rows=[json.loads(line) for line in self.probe('json').splitlines()]
        for row,expected in zip(rows,['Running','Idle','N/A']):
            self.assertEqual([row[k] for k in ['CPU0_state','CPU1_state','NPU_state']],[expected]*3)
            self.assertEqual(row['sample_interval_ms'],1000)
        self.assertTrue(all(rows[-1][k] is None for k in ['C0_percent','C1_percent','NPU_command_busy_percent']))

    def test_counter_reset_invalid_window_and_busy_math(self):
        self.assertEqual(self.probe('percent').decode().split(),['50.0','50.0','-1.0','-1.0','-1.0'])

    def test_integer_pid_matches_recorded_oracles(self):
        output=subprocess.check_output([str(self.work/'pid_oracle')],text=True)
        self.assertEqual(output.splitlines(),['1000 902335789','5000 2655560521'])

if __name__=='__main__':unittest.main()
