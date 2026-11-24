/* Copyright (c) 2026 AmiCradle Team. OpenAmigaAudioTools, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * IMA ADPCM WAV, mono (imaadpcm.h).
 */
#include <stdlib.h>
#include <string.h>

#include "imaadpcm.h"

static const short steps[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};
static const signed char index_step[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

static void put16(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }
static void put32(unsigned char *p, unsigned long v)
{ p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static unsigned get16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned long get32(const unsigned char *p)
{ return p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24; }

#define HDR 60      /* RIFF 12, fmt 8 + 20, fact 8 + 4, data 8 */

static unsigned long blocks_for(unsigned long n) { return (n + IMA_SPB - 1) / IMA_SPB; }

unsigned long ima_wav_bytes(unsigned long n)
{
    return HDR + blocks_for(n) * IMA_BLOCK;
}

/* One block from pcm[base..], the step index carried from the block before. */
static void encode_block(unsigned char *b, const short *pcm, unsigned long n, unsigned long base, int *index)
{
    long pred = base < n ? pcm[base] : 0;
    int k;
    memset(b, 0, IMA_BLOCK);
    put16(b, (unsigned)pred & 0xffff);
    b[2] = (unsigned char)*index;
    b[3] = 0;
    for (k = 1; k < IMA_SPB; k++) {
        long s = base + k < n ? pcm[base + k] : 0;
        long step = steps[*index], diff = s - pred, vp = step >> 3;
        int code = 0, j = k - 1;
        if (diff < 0) { code = 8; diff = -diff; }
        if (diff >= step) { code |= 4; diff -= step; vp += step; }
        step >>= 1;
        if (diff >= step) { code |= 2; diff -= step; vp += step; }
        step >>= 1;
        if (diff >= step) { code |= 1; vp += step; }
        pred = code & 8 ? pred - vp : pred + vp;
        if (pred > 32767) pred = 32767;
        else if (pred < -32768) pred = -32768;
        *index += index_step[code];
        if (*index < 0) *index = 0;
        else if (*index > 88) *index = 88;
        if (j & 1) b[4 + (j >> 1)] |= code << 4;
        else b[4 + (j >> 1)] = code;
    }
}

int ima_write_wav(FILE *f, const short *pcm, unsigned long n, long rate)
{
    unsigned char h[HDR], b[IMA_BLOCK];
    unsigned long nb = blocks_for(n), i, data = nb * IMA_BLOCK;
    int index = 0;

    memcpy(h, "RIFF", 4); put32(h + 4, HDR - 8 + data);
    memcpy(h + 8, "WAVEfmt ", 8); put32(h + 16, 20);
    put16(h + 20, 0x11); put16(h + 22, 1);
    put32(h + 24, rate); put32(h + 28, (unsigned long)rate * IMA_BLOCK / IMA_SPB);
    put16(h + 32, IMA_BLOCK); put16(h + 34, 4);
    put16(h + 36, 2); put16(h + 38, IMA_SPB);
    memcpy(h + 40, "fact", 4); put32(h + 44, 4); put32(h + 48, n);
    memcpy(h + 52, "data", 4); put32(h + 56, data);
    if (fwrite(h, 1, HDR, f) != HDR) return -1;
    for (i = 0; i < nb; i++) {
        encode_block(b, pcm, n, i * IMA_SPB, &index);
        if (fwrite(b, 1, IMA_BLOCK, f) != IMA_BLOCK) return -1;
    }
    return 0;
}

static void decode_block(const unsigned char *b, short *out, unsigned long count)
{
    long pred = (short)get16(b);
    int index = b[2] > 88 ? 88 : b[2];
    unsigned long k;
    if (!count) return;
    out[0] = (short)pred;
    for (k = 1; k < count; k++) {
        unsigned long j = k - 1;
        int code = (b[4 + (j >> 1)] >> ((j & 1) * 4)) & 15;
        long step = steps[index], vp = step >> 3;
        if (code & 4) vp += step;
        if (code & 2) vp += step >> 1;
        if (code & 1) vp += step >> 2;
        pred = code & 8 ? pred - vp : pred + vp;
        if (pred > 32767) pred = 32767;
        else if (pred < -32768) pred = -32768;
        out[k] = (short)pred;
        index += index_step[code];
        if (index < 0) index = 0;
        else if (index > 88) index = 88;
    }
}

int ima_read_wav(FILE *f, unsigned long *n, long *rate, short **pcm)
{
    unsigned char h[12], ck[8], fmt[20], b[IMA_BLOCK];
    unsigned long riff, pos = 12, frames = 0, data_at = 0, data_len = 0;
    int have_fmt = 0, have_fact = 0;
    long r = 0;

    if (pcm) *pcm = NULL;
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return -1;
    riff = get32(h + 4) + 8;
    while (pos + 8 <= riff) {
        unsigned long size;
        if (fseek(f, (long)pos, SEEK_SET) || fread(ck, 1, 8, f) != 8) return -1;
        size = get32(ck + 4);
        if (!memcmp(ck, "fmt ", 4)) {
            if (size < 20 || fread(fmt, 1, 20, f) != 20) return -1;
            if (get16(fmt) != 0x11 || get16(fmt + 2) != 1 || get16(fmt + 12) != IMA_BLOCK
                || get16(fmt + 14) != 4 || get16(fmt + 18) != IMA_SPB) return -1;
            r = (long)get32(fmt + 4);
            have_fmt = 1;
        } else if (!memcmp(ck, "fact", 4)) {
            unsigned char v[4];
            if (size < 4 || fread(v, 1, 4, f) != 4) return -1;
            frames = get32(v);
            have_fact = 1;
        } else if (!memcmp(ck, "data", 4)) {
            data_at = pos + 8;
            data_len = size;
        }
        pos += 8 + size + (size & 1);
    }
    if (!have_fmt || !have_fact || !data_at || data_len % IMA_BLOCK
        || blocks_for(frames) != data_len / IMA_BLOCK) return -1;
    *n = frames;
    *rate = r;
    if (pcm) {
        unsigned long i, nb = data_len / IMA_BLOCK;
        short *out = malloc((nb ? nb * IMA_SPB : 1) * sizeof *out);
        if (!out || fseek(f, (long)data_at, SEEK_SET)) { free(out); return -1; }
        for (i = 0; i < nb; i++) {
            if (fread(b, 1, IMA_BLOCK, f) != IMA_BLOCK) { free(out); return -1; }
            decode_block(b, out + i * IMA_SPB, IMA_SPB);
        }
        *pcm = out;
    }
    return 0;
}
