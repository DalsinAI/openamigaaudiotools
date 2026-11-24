/* Copyright (c) 2026 AmiCradle Team. OpenAmigaAudioTools, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * IMA ADPCM WAV (Microsoft's WAVE_FORMAT_IMA_ADPCM, 0x11), mono: 1024-byte
 * blocks of 2041 samples, each block with its own predictor and step
 * index, so a player decodes it as it plays and seeks to a block. The
 * layout the game ports' mixers play (amiga/sdl_mixer_lite.c and the common
 * mixer), and byte for byte what the ports' amiga/tools/ogg2wav.py adpcm
 * made on the PC, so a WAV fallback made on the Amiga at install time is
 * the one the ports used to ship.
 *
 * Written from Microsoft's "Multimedia Standards Update" (IMA ADPCM) and
 * the IMA's Recommended Practices for Digital Audio; our own code. No
 * sound datatype writes or reads this format, so x2iff writes it itself,
 * on the PC and on the Amiga.
 */
#ifndef IMAADPCM_H
#define IMAADPCM_H

#include <stdio.h>

#define IMA_BLOCK 1024                          /* bytes a block */
#define IMA_SPB   ((IMA_BLOCK - 4) * 2 + 1)     /* 2041 samples a block */

/* Bytes of the whole file for n samples. */
unsigned long ima_wav_bytes(unsigned long n);

/* Writes n 16-bit mono samples at rate as an IMA ADPCM WAV to f.
 * 0, or -1 when a write fails. */
int ima_write_wav(FILE *f, const short *pcm, unsigned long n, long rate);

/* Reads an IMA ADPCM WAV that ima_write_wav's layout describes (mono,
 * 1024-byte blocks; chunks in any order): its sample count, rate and,
 * when pcm is not NULL, the samples, decoded (malloc'd: the caller frees
 * them). 0, or -1 when the file is not one. */
int ima_read_wav(FILE *f, unsigned long *n, long *rate, short **pcm);

#endif
