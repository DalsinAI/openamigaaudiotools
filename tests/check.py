#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. OpenAmigaAudioTools, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""The tools' tests, on the PC: convert the tests' inputs every way and
check the files.

    tests/check.py BINDIR [WORKDIR]

BINDIR holds ogg2iff, mp32iff, wav2iff, flac2iff, mod2iff and x2iff
(build/host). ogg2iff goes through every option on tests/tone.ogg; then
each other tool on its own format: FLAC, WAV and AIFF must come out sample
for sample as they went in, MP3 within a little of the source, the module
at its pitch and length; x2iff must write what each single tool writes.

Each output is read by a strict parser written from the format's
specification (EA IFF 85 with 8SVX and its CHAN chunk, AIFF 1.3, RIFF WAVE),
not by the code that wrote it: chunk sizes, padding, FORM/RIFF lengths,
VHDR/COMM/fmt fields. Then the sound: frame counts, the 440 Hz left and
1000 Hz right tones' levels, normalisation, trimming, dither. When the
system has libsndfile, the 16-bit decode is compared with libvorbis's.
`file` is asked about each output when it is there.
"""
import ctypes, ctypes.util, math, os, shutil, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TONE = os.path.join(HERE, "tone.ogg")
FRAMES, RATE = 110250, 44100
fails = 0


def check(cond, what):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        fails += 1


def chunks(data, off, end, be=True):
    """(id, body) of each chunk in data[off:end]; checks sizes and padding."""
    out = []
    while off < end:
        assert off + 8 <= end, "chunk header runs past its container"
        cid = data[off:off + 4]
        size = struct.unpack(">I" if be else "<I", data[off + 4:off + 8])[0]
        assert off + 8 + size <= end, "chunk %r runs past its container" % cid
        out.append((cid, data[off + 8:off + 8 + size]))
        off += 8 + size + (size & 1)
    assert off == end, "container length doesn't match its chunks"
    return out


def ext80(b):
    exp, hi, lo = struct.unpack(">HII", b)
    return ((hi << 32) | lo) * 2.0 ** ((exp & 0x7fff) - 16383 - 63)


def read_iff(path):
    data = open(path, "rb").read()
    assert data[:4] == b"FORM", "not a FORM"
    size = struct.unpack(">I", data[4:8])[0]
    assert size + 8 + (size & 1) == len(data), "FORM length %d, file %d" % (size, len(data))
    return data[8:12], chunks(data, 12, 8 + size)


def read_aiff(path):
    kind, cks = read_iff(path)
    assert kind == b"AIFF", "FORM type %r" % kind
    d = dict(cks)
    comm, ssnd = d[b"COMM"], d[b"SSND"]
    assert len(comm) == 18, "COMM is %d bytes" % len(comm)
    ch, frames, bits = struct.unpack(">hIh", comm[:8])
    rate = ext80(comm[8:18])
    assert rate == int(rate), "COMM rate %r" % rate
    rate = int(rate)
    offset, block = struct.unpack(">II", ssnd[:8])
    assert offset == 0 and block == 0
    pcm = ssnd[8:]
    assert len(pcm) == frames * ch * bits // 8, "SSND holds %d bytes" % len(pcm)
    fmt = ">%dh" % (len(pcm) // 2) if bits == 16 else "%db" % len(pcm)
    s = struct.unpack(fmt, pcm)
    return ch, rate, bits, [list(s[c::ch]) for c in range(ch)]


def read_8svx(path):
    kind, cks = read_iff(path)
    assert kind == b"8SVX", "FORM type %r" % kind
    assert cks[0][0] == b"VHDR", "VHDR isn't first"
    d = dict(cks)
    v = d[b"VHDR"]
    assert len(v) == 20
    one, rep, cyc, rate, octave, comp, vol = struct.unpack(">IIIHBBI", v)
    assert rep == 0 and cyc == 0 and octave == 1 and comp == 0 and vol == 0x10000
    ch = 1
    if b"CHAN" in d:
        assert [k for k, _ in cks].index(b"CHAN") < [k for k, _ in cks].index(b"BODY")
        assert struct.unpack(">I", d[b"CHAN"])[0] == 6, "CHAN isn't stereo (6)"
        ch = 2
    body = d[b"BODY"]
    assert len(body) == one * ch, "BODY %d bytes for %d samples x %d" % (len(body), one, ch)
    s = struct.unpack("%db" % len(body), body)
    return ch, rate, 8, [list(s[c * one:(c + 1) * one]) for c in range(ch)]


def read_wav(path):
    data = open(path, "rb").read()
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE"
    size = struct.unpack("<I", data[4:8])[0]
    assert size + 8 == len(data), "RIFF length %d, file %d" % (size, len(data))
    d = dict(chunks(data, 12, 8 + size, be=False))
    tag, ch, rate, bps, align, bits = struct.unpack("<HHIIHH", d[b"fmt "])
    assert tag == 1 and align == ch * bits // 8 and bps == rate * align
    pcm = d[b"data"]
    fmt = "<%dh" % (len(pcm) // 2) if bits == 16 else "%dB" % len(pcm)
    s = struct.unpack(fmt, pcm)
    if bits == 8:
        s = [x - 128 for x in s]
    return ch, rate, bits, [list(s[c::ch]) for c in range(ch)]


def level(x, freq, rate, full):
    """Amplitude of freq in x (one channel, ints) as a fraction of full scale."""
    n = len(x)
    w = 2 * math.pi * freq / rate
    re = sum(v * math.cos(w * i) for i, v in enumerate(x))
    im = sum(v * math.sin(w * i) for i, v in enumerate(x))
    return 2 * math.hypot(re, im) / n / full


def mid(x, rate):
    """0.5 s from the middle of the tone (1.0 .. 1.5 s at the source's time)."""
    a = int(1.0 * rate)
    return x[a:a + rate // 2]


def run(exe, *args):
    r = subprocess.run([exe] + list(args), capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr)
    return r.returncode


def libsndfile_decode(path):
    lib = ctypes.util.find_library("sndfile")
    if not lib:
        return None
    lib = ctypes.CDLL(lib)

    class SF_INFO(ctypes.Structure):
        _fields_ = [("frames", ctypes.c_int64), ("samplerate", ctypes.c_int),
                    ("channels", ctypes.c_int), ("format", ctypes.c_int),
                    ("sections", ctypes.c_int), ("seekable", ctypes.c_int)]
    lib.sf_open.restype = ctypes.c_void_p
    lib.sf_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(SF_INFO)]
    lib.sf_readf_float.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.c_int64]
    lib.sf_readf_float.restype = ctypes.c_int64
    lib.sf_close.argtypes = [ctypes.c_void_p]
    info = SF_INFO()
    f = lib.sf_open(path.encode(), 0x10, ctypes.byref(info))
    if not f:
        return None
    buf = (ctypes.c_float * (info.frames * info.channels))()
    n = lib.sf_readf_float(f, buf, info.frames)
    lib.sf_close(f)
    return [[buf[i * info.channels + c] for i in range(n)] for c in range(info.channels)]


