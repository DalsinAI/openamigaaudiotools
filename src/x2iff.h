/* Copyright (c) 2026 AmiCradle Team. OpenAmigaAudioTools, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * x2iff: what the converter (x2iff.c) and the decoders (sources.c) share.
 * Which decoders a program has is chosen when it is built:
 *   X2IFF_OGG   Ogg Vorbis, stb_vorbis          (ogg2iff)
 *   X2IFF_MP3   MP3, dr_mp3                      (mp32iff)
 *   X2IFF_WAV   WAV, AIFF/AIFC, W64, RF64, dr_wav (wav2iff)
 *   X2IFF_FLAC  FLAC, dr_flac                    (flac2iff)
 *   X2IFF_XMP   MOD, XM, S3M, IT, MED and the other tracker formats, libxmp (mod2iff)
 * x2iff has them all. That is the PC's side (sources.c).
 *
 * On the Amiga every read and every save goes through datatypes
 * (dtio.c), and the programs hold no decoders: a sound is opened with
 * NewDTObject (gfopensound, gfopenmodule, gfopenaiff, gfopenwave or any
 * other sound datatype) and streamed with OIM_READSOUND, and the result is
 * saved by gfopenaiff or gfopenwave.datatype (AIFF, WAV, raw PCM) or by
 * sound.datatype (8SVX) with DTM_WRITE (datatypes/gfopenimage.h, in
 * DalsinAI/gfopendatatypes). The programs differ only in their names.
 * --verify opens what was written again, through datatypes.
 */
#ifndef X2IFF_H
#define X2IFF_H

#include <stdio.h>

#ifndef X2IFF_NAME
#define X2IFF_NAME "x2iff"
#endif
#define X2IFF_VERSION "1.0"

#if defined(__amigaos__) || defined(AMIGA)
#define X2IFF_AMIGA 1
#endif

#define BLOCK 4096      /* frames per decoded block */
#define MAXCH 8

typedef struct {
    int fmt;
    long rate;          /* 0: keep the source rate */
    int mono;
    int bits;           /* 0: the format's default */
    int big_endian;     /* raw only; default big, the 68k's order */
    int unsigned8;      /* raw 8-bit only */
    int normalize;
    double norm_db;
    double gain_db;
    double start, end, length;  /* seconds; end/length < 0: to the end */
    int trim_silence;
    double silence_db;
    int quality;        /* 0, 1, 2: resampler length */
    int dither;
    int info;
    int verbose;
    const char *out;
    /* tracker modules (libxmp) */
    int loops;          /* times through the song */
    int separation;     /* stereo separation, percent; -1: libxmp's default */
    int interp;         /* 0 nearest, 1 linear, 2 spline */
    int paula;          /* libxmp's Paula mixer for Amiga modules */
    int verify;         /* the Amiga: open each output again through datatypes */
} Opts;

typedef struct Source Source;
struct Source {
    const char *kind;           /* "Ogg Vorbis", "MP3", ... */
    int channels;
    long rate;
    unsigned long total;        /* frames; 0: not known */
    int rendered_mono;          /* already mono (libxmp mixed it) */
    /* n frames into out[0..channels-1]; 0 at the end, < 0 on an error */
    int (*read)(Source *, float **out, int n);
    int (*rewind)(Source *);    /* 0: back at the start */
    int (*seek)(Source *, unsigned long frame);  /* 1: there; NULL or 0: decode up to it */
    void (*info)(Source *, FILE *);
    void (*close)(Source *);
    void *h;
    float *ibuf;                /* interleaved scratch for the decoders that want it */
};

/* Open path with whichever compiled-in decoder takes it. want_rate and o
 * matter to libxmp, which renders at the rate it is asked for. NULL: an
 * error, already printed. */
Source *src_open(const char *path, const Opts *o, long want_rate);
/* Does a batch take this file? (by its name: extension or Amiga prefix) */
int src_takes_name(const char *name);
/* "mod.song": the Amiga's prefix naming; how long the prefix is with its
 * dot, or 0 */
size_t src_prefix_len(const char *name);
/* The decoders built in, for --help */
const char *src_kinds(void);

#ifdef X2IFF_AMIGA
/* Open path as a sound datatype object and say what it holds: 0 if it
 * opens, with frames, channels and rate filled in; -1 if it doesn't. */
int dt_probe(const char *path, unsigned long *frames, int *channels, long *rate);
/* Save frames of 16-bit interleaved pcm through datatypes: fmt 0 AIFF,
 * 1 8SVX, 2 WAV, 3 raw (x2iff.c's F_); bits 8 or 16 (8: each sample's high
 * byte is written, already dithered). 0, or -1 with the reason printed. */
int dt_save(const char *path, int fmt, int bits, int big_endian, int unsigned8,
            const short *pcm, unsigned long frames, int channels, long rate);
#endif

void *xcalloc(size_t n, size_t s);

#endif
