# SPDX-License-Identifier: GPL-2.0-or-later
"""NVDA worker with the real C library and a deterministic audio device surrogate."""
import importlib
import json
import logging
from pathlib import Path
import sys
import threading
import time
import types
import unittest

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'nvda/addon/synthDrivers/synthe3_ng'
# Load the NVDA-independent modules without importing the NVDA driver entrypoint.
package = types.ModuleType('synthe3_test')
package.__path__ = [str(PACKAGE)]
sys.modules['synthe3_test'] = package
Native = importlib.import_module('synthe3_test._native').Native
Cancelled = importlib.import_module('synthe3_test._native').Cancelled
worker_module = importlib.import_module('synthe3_test._worker')
Worker, Settings = worker_module.Worker, worker_module.Settings


class Player:
    def __init__(self):
        self.lock = threading.RLock()
        self.buffer = []
        self.played = bytearray()
        self.stops = 0
        self.closed = False
        self.paused = False
        self.feed_entered = threading.Event()
        self.idle_entered = threading.Event()
        self.hold_idle = False
        self.play = threading.Event()
        self.resumed = threading.Event()
        self.resumed.set()
        self.late_feed = False
        self.allow_feed = threading.Event()

    def feed(self, data, onDone=None):
        if data and self.late_feed:
            self.late_feed = False
            self.feed_entered.set()
            if not self.allow_feed.wait(3):
                raise RuntimeError('Test did not release late feed')
        with self.lock:
            self.buffer.append((data, onDone))
        if data:
            self.feed_entered.set()

    def idle(self):
        self.idle_entered.set()
        if self.hold_idle and not self.play.wait(3):
            raise RuntimeError('Test did not release playback')
        if not self.resumed.wait(3):
            raise RuntimeError('Test did not resume playback')
        with self.lock:
            buffered, self.buffer = self.buffer, []
        for data, callback in buffered:
            self.played.extend(data)
            if callback:
                callback()

    def stop(self):
        with self.lock:
            self.stops += 1
            self.buffer.clear()
        self.play.set()
        self.resumed.set()

    def pause(self, enabled):
        self.paused = enabled
        if enabled:
            self.resumed.clear()
        else:
            self.resumed.set()

    def close(self):
        self.stop()
        self.closed = True


