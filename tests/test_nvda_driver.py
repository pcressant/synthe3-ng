# SPDX-License-Identifier: GPL-2.0-or-later
"""Driver entrypoint contract and main-thread notification lifecycle.

NVDA host services are substituted; C synthesis and worker remain real.
"""
from collections import deque
import importlib.util
from pathlib import Path
import sys
import threading
import types
import unittest
from unittest.mock import patch

from test_nvda import Native, Player, ROOT


class Base:
    def __init__(self): pass
    def terminate(self): pass
    def initSettings(self): pass
    def _unregisterConfigSaveAction(self): pass
    @classmethod
    def VoiceSetting(cls): return 'voice'
    @classmethod
    def RateSetting(cls): return 'rate'
    @classmethod
    def PitchSetting(cls): return 'pitch'
    @classmethod
    def InflectionSetting(cls): return 'inflection'
    @classmethod
    def VolumeSetting(cls): return 'volume'


class Index:
    def __init__(self, index): self.index = index
class Character:
    def __init__(self, state): self.state = state
class Break:
    def __init__(self, time): self.time = time
class Rate:
    settingName = 'rate'
    def __init__(self, newValue): self.newValue = newValue
class Pitch(Rate): settingName = 'pitch'
class Volume(Rate): settingName = 'volume'


class DriverTests(unittest.TestCase):
    def setUp(self):
        self.actions = []
        self.callbacks = deque()
        self.changed = threading.Condition()
        self.player = Player()
        self.player.enableTrimmingLeadingSilence = lambda enabled: None
        self.errors = []
        def action(name):
            return type('Action', (), {'notify': staticmethod(lambda **kwargs: self.actions.append((name, kwargs)))})()
        modules = {
            'config': types.SimpleNamespace(conf={'audio': {'outputDevice': 'test-device'}}),
            'logHandler': types.SimpleNamespace(log=types.SimpleNamespace(
                exception=self.errors.append, debugWarning=lambda *_: None)),
            'nvwave': types.SimpleNamespace(WavePlayer=lambda **kwargs: self.player),
            'queueHandler': types.SimpleNamespace(eventQueue=object(), queueFunction=self.enqueue),
            'speech': types.ModuleType('speech'),
            'speech.commands': types.SimpleNamespace(IndexCommand=Index, CharacterModeCommand=Character,
                BreakCommand=Break, RateCommand=Rate, PitchCommand=Pitch, VolumeCommand=Volume),
            'synthDriverHandler': types.SimpleNamespace(SynthDriver=Base,
                VoiceInfo=lambda *args: args, synthIndexReached=action('index'), synthDoneSpeaking=action('done')),
        }
        with patch.dict(sys.modules, modules):
            spec = importlib.util.spec_from_file_location('synthe3_driver_test',
                ROOT / 'nvda/addon/synthDrivers/synthe3_ng/__init__.py',
                submodule_search_locations=[str(ROOT / 'nvda/addon/synthDrivers/synthe3_ng')])
            module = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = module
            spec.loader.exec_module(module)
        native_class = module.Native
        module.Native = lambda: native_class(ROOT / 'build/libsynthe3.so')
        self.driver = module.SynthDriver()
        self.addCleanup(self.driver.terminate)

    def enqueue(self, queue, callback, *args):
        with self.changed:
            self.callbacks.append((callback, args))
            self.changed.notify_all()

    def wait_done(self):
        with self.changed:
            self.assertTrue(self.changed.wait_for(
                lambda: any(args[-1] is None for _, args in self.callbacks), timeout=5))
        self.assertFalse(self.errors)

    def pump(self):
        while self.callbacks:
            callback, args = self.callbacks.popleft()
            callback(*args)

    def test_driver_maps_commands_and_reports_on_main_queue(self):
        self.driver._set_rate(77)
        self.assertEqual(self.driver._get_rate(), 77)
        self.assertEqual(self.driver._get_voice(), 'michel')
        self.assertEqual(self.driver._get_language(), 'fr')
        self.assertIn('michel', self.driver._getAvailableVoices())
        self.driver.speak([Index(1), Pitch(80), Rate(50), Volume(100),
                           Character(True), 'a!', Character(False), Break(20), Index(2)])
        self.wait_done()
        self.assertEqual(self.actions, [])
        self.pump()
        self.assertEqual([name for name, _ in self.actions], ['index', 'index', 'done'])
        self.assertEqual([args['index'] for name, args in self.actions if name == 'index'], [1, 2])
        self.assertTrue(all(args['synth'] is self.driver for _, args in self.actions))
        self.assertEqual(self.driver._get_rate(), 77)

    def test_queued_notifications_are_invalidated_by_cancel(self):
        self.driver.speak(['Bonjour.', Index(1)])
        self.wait_done()
        self.driver.cancel()
        self.pump()
        self.assertEqual(self.actions, [])
        self.driver.speak(['Bonjour.', Index(2)])
        self.wait_done()
        self.pump()
        self.assertEqual([args['index'] for name, args in self.actions if name == 'index'], [2])

    def test_old_done_is_not_delivered_while_new_work_is_pending(self):
        self.driver.speak(['Bonjour.', Index(1)])
        self.wait_done()
        self.driver.pause(True)
        self.driver.speak(['Bonjour.', Index(2)])
        self.pump()
        self.assertNotIn('done', [name for name, _ in self.actions])
        self.driver.pause(False)
        self.wait_done()
        self.pump()
        self.assertEqual([name for name, _ in self.actions].count('done'), 1)

    def test_termination_invalidates_queued_notifications(self):
        self.driver.speak(['Bonjour.', Index(1)])
        self.wait_done()
        self.driver.terminate()
        self.pump()
        self.assertEqual(self.actions, [])

    def test_failed_settings_load_releases_the_native_instance(self):
        with patch.object(Base, 'initSettings', side_effect=RuntimeError('invalid profile')):
            with self.assertRaisesRegex(RuntimeError, 'invalid profile'):
                self.driver.initSettings()
        self.assertFalse(self.driver._worker._thread.is_alive())
        native = Native(ROOT / 'build/libsynthe3.so')
        native.close()
