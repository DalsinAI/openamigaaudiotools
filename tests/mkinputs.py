#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. OpenAmigaAudioTools, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""Make the tests' inputs in tests/ (our own tones and module: CC0).

    tests/mkinputs.py

tone.*: 2.5 s, 44100 Hz stereo: 0.5 s of silence, 1.5 s of 440 Hz on the
left and 1000 Hz on the right at -6 dBFS, 0.5 s of silence. tone.ogg (Vorbis
q0.5), tone.mp3 (128 kb/s), tone.flac, tone.wav and tone.aiff (16-bit) are
written through the system's libsndfile, loaded with ctypes.

tone.mod: a ProTracker module, built here byte by byte. One looped 32-byte
sine cycle, played at C-2 (period 428) on channel 1 (hard left) for 16 rows
at speed 6, tempo 125: 1.92 s of a 3546895 / 428 / 32 = 258.98 Hz tone.

The files are committed, so the tests need none of this.
"""
import ctypes, ctypes.util, math, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
RATE, CH = 44100, 2
SFM_WRITE = 0x20
FORMATS = {
    "tone.ogg": 0x200000 | 0x0060,          # OGG | VORBIS
    "tone.mp3": 0x230000 | 0x0082,          # MPEG | LAYER_III
    "tone.flac": 0x170000 | 0x0002,         # FLAC | PCM_16
    "tone.wav": 0x010000 | 0x0002,          # WAV | PCM_16
    "tone.aiff": 0x020000 | 0x0002,         # AIFF | PCM_16
}
SFC_SET_VBR_ENCODING_QUALITY = 0x1300
SFC_SET_BITRATE_MODE, SFC_SET_COMPRESSION_LEVEL = 0x1305, 0x1301


class SF_INFO(ctypes.Structure):
    _fields_ = [("frames", ctypes.c_int64), ("samplerate", ctypes.c_int),
                ("channels", ctypes.c_int), ("format", ctypes.c_int),
                ("sections", ctypes.c_int), ("seekable", ctypes.c_int)]


def tone():
    pad, n = RATE // 2, RATE * 3 // 2
    s = []
    for i in range(pad + n + pad):
        if pad <= i < pad + n:
            t = (i - pad) / RATE
            env = min(1.0, (i - pad) / 220.0, (pad + n - 1 - i) / 220.0)   # 5 ms fades
            s += [0.5 * env * math.sin(2 * math.pi * 440 * t),
                  0.5 * env * math.sin(2 * math.pi * 1000 * t)]
        else:
            s += [0.0, 0.0]
    return s


def write_sndfile(lib, path, fmt, s):
    info = SF_INFO(0, RATE, CH, fmt, 0, 0)
    f = lib.sf_open(path.encode(), SFM_WRITE, ctypes.byref(info))
    if not f:
        sys.exit("libsndfile can't write %s" % path)
    if fmt & 0xff0000 == 0x200000:
        q = ctypes.c_double(0.5)
        lib.sf_command(f, SFC_SET_VBR_ENCODING_QUALITY, ctypes.byref(q), ctypes.sizeof(q))
    buf = (ctypes.c_float * len(s))(*s)
    lib.sf_writef_float(f, buf, len(s) // CH)
    lib.sf_close(f)


def mod():
    cyc = bytes((round(127 * math.sin(2 * math.pi * i / 32)) & 0xff) for i in range(32))
    m = bytearray(b"x2iff test tone".ljust(20, b"\0"))
    for i in range(31):
        if i == 0:   # 16 words, volume 64, looped from 0 for 16 words
            m += b"sine".ljust(22, b"\0") + struct.pack(">HBBHH", 16, 0, 64, 0, 16)
        else:
            m += bytes(22) + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
    m += bytes([1, 127]) + bytes(128) + b"M.K."
    pat = bytearray(64 * 4 * 4)
    # row 0, channel 1: sample 1, period 428 (C-2)
    pat[0:4] = bytes([0x00 | (428 >> 8), 428 & 0xff, 0x10, 0x00])
    # row 15, channel 2: D00, pattern break: the song is 16 rows
    r = 15 * 16 + 4
    pat[r:r + 4] = bytes([0, 0, 0x0D, 0x00])
    m += pat + cyc
    return bytes(m)


def main():
    lib = ctypes.CDLL(ctypes.util.find_library("sndfile") or "libsndfile.so.1")
    lib.sf_open.restype = ctypes.c_void_p
    lib.sf_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(SF_INFO)]
    lib.sf_writef_float.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.c_int64]
    lib.sf_command.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
    lib.sf_close.argtypes = [ctypes.c_void_p]
    s = tone()
    for name, fmt in FORMATS.items():
        write_sndfile(lib, os.path.join(HERE, name), fmt, s)
    open(os.path.join(HERE, "tone.mod"), "wb").write(mod())


main()
