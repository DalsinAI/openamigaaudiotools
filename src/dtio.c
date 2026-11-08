/* Copyright (c) 2026 AmiCradle Team. OpenAmigaAudioTools, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * x2iff on the Amiga: every read and every save through datatypes.
 *
 * Read: NewDTObject(GID_SOUND) with OIA_Reader. A datatype that answers
 * OIA_Channels (gfopensound, gfopenmodule, gfopenaiff, gfopenwave) streams
 * its sound with OIM_READSOUND, a piece at a time; any other sound
 * datatype (8svx, the system's) has the sample whole in SDTA_Sample, or
 * SDTA_LeftSample and SDTA_RightSample, as sound.datatype has always kept
 * it: 8-bit, or 16-bit with V44's SDTA_SampleType.
 *
 * Save: a DTST_RAM object of gfopenaiff or gfopenwave.datatype holding the
 * sound (OIA_PCM), written with DTM_WRITE: DTWM_RAW for AIFF, WAV and raw
 * PCM (OIA_SampleBits, OIA_Headerless), DTWM_IFF for 8SVX (sound.datatype's
 * own writer).
 *
 * The tags and the method are datatypes/gfopenimage.h's, from
 * DalsinAI/gfopendatatypes; the copies below are its numbers.
 */
#include <stdlib.h>
#include <string.h>

#include "x2iff.h"

#include <exec/memory.h>
#include <dos/dos.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/soundclass.h>
#include <intuition/classusr.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>

/* datatypes/gfopenimage.h (gfopendatatypes) */
#ifndef OIM_READSOUND
#define OIA_Dummy      (TAG_USER + 0x0DA15000)
#define OIA_Reader     (OIA_Dummy + 7)
#define OIA_MixRate    (OIA_Dummy + 8)
#define OIA_MixInterp  (OIA_Dummy + 9)
#define OIA_Channels   (OIA_Dummy + 10)
#define OIA_PCM        (OIA_Dummy + 11)
#define OIA_SampleBits (OIA_Dummy + 12)
#define OIA_Headerless (OIA_Dummy + 13)
#define OIM_READSOUND  (TAG_USER + 0x0DA15903)
#endif
#ifndef SDTA_SampleType
#define SDTA_SampleType (DTA_Dummy + 500 + 30)
#define SDTA_Frequency  (DTA_Dummy + 500 + 32)
#define SDTST_M8S  0
#define SDTST_S8S  1
#define SDTST_M16S 2
#define SDTST_S16S 3
#endif

struct Library *DataTypesBase;
struct IntuitionBase *IntuitionBase;

static int libs(void)
{
    if (!IntuitionBase) IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    if (!DataTypesBase) DataTypesBase = OpenLibrary("datatypes.library", 39);
    if (!IntuitionBase || !DataTypesBase) {
        fprintf(stderr, X2IFF_NAME ": needs datatypes.library 39 (AmigaOS 3.0) or newer\n");
        return -1;
    }
    return 0;
}

static void dt_error(const char *path, const char *what)
{
    LONG e = IoErr();
    char buf[100];
    if (e >= DTERROR_UNKNOWN_DATATYPE && e <= DTERROR_UNKNOWN_DATATYPE + 99)
        snprintf(buf, sizeof buf, "%s", GetDTString(e));
    else
        Fault(e, NULL, buf, sizeof buf);
    fprintf(stderr, X2IFF_NAME ": %s: %s%s%s\n", path, what, e ? ": " : "", e ? buf : "");
}

/* --- reading ------------------------------------------------------------- */

typedef struct {
    Object *obj;
    char *path;
    ULONG mixRate, interp;
    int stream;                 /* answers OIM_READSOUND */
    /* the whole sample, from a datatype that doesn't stream */
    const void *ch[2];          /* BYTE * or WORD *, per channel; or one interleaved */
    int type;                   /* SDTST_ */
    unsigned long pos;
    WORD *buf;                  /* OIM_READSOUND's frames */
} Dt;

static Object *dt_new(Dt *d)
{
    return NewDTObject((APTR)d->path, DTA_GroupID, GID_SOUND,
                       OIA_Reader, TRUE, OIA_MixRate, d->mixRate, OIA_MixInterp, d->interp, TAG_DONE);
}

/* What the object holds: 0, with channels, rate and frames; -1 if it
 * holds no sound we can read. */