class NvdaTests(unittest.TestCase):
    def setUp(self):
        self.native = Native(ROOT / 'build/libsynthe3.so')
        self.player = Player()
        self.notifications = []
        self.errors = []
        self.changed = threading.Condition()
        self.worker = Worker(self.native, self.player, self.notify, self.error)
        self.addCleanup(self.worker.terminate)

    def notify(self, generation, index):
        with self.changed:
            self.notifications.append((generation, index))
            self.changed.notify_all()

    def error(self, message):
        self.errors.append(message)
        logging.exception(message)

    def done(self, count=1):
        with self.changed:
            self.assertTrue(self.changed.wait_for(
                lambda: sum(index is None for _, index in self.notifications) >= count, timeout=5))
        self.assertFalse(self.errors)

    def test_shared_preparation_keeps_pcm_across_unicode_and_marks(self):
        events = [('index', 0), ('text', 'Très bien, cœur'), ('index', 1),
                  ('index', 2), ('text', ' et sœur — oui !'), ('index', 3)]
        self.worker.submit(events, Settings())
        self.done()
        marked = bytes(self.player.played)
        self.assertEqual([i for _, i in self.notifications], [0, 1, 2, 3, None])
        self.worker.submit([('text', 'Très bien, cœur et sœur — oui !')], Settings())
        self.done(2)
        self.assertEqual(marked, bytes(self.player.played[len(marked):]))

    def test_indices_and_done_follow_playback(self):
        self.player.hold_idle = True
        self.worker.submit([('index', 1), ('text', 'Bonjour.'), ('index', 2)], Settings())
        self.assertTrue(self.player.idle_entered.wait(3))
        self.assertEqual(self.notifications, [])
        self.player.play.set()
        self.done()
        self.assertEqual([i for _, i in self.notifications], [1, 2, None])

    def test_cancel_discards_queued_speech_and_late_feed(self):
        self.player.late_feed = True
        self.worker.submit([('text', 'Ancien message.'), ('index', 11)], Settings())
        self.assertTrue(self.player.feed_entered.wait(3))
        self.worker.submit([('text', 'Message en attente.'), ('index', 12)], Settings())
        start = time.monotonic()
        self.worker.cancel()  # Must return while the worker is blocked in feed.
        self.assertLess(time.monotonic() - start, .5)
        self.worker.submit([('text', 'Bonjour.'), ('index', 13)], Settings())
        self.player.allow_feed.set()
        self.done()
        self.assertEqual([i for _, i in self.notifications], [13, None])
        self.assertGreaterEqual(self.player.stops, 2)
        request = self.native.request()
        try:
            expected, _ = self.native.render(request, b'Bonjour.', Settings().engine(), [])
        finally:
            self.native.release(request)
        self.assertEqual(bytes(self.player.played), expected)
        self.assertFalse(self.worker.valid(0))

    def test_pause_before_synthesis_then_resume(self):
        self.worker.pause(True)
        self.worker.submit([('text', 'Bonjour.'), ('index', 2)], Settings())
        self.assertFalse(self.player.feed_entered.wait(.05))
        self.assertEqual(self.notifications, [])
        self.worker.pause(False)
        self.done()
        self.assertGreater(len(self.player.played), 0)

    def test_cancel_while_paused_and_restart(self):
        self.worker.pause(True)
        self.worker.submit([('text', 'Ancien.'), ('index', 1)], Settings())
        self.worker.cancel()
        self.worker.submit([('text', 'Nouveau.'), ('index', 2)], Settings())
        self.done()
        self.assertEqual([i for _, i in self.notifications], [2, None])

    def test_empty_and_whitespace_indices_do_not_need_audio(self):
        self.worker.submit([('index', 1), ('text', '  '), ('index', 2)], Settings())
        self.done()
        self.assertEqual([i for _, i in self.notifications], [1, 2, None])
        self.assertEqual(self.player.played, b'')

    def test_break_is_silence_and_volume_zero_is_silence(self):
        self.worker.submit([('text', 'Bonjour.'), ('break', 100), ('index', 1)], Settings(volume=0))
        self.done()
        request = self.native.request()
        try:
            pcm, _ = self.native.render(request, b'Bonjour.', Settings(volume=0).engine(), [])
        finally:
            self.native.release(request)
        self.assertEqual(bytes(self.player.played), pcm + b'\0' * 4410)

    def test_prosody_changes_are_local_to_the_sequence(self):
        self.worker.submit([('pitch', 80), ('rate', 77), ('text', 'Bonjour.'),
                            ('volume', 0), ('text', 'Bonjour.')], Settings())
        self.done()
        modified = bytes(self.player.played)
        self.worker.submit([('text', 'Bonjour.')], Settings())
        self.done(2)
        request = self.native.request()
        try:
            expected, _ = self.native.render(request, b'Bonjour.', Settings().engine(), [])
        finally:
            self.native.release(request)
        self.assertNotEqual(modified, expected)
        self.assertEqual(bytes(self.player.played[len(modified):]), expected)

    def test_spelling_uses_common_character_names(self):
        self.worker.submit([('spelling', True), ('text', 'a!'), ('index', 1)], Settings())
        self.done()
        request = self.native.request()
        try:
            expected, _ = self.native.render(request, b"a, point d'exclamation", Settings().engine(), [])
        finally:
            self.native.release(request)
        self.assertEqual(bytes(self.player.played), expected)

    def test_long_text_and_boundary_marks_do_not_change_audio(self):
        text = 'Bonjour, cœur et sœur. ' * 30
        self.worker.submit([('text', text[:240]), ('index', 1), ('text', text[240:])], Settings(inflection=20))
        self.done()
        marked = bytes(self.player.played)
        self.worker.submit([('text', text)], Settings(inflection=20))
        self.done(2)
        self.assertEqual(marked, bytes(self.player.played[len(marked):]))

    def test_termination_wakes_paused_worker_and_reopens_native(self):
        self.worker.pause(True)
        self.worker.submit([('text', 'Bonjour.')], Settings())
        self.worker.terminate()
        self.assertFalse(self.worker._thread.is_alive())
        self.assertTrue(self.player.closed)
        self.assertFalse(self.worker.valid(0))
        native = Native(ROOT / 'build/libsynthe3.so')
        native.close()

    def test_cancelled_token_is_not_reset_by_text_stream(self):
        request = self.native.request()
        self.native.cancel(request)
        stream = self.native.stream(request, lambda *_: self.fail('Cancelled stream emitted data'))
        try:
            with self.assertRaises(Cancelled):
                stream.append('Bonjour.')
            with self.assertRaises(Cancelled):
                stream.finish()
        finally:
            stream.close()
            self.native.release(request)

    def test_pause_during_playback_preserves_audio_and_position(self):
        self.player.hold_idle = True
        self.worker.submit([('text', 'Bonjour.'), ('index', 1)], Settings())
        self.assertTrue(self.player.idle_entered.wait(3))
        self.worker.pause(True)
        self.player.play.set()
        with self.changed:
            self.assertFalse(self.changed.wait_for(lambda: bool(self.notifications), timeout=.05))
        self.worker.pause(False)
        self.done()
        self.assertEqual([index for _, index in self.notifications], [1, None])
        request = self.native.request()
        try:
            expected, _ = self.native.render(request, b'Bonjour.', Settings().engine(), [])
        finally:
            self.native.release(request)
        self.assertEqual(bytes(self.player.played), expected)

    def test_thirty_cancel_restart_cycles_do_not_resurrect_old_jobs(self):
        for cycle in range(30):
            self.worker.pause(True)
            self.worker.submit([('text', 'Ancien.'), ('index', cycle * 2)], Settings())
            self.worker.cancel()
            self.worker.submit([('text', 'Bonjour.'), ('index', cycle * 2 + 1)], Settings())
            self.done(cycle + 1)
        self.assertEqual([index for _, index in self.notifications if index is not None],
                         list(range(1, 60, 2)))
