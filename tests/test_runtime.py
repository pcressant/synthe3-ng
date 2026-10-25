# SPDX-License-Identifier: GPL-2.0-or-later
"""Trace the real module: no children, threads or writable files, including INIT."""
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path
import unittest
from test_module import Protocol, pcm_from


@unittest.skipUnless(shutil.which('strace'), 'strace unavailable')
class RuntimeTests(unittest.TestCase):
    def test_no_processes_threads_or_file_writes(self):
        probe = subprocess.run(['strace', '-o', '/dev/null', '-e', 'none', 'true'], capture_output=True)
        if probe.returncode:
            self.skipTest('ptrace unavailable: ' + probe.stderr.decode().strip())
        with tempfile.TemporaryDirectory() as directory:
            trace = Path(directory) / 'syscalls'
            client = Protocol(prefix=['strace', '-f', '-o', str(trace),
                                      '-e', 'trace=%process,%file'])
            try:
                client.init()
                for _ in range(3):
                    client.send('SPEAK\nBonjour, ceci est un test.\n.\n')
                    self.assertTrue(pcm_from(client.until(b'702 ')))
            finally:
                client.close()
            data = trace.read_text()
            self.assertEqual(data.count('execve('), 1, data)
            self.assertNotRegex(data, r'\b(?:clone3?|fork|vfork)\(')
            self.assertNotRegex(data, r'O_(?:WRONLY|RDWR|CREAT|TRUNC|APPEND)')
            self.assertNotRegex(data, r'\b(?:creat|unlink|mkdir|rename)\(')


@unittest.skipUnless(Path('/proc/self/stat').exists(), 'Linux procfs unavailable')
class IdleTests(unittest.TestCase):
    def test_idle_blocks_without_periodic_wakeups(self):
        client = Protocol()
        try:
            client.init()
            client.send('SPEAK\nBonjour.\n.\n')
            client.until(b'702 ')
            proc = Path('/proc') / str(client.p.pid)

            def counters():
                stat = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
                ticks = int(stat[11]) + int(stat[12])
                switches = sum(int(line.split(':')[1]) for line in
                               (proc / 'status').read_text().splitlines()
                               if line.startswith(('voluntary_ctxt_switches:',
                                                   'nonvoluntary_ctxt_switches:')))
                return ticks, switches

            # Allow the final event flush to settle before measuring idle.
            time.sleep(.02)
            ticks, switches = counters()
            time.sleep(.25)
            after_ticks, after_switches = counters()
            self.assertLessEqual(after_ticks - ticks, 1)
            self.assertLessEqual(after_switches - switches, 2)
        finally:
            client.close()
