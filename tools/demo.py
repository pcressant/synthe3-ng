#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run an isolated Speech Dispatcher without touching user configuration."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serve', action='store_true', help='Keep the isolated server running')
parser.add_argument('--check-interruptions', action='store_true', help='Run the silent ng interruption integration test')
parser.add_argument('--text', default='Bonjour. Vous retrouvez la voix de Synthé trois, avec Speech Dispatcher.')
parser.add_argument('--variant', choices=('ng',), default='ng')
rates = parser.add_mutually_exclusive_group()
rates.add_argument('--rate', type=int, default=0, help='Speech Dispatcher rate (-100 to 100)')
rates.add_argument('--orca-rate', type=int, help='Orca rate (0 to 100)')
parser.add_argument('--modulation', type=float, default=5,
                    help='Orca modulation (0 to 10; historical intonation through 5)')
args = parser.parse_args()
if not 0 <= args.modulation <= 10:
    parser.error('--modulation must be between 0 and 10')
if args.orca_rate is not None:
    if not 0 <= args.orca_rate <= 100:
        parser.error('--orca-rate must be between 0 and 100')
    args.rate = 2 * args.orca_rate - 100
if not -100 <= args.rate <= 100:
    parser.error('--rate must be between -100 and 100')

module_name = 'synthe3_ng'
if args.check_interruptions and (args.variant != 'ng' or args.serve):
    parser.error('--check-interruptions requires --variant ng without --serve')
if args.check_interruptions and not (ROOT / 'build/check_speechd').is_file():
    parser.error('Run make build/check_speechd first')
for command in ('speech-dispatcher', 'spd-say'):
    if not shutil.which(command):
        parser.error(command + ' is required')
module = ROOT / 'build/sd_synthe3_ng'
if not module.is_file():
    parser.error('Run make first')
with tempfile.TemporaryDirectory(prefix='synthe3-demo-') as directory:
    base = Path(directory)
    (base / 'modules').mkdir()
    (base / 'log').mkdir()
    (base / 'modules/synthe3_ng.conf').write_text('')
    (base / 'speechd.conf').write_text(
        f'AddModule "{module_name}" "{module}" "synthe3_ng.conf"\n'
        f'DefaultModule {module_name}\nDefaultLanguage fr\nAudioOutputMethod pulse\nLogLevel 3\n')
    socket = base / 'speechd.sock'
    env = dict(os.environ, SPEECHD_ADDRESS='unix_socket:' + str(socket))
    with (base / 'server.log').open('w') as log:
        server = subprocess.Popen(['speech-dispatcher', '-s', '-t', '0', '-C', str(base),
                                   '-S', str(socket), '-P', str(base / 'speechd.pid'),
                                   '-L', str(base / 'log')], stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 10
            while not socket.exists():
                if server.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError('Server startup failed: ' + (base / 'server.log').read_text())
                time.sleep(.05)
            subprocess.run(['spd-say', '-o', module_name, '-L'], env=env, check=True, timeout=10)
            if args.check_interruptions:
                subprocess.run([str(ROOT / 'build/check_speechd')], env=env, check=True, timeout=120)
            elif args.serve:
                print('Serveur isolé prêt. Dans un autre terminal :', flush=True)
                print(f"export SPEECHD_ADDRESS='unix_socket:{socket}'", flush=True)
                print(f"spd-say -o {module_name} -l fr -w 'Bonjour.'", flush=True)
                print('Ctrl+C pour arrêter.', flush=True)
                server.wait()
            else:
                subprocess.run(['spd-say', '-o', module_name, '-l', 'fr', '-r', str(args.rate), '-R', str(int(20 * args.modulation - 100)), '-w', args.text],
                               env=env, check=True, timeout=120)
                print('Lecture terminée par Speech Dispatcher.', flush=True)
        except KeyboardInterrupt:
            pass
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