def main():
    bindir = os.path.abspath(sys.argv[1])
    exe = os.path.join(bindir, "ogg2iff")
    work = sys.argv[2] if len(sys.argv) > 2 else tempfile.mkdtemp(prefix="ogg2iff-")
    os.makedirs(work, exist_ok=True)
    o = lambda n: os.path.join(work, n)
    has_file = shutil.which("file")

    def conv(name, *args):
        r = run(exe, "-o", o(name), *args, TONE)
        check(r == 0, "ogg2iff %s -> %s" % (" ".join(args), name))
        if has_file and r == 0:
            print("     file: " + subprocess.run(["file", "-b", o(name)], capture_output=True,
                                                 text=True).stdout.strip())
        return r == 0

    def tones(name, ch, rate, bits, s, full, want_rate, want_ch, want_frames, l440=0.5, r1000=0.5):
        check(rate == want_rate, "%s: rate %s" % (name, rate))
        check(ch == want_ch, "%s: %d channel(s)" % (name, ch))
        check(abs(len(s[0]) - want_frames) <= 0, "%s: %d frames (want %d)" % (name, len(s[0]), want_frames))
        a = level(mid(s[0], rate), 440, rate, full)
        b = level(mid(s[-1], rate), 1000, rate, full)
        check(abs(a - l440) < 0.02, "%s: 440 Hz at %.3f (want %.3f)" % (name, a, l440))
        check(abs(b - r1000) < 0.02, "%s: 1000 Hz at %.3f (want %.3f)" % (name, b, r1000))
        if ch == 2:
            x = level(mid(s[0], rate), 1000, rate, full)
            check(x < 0.005, "%s: no 1000 Hz on the left (%.4f)" % (name, x))

    # 16-bit WAV at the source's rate, against libvorbis's decode
    if conv("a.wav", "-f", "wav"):
        ch, rate, bits, s = read_wav(o("a.wav"))
        tones("a.wav", ch, rate, bits, s, 32768, RATE, 2, FRAMES)
        ref = libsndfile_decode(TONE)
        if ref:
            d = max(abs(s[c][i] - ref[c][i] * 32768) for c in range(2) for i in range(len(ref[0])))
            check(len(ref[0]) == len(s[0]) and d < 4, "a.wav: within %.1f LSB of libvorbis's decode" % d)
        else:
            print("skip libvorbis comparison (no libsndfile)")

    # AIFF: the same samples, big-endian
    if conv("a.aiff"):
        ch, rate, bits, s = read_aiff(o("a.aiff"))
        tones("a.aiff", ch, rate, bits, s, 32768, RATE, 2, FRAMES)
        check(s == read_wav(o("a.wav"))[3], "a.aiff: the same samples as a.wav")

    # 8SVX mono 22050: both tones, mixed down (each at half level)
    if conv("m.8svx", "-f", "8svx", "-m", "-r", "22050"):
        ch, rate, bits, s = read_8svx(o("m.8svx"))
        tones("m.8svx", ch, rate, bits, s, 128, 22050, 1, 55125, 0.25, 0.25)
        q = s[0][:5000]   # silence: TPDF dither, within +-1 LSB, not all zero
        check(max(map(abs, q)) <= 1 and any(q), "m.8svx: dithered silence (+-1 LSB)")

    if conv("m0.8svx", "-f", "8svx", "-m", "-r", "22050", "--no-dither"):
        s = read_8svx(o("m0.8svx"))[3]
        check(not any(s[0][:5000]), "m0.8svx: --no-dither silence is zero")

    # 8SVX stereo at a Paula rate: CHAN, left then right in BODY
    if conv("s.8svx", "-f", "8svx", "-r", "28867"):
        ch, rate, bits, s = read_8svx(o("s.8svx"))
        tones("s.8svx", ch, rate, bits, s, 128, 28867, 2, (FRAMES * 28867 + RATE - 1) // RATE)

    # 8-bit AIFF and WAV
    if conv("b8.aiff", "-b", "8", "-r", "11025"):
        ch, rate, bits, s = read_aiff(o("b8.aiff"))
        check(bits == 8, "b8.aiff: 8-bit")
        tones("b8.aiff", ch, rate, bits, s, 128, 11025, 2, (FRAMES + 3) // 4)
    if conv("b8.wav", "-f", "wav", "-b", "8", "-m"):
        ch, rate, bits, s = read_wav(o("b8.wav"))
        tones("b8.wav", ch, rate, bits, s, 128, RATE, 1, FRAMES, 0.25, 0.25)

    # raw: 16-bit both orders, 8-bit signed and unsigned
    if conv("be.raw", "-f", "raw", "-r", "11025") and conv("le.raw", "-f", "raw", "-e", "little", "-r", "11025"):
        be, le = open(o("be.raw"), "rb").read(), open(o("le.raw"), "rb").read()
        n = (FRAMES + 3) // 4
        check(len(be) == n * 4, "be.raw: %d bytes" % len(be))
        check(be[0::2] == le[1::2] and be[1::2] == le[0::2], "le.raw: be.raw byte-swapped")
        s = struct.unpack(">%dh" % (len(be) // 2), be)
        check(abs(level(mid(s[0::2], 11025), 440, 11025, 32768) - 0.5) < 0.02, "be.raw: 440 Hz left")
    if conv("s8.raw", "-f", "raw", "-b", "8", "-m", "--no-dither") and \
       conv("u8.raw", "-f", "raw", "-b", "8", "-m", "-u", "--no-dither"):
        a, b = open(o("s8.raw"), "rb").read(), open(o("u8.raw"), "rb").read()
        check(len(a) == FRAMES and bytes((x + 128) & 255 for x in a) == b, "u8.raw: s8.raw + 128")

    # normalise and trim: peak at -0.1 dBFS, silence gone
    if conv("nt.aiff", "-n", "-T"):
        ch, rate, bits, s = read_aiff(o("nt.aiff"))
        peak = max(max(map(abs, s[0])), max(map(abs, s[1]))) / 32768
        db = 20 * math.log10(peak)
        check(abs(db + 0.1) < 0.05, "nt.aiff: peak %.3f dBFS" % db)
        check(abs(len(s[0]) - 1.5 * RATE) < 0.06 * RATE, "nt.aiff: %d frames, about 1.5 s" % len(s[0]))

    if conv("cut.aiff", "-s", "0.75", "-l", "1.0"):
        ch, rate, bits, s = read_aiff(o("cut.aiff"))
        check(len(s[0]) == RATE, "cut.aiff: exactly 1.0 s (%d frames)" % len(s[0]))
        full = read_aiff(o("a.aiff"))[3]
        a = int(0.75 * RATE)
        d = max(abs(s[c][i] - full[c][a + i]) for c in range(2) for i in range(len(s[0])))
        check(d <= 1, "cut.aiff: the same samples as a.aiff from 0.75 s (max diff %d)" % d)

    # the resampler keeps an in-band tone's level and removes one above the new Nyquist
    if conv("lp.aiff", "-r", "1600", "-q", "2"):
        ch, rate, bits, s = read_aiff(o("lp.aiff"))
        a = level(mid(s[0], rate), 440, rate, 32768)
        x = max(map(abs, mid(s[1], rate))) / 32768
        check(abs(a - 0.5) < 0.02, "lp.aiff: 440 Hz kept at 1600 Hz (%.3f)" % a)
        check(x < 0.001, "lp.aiff: 1000 Hz (above 800 Hz) gone, peak %.5f" % x)

    # batch: a directory in, a directory out
    src = o("batch-in")
    os.makedirs(src, exist_ok=True)
    for n in ("one.ogg", "TWO.OGG"):
        shutil.copy(TONE, os.path.join(src, n))
    open(os.path.join(src, "readme.txt"), "w").write("not audio\n")
    r = run(exe, "-f", "8svx", "-r", "11025", "-o", o("batch-out"), src)
    got = sorted(os.listdir(o("batch-out"))) if os.path.isdir(o("batch-out")) else []
    check(r == 0 and got == ["TWO.8svx", "one.8svx"], "batch: %s" % got)

    check(run(exe, "-i", TONE) == 0, "--info")
    check(run(exe, "-o", o("bad.aiff"), os.path.join(HERE, "check.py")) != 0, "a non-Vorbis input fails")
    check(not os.path.exists(o("bad.aiff")), "and leaves no output")

    others(bindir, o)

    print("%d failed" % fails if fails else "all passed")
    sys.exit(1 if fails else 0)


def others(bindir, o):
    t = lambda n: os.path.join(bindir, n)
    src = lambda n: os.path.join(HERE, n)
    ref = read_wav(src("tone.wav"))[3]             # the 16-bit tone every lossless input holds

    def conv(tool, name, inp, *args):
        r = run(t(tool), "-o", o(name), *args, src(inp))
        check(r == 0, "%s %s %s -> %s" % (tool, " ".join(args), inp, name))
        return r == 0

    # lossless in: the same samples out
    for tool, inp in (("wav2iff", "tone.wav"), ("wav2iff", "tone.aiff"), ("flac2iff", "tone.flac")):
        name = inp.replace(".", "-") + ".aiff"
        if conv(tool, name, inp):
            ch, rate, bits, s = read_aiff(o(name))
            check((ch, rate, bits) == (2, RATE, 16) and s == ref, "%s: sample for sample as %s" % (name, inp))

    # MP3: length (the encoder's delay taken off) and both tones
    if conv("mp32iff", "mp3.aiff", "tone.mp3"):
        ch, rate, bits, s = read_aiff(o("mp3.aiff"))
        check(len(s[0]) == FRAMES, "mp3.aiff: %d frames" % len(s[0]))
        a = level(mid(s[0], rate), 440, rate, 32768)
        b = level(mid(s[1], rate), 1000, rate, 32768)
        check(abs(a - 0.5) < 0.02 and abs(b - 0.5) < 0.02, "mp3.aiff: 440 Hz %.3f, 1000 Hz %.3f" % (a, b))

    # x2iff writes what each single tool writes
    for tool, inp in (("ogg2iff", "tone.ogg"), ("mp32iff", "tone.mp3"), ("wav2iff", "tone.wav"),
                      ("flac2iff", "tone.flac"), ("mod2iff", "tone.mod")):
        one, both = o("one-" + inp + ".aiff"), o("x2-" + inp + ".aiff")
        r1 = run(t(tool), "-r", "22050", "-o", one, src(inp))
        r2 = run(t("x2iff"), "-r", "22050", "-o", both, src(inp))
        check(r1 == 0 and r2 == 0 and open(one, "rb").read() == open(both, "rb").read(),
              "x2iff on %s writes what %s does" % (inp, tool))
    check(run(t("ogg2iff"), "-o", o("wrong.aiff"), src("tone.flac")) != 0, "ogg2iff refuses a FLAC")

    # the module: 3546895 / 428 / 32 = 258.98 Hz on the left channel for 16 rows, 1.92 s
    if conv("mod2iff", "mod.aiff", "tone.mod"):
        ch, rate, bits, s = read_aiff(o("mod.aiff"))
        check(rate == 44100 and abs(len(s[0]) - 1.92 * 44100) < 0.02 * 44100, "mod.aiff: %d frames, about 1.92 s" % len(s[0]))
        m = s[0][22050:22050 + 22050]
        f = 3546895 / 428 / 32
        a, a2 = level(m, f, rate, 32768), level(m, 2 * f, rate, 32768)
        check(a > 0.1 and a2 < a / 10, "mod.aiff: %.2f Hz at %.3f (its octave %.4f)" % (f, a, a2))
        l, r = max(map(abs, m)), max(map(abs, s[1][22050:44100]))
        check(l > r, "mod.aiff: channel 1 is on the left (%d over %d)" % (l, r))
    if conv("mod2iff", "mod2.aiff", "tone.mod", "--loops", "2", "-m", "-r", "22050"):
        ch, rate, bits, s = read_aiff(o("mod2.aiff"))
        check(ch == 1 and abs(len(s[0]) - 2 * 1.92 * 22050) < 0.04 * 22050, "mod2.aiff: twice through, mono, %d frames" % len(s[0]))
    if conv("mod2iff", "mod.8svx", "tone.mod", "-f", "8svx", "-r", "28867", "--paula"):
        ch, rate, bits, s = read_8svx(o("mod.8svx"))
        check(rate == 28867 and ch == 2, "mod.8svx: stereo at 28867 Hz with the Paula mixer")

    # batches: the Amiga's mod.name, and an AIFF that would be written over itself
    bdir = o("batch-mod")
    os.makedirs(bdir, exist_ok=True)
    shutil.copy(src("tone.mod"), os.path.join(bdir, "mod.tune"))
    shutil.copy(src("tone.mod"), os.path.join(bdir, "song.xm.txt"))
    r = run(t("mod2iff"), "-f", "8svx", bdir)
    got = sorted(f for f in os.listdir(bdir) if f.endswith(".8svx"))
    check(r == 0 and got == ["tune.8svx"], "mod2iff: mod.tune becomes tune.8svx (%s)" % got)
    sdir = o("batch-self")
    os.makedirs(sdir, exist_ok=True)
    shutil.copy(src("tone.aiff"), os.path.join(sdir, "tone.aiff"))
    before = open(os.path.join(sdir, "tone.aiff"), "rb").read()
    r = run(t("wav2iff"), sdir)
    check(r != 0 and open(os.path.join(sdir, "tone.aiff"), "rb").read() == before,
          "wav2iff won't write tone.aiff over itself")


main()
