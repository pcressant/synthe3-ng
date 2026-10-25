#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Measure ng module CPU through the real server-audio protocol, without playback.

Only the tested module and its waited-for descendants contribute to child CPU.
Wall time also includes this driver, pipe transfers and optional PCM decoding.
No Speech Dispatcher server or audio device is needed.
"""
import argparse
import hashlib
import json
import platform
import queue
import resource
import shlex
import subprocess
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEXT = 'Bonjour, ceci est une phrase de comparaison. Les oiseaux chantent dans les arbres.'


class Client:
    def __init__(self, command):
        self.p = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)
        self.lines = queue.Queue()
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self):
        for line in self.p.stdout:
            self.lines.put(line)
        self.lines.put(None)

    def send(self, data):
        self.p.stdin.write(data.encode('utf-8'))
        self.p.stdin.flush()

    def until(self, prefix):
        deadline = time.monotonic() + 30
        lines = []
        while True:
            line = self.lines.get(timeout=max(.01, deadline - time.monotonic()))
            if line is None:
                raise RuntimeError('Module exited: ' + self.p.stderr.read().decode(errors='replace'))
            if line.startswith((b'3', b'4', b'703 ', b'704 ')):
                raise RuntimeError('Unexpected module response: ' + repr(line))
            lines.append(line)
            if line.startswith(prefix):
                return lines

    def close(self):
        try:
            if self.p.poll() is None:
                try:
                    self.send('QUIT\n')
                except BrokenPipeError:
                    pass
            try:
                self.p.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.p.kill()
                self.p.wait()
        finally:
            self.reader.join(timeout=1)
            self.p.stdin.close()
            self.p.stdout.close()
            self.p.stderr.close()


def pcm_from(lines):
    pcm = bytearray()
    for line in lines:
        if not line.startswith(b'705-AUDIO\0'):
            continue
        escaped = False
        for value in line[len(b'705-AUDIO\0'):-1]:
            if escaped:
                pcm.append(value ^ 0x20)
                escaped = False
            elif value == 0x7d:
                escaped = True
            else:
                pcm.append(value)
        if escaped:
            raise RuntimeError('Truncated PCM escape')
    return pcm


def measure(command, text, iterations, decode_pcm):
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.monotonic()
    client = Client(command)
    samples = digest = None
    try:
        client.send('INIT\n')
        client.until(b'299 ')
        client.send('AUDIO\naudio_output_method=server\n.\n')
        client.until(b'203 ')
        stuffed = '\n'.join('.' + line if line.startswith('.') else line
                            for line in text.split('\n'))
        for _ in range(iterations):
            client.send('SPEAK\n' + stuffed + '\n.\n')
            lines = client.until(b'702 ')
            samples = sum(int(line.split(b'=', 1)[1]) for line in lines
                          if line.startswith(b'705-num_samples='))
            if not samples:
                raise RuntimeError('No PCM returned by module')
            if decode_pcm:
                digest = hashlib.sha256(pcm_from(lines)).hexdigest()
    finally:
        client.close()
    wall = time.monotonic() - start
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    if client.p.returncode:
        raise RuntimeError(f'Module exit status: {client.p.returncode}')
    user = after.ru_utime - before.ru_utime
    system = after.ru_stime - before.ru_stime
    return dict(user_cpu_seconds=user, system_cpu_seconds=system, cpu_seconds=user + system,
                wall_seconds=wall, last_pcm_samples=samples, last_pcm_sha256=digest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', default=str(ROOT / 'build/sd_synthe3_ng'),
                        help='Module command, quoted as one argument; never run through a shell')
    parser.add_argument('--baseline', help='Optional historical module command')
    parser.add_argument('--iterations', type=int, default=10)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--decode-pcm', action='store_true',
                        help='Decode and hash PCM, as in the initial 10-utterance measurement')
    parser.add_argument('--text', default=TEXT)
    parser.add_argument('--json', type=Path, help='Save full results and method as JSON')
    args = parser.parse_args()
    if args.iterations < 1 or args.repeats < 1 or not args.text.strip() or '\0' in args.text:
        parser.error('Positive iteration/repeat counts and nonempty NUL-free text are required')
    commands = {'candidate': shlex.split(args.candidate)}
    if args.baseline:
        commands = {'baseline': shlex.split(args.baseline), **commands}
    if any(not command for command in commands.values()):
        parser.error('Empty module command')
    result = dict(timestamp_utc=datetime.now(timezone.utc).isoformat(),
                  platform=platform.platform(), python=platform.python_version(), text=args.text,
                  iterations=args.iterations, repeats=args.repeats, decode_pcm=args.decode_pcm,
                  initialization_included=True, playback=False,
                  child_cpu_scope='module and waited-for descendants; excludes driver CPU',
                  commands=commands, runs=[])
    for repeat in range(args.repeats):
        order = list(commands)
        if repeat % 2:
            order.reverse()
        for label in order:
            run = measure(commands[label], args.text, args.iterations, args.decode_pcm)
            result['runs'].append(dict(repeat=repeat + 1, label=label, **run))
            print(f"{label}, série {repeat + 1}: CPU {run['cpu_seconds']:.4f} s, "
                  f"temps écoulé {run['wall_seconds']:.4f} s, "
                  f"{run['last_pcm_samples']} échantillons/message", flush=True)
    if args.json:
        args.json.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    if args.baseline and args.decode_pcm:
        hashes = {run['last_pcm_sha256'] for run in result['runs']}
        if len(hashes) != 1:
            raise RuntimeError('Baseline/candidate PCM hashes differ; see recorded results')


if __name__ == '__main__':
    main()
