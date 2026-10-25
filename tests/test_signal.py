# SPDX-License-Identifier: GPL-2.0-or-later
"""PCM fingerprints captured from the pre-port C++ renderer + Python ng DSP.

These are independent reference results, never regenerated from the C port.
Python is only a test dependency.
"""
import array
import math
import os
from unittest.mock import patch
import hashlib
import json
from pathlib import Path
import unittest
from test_module import Protocol, pcm_from


class VoiceCompatibilityTests(unittest.TestCase):
    def test_historical_pcm(self):
        historical = Path(__file__).resolve().parents[1] / 'build/sd_synthe3_ng_historical'
        with patch.dict(os.environ, SYNTHE3_TEST_BINARY=str(historical)):
            client = Protocol()
        try:
            client.init()
            cases = json.loads((Path(__file__).with_name('voice_reference.json')).read_text())
            for case in cases:
                with self.subTest(text=case['text'], rate=case['rate'], pitch=case['pitch']):
                    client.send(f"SET\nrate={case['rate']}\npitch={case['pitch']}\n.\n")
                    client.until(b'203 ')
                    client.send('SPEAK\n' + case['text'] + '\n.\n')
                    pcm = pcm_from(client.until(b'702 '))
                    self.assertEqual(len(pcm), case['bytes'])
                    self.assertEqual(hashlib.sha256(pcm).hexdigest(), case['sha256'])
        finally:
            client.close()

    def test_new_default_pitch_and_gain(self):
        historical = Path(__file__).resolve().parents[1] / 'build/sd_synthe3_ng_historical'
        with patch.dict(os.environ, SYNTHE3_TEST_BINARY=str(historical)):
            baseline = Protocol()
        current = Protocol()
        try:
            baseline.init()
            current.init()
            for client, pitch in [(baseline, -17), (current, 0)]:
                client.send(f'SET\nrate=54\npitch={pitch}\nvolume=0\n.\n')
                client.until(b'203 ')
            signals = []
            for client in (baseline, current):
                client.send('SPEAK\nBonjour, les oiseaux chantent dans les arbres.\n.\n')
                signals.append(array.array('h', pcm_from(client.until(b'702 '))))
            old, new = signals
            self.assertEqual(len(old), len(new))
            self.assertGreater(len(new), 0)
            self.assertLess(max(map(abs, new)), 32767)
            gain = 10 ** (0 / 20)
            for a, b in zip(old, new):
                self.assertLessEqual(abs(b - a * gain), 1.5)
            rms_ratio = math.sqrt(sum(x*x for x in new) / sum(x*x for x in old))
            self.assertAlmostEqual(20 * math.log10(rms_ratio), 0, places=3)
        finally:
            baseline.close()
            current.close()

    def test_embedded_voice_is_unchanged(self):
        import re
        root = Path(__file__).resolve().parents[1]
        header = (root / 'src/voice_data.h').read_text()
        arrays = {name: bytes(map(int, re.findall(r'\d+', data)))
                  for name, data in re.findall(r'char (\w+)\[\] = \{(.*?)\};', header, re.S)}
        seg = (root / 'upstream/Michel.seg').read_bytes()
        n = int.from_bytes(seg[:2], 'little')
        self.assertEqual(arrays['voice_addresses'], seg[4:4+n*2])
        self.assertEqual(arrays['voice_segments'], seg[4+n*2:])
        self.assertEqual(arrays['voice_tables'], (root / 'upstream/Synthe.tab').read_bytes())
