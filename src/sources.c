/* Copyright (c) 2026 AmiCradle Team. OpenAmigaAudioTools, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * x2iff's decoders behind one interface (x2iff.h). The third-party
 * decoders are fetched at build time (UPSTREAM.json); each is compiled in
 * only when its X2IFF_ flag is set. All of them stream from the file.
 * This is the PC's side; on the Amiga every read and save goes through
 * datatypes instead (dtio.c), and this file isn't built.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "x2iff.h"

#ifdef X2IFF_OGG
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_INTEGER_CONVERSION
#include "stb_vorbis.c"
#endif
#ifdef X2IFF_MP3
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_SIMD
#include "dr_mp3.h"
#endif
#ifdef X2IFF_WAV
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#endif
#ifdef X2IFF_FLAC
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_SIMD
#include "dr_flac.h"
#endif
#ifdef X2IFF_XMP
#define LIBXMP_STATIC
#include <xmp.h>
#endif

#if defined(X2IFF_MP3) || defined(X2IFF_WAV) || defined(X2IFF_FLAC)
static void deinterleave(const float *in, float **out, int ch, int n)
{
    int i, c;
    for (i = 0; i < n; i++)
        for (c = 0; c < ch; c++) out[c][i] = *in++;
}
#endif

static Source *new_source(const char *kind)
{
    Source *s = xcalloc(1, sizeof *s);
    s->kind = kind;
    return s;
}

static void free_source(Source *s)
{
    free(s->ibuf);
    free(s);
}

/* --- Ogg Vorbis: stb_vorbis ---------------------------------------- */
#ifdef X2IFF_OGG
typedef struct {
    stb_vorbis *v;
    stb_vorbis_alloc al;
} Ogg;

static int ogg_read(Source *s, float **out, int n)
{
    return stb_vorbis_get_samples_float(((Ogg *)s->h)->v, s->channels, out, n);
}

static int ogg_rewind(Source *s) { return stb_vorbis_seek_start(((Ogg *)s->h)->v) ? 0 : -1; }

static int ogg_seek(Source *s, unsigned long f) { return stb_vorbis_seek(((Ogg *)s->h)->v, (unsigned)f); }

static void ogg_info(Source *s, FILE *f)
{
    Ogg *g = s->h;
    stb_vorbis_info vi = stb_vorbis_get_info(g->v);
    stb_vorbis_comment cm = stb_vorbis_get_comment(g->v);
    int i;
    fprintf(f, "  decoder memory: %u setup + %u temp bytes, max frame %d\n",
            vi.setup_memory_required, vi.temp_memory_required, vi.max_frame_size);
    if (cm.vendor) fprintf(f, "  vendor: %s\n", cm.vendor);
    for (i = 0; i < cm.comment_list_length; i++) fprintf(f, "  %s\n", cm.comment_list[i]);
}

static void ogg_close(Source *s)
{
    Ogg *g = s->h;
    stb_vorbis_close(g->v);
    free(g->al.alloc_buffer);
    free(g);
    free_source(s);
}

static Source *ogg_open(const char *path)
{
    int err = 0, size;
    Ogg *g = xcalloc(1, sizeof *g);
    Source *s;
    stb_vorbis_info vi;
    /* stb_vorbis takes all its memory from one buffer (no malloc/alloca
     * per frame); grow it until the stream's setup fits. */
    for (size = 256 * 1024; size <= 16 * 1024 * 1024; size *= 2) {
        g->al.alloc_buffer = malloc(size);
        g->al.alloc_buffer_length_in_bytes = size;
        if (!g->al.alloc_buffer) break;
        g->v = stb_vorbis_open_filename(path, &err, &g->al);
        if (g->v) break;
        free(g->al.alloc_buffer);
        g->al.alloc_buffer = NULL;
        if (err != VORBIS_outofmem) break;
    }
    if (!g->v) {
        fprintf(stderr, X2IFF_NAME ": %s: can't read it as Ogg Vorbis (stb_vorbis error %d)\n", path, err);
        free(g);
        return NULL;
    }
    vi = stb_vorbis_get_info(g->v);
    s = new_source("Ogg Vorbis");
    s->h = g;
    s->channels = vi.channels;
    s->rate = vi.sample_rate;
    s->total = stb_vorbis_stream_length_in_samples(g->v);
    s->read = ogg_read;
    s->rewind = ogg_rewind;
    s->seek = ogg_seek;
    s->info = ogg_info;
    s->close = ogg_close;
    return s;
}
#endif

