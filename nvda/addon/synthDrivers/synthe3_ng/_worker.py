# SPDX-License-Identifier: GPL-2.0-or-later
"""Single persistent synthesis worker; NVDA/audio dependencies are injected."""
from collections import deque
from dataclasses import dataclass, field, replace
import threading

from ._native import Cancelled


def percent(value):
    return max(0, min(100, int(value)))


@dataclass(frozen=True)
class Settings:
    rate: int = 50
    pitch: int = 50
    volume: int = 100
    inflection: int = 0

    def engine(self):
        rate = 2 * percent(self.rate) - 100
        if rate <= 0:
            speed = round(4 + rate * 4 / 100)
        else:
            calibrated = rate * 80 / 54 if rate <= 54 else 80 + (rate - 54) * 20 / 46
            speed = round(4 + calibrated * 11 / 100)
        pitch = 2 * percent(self.pitch) - 100
        height = round(5 + pitch * (5 if pitch < 0 else 10) / 100)
        return speed, height, percent(self.inflection), percent(self.volume) - 100


@dataclass
class Job:
    generation: int
    request: int
    events: tuple
    settings: Settings
    indices: dict = field(default_factory=dict)
    serial: int = 0
    audio_started: bool = False


class Worker:
    PACKET_SAMPLES = 256

    def __init__(self, native, player, notify, error):
        self.native, self.player = native, player
        self.notify, self.error = notify, error
        self._cv = threading.Condition()
        self._control = threading.RLock()
        self._queue = deque()
        self._current = None
        self._generation = 0
        self._closing = self._paused = self._stopping = False
        self._thread = threading.Thread(target=self._run, name="Synthé3 synthesis", daemon=True)
        self._thread.start()

    def valid(self, generation):
        with self._cv:
            return not self._closing and generation == self._generation

    def idle(self, generation):
        with self._cv:
            return (not self._closing and generation == self._generation
                    and self._current is None and not self._queue)

    def submit(self, events, settings):
        with self._control, self._cv:
            if self._closing:
                return
            request = self.native.request()  # Created before the work is visible.
            self._queue.append(Job(self._generation, request, tuple(events), settings))
            self._cv.notify_all()

    def cancel(self):
        with self._control:
            with self._cv:
                if self._closing:
                    return
                self._stopping = True
                self._generation += 1
                self._paused = False
                if self._current:
                    self.native.cancel(self._current.request)
                while self._queue:
                    job = self._queue.popleft()
                    self.native.cancel(job.request)
                    self.native.release(job.request)
                self._cv.notify_all()
            try:
                # Never take a lock held across render/feed/idle here.
                self.player.stop()
            finally:
                with self._cv:
                    self._stopping = False
                    self._cv.notify_all()

    def pause(self, enabled):
        with self._control:
            with self._cv:
                if self._closing:
                    return
                self._paused = bool(enabled)
                self._cv.notify_all()
            self.player.pause(bool(enabled))

    def terminate(self):
        with self._control:
            with self._cv:
                if self._closing:
                    return
            self.cancel()
            with self._cv:
                self._closing = True
                self._cv.notify_all()
        self._thread.join(timeout=5)
        if self._thread.is_alive():
            # Worker retains the native/player references until it unwinds.
            raise RuntimeError("Synthé3 worker did not stop within five seconds")

    def _ready(self, job):
        with self._cv:
            while not self._closing and job.generation == self._generation and (self._paused or self._stopping):
                self._cv.wait()
            if self._closing or job.generation != self._generation:
                raise Cancelled()

    def _audio(self, job, operation):
        self._ready(job)
        try:
            operation()
        finally:
            # cancel may stop the device just before an already reserved feed
            # enters native code. Purge again before this worker starts new work.
            if not self.valid(job.generation):
                self.player.stop()
            else:
                with self._cv:
                    paused = self._paused
                if paused:
                    self.player.pause(True)
        self._ready(job)

    def _reached(self, job, serial):
        with self._cv:
            if self._closing or job.generation != self._generation:
                return
            index = job.indices.pop(serial, None)
        if index is not None:
            self.notify(job.generation, index)

    def _schedule_index(self, job, serial):
        self._audio(job, lambda: self.player.feed(
            b"", onDone=lambda: self._reached(job, serial)))

    def _mark(self, job, index):
        self._ready(job)
        with self._cv:
            serial = job.serial
            job.serial += 1
            job.indices[serial] = index
        if job.audio_started:
            self._schedule_index(job, serial)

    def _data(self, job, data):
        if not data:
            return
        if not job.audio_started:
            job.audio_started = True
            # Leading indices need a real stream to fire zero-length callbacks.
            with self._cv:
                pending = tuple(job.indices)
            for serial in pending:
                self._schedule_index(job, serial)
        for start in range(0, len(data), self.PACKET_SAMPLES * 2):
            packet = data[start:start + self.PACKET_SAMPLES * 2]
            self._audio(job, lambda packet=packet: self.player.feed(packet))

    def _speak(self, job):
        settings = job.settings

        def prepared(text, marks):
            self._ready(job)
            if not text:
                for _, index in marks:
                    self._mark(job, index)
                return
            pcm, marks = self.native.render(job.request, text, settings.engine(), marks)
            cursor = 0
            for sample, index in marks:
                boundary = sample * 2
                self._data(job, pcm[cursor:boundary])
                self._mark(job, index)
                cursor = boundary
            self._data(job, pcm[cursor:])

        stream = self.native.stream(job.request, prepared)
        try:
            for kind, value in job.events:
                self._ready(job)
                if kind == "text":
                    stream.append(value)
                elif kind == "index":
                    stream.mark(value)
                elif kind == "spelling":
                    stream.finish()
                    stream.spelling(value)
                elif kind in ("rate", "pitch", "volume"):
                    stream.finish()
                    settings = replace(settings, **{kind: percent(value)})
                elif kind == "break":
                    stream.finish()
                    samples = max(0, int(value)) * 22050 // 1000
                    while samples:
                        n = min(samples, self.PACKET_SAMPLES)
                        self._data(job, b"\0" * (n * 2))
                        samples -= n
                else:
                    raise ValueError(f"Unsupported speech event: {kind}")
            stream.finish()
            if job.audio_started:
                self._audio(job, self.player.idle)
            self._ready(job)
            # Index-only/whitespace sequences have no audio clock. Also drain
            # any end marker not fired by a disappearing audio device.
            with self._cv:
                pending = tuple(job.indices)
            for serial in pending:
                self._reached(job, serial)
        finally:
            stream.close()

    def _run(self):
        try:
            while True:
                with self._cv:
                    while not self._closing and (not self._queue or self._stopping):
                        self._cv.wait()
                    if self._closing:
                        return
                    job = self._current = self._queue.popleft()
                done = False
                try:
                    self._speak(job)
                    done = True
                except Cancelled:
                    # Final barrier before this sole worker can play a new job.
                    try:
                        self.player.stop()
                    except Exception:
                        self.error("Synthé3 audio cancellation failed")
                except Exception:
                    self.error("Synthé3 synthesis failed")
                    try:
                        self.player.stop()
                    except Exception:
                        self.error("Synthé3 audio stop failed")
                    done = True
                finally:
                    with self._cv:
                        self.native.release(job.request)
                        self._current = None
                        drained = not self._queue
                    if done and drained and self.valid(job.generation):
                        self.notify(job.generation, None)
        finally:
            try:
                self.player.close()
            finally:
                self.native.close()
