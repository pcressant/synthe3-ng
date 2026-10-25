# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the actual renderer and binary Speech Dispatcher module protocol."""
import array
import importlib.util
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src'))


class Protocol:
    def __init__(self, config=None, prefix=None):
        self.p = subprocess.Popen((prefix or []) + [os.environ.get('SYNTHE3_TEST_BINARY', str(ROOT / 'build/sd_synthe3_ng'))] +
                                  ([str(config)] if config else []), stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.lines = queue.Queue()
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self):
        for line in self.p.stdout:
            self.lines.put(line)
        self.lines.put(None)

    def send(self, text):
        self.p.stdin.write(text.encode())
        self.p.stdin.flush()

    def until(self, prefix, timeout=10):
        end = time.monotonic() + timeout
        result = []
        while True:
            line = self.lines.get(timeout=max(0.01, end-time.monotonic()))
            if line is None:
                raise AssertionError('Unexpected EOF: ' + self.p.stderr.read().decode())
            result.append(line)
            if line.startswith(prefix):
                return result

    def init(self):
        self.send('INIT\n')
        self.until(b'299 ')
        self.send('AUDIO\naudio_output_method=server\n.\n')
        self.until(b'203 ')

    def close(self):
        if self.p.poll() is None:
            self.send('QUIT\n')
            self.p.wait(timeout=5)
        else:
            self.p.wait(timeout=5)
        self.reader.join(timeout=1)
        self.p.stdin.close()
        self.p.stdout.close()
        self.p.stderr.close()


def pcm_from(lines):
    pcm = bytearray()
    for line in lines:
        if not line.startswith(b'705-AUDIO\0'):
            continue
        data = line[len(b'705-AUDIO\0'):-1]
        i = 0
        while i < len(data):
            byte = data[i]
            if byte == 0x7d:
                i += 1
                byte = data[i] ^ 0x20
            pcm.append(byte)
            i += 1
    return pcm