/* --- MP3: dr_mp3 --------------------------------------------------- */
#ifdef X2IFF_MP3
static int mp3_read(Source *s, float **out, int n)
{
    drmp3_uint64 got = drmp3_read_pcm_frames_f32((drmp3 *)s->h, (drmp3_uint64)n, s->ibuf);
    deinterleave(s->ibuf, out, s->channels, (int)got);
    return (int)got;
}

static int mp3_rewind(Source *s) { return drmp3_seek_to_pcm_frame((drmp3 *)s->h, 0) ? 0 : -1; }

static int mp3_seek(Source *s, unsigned long f) { return drmp3_seek_to_pcm_frame((drmp3 *)s->h, f) ? 1 : 0; }

static void mp3_close(Source *s)
{
    drmp3_uninit((drmp3 *)s->h);
    free(s->h);
    free_source(s);
}

static Source *mp3_open(const char *path)
{
    drmp3 *m = xcalloc(1, sizeof *m);   /* big: never on the Amiga's stack */
    Source *s;
    if (!drmp3_init_file(m, path, NULL)) {
        fprintf(stderr, X2IFF_NAME ": %s: can't read it as MP3\n", path);
        free(m);
        return NULL;
    }
    s = new_source("MP3");
    s->h = m;
    s->channels = (int)m->channels;
    s->rate = (long)m->sampleRate;
    /* counting reads the frame headers, not the audio */
    s->total = (unsigned long)drmp3_get_pcm_frame_count(m);
    drmp3_seek_to_pcm_frame(m, 0);
    s->ibuf = xcalloc((size_t)BLOCK * (s->channels > 0 ? s->channels : 1), sizeof(float));
    s->read = mp3_read;
    s->rewind = mp3_rewind;
    s->seek = mp3_seek;
    s->close = mp3_close;
    return s;
}
#endif

/* --- WAV, AIFF, W64, RF64: dr_wav ---------------------------------- */
#ifdef X2IFF_WAV
static int wav_read(Source *s, float **out, int n)
{
    drwav_uint64 got = drwav_read_pcm_frames_f32((drwav *)s->h, (drwav_uint64)n, s->ibuf);
    deinterleave(s->ibuf, out, s->channels, (int)got);
    return (int)got;
}

static int wav_rewind(Source *s) { return drwav_seek_to_pcm_frame((drwav *)s->h, 0) ? 0 : -1; }

static int wav_seek(Source *s, unsigned long f) { return drwav_seek_to_pcm_frame((drwav *)s->h, f) ? 1 : 0; }

static void wav_info(Source *s, FILE *f)
{
    drwav *w = s->h;
    static const char *const cont[] = { "RIFF", "RIFX", "W64", "RF64", "AIFF" };
    fprintf(f, "  container %s, format tag 0x%04x, %u bits per sample\n",
            (unsigned)w->container < 5 ? cont[w->container] : "?",
            (unsigned)w->translatedFormatTag, (unsigned)w->bitsPerSample);
}

static void wav_close(Source *s)
{
    drwav_uninit((drwav *)s->h);
    free(s->h);
    free_source(s);
}

static Source *wav_open(const char *path)
{
    drwav *w = xcalloc(1, sizeof *w);
    Source *s;
    if (!drwav_init_file(w, path, NULL)) {
        fprintf(stderr, X2IFF_NAME ": %s: can't read it as WAV, AIFF, W64 or RF64\n", path);
        free(w);
        return NULL;
    }
    s = new_source(w->container == drwav_container_aiff ? "AIFF" : "WAV");
    s->h = w;
    s->channels = (int)w->channels;
    s->rate = (long)w->sampleRate;
    s->total = (unsigned long)w->totalPCMFrameCount;
    s->ibuf = xcalloc((size_t)BLOCK * (s->channels > 0 ? s->channels : 1), sizeof(float));
    s->read = wav_read;
    s->rewind = wav_rewind;
    s->seek = wav_seek;
    s->info = wav_info;
    s->close = wav_close;
    return s;
}
#endif

