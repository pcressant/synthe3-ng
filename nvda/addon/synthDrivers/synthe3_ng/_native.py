# SPDX-License-Identifier: GPL-2.0-or-later
"""ctypes boundary: all text preparation and synthesis stay in the shared C code."""
import ctypes as C
from pathlib import Path

ABI_VERSION = 2


class Cancelled(Exception):
    pass


class PreparedMark(C.Structure):
    _fields_ = [("offset", C.c_uint32), ("index", C.c_uint32)]


PREPARED = C.CFUNCTYPE(C.c_int32, C.c_void_p, C.c_void_p, C.c_uint32,
                      C.POINTER(PreparedMark), C.c_uint32)


class Native:
    def __init__(self, path=None):
        path = Path(path) if path else Path(__file__).parent / "bin" / "synthe3.dll"
        self.dll = C.CDLL(str(path.resolve()))
        signatures = {
            "version": (C.c_uint32, []),
            "open": (C.c_int32, []), "close": (C.c_int32, []),
            "request_create": (C.c_void_p, []),
            "request_cancel": (None, [C.c_void_p]),
            "request_destroy": (None, [C.c_void_p]),
            "render": (C.c_int32, [C.c_void_p, C.c_char_p, C.c_uint32] + [C.c_int32] * 4
                       + [C.POINTER(C.POINTER(C.c_int16)), C.POINTER(C.c_uint32)]),
            "sample_at": (C.c_int32, [C.c_uint32, C.POINTER(C.c_uint32)]),
            "text_create": (C.c_void_p, [C.c_void_p, PREPARED, C.c_void_p]),
            "text_append": (C.c_int32, [C.c_void_p, C.c_char_p, C.c_uint32]),
            "text_mark": (C.c_int32, [C.c_void_p, C.c_uint32]),
            "text_finish": (C.c_int32, [C.c_void_p]),
            "text_spelling": (C.c_int32, [C.c_void_p, C.c_int32]),
            "text_destroy": (None, [C.c_void_p]),
        }
        for name, (result, args) in signatures.items():
            fn = getattr(self.dll, "s3_api_" + name)
            fn.restype, fn.argtypes = result, args
            setattr(self, "_" + name, fn)
        if self._version() != ABI_VERSION:
            raise RuntimeError("Synthé3: incompatible DLL ABI")
        self.check(self._open())

    @staticmethod
    def check(status):
        if status == 1:
            raise Cancelled()
        if status:
            raise RuntimeError(f"Synthé3 native status {status}")

    def request(self):
        value = self._request_create()
        if not value:
            raise MemoryError("Synthé3 request allocation failed")
        return value

    def cancel(self, request):
        self._request_cancel(request)

    def release(self, request):
        self._request_destroy(request)

    def close(self):
        self.check(self._close())

    def stream(self, request, callback):
        return TextStream(self, request, callback)

    def render(self, request, text, settings, marks):
        pcm, count = C.POINTER(C.c_int16)(), C.c_uint32()
        self.check(self._render(request, text, len(text), *settings, C.byref(pcm), C.byref(count)))
        positions = []
        previous = 0
        for offset, index in marks:
            sample = C.c_uint32()
            self.check(self._sample_at(offset, C.byref(sample)))
            previous = max(previous, min(count.value, sample.value))
            positions.append((previous, index))
        # Borrowed C buffers must not survive the next render.
        return C.string_at(pcm, count.value * 2), positions


class TextStream:
    def __init__(self, native, request, callback):
        self.native = native
        self.error = None

        def prepared(context, text, length, marks, count):
            try:
                callback(C.string_at(text, length), [(marks[i].offset, marks[i].index) for i in range(count)])
                return 0
            except Cancelled as error:
                self.error = error
                return 1
            except Exception as error:
                # ctypes callbacks cannot propagate exceptions across C frames.
                self.error = error
                return -1

        self.callback = PREPARED(prepared)  # Keep alive until after native destruction.
        self.handle = native._text_create(request, self.callback, None)
        if not self.handle:
            raise MemoryError("Synthé3 text stream allocation failed")

    def _check(self, status):
        if self.error is not None:
            raise self.error
        self.native.check(status)

    def append(self, text):
        # Bounded calls allow cancellation during Unicode encoding as well.
        for offset in range(0, len(text), 4096):
            data = text[offset:offset + 4096].replace("\0", " ").encode("utf-8", errors="replace")
            self._check(self.native._text_append(self.handle, data, len(data)))

    def mark(self, index):
        if not 0 <= index <= 0xFFFFFFFF:
            raise ValueError("NVDA index outside uint32 range")
        self._check(self.native._text_mark(self.handle, index))

    def finish(self):
        self._check(self.native._text_finish(self.handle))

    def spelling(self, enabled):
        self._check(self.native._text_spelling(self.handle, int(enabled)))

    def close(self):
        if self.handle:
            self.native._text_destroy(self.handle)
            self.handle = None
