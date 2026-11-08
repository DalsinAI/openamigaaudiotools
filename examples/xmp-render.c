/* Copyright (c) 2026 AmiCradle Team. OpenAmigaAudioTools, MIT licence.
 * SPDX-License-Identifier: MIT
 *
 * xmp-render: libxmp's smallest useful program. Loads a module, says what
 * it is, and mixes it once through to raw 16-bit big-endian stereo PCM
 * (the 68k's own order) at a rate you choose.
 *
 *     xmp-render MODULE OUT.raw [RATE]
 *
 * Amiga (the release's lib/ for a 68040 with an FPU; lib/68000 for any):
 *     m68k-amigaos-gcc -O2 -m68040 -m68881 -noixemul -DLIBXMP_STATIC \
 *         -Iinclude xmp-render.c -Llib -lxmp -lm -o xmp-render
 * or with the release's pkg-config file:
 *     m68k-amigaos-gcc -O2 -m68040 -m68881 -noixemul xmp-render.c \
 *         $(PKG_CONFIG_PATH=lib/pkgconfig pkg-config --cflags --libs libxmp) -o xmp-render
 */
#include <stdio.h>
#include <stdlib.h>
#include <xmp.h>

int main(int argc, char **argv)
{
    xmp_context c;
    struct xmp_module_info mi;
    struct xmp_frame_info fi;
    FILE *out;
    int rate = argc > 3 ? atoi(argv[3]) : 22050;
    unsigned long frames = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: xmp-render MODULE OUT.raw [RATE]\n");
        return 10;
    }
    c = xmp_create_context();
    if (xmp_load_module(c, argv[1]) != 0) {
        fprintf(stderr, "xmp-render: %s isn't a module libxmp %s knows\n", argv[1], xmp_version);
        xmp_free_context(c);
        return 10;
    }
    xmp_get_module_info(c, &mi);
    printf("%s: \"%s\", %s, %d channels\n", argv[1], mi.mod->name, mi.mod->type, mi.mod->chn);
    if (!(out = fopen(argv[2], "wb")) || xmp_start_player(c, rate, 0) != 0) {
        fprintf(stderr, "xmp-render: can't write %s at %d Hz\n", argv[2], rate);
        xmp_release_module(c);
        xmp_free_context(c);
        return 10;
    }
    while (xmp_play_frame(c) == 0) {
        int i;
        const short *s;
        xmp_get_frame_info(c, &fi);
        if (fi.loop_count > 0) break;            /* once through */
        s = fi.buffer;
        for (i = 0; i < fi.buffer_size / 2; i++) {
            putc((s[i] >> 8) & 0xff, out);      /* big-endian on any host */
            putc(s[i] & 0xff, out);
        }
        frames += fi.buffer_size / 4;
    }
    printf("%lu frames at %d Hz (%.2f s)\n", frames, rate, (double)frames / rate);
    fclose(out);
    xmp_end_player(c);
    xmp_release_module(c);
    xmp_free_context(c);
    return 0;
}