/* --- FLAC: dr_flac ------------------------------------------------- */
#ifdef X2IFF_FLAC
static int flac_read(Source *s, float **out, int n)
{
    drflac_uint64 got = drflac_read_pcm_frames_f32((drflac *)s->h, (drflac_uint64)n, s->ibuf);
    deinterleave(s->ibuf, out, s->channels, (int)got);
    return (int)got;
}

static int flac_rewind(Source *s) { return drflac_seek_to_pcm_frame((drflac *)s->h, 0) ? 0 : -1; }

static int flac_seek(Source *s, unsigned long f) { return drflac_seek_to_pcm_frame((drflac *)s->h, f) ? 1 : 0; }

static void flac_info(Source *s, FILE *f)
{
    drflac *d = s->h;
    fprintf(f, "  %u bits per sample, max block %u frames\n",
            (unsigned)d->bitsPerSample, (unsigned)d->maxBlockSizeInPCMFrames);
}

static void flac_close(Source *s)
{
    drflac_close((drflac *)s->h);
    free_source(s);
}

static Source *flac_open(const char *path)
{
    drflac *d = drflac_open_file(path, NULL);
    Source *s;
    if (!d) {
        fprintf(stderr, X2IFF_NAME ": %s: can't read it as FLAC\n", path);
        return NULL;
    }
    s = new_source("FLAC");
    s->h = d;
    s->channels = (int)d->channels;
    s->rate = (long)d->sampleRate;
    s->total = (unsigned long)d->totalPCMFrameCount;
    s->ibuf = xcalloc((size_t)BLOCK * (s->channels > 0 ? s->channels : 1), sizeof(float));
    s->read = flac_read;
    s->rewind = flac_rewind;
    s->seek = flac_seek;
    s->info = flac_info;
    s->close = flac_close;
    return s;
}
#endif

/* --- tracker modules: libxmp --------------------------------------- */
/* libxmp renders at the rate and in the channels it is asked for, so the
 * converter neither resamples nor mixes down after it. */
#ifdef X2IFF_XMP
typedef struct {
    xmp_context c;
    const Opts *o;
    long rate;
    int flags;
    const short *pcm;   /* what is left of the last frame */
    int left;           /* in frames */
    int done;
} Xmp;

static int xmp_begin(Source *s)
{
    Xmp *x = s->h;
    if (xmp_start_player(x->c, (int)x->rate, x->flags)) return -1;
    xmp_set_player(x->c, XMP_PLAYER_INTERP, x->o->interp);
    if (x->o->separation >= 0) xmp_set_player(x->c, XMP_PLAYER_MIX, x->o->separation);
    if (x->o->paula) xmp_set_player(x->c, XMP_PLAYER_FLAGS, XMP_FLAGS_A500);
    x->left = 0;
    x->done = 0;
    return 0;
}

static int xmp_read(Source *s, float **out, int n)
{
    Xmp *x = s->h;
    int got = 0, c, ch = s->channels;
    while (got < n) {
        if (!x->left) {
            struct xmp_frame_info fi;
            if (x->done || xmp_play_frame(x->c) != 0) break;
            xmp_get_frame_info(x->c, &fi);
            if (fi.loop_count >= x->o->loops) { x->done = 1; break; }
            x->pcm = fi.buffer;
            x->left = fi.buffer_size / (2 * ch);
        }
        while (x->left && got < n) {
            for (c = 0; c < ch; c++) out[c][got] = *x->pcm++ * (1.0f / 32768.0f);
            x->left--;
            got++;
        }
    }
    return got;
}

static int xmp_rewind_src(Source *s)
{
    xmp_end_player(((Xmp *)s->h)->c);
    return xmp_begin(s);
}

