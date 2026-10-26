# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the shared ABI through its offline renderer and independent PCM oracle."""
import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from check_api_pcm import check


class PublicApiTests(unittest.TestCase):
    def test_historical_pcm_through_shared_library(self):
        report = check(ROOT / 'build/render_api')
        for case in report['cases']:
            with self.subTest(text=case['text'], rate=case['rate'], pitch=case['pitch']):
                self.assertTrue(case['match'], case)
