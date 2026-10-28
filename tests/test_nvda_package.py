# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('nvda_builder', ROOT / 'tools/build_nvda_addon.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def pe_fixture():
    data = bytearray(256)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3C, 128)
    data[128:132] = b'PE\0\0'
    struct.pack_into('<H', data, 132, 0x8664)
    struct.pack_into('<H', data, 150, 0x2000)
    return bytes(data) + b's3_api_version\0s3_api_text_create\0s3_api_text_append\0s3_api_request_cancel\0'


class PackageTests(unittest.TestCase):
    def test_minimal_archive_layout_and_reproducibility(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            dll = directory / 'test.dll'
            dll.write_bytes(pe_fixture())
            first = builder.build(dll, directory / 'first.nvda-addon')
            second = builder.build(dll, directory / 'second.nvda-addon')
            self.assertEqual(first.read_bytes(), second.read_bytes())
            with zipfile.ZipFile(first) as archive:
                self.assertIsNone(archive.testzip())
                names = set(archive.namelist())
                self.assertEqual({'manifest.ini', 'synthDrivers/synthe3_ng/__init__.py',
                    'synthDrivers/synthe3_ng/_worker.py', 'synthDrivers/synthe3_ng/_native.py',
                    'synthDrivers/synthe3_ng/bin/synthe3.dll', 'doc/fr/readme.html',
                    'doc/en/readme.html', 'licenses/LICENSE-GPL', 'licenses/LICENSE-LGPL',
                    'LICENSE.md', 'UPSTREAM.md'}, names)
                self.assertFalse(any('__pycache__' in name or name.endswith('.pyc') for name in names))
                self.assertEqual(archive.read('synthDrivers/synthe3_ng/bin/synthe3.dll'), dll.read_bytes())
                self.assertFalse(any(name.startswith('/') or '..' in Path(name).parts for name in names))
        manifest = builder.manifest()
        self.assertEqual(manifest['url'], 'https://github.com/pcressant/synthe3-ng')
        self.assertEqual(manifest['docFileName'], 'readme.html')
        self.assertEqual(manifest['minimumNVDAVersion'], '2026.2')

    def test_wrong_architecture_or_old_dll_is_rejected(self):
        for value in (b'', b'MZ', b'not a DLL', pe_fixture()[:256]):
            with self.subTest(value=value[:8]), self.assertRaises(ValueError):
                builder.validate_dll(value)
        data = bytearray(pe_fixture())
        struct.pack_into('<H', data, 132, 0x14C)
        with self.assertRaises(ValueError):
            builder.validate_dll(data)