static void xmp_info_src(Source *s, FILE *f)
{
    struct xmp_module_info mi;
    xmp_get_module_info(((Xmp *)s->h)->c, &mi);
    fprintf(f, "  \"%s\": %s, %d channels, %d patterns, %d instruments, %d samples\n",
            mi.mod->name, mi.mod->type, mi.mod->chn, mi.mod->pat, mi.mod->ins, mi.mod->smp);
}

static void xmp_close_src(Source *s)
{
    Xmp *x = s->h;
    xmp_end_player(x->c);
    xmp_release_module(x->c);
    xmp_free_context(x->c);
    free(x);
    free_source(s);
}

static Source *xmp_open(const char *path, const Opts *o, long rate)
{
    Xmp *x = xcalloc(1, sizeof *x);
    Source *s;
    struct xmp_frame_info fi;
    x->c = xmp_create_context();
    x->o = o;
    x->rate = rate ? rate : 44100;
    x->flags = o->mono ? XMP_FORMAT_MONO : 0;
    if (!x->c || xmp_load_module(x->c, path) != 0) {
        fprintf(stderr, X2IFF_NAME ": %s: libxmp doesn't know it as a module\n", path);
        if (x->c) xmp_free_context(x->c);
        free(x);
        return NULL;
    }
    s = new_source("module");
    s->h = x;
    s->channels = o->mono ? 1 : 2;
    s->rendered_mono = o->mono;
    s->rate = x->rate;
    if (xmp_begin(s)) {
        fprintf(stderr, X2IFF_NAME ": %s: libxmp can't play it at %ld Hz\n", path, x->rate);
        xmp_release_module(x->c);
        xmp_free_context(x->c);
        free(x);
        free_source(s);
        return NULL;
    }
    xmp_get_frame_info(x->c, &fi);
    s->total = (unsigned long)((double)fi.total_time * o->loops * x->rate / 1000.0);
    s->read = xmp_read;
    s->rewind = xmp_rewind_src;
    s->info = xmp_info_src;
    s->close = xmp_close_src;
    return s;
}

/* The tracker formats' names, as extensions (song.mod) or as the Amiga's
 * prefixes (mod.song). */
static const char *const mod_names[] = {
    "mod", "xm", "s3m", "it", "med", "mmd0", "mmd1", "mmd2", "mmd3", "mtm", "669",
    "far", "stm", "ult", "okt", "okta", "ptm", "amf", "dbm", "digi", "dsm", "emod",
    "fnk", "gdm", "ims", "imf", "liq", "mdl", "mgt", "psm", "rtm", "sfx", "stx",
    "sym", "umx", "j2b", "mfp", "abk", "nt", "st26", "flt", "wow", "dtm", "coco",
    "mus", "hmn", "pt36", "fc", "gmc", "ice", "xmf", "chip", "tcb", NULL
};
#endif

/* --- picking a decoder --------------------------------------------- */

static int ext_is(const char *name, const char *const *list)
{
    const char *d = strrchr(name, '.');
    char e[12];
    size_t i, n;
    if (!d || (n = strlen(d + 1)) == 0 || n >= sizeof e) return 0;
    for (i = 0; i <= n; i++) e[i] = (char)tolower((unsigned char)d[1 + i]);
    for (i = 0; list[i]; i++) if (!strcmp(e, list[i])) return 1;
    return 0;
}

static int takes_ext(const char *name);

size_t src_prefix_len(const char *name)
{
#ifdef X2IFF_XMP
    size_t i, n, k;
    if (takes_ext(name)) return 0;     /* it.song.xm is a .xm */
    for (i = 0; mod_names[i]; i++) {
        n = strlen(mod_names[i]);
        if (strlen(name) <= n + 1 || name[n] != '.') continue;
        for (k = 0; k < n && tolower((unsigned char)name[k]) == mod_names[i][k]; k++) ;
        if (k == n) return n + 1;
    }
#endif
    (void)name;
    return 0;
}

