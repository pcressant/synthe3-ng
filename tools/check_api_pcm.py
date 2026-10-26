#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare an offline API renderer to the unchanged historical PCM oracle.

Runs on Linux or Windows; --runner can specify Wine for a Windows executable.
Historical settings deliberately use pitch base 6, not the current default 5.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def engine_settings(rate, pitch):
    def level(value, base):
        return round(base + value * (base if value < 0 else 15 - base) / 100)
    if rate <= 0:
        speed = level(rate, 4)
    else:
        calibrated = rate * 80 / 54 if rate <= 54 else 80 + (rate - 54) * 20 / 46
        speed = round(4 + calibrated * 11 / 100)
    return speed, level(pitch, 6)


def check(renderer, runner=None):
    cases = json.loads((ROOT / 'tests/voice_reference.json').read_text())
    results = []
    with tempfile.TemporaryDirectory(prefix='synthe3-api-') as directory:
        source = Path(directory) / 'input.txt'
        target = Path(directory) / 'output.pcm'
        for case in cases:
            source.write_text(case['text'], encoding='utf-8')
            rate, pitch = engine_settings(case['rate'], case['pitch'])
            command = ([runner] if runner else []) + [str(Path(renderer).resolve()),
                str(source), str(target), str(rate), str(pitch), '0', '0']
            subprocess.run(command, check=True, capture_output=True, timeout=30)
            pcm = target.read_bytes()
            digest = hashlib.sha256(pcm).hexdigest()
            results.append({
                'text': case['text'], 'rate': case['rate'], 'pitch': case['pitch'],
                'bytes': len(pcm), 'sha256': digest,
                'expected_bytes': case['bytes'], 'expected_sha256': case['sha256'],
                'match': len(pcm) == case['bytes'] and digest == case['sha256'],
            })
    return {'renderer': str(Path(renderer).resolve()), 'runner': runner,
            'matched': sum(row['match'] for row in results), 'total': len(results),
            'cases': results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renderer', default=str(ROOT / 'build/render_api'))
    parser.add_argument('--runner', help='Optional executable launcher, e.g. /path/to/wine')
    parser.add_argument('--report', type=Path, help='Write detailed JSON results')
    args = parser.parse_args()
    report = check(args.renderer, args.runner)
    if args.report:
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f"Historical PCM: {report['matched']}/{report['total']} exact matches")
    return 0 if report['matched'] == report['total'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
