# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise real native-module cancellation, including preprocessing and reuse.

The timing ceiling is a regression guard for hangs, not an audio-device latency
promise. The manual server integration check measures the Speech Dispatcher path.
"""
import queue
import time
import unittest
from test_module import Protocol, pcm_from


class InterruptionTests(unittest.TestCase):
    def setUp(self):
        self.client = Protocol()
        self.client.init()
        self.addCleanup(self.client.close)

    def speak(self, text):
        self.client.send('SPEAK\n' + text + '\n.\n')
        self.client.until(b'701 ')

    def stop(self):
        start = time.monotonic()
        self.client.send('STOP\n')
        lines = self.client.until(b'703 ', timeout=1)
        elapsed = time.monotonic() - start
        self.assertLess(elapsed, .5)
        self.assertNotIn(b'702 END\n', lines)
        return lines

    def reference(self):
        self.client.send('SPEAK\nReprise de la lecture.\n.\n')
        return pcm_from(self.client.until(b'702 '))

    def test_rapid_stop_and_restart(self):
        reference = self.reference()
        text = 'Les oiseaux chantent dans les arbres. ' * 500
        latencies = []
        for i in range(60):
            with self.subTest(iteration=i):
                self.client.send(f'SET\nrate={(-100, 0, 100)[i % 3]}\npitch={(-100, 100)[i % 2]}\npitch_range={(0, 40, 100)[i % 3]}\n.\n')
                self.client.until(b'203 ')
                self.speak(text)
                if i % 4:
                    time.sleep((i % 4) * .001)
                start = time.monotonic()
                self.stop()
                latencies.append(time.monotonic() - start)
                self.client.send('SET\nrate=0\npitch=0\npitch_range=0\n.\n')
                self.client.until(b'203 ')
                self.assertEqual(self.reference(), reference)
        # Visible when run directly: includes Python scheduling and pipe transport.
        print(f'\nSTOP, 60 rafales : maximum {max(latencies)*1000:.1f} ms', flush=True)

    def test_stop_during_audio_delivery(self):
        self.speak('Une longue lecture à interrompre. ' * 500)
        self.client.until(b'705 AUDIO')
        self.stop()
        self.assertTrue(self.reference())

    def test_stop_during_xml_or_whitespace_processing(self):
        for text in ['<speak>' + '<span> </span>' * 50000 + '</speak>',
                     ' ' * 900000,
                     '&invalid; ' + '<span> </span>' * 50000]:
            with self.subTest(prefix=text[:30]):
                self.speak(text)
                result = self.stop()
                if not text.startswith('&invalid;'):
                    self.assertFalse(pcm_from(result))
                self.assertTrue(self.reference())

    def test_stop_overrides_pending_pause(self):
        for commands in ['PAUSE\nSTOP\n', 'STOP\nPAUSE\n', 'STOP\nSTOP\n']:
            with self.subTest(commands=commands):
                self.speak('Une longue lecture. ' * 1000 + '<mark name="__spd_1"/>Suite.')
                self.client.send(commands)
                lines = self.client.until(b'703 ', timeout=1)
                self.assertNotIn(b'704 PAUSE\n', lines)
                self.assertNotIn(b'700-__spd_1\n', lines)
                self.assertTrue(self.reference())

    def test_quit_during_synthesis_unwinds_before_freeing(self):
        self.speak('Une longue lecture. ' * 1000)
        self.client.send('QUIT\n')
        self.client.until(b'210 ', timeout=1)
        self.assertEqual(self.client.p.wait(timeout=1), 0)
        self.client.reader.join(timeout=1)
        remaining = []
        while True:
            try:
                remaining.append(self.client.lines.get_nowait())
            except queue.Empty:
                break
        self.assertEqual(remaining, [None])


if __name__ == '__main__':
    unittest.main()