static int takes_ext(const char *name)
{
#ifdef X2IFF_OGG
    static const char *const ogg[] = { "ogg", "oga", NULL };
    if (ext_is(name, ogg)) return 1;
#endif
#ifdef X2IFF_MP3
    static const char *const mp3[] = { "mp3", NULL };
    if (ext_is(name, mp3)) return 1;
#endif
#ifdef X2IFF_FLAC
    static const char *const flac[] = { "flac", NULL };
    if (ext_is(name, flac)) return 1;
#endif
#ifdef X2IFF_WAV
    /* AIFF is also what the converter writes: in x2iff, AIFF input is
     * taken one file at a time, so a batch never converts its own output */
# if defined(X2IFF_OGG) || defined(X2IFF_MP3) || defined(X2IFF_FLAC) || defined(X2IFF_XMP)
    static const char *const wav[] = { "wav", "w64", "rf64", NULL };
# else
    static const char *const wav[] = { "wav", "w64", "rf64", "aif", "aiff", "aifc", NULL };
# endif
    if (ext_is(name, wav)) return 1;
#endif
#ifdef X2IFF_XMP
    if (ext_is(name, mod_names)) return 1;
#endif
    (void)name;
    return 0;
}

int src_takes_name(const char *name)
{
    return takes_ext(name) || src_prefix_len(name) > 0;
}

const char *src_kinds(void)
{
    return ""
#ifdef X2IFF_OGG
        " Ogg-Vorbis"
#endif
#ifdef X2IFF_MP3
        " MP3"
#endif
#ifdef X2IFF_WAV
        " WAV/AIFF/W64/RF64"
#endif
#ifdef X2IFF_FLAC
        " FLAC"
#endif
#ifdef X2IFF_XMP
        " MOD/XM/S3M/IT/MED/..."
#endif
        ;
}

Source *src_open(const char *path, const Opts *o, long want_rate)
{
    unsigned char m[16];
    size_t n = 0;
    int tried = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, X2IFF_NAME ": can't open %s\n", path);
        return NULL;
    }
    memset(m, 0, sizeof m);
    n = fread(m, 1, sizeof m, f);
    fclose(f);
    (void)n; (void)o; (void)want_rate; (void)tried;

#ifdef X2IFF_OGG
    if (!memcmp(m, "OggS", 4)) return ogg_open(path);
    tried++;
#endif
#ifdef X2IFF_FLAC
    if (!memcmp(m, "fLaC", 4)) return flac_open(path);
    tried++;
#endif
#ifdef X2IFF_WAV
    if (!memcmp(m, "RIFF", 4) || !memcmp(m, "RIFX", 4) || !memcmp(m, "RF64", 4) ||
        !memcmp(m, "riff", 4) || (!memcmp(m, "FORM", 4) && (!memcmp(m + 8, "AIFF", 4) || !memcmp(m + 8, "AIFC", 4))))
        return wav_open(path);
    tried++;
#endif
#ifdef X2IFF_MP3
    if (!memcmp(m, "ID3", 3)) return mp3_open(path);
    tried++;
#endif
#ifdef X2IFF_XMP
    {
        struct xmp_test_info ti;
        if (xmp_test_module((char *)path, &ti) == 0) return xmp_open(path, o, want_rate);
    }
    tried++;
#endif
#ifdef X2IFF_MP3
    if (m[0] == 0xff && (m[1] & 0xe0) == 0xe0) return mp3_open(path);
#endif
    /* a program with one decoder says why it can't */
#if defined(X2IFF_OGG) && !defined(X2IFF_MP3) && !defined(X2IFF_WAV) && !defined(X2IFF_FLAC) && !defined(X2IFF_XMP)
    return ogg_open(path);
#elif defined(X2IFF_MP3) && !defined(X2IFF_OGG) && !defined(X2IFF_WAV) && !defined(X2IFF_FLAC) && !defined(X2IFF_XMP)
    return mp3_open(path);
#elif defined(X2IFF_WAV) && !defined(X2IFF_OGG) && !defined(X2IFF_MP3) && !defined(X2IFF_FLAC) && !defined(X2IFF_XMP)
    return wav_open(path);
#elif defined(X2IFF_FLAC) && !defined(X2IFF_OGG) && !defined(X2IFF_MP3) && !defined(X2IFF_WAV) && !defined(X2IFF_XMP)
    return flac_open(path);
#else
    fprintf(stderr, X2IFF_NAME ": %s: not a format this program reads (it reads%s)\n", path, src_kinds());
    return NULL;
#endif
}
