# OpenAmigaAudioTools

Audio converters for AmigaOS 3.x and Linux, from the AmiCradle Team. They
turn music and sounds into what an Amiga plays without decoding:

| Tool | Reads |
| --- | --- |
| `ogg2iff` | Ogg Vorbis |
| `mp32iff` | MP3 |
| `wav2iff` | WAV, AIFF, AIFC, W64, RF64 |
| `flac2iff` | FLAC |
| `mod2iff` | MOD, XM, S3M, IT, MED, Oktalyzer, DigiBooster and the other tracker formats |
| `x2iff` | all of these |

Each one writes:

- **AIFF**, 16-bit big-endian (the default). This is the 68k's own byte
  order, so AHI or a port's mixer plays it with no swapping.
- **IFF 8SVX**, 8-bit signed, mono or stereo (`CHAN`), for Paula and the
  classic players.
- **WAV**.
- **raw PCM**, 8 or 16-bit, in either byte order.

Also here: **libxmp 4.7.3 for AmigaOS 3.x**, a link library for the 68040
with an FPU and for the 68000, with headers, pkg-config files and an
example.

## On the Amiga: datatypes, read and save

On the Amiga every read and every save goes through datatypes. The
programs hold no decoders of their own; the same program under six names.

- **Read:** a sound is opened with `NewDTObject`, so any installed sound
  datatype is an input, and streamed with `OIM_READSOUND`, a piece at a
  time.
- **Save:** the result goes out through `DTM_WRITE`.
  - AIFF, WAV and raw PCM: `gfopenaiff.datatype` or `gfopenwave.datatype`.
  - 8SVX: sound.datatype's own writer.
- **`--verify`** opens what was written again through the datatypes.

Where a format had no datatype, or no way for a program to read or save
it, the missing piece was added to
[GFOpenDatatypes](https://github.com/DalsinAI/gfopendatatypes):

- `gfopenaiff.datatype` and `gfopenwave.datatype`: read, and save with
  `DTWM_RAW`.
- `gfopensound.datatype` 47.2: FLAC, Ogg Vorbis and MP3 decoded on the 68k.
- `gfopenmodule.datatype` 47.4: a reader mode that mixes a module for a
  program.
- `OIM_READSOUND` and its tags, in `datatypes/gfopenimage.h`.

These tools need GFOpenDatatypes with those versions installed.

A datatype keeps the result whole in memory until it saves it, so an
Amiga needs RAM for the output: about 5 MB a minute of 22 kHz stereo
16-bit audio. The reading side streams.

## On Linux: the same converter, its own decoders

On Linux the tools carry their decoders:

- stb_vorbis, dr_mp3, dr_wav and dr_flac (public domain or MIT/MIT-0);
- libxmp (MIT).

They stream from end to end. Third-party code is never committed:
`tools/fetch.py` fetches each part at the commit or release `UPSTREAM.json`
pins, and checks its sha256. `patches/` holds the Team's fixes, applied to a
copy.

## Options

    -f, --format aiff|8svx|wav|raw   (default aiff)
    -o, --output FILE|DIR      a folder when there are several inputs or a folder in
    -r, --rate HZ              resample: 22050, 11025, 28867 (Paula), any rate 1000-192000
    -m, --mono                 downmix
    -b, --bits 8|16            8-bit gets TPDF dither (--no-dither to turn it off)
    -e, --endian big|little    raw 16-bit (default big)     -u  raw 8-bit unsigned
    -n, --normalize[=DB]       peak to DB dBFS (default -0.1); reads the sound twice
    -g, --gain DB              -s/-t/-l SEC: --start, --end, --length
    -T, --trim-silence[=DB]    cut silence below DB (default -50 dBFS) at both ends
    -q, --quality 0|1|2        resampler length (Kaiser-windowed sinc, about 86 dB at 1)
    -i, --info                 describe, write nothing     -v  verbose
    modules: --interp nearest|linear|spline, --loops N, --separation P, --paula
             (on the Amiga only --interp, as gfopenmodule.datatype mixes)
    Amiga: --verify

A folder as input converts every file in it that the tool reads (on the
Amiga, every file a sound datatype opens). The Amiga's `mod.name` naming
becomes `name.aiff`. A file is never written over its own input.

The order of work is: decode, cut, downmix, gain, resample, quantise
(dither on 8-bit), save.

## Building

    make            # build/host, build/amiga (and libxmp's), then the tests
    make release    # build/release: the archives and SHA256SUMS

- **Linux:** `cc`.
- **Amiga:** GFOpenAmigaGCC 16.2 (`AMIGA_CC`, else `m68k-amigaos-gcc` on
  `PATH`, else `~/CradleLab/toolchain/...`), with `-m68040 -m68881
  -noixemul` and the Team's GCC 16 flags. The OpenNDK headers come from
  `OPENNDK` (default `~/CradleLab/gfopensdk/OpenNDK/Include_H`).
- **Checks:** every 68k program and libxmp object goes through
  `tools/fpcr_check.py`, for GCC 16's misplaced float-to-int store at
  `-m68040`.
- **libxmp** is built from its own source lists, whole, depackers and
  ProWizard included: once for the 68040 with an FPU, once for the 68000.

## Tests

`make test`, or `tests/check.py build/host`: 92 checks on the PC.

- **Format checks:** each file written is read by a strict parser written
  from its specification (EA IFF 85 / 8SVX with CHAN, AIFF 1.3, RIFF
  WAVE).
- **Sound checks:** frame counts and tone levels per channel; WAV, AIFF
  and FLAC in give the same samples out; Vorbis is within 0.5 LSB of
  libvorbis.
- **The module test:** the MOD made byte by byte gives its 258.98 Hz tone
  for 1.92 s, once or twice through.
- **Options:** dither, normalising, trimming, cutting, resampling (a tone
  over the new Nyquist frequency is gone), raw byte orders, batches, the
  `mod.` prefix, and the refusal to overwrite an input.
- **x2iff** writes byte for byte what each single tool writes.

The inputs in `tests/` are our own (`tests/mkinputs.py`, CC0). The
datatypes have their own tests in GFOpenDatatypes (`tests/test-pcm.sh`,
`tests/test-sound.sh`).

**Not yet run on an Amiga.** The Amiga programs and libxmp are built and
checked, but untested on a machine.

## Speed

On the PC (a Ryzen 5 3500U), decoding runs a few hundred times faster
than real time.

On the Amiga the datatypes do the decoding, and they are soft float, so
they also run on a 68LC040. These figures are estimates, not measured on
a machine:
- **FLAC:** integer maths; about real time on a 25 MHz 68040.
- **Ogg Vorbis and MP3:** float maths, through the ROM's math libraries;
  many times slower than real time.
- **Modules:** mixed by libxmp in `gfopenmodule`; about real time at
  22 kHz, or on a cores board when there is one.

Convert on the PC when the port's build allows it; the Amiga side is for
offline work there.

MIT, Copyright (c) 2026 AmiCradle Team. stb_vorbis, dr_libs and libxmp keep
their own licences (Licences/ in the Linux archive, docs/ in libxmp's).
