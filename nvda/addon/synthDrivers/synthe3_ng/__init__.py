# SPDX-License-Identifier: GPL-2.0-or-later
"""Native Synthé3-ng driver for NVDA; French voice Michel."""
from collections import OrderedDict
from pathlib import Path
import ctypes
import sys

import config
from logHandler import log
import nvwave
import queueHandler
from speech.commands import (
    IndexCommand, CharacterModeCommand, BreakCommand,
    RateCommand, PitchCommand, VolumeCommand,
)
from synthDriverHandler import SynthDriver as BaseSynthDriver, VoiceInfo, synthIndexReached, synthDoneSpeaking

from ._native import Native
from ._worker import Settings, Worker, percent


class SynthDriver(BaseSynthDriver):
    name = "synthe3_ng"
    description = "Synthé3-ng — Michel (français)"
    supportedSettings = (
        BaseSynthDriver.VoiceSetting(), BaseSynthDriver.RateSetting(),
        BaseSynthDriver.PitchSetting(), BaseSynthDriver.InflectionSetting(),
        BaseSynthDriver.VolumeSetting(),
    )
    supportedCommands = {IndexCommand, CharacterModeCommand, BreakCommand,
                         RateCommand, PitchCommand, VolumeCommand}
    supportedNotifications = {synthIndexReached, synthDoneSpeaking}

    @classmethod
    def check(cls):
        return (sys.platform == "win32" and ctypes.sizeof(ctypes.c_void_p) == 8
                and (Path(__file__).parent / "bin" / "synthe3.dll").is_file())

    def __init__(self):
        self._settings = Settings()
        self._terminated = False
        native = Native()
        try:
            player = nvwave.WavePlayer(channels=1, samplesPerSec=22050, bitsPerSample=16,
                                      outputDevice=config.conf["audio"]["outputDevice"])
            # Keep the DSP's own fades and preserve explicit BreakCommand silence.
            player.enableTrimmingLeadingSilence(False)
        except Exception:
            native.close()
            raise
        try:
            self._worker = Worker(native, player, self._notify, log.exception)
        except Exception:
            player.close()
            native.close()
            raise
        try:
            super().__init__()
        except Exception:
            self._worker.terminate()
            raise

    def initSettings(self):
        try:
            super().initSettings()
        except Exception:
            try:
                self._worker.terminate()
            finally:
                self._terminated = True
                self._unregisterConfigSaveAction()
            raise

    def _notify(self, generation, index):
        queueHandler.queueFunction(queueHandler.eventQueue, self._deliver, generation, index)

    def _deliver(self, generation, index):
        if not self._worker.valid(generation):
            return
        if index is None:
            if self._worker.idle(generation):
                synthDoneSpeaking.notify(synth=self)
        else:
            synthIndexReached.notify(synth=self, index=index)

    def speak(self, speechSequence):
        events = []
        for item in speechSequence:
            if isinstance(item, str):
                events.append(("text", item))
            elif isinstance(item, IndexCommand):
                events.append(("index", item.index))
            elif isinstance(item, CharacterModeCommand):
                events.append(("spelling", item.state))
            elif isinstance(item, BreakCommand):
                events.append(("break", item.time))
            elif isinstance(item, (RateCommand, PitchCommand, VolumeCommand)):
                events.append((item.settingName, percent(item.newValue)))
            else:
                log.debugWarning("Synthé3: unsupported speech command %r", item)
        self._worker.submit(events, self._settings)

    def cancel(self):
        self._worker.cancel()

    def pause(self, switch):
        self._worker.pause(switch)

    def terminate(self):
        if self._terminated:
            return
        self._terminated = True
        try:
            super().terminate()
        finally:
            self._worker.terminate()

    def _get_voice(self):
        return "michel"

    def _set_voice(self, value):
        if value != "michel":
            raise ValueError("Synthé3 only provides the French voice Michel")

    def _getAvailableVoices(self):
        return OrderedDict(michel=VoiceInfo("michel", "Michel", "fr"))

    def _get_language(self):
        return "fr"

    def _get_rate(self):
        return self._settings.rate

    def _set_rate(self, value):
        self._set_setting("rate", value)

    def _get_pitch(self):
        return self._settings.pitch

    def _set_pitch(self, value):
        self._set_setting("pitch", value)

    def _get_volume(self):
        return self._settings.volume

    def _set_volume(self, value):
        self._set_setting("volume", value)

    def _get_inflection(self):
        return self._settings.inflection

    def _set_inflection(self, value):
        self._set_setting("inflection", value)

    def _set_setting(self, name, value):
        from dataclasses import replace
        self._settings = replace(self._settings, **{name: percent(value)})