static int dt_describe(Dt *d, int *channels, long *rate, unsigned long *frames)
{
    ULONG ch = 0, len = 0, sps = 0, freq = 0, period = 0, type = 0xffffffffUL;
    BYTE *s = NULL, *l = NULL, *r = NULL;
    struct VoiceHeader *vh = NULL;

    GetDTAttrs(d->obj, SDTA_SampleLength, (ULONG)&len, SDTA_Period, (ULONG)&period,
               SDTA_VoiceHeader, (ULONG)&vh, TAG_DONE);
    GetDTAttrs(d->obj, SDTA_SamplesPerSec, (ULONG)&sps, TAG_DONE);
    GetDTAttrs(d->obj, SDTA_Frequency, (ULONG)&freq, TAG_DONE);
    if (!sps) sps = freq;
    if (!sps && vh) sps = vh->vh_SamplesPerSec;
    if (!sps && period) sps = 3546895 / period;     /* PAL clock */
    d->stream = GetDTAttrs(d->obj, OIA_Channels, (ULONG)&ch, TAG_DONE) == 1 && (ch == 1 || ch == 2);
    if (d->stream) {
        *channels = (int)ch;
        *rate = (long)sps;
        *frames = len;
        return sps ? 0 : -1;
    }
    GetDTAttrs(d->obj, SDTA_Sample, (ULONG)&s, TAG_DONE);
    GetDTAttrs(d->obj, SDTA_LeftSample, (ULONG)&l, SDTA_RightSample, (ULONG)&r, TAG_DONE);
    GetDTAttrs(d->obj, SDTA_SampleType, (ULONG)&type, TAG_DONE);
    if (type > SDTST_S16S) type = SDTST_M8S;
    d->type = (int)type;
    if (type == SDTST_S8S || type == SDTST_S16S) {
        d->ch[0] = s; d->ch[1] = NULL; *channels = 2;   /* interleaved */
    } else if (l && r && l != r) {
        d->ch[0] = l; d->ch[1] = r; *channels = 2;
    } else {
        d->ch[0] = s ? s : (l ? l : r); d->ch[1] = NULL; *channels = 1;
    }
    *rate = (long)sps;
    *frames = len;
    return d->ch[0] && len && sps ? 0 : -1;
}

static int dt_read(Source *s, float **out, int n)
{
    Dt *d = s->h;
    int c, i;
    if (d->stream) {
        ULONG got = DoMethod(d->obj, OIM_READSOUND, NULL, (ULONG)d->buf, (ULONG)n);
        const WORD *p = d->buf;
        for (i = 0; i < (int)got; i++)
            for (c = 0; c < s->channels; c++) out[c][i] = *p++ * (1.0f / 32768.0f);
        return (int)got;
    }
    if (d->pos >= s->total) return 0;
    if ((unsigned long)n > s->total - d->pos) n = (int)(s->total - d->pos);
    for (c = 0; c < s->channels; c++) {
        int wide = d->type == SDTST_M16S || d->type == SDTST_S16S;
        int inter = d->type == SDTST_S8S || d->type == SDTST_S16S;
        const void *base = inter ? d->ch[0] : d->ch[c];
        int step = inter ? 2 : 1, off = inter ? c : 0;
        for (i = 0; i < n; i++) {
            unsigned long k = (d->pos + i) * step + off;
            out[c][i] = wide ? ((const WORD *)base)[k] * (1.0f / 32768.0f)
                             : ((const BYTE *)base)[k] * (1.0f / 128.0f);
        }
    }
    d->pos += n;
    return n;
}

/* A streaming object goes back to the start by being opened again. */
static int dt_rewind(Source *s)
{
    Dt *d = s->h;
    int ch;
    long rate;
    unsigned long frames;
    if (!d->stream) { d->pos = 0; return 0; }
    DisposeDTObject(d->obj);
    if (!(d->obj = dt_new(d)) || dt_describe(d, &ch, &rate, &frames) || ch != s->channels)
        return -1;
    return 0;
}

static void dt_info(Source *s, FILE *f)
{
    Dt *d = s->h;
    struct DataType *dtn = NULL;
    STRPTR by = NULL;
    GetDTAttrs(d->obj, DTA_DataType, (ULONG)&dtn, TAG_DONE);
    GetDTAttrs(d->obj, OIA_Dummy + 1, (ULONG)&by, TAG_DONE);     /* OIA_DecodedBy */
    fprintf(f, "  datatype: %s (%s)%s%s\n",
            dtn && dtn->dtn_Header && dtn->dtn_Header->dth_Name ? (const char *)dtn->dtn_Header->dth_Name : "?",
            dtn && dtn->dtn_Header && dtn->dtn_Header->dth_BaseName ? (const char *)dtn->dtn_Header->dth_BaseName : "?",
            d->stream ? ", streamed" : ", whole", "");
    if (by) fprintf(f, "  decoded by: %s\n", (const char *)by);
}

static void dt_close(Source *s)
{
    Dt *d = s->h;
    if (d->obj) DisposeDTObject(d->obj);
    free(d->buf);
    free(d->path);
    free(d);
    free(s->ibuf);
    free(s);
}

