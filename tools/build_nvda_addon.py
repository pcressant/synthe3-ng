#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build a deterministic NVDA add-on with its x64 DLL, user help and licenses."""
import argparse
import json
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ADDON = ROOT / 'nvda/addon'


def manifest():
    return {key.strip(): json.loads(value.strip()) for line in (ADDON / 'manifest.ini').read_text(encoding='utf-8').splitlines()
            if line.strip() for key, value in [line.split('=', 1)]}


def validate_dll(data):
    try:
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        machine = struct.unpack_from('<H', data, pe + 4)[0]
        characteristics = struct.unpack_from('<H', data, pe + 22)[0]
        valid = data[:2] == b'MZ' and data[pe:pe+4] == b'PE\0\0' and machine == 0x8664 and characteristics & 0x2000
    except struct.error:
        valid = False
    if not valid:
        raise ValueError('Expected an x64 PE DLL')
    for name in ('s3_api_version', 's3_api_text_create', 's3_api_text_append', 's3_api_request_cancel'):
        if name.encode('ascii') + b'\0' not in data:
            raise ValueError(f'DLL is missing required ABI 2 symbol: {name}')


def build(dll, output):
    data = Path(dll).read_bytes()
    validate_dll(data)
    files = {}
    for path in ADDON.rglob('*'):
        if path.is_file() and '__pycache__' not in path.parts and path.suffix != '.pyc':
            files[path.relative_to(ADDON).as_posix()] = path.read_bytes()
    files['synthDrivers/synthe3_ng/bin/synthe3.dll'] = data
    for name in ('LICENSE.md', 'UPSTREAM.md'):
        files[name] = (ROOT / name).read_bytes()
    for path in (ROOT / 'upstream').glob('LICENSE-*'):
        files['licenses/' + path.name] = path.read_bytes()
    for name, source in files.items():
        if name.endswith('.py'):
            compile(source, name, 'exec')
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, content in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            archive.writestr(info, content)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise ValueError('Corrupt add-on archive')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', required=True, type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    info = manifest()
    target = args.output or ROOT / 'build' / f"synthe3-ng-{info['version']}.nvda-addon"
    print(build(args.dll, target))


if __name__ == '__main__':
    main()