class ModuleTests(unittest.TestCase):
    def setUp(self):
        self.client = Protocol()
        self.addCleanup(self.client.close)
        self.client.init()

    def say(self, text):
        self.client.send('SPEAK\n' + text + '\n.\n')
        return self.client.until(b'702 ')

    def test_voice_and_real_audio(self):
        self.client.send('LIST VOICES\n')
        voices = self.client.until(b'200 ')
        self.assertIn('200-Synthé3-ng\tfr\tnone\n'.encode(), voices)
        result = self.say('À l’école, les élèves écoutent Synthé trois.')
        self.assertIn(b'200 OK SPEAKING\n', result)
        self.assertIn(b'701 BEGIN\n', result)
        self.assertIn(b'705-sample_rate=22050\n', result)
        pcm = pcm_from(result)
        self.assertGreater(len(pcm), 10000)
        self.assertGreater(max(array.array('h', pcm)), 100)

    def test_volume_silence_and_mark(self):
        self.client.send('SET\nvolume=-100\n.\n')
        self.client.until(b'203 OK SETTINGS')
        result = self.say('<speak>Bonjour.<mark name="position-1"/>Au revoir.</speak>')
        self.assertIn(b'700-position-1\n', result)
        pcm = pcm_from(result)
        self.assertGreater(len(pcm), 10000)
        self.assertFalse(any(pcm))

    def test_stop_pause_and_reuse(self):
        for command, event in [('STOP', b'703 '), ('PAUSE', b'704 ')]:
            self.client.send('SPEAK\n' + ('Une longue phrase française. ' * 1000) + '<mark name="__spd_stop"/>' + '\n.\n')
            self.client.until(b'701 ')
            start = time.monotonic()
            self.client.send(command + '\n')
            self.client.until(event)
            self.assertLess(time.monotonic()-start, 2)
            self.assertTrue(pcm_from(self.say('La parole reprend.')))

    def test_bad_setting_does_not_poison_next_message(self):
        self.client.send('SET\nrate=101\n.\n')
        self.client.until(b'303 ')
        self.assertTrue(pcm_from(self.say('Bonjour.')))

    def test_multiline_and_dot_stuffing(self):
        self.assertTrue(pcm_from(self.say('Bonjour.\n..\nAu revoir.')))

    def test_rate_changes_duration(self):
        text = 'Bonjour, ceci est une phrase de comparaison.'
        self.client.send('SET\nrate=-70\n.\n')
        self.client.until(b'203 OK SETTINGS')
        slow = len(pcm_from(self.say(text)))
        self.client.send('SET\nrate=70\n.\n')
        self.client.until(b'203 OK SETTINGS')
        fast = len(pcm_from(self.say(text)))
        self.assertGreater(slow, fast * 1.5)

    def test_pause_at_orca_mark(self):
        self.client.send('SPEAK\n' + ('Une longue phrase. ' * 200) + '<mark name="__spd_1"/>Suite.\n.\n')
        self.client.until(b'701 ')
        self.client.send('PAUSE\n')
        lines = self.client.until(b'704 ')
        self.assertIn(b'700-__spd_1\n', lines)
        self.assertTrue(pcm_from(self.say('Reprise.')))

    def test_settings_snapshot_and_special_keys(self):
        for command, value in [('CHAR', '&'), ('KEY', 'Return'), ('SPEAK', 'cœur et sœur — très bien !')]:
            self.client.send(command + '\n' + value + '\n.\n')
            self.assertTrue(pcm_from(self.client.until(b'702 ')))

    def test_punctuation_spelling_and_entities(self):
        for setting in ['punctuation_mode=all', 'spelling_mode=on']:
            self.client.send('SET\n' + setting + '\n.\n')
            self.client.until(b'203 ')
            self.assertTrue(pcm_from(self.say('<speak>A &amp; B.</speak>')))

    def test_repeated_messages_are_identical(self):
        first = pcm_from(self.say('Bonjour, les oiseaux chantent.'))
        for _ in range(8):
            self.assertEqual(first, pcm_from(self.say('Bonjour, les oiseaux chantent.')))

    def test_numbers_with_symbol_preprocessor_spaces(self):
        # Speech Dispatcher SymbolsPreproc inserts a space before a retained
        # decimal comma. The engine must not ingest that comma as a digit.
        self.client.send('SET\nrate=54\n.\n')
        self.client.until(b'203 ')
        for spaced, reference in [('5 ,58', '5,58'), ('3 ,15', '3,15'),
                                  ('3 ,14', '3,14'), ('5 ?32', '5?32'),
                                  ('5 , 58', '5, 58'), ('5  pommes', '5 pommes'),
                                  ('123 456', '123456')]:
            with self.subTest(text=spaced):
                self.assertEqual(pcm_from(self.say(spaced)), pcm_from(self.say(reference)))
        preprocessed = '<speak>5 ,58 5 32 3 ,15, 3 ,14</speak>'
        reference = pcm_from(self.say('5,58 5 32 3,15, 3,14'))
        self.assertEqual(pcm_from(self.say(preprocessed)), reference)
        marked = '<speak>5 <mark name="decimal"/>,58 5 32 3 ,15, 3 ,14</speak>'
        result = self.say(marked)
        self.assertEqual(pcm_from(result), reference)
        self.assertIn(b'700-decimal\n', result)

    def test_modulation_threshold_progression_and_reset(self):
        text = 'Demain, nous partons ensemble. Tu viens avec nous ?'
        reference = pcm_from(self.say(text))
        for value in (-100, -20, 0):
            self.client.send(f'SET\npitch_range={value}\n.\n')
            self.client.until(b'203 ')
            self.assertEqual(pcm_from(self.say(text)), reference)
        outputs = []
        for value in (1, 20, 40, 100):
            self.client.send(f'SET\npitch_range={value}\n.\n')
            self.client.until(b'203 ')
            pcm = pcm_from(self.say(text))
            self.assertNotEqual(pcm, reference)
            self.assertEqual(pcm, pcm_from(self.say(text)))
            self.assertLess(abs(len(pcm) - len(reference)), len(reference) * .03)
            outputs.append(pcm)
        self.assertEqual(len(set(map(bytes, outputs))), 4)
        self.client.send('SET\npitch_range=101\n.\n')
        self.client.until(b'303 ')
        self.client.send('SET\npitch_range=0\n.\n')
        self.client.until(b'203 ')
        self.assertEqual(pcm_from(self.say(text)), reference)

    def test_modulated_long_ssml_preserves_audio_and_marks(self):
        self.client.send('SET\nrate=54\npitch_range=40\n.\n')
        self.client.until(b'203 ')
        text = 'Bonjour les amis, nous partons demain. Tu viens aussi ? ' * 12
        reference = pcm_from(self.say(text))
        words = text.split(' ')
        ssml = '<speak>' + ' '.join(f'<mark name="m{i}"/>{word}'
                                   for i, word in enumerate(words)) + '</speak>'
        result = self.say(ssml)
        self.assertEqual(pcm_from(result), reference)
        marks = [line for line in result if line.startswith(b'700-')]
        self.assertEqual(marks, [f'700-m{i}\n'.encode() for i in range(len(words))])

    def test_orca_word_marks_preserve_continuous_audio(self):
        import re
        for text in ['Bonjour les amis nous partons ensemble demain matin.',
                     'À l’école, les élèves écoutent une histoire. ' * 20]:
            with self.subTest(text=text[:50]):
                self.client.send('SET\nrate=40\n.\n')
                self.client.until(b'203 ')
                reference = pcm_from(self.say(text))
                marks = []
                parts = []
                end = 0
                for word in re.finditer(r'\S+', text):
                    name = f'{word.start()}:{word.end()}'
                    marks.append(name)
                    parts.extend([text[end:word.start()], f'<mark name="{name}"/>', word.group()])
                    end = word.end()
                result = self.say('<speak>' + ''.join(parts) + text[end:] + '</speak>')
                self.assertEqual(pcm_from(result), reference)
                actual = [line.decode().strip()[4:] for line in result if line.startswith(b'700-')]
                self.assertEqual(actual, marks)
                # Bookmarks must be interleaved with audio, not all emitted
                # before synthesis or deferred until the end of the message.
                samples = 0
                positions = []
                for line in result:
                    if line.startswith(b'705-num_samples='):
                        packet = int(line.split(b'=')[1])
                        self.assertLessEqual(packet, 1024)
                        samples += packet
                    elif line.startswith(b'700-'):
                        positions.append(samples)
                self.assertEqual(positions[0], 0)
                self.assertGreater(positions[len(positions) // 2], 0)
                self.assertLess(positions[-1], samples)
                self.assertEqual(positions, sorted(positions))

    def test_ssml_nodes_and_adjacent_marks_do_not_split_audio(self):
        reference = pcm_from(self.say('Bonjour les amis & les oiseaux.'))
        result = self.say('<speak><mark name="a"/><mark name="b"/>'
                          'Bonjour <emphasis>les amis</emphasis> &amp; les oiseaux.'
                          '<mark name="end"/></speak>')
        self.assertEqual(pcm_from(result), reference)
        self.assertEqual([line for line in result if line.startswith(b'700-')],
                         [b'700-a\n', b'700-b\n', b'700-end\n'])

    def test_pause_at_mark_within_continuous_fragment(self):
        self.client.send('SET\npitch_range=40\n.\n')
        self.client.until(b'203 ')
        self.client.send('SPEAK\n<speak>Bonjour les amis '
                         '<mark name="__spd_pause"/>nous partons ensemble '
                         '<mark name="later"/>demain matin.</speak>\n.\nPAUSE\n')
        result = self.client.until(b'704 ')
        self.assertIn(b'700-__spd_pause\n', result)
        self.assertNotIn(b'700-later\n', result)
        # libspeechd_module may suppress packets as soon as PAUSE is pending.
        # In all cases no audio may follow the bookmark where it takes effect.
        at = result.index(b'700-__spd_pause\n')
        self.assertFalse(pcm_from(result[at:]))
        self.assertTrue(pcm_from(self.say('La lecture reprend.')))


class StartupTests(unittest.TestCase):
    def test_obsolete_configuration_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'module.conf'
            config.write_text('Renderer "/missing"\n')
            client = Protocol(config)
            try:
                client.send('INIT\n')
                client.p.wait(timeout=5)
                self.assertNotEqual(client.p.returncode, 0)
            finally:
                client.close()

if __name__ == '__main__':
    unittest.main()