Source *src_open(const char *path, const Opts *o, long want_rate)
{
    Dt *d;
    Source *s;
    int ch;
    long rate;
    unsigned long frames;

    if (libs()) return NULL;
    d = xcalloc(1, sizeof *d);
    d->path = xcalloc(strlen(path) + 1, 1);
    strcpy(d->path, path);
    d->mixRate = want_rate ? (ULONG)want_rate : 44100;
    d->interp = (ULONG)o->interp;
    if (!(d->obj = dt_new(d))) {
        dt_error(path, "no sound datatype opens it");
        free(d->path);
        free(d);
        return NULL;
    }
    if (dt_describe(d, &ch, &rate, &frames)) {
        fprintf(stderr, X2IFF_NAME ": %s: its datatype gives no samples to read\n", path);
        DisposeDTObject(d->obj);
        free(d->path);
        free(d);
        return NULL;
    }
    if (d->stream) d->buf = xcalloc((size_t)BLOCK * ch, sizeof(WORD));
    s = xcalloc(1, sizeof *s);
    s->kind = d->stream ? "sound (datatype, streamed)" : "sound (datatype)";
    s->h = d;
    s->channels = ch;
    s->rate = rate;
    s->total = frames;
    s->read = dt_read;
    s->rewind = dt_rewind;
    s->info = dt_info;
    s->close = dt_close;
    return s;
}

int dt_probe(const char *path, unsigned long *frames, int *channels, long *rate)
{
    Dt d;
    int r;
    if (libs()) return -1;
    memset(&d, 0, sizeof d);
    d.path = (char *)path;
    d.mixRate = 44100;
    d.interp = 1;
    if (!(d.obj = NewDTObject((APTR)path, DTA_GroupID, GID_SOUND, TAG_DONE))) return -1;
    r = dt_describe(&d, channels, rate, frames);
    DisposeDTObject(d.obj);
    return r;
}

/* Batches: every file is offered to the datatypes (src_open says no to
 * what isn't a sound); x2iff.c leaves out its own output's kind. */
int src_takes_name(const char *name)
{
    return name[0] != '.';
}

size_t src_prefix_len(const char *name)
{
    static const char *const pre[] = { "mod.", "med.", "xm.", "s3m.", "it.", "okta.", "digi.", NULL };
    size_t i, n;
    for (i = 0; pre[i]; i++) {
        n = strlen(pre[i]);
        if (strlen(name) > n && !strncasecmp(name, pre[i], n)) return n;
    }
    return 0;
}

const char *src_kinds(void)
{
    return " any sound a datatype opens";
}

/* --- saving --------------------------------------------------------------- */

int dt_save(const char *path, int fmt, int bits, int big_endian, int unsigned8,
            const short *pcm, unsigned long frames, int channels, long rate)
{
    /* x2iff.c's formats: 0 AIFF, 1 8SVX, 2 WAV, 3 raw */
    const char *cls = fmt == 2 ? "gfopenwave" : "gfopenaiff";
    ULONG mode = fmt == 1 ? DTWM_IFF : DTWM_RAW;
    BOOL bare = fmt == 3;
    Object *obj;
    BPTR fh;
    struct TagItem wtags[3];
    ULONG ok;

    if (libs()) return -1;
    if (fmt == 3)                              /* raw: the byte order picks the class */
        cls = (bits == 8 ? unsigned8 : !big_endian) ? "gfopenwave" : "gfopenaiff";
    obj = NewDTObject(NULL, DTA_SourceType, DTST_RAM, DTA_BaseName, (ULONG)cls, DTA_GroupID, GID_SOUND,
                      OIA_PCM, (ULONG)pcm, OIA_Channels, (ULONG)channels,
                      SDTA_SampleLength, frames, SDTA_SamplesPerSec, (ULONG)rate, TAG_DONE);
    if (!obj) {
        dt_error(path, fmt == 1 ? "gfopenaiff.datatype can't hold it for sound.datatype"
                                : "no gfopenaiff or gfopenwave.datatype (GFOpenDatatypes) to save it");
        return -1;
    }
    if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) {
        dt_error(path, "can't write");
        DisposeDTObject(obj);
        return -1;
    }
    wtags[0].ti_Tag = OIA_SampleBits; wtags[0].ti_Data = (ULONG)bits;
    wtags[1].ti_Tag = OIA_Headerless; wtags[1].ti_Data = bare;
    wtags[2].ti_Tag = TAG_DONE;
    ok = DoMethod(obj, DTM_WRITE, NULL, (ULONG)fh, mode, (ULONG)wtags);
    if (!ok) dt_error(path, fmt == 1 ? "sound.datatype didn't write it as 8SVX" : "the datatype didn't write it");
    Close(fh);
    DisposeDTObject(obj);
    if (!ok) {
        DeleteFile((STRPTR)path);
        return -1;
    }
    return 0;
}
