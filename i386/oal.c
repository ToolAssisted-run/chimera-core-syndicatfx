/* oal.c - the OpenAL subset bfsoundlib's drv_oal.c uses, for the core: a deterministic software mixer.
 *
 * Buffers keep a copy of their PCM; sources play their queue at the buffer's rate times the pitch, with
 * the gain; a mono source is heard on both sides (the game centres all its sounds, EQUL_PAN, and the
 * original's card played them in mono), so the position is not used. Nothing plays by itself: oal_render() mixes the
 * next N frames of every playing source (44100 Hz stereo, integer arithmetic), which is the core's step
 * - playback, and so AL_BUFFERS_PROCESSED, advances only there, in the game's own time. */
#include <stdlib.h>
#include <string.h>
#include <AL/al.h>
#include <AL/alc.h>
#include "oal.h"

#define MAX_BUFFERS 256     /* drv_oal.c: 3 per source */
#define MAX_SOURCES 80      /* 64 samples, 3 sequences, the Ogg stream */
#define QUEUE_LEN 64

typedef struct { int used, bits, channels; unsigned freq, frames, cap; unsigned char *data; } Buf;
typedef struct {
    int used; ALint state; int looping;
    ALuint queue[QUEUE_LEN]; int qhead, qcount, processed;   /* queue[qhead..], the first 'processed' done */
    unsigned long long pos;                                  /* position in the current buffer, 32.32 frames */
    int gain_q16, pitch_q16;
} Src;
static Buf bufs[MAX_BUFFERS + 1];
static Src srcs[MAX_SOURCES + 1];
static ALenum err;
static int device_open;
static char device_dummy, context_dummy;

int oal_active(void) { return device_open; }

/* ---------------------------------------------------------------- devices and contexts */
ALCdevice *alcOpenDevice(const ALCchar *name) { (void)name; device_open = 1; return (ALCdevice *)&device_dummy; }
ALCboolean alcCloseDevice(ALCdevice *d) { (void)d; device_open = 0; return ALC_TRUE; }
ALCcontext *alcCreateContext(ALCdevice *d, const ALCint *a) { (void)d; (void)a; return (ALCcontext *)&context_dummy; }
void alcDestroyContext(ALCcontext *c) { (void)c; }
ALCcontext *alcGetCurrentContext(void) { return (ALCcontext *)&context_dummy; }
ALCenum alcGetError(ALCdevice *d) { (void)d; return ALC_NO_ERROR; }
ALCboolean alcMakeContextCurrent(ALCcontext *c) { (void)c; return ALC_TRUE; }
ALenum alGetError(void) { ALenum e = err; err = AL_NO_ERROR; return e; }

/* ---------------------------------------------------------------- buffers */
void alGenBuffers(ALsizei n, ALuint *out)
{
    for (ALsizei i = 0; i < n; i++) {
        out[i] = 0;
        for (ALuint b = 1; b <= MAX_BUFFERS; b++) if (!bufs[b].used) { bufs[b].used = 1; bufs[b].frames = 0; out[i] = b; break; }
        if (!out[i]) err = AL_OUT_OF_MEMORY;
    }
}
void alDeleteBuffers(ALsizei n, const ALuint *b)
{
    for (ALsizei i = 0; i < n; i++) if (b[i] && b[i] <= MAX_BUFFERS) bufs[b[i]].used = 0;   /* the storage stays for the next */
}
void alBufferData(ALuint b, ALenum format, const ALvoid *data, ALsizei size, ALsizei freq)
{
    if (!b || b > MAX_BUFFERS || !bufs[b].used) { err = AL_INVALID_NAME; return; }
    Buf *bf = &bufs[b];
    bf->bits = (format == AL_FORMAT_MONO16 || format == AL_FORMAT_STEREO16) ? 16 : 8;
    bf->channels = (format == AL_FORMAT_STEREO8 || format == AL_FORMAT_STEREO16) ? 2 : 1;
    bf->freq = (unsigned)freq;
    unsigned fb = (unsigned)(bf->bits / 8 * bf->channels);
    bf->frames = fb ? (unsigned)size / fb : 0;
    if ((unsigned)size > bf->cap) { free(bf->data); bf->data = malloc((size_t)size); bf->cap = bf->data ? (unsigned)size : 0; }
    if (!bf->data) { bf->frames = 0; err = AL_OUT_OF_MEMORY; return; }
    if (size) memcpy(bf->data, data, (size_t)size);
}

/* ---------------------------------------------------------------- sources */
static Src *src(ALuint s) { return s && s <= MAX_SOURCES && srcs[s].used ? &srcs[s] : NULL; }
static int processed(const Src *p) { return p->state == AL_STOPPED ? p->qcount : p->processed; }   /* a stopped source's are all */
void alGenSources(ALsizei n, ALuint *out)
{
    for (ALsizei i = 0; i < n; i++) {
        out[i] = 0;
        for (ALuint k = 1; k <= MAX_SOURCES; k++) if (!srcs[k].used) {
            memset(&srcs[k], 0, sizeof srcs[k]); srcs[k].used = 1; srcs[k].state = AL_INITIAL;
            srcs[k].gain_q16 = 65536; srcs[k].pitch_q16 = 65536; out[i] = k; break;
        }
        if (!out[i]) err = AL_OUT_OF_MEMORY;
    }
}
void alDeleteSources(ALsizei n, const ALuint *s) { for (ALsizei i = 0; i < n; i++) if (src(s[i])) srcs[s[i]].used = 0; }
void alSourceQueueBuffers(ALuint s, ALsizei n, const ALuint *b)
{
    Src *p = src(s); if (!p) { err = AL_INVALID_NAME; return; }
    for (ALsizei i = 0; i < n && p->qcount < QUEUE_LEN; i++) p->queue[(p->qhead + p->qcount++) % QUEUE_LEN] = b[i];
}
void alSourceUnqueueBuffers(ALuint s, ALsizei n, ALuint *b)
{
    Src *p = src(s); if (!p) { err = AL_INVALID_NAME; return; }
    if (n > processed(p)) { err = AL_INVALID_VALUE; return; }
    for (ALsizei i = 0; i < n; i++) {
        b[i] = p->queue[p->qhead]; p->qhead = (p->qhead + 1) % QUEUE_LEN; p->qcount--;
        if (p->processed > 0) p->processed--;
    }
}
void alSourcePlay(ALuint s)
{
    Src *p = src(s); if (!p) { err = AL_INVALID_NAME; return; }
    if (p->state == AL_PAUSED) { p->state = AL_PLAYING; return; }
    if (p->state == AL_PLAYING) return;
    p->state = p->processed < p->qcount ? AL_PLAYING : AL_STOPPED;
    p->pos = 0;
}
void alSourceStop(ALuint s)
{
    Src *p = src(s); if (!p) { err = AL_INVALID_NAME; return; }
    p->state = AL_STOPPED; p->processed = p->qcount; p->pos = 0;
}
void alSourcePause(ALuint s) { Src *p = src(s); if (p && p->state == AL_PLAYING) p->state = AL_PAUSED; }
void alSourcef(ALuint s, ALenum param, ALfloat v)
{
    Src *p = src(s); if (!p) { err = AL_INVALID_NAME; return; }
    if (param == AL_GAIN) p->gain_q16 = (int)(v * 65536.0f + 0.5f);
    else if (param == AL_PITCH) p->pitch_q16 = (int)(v * 65536.0f + 0.5f);
}
void alSourcei(ALuint s, ALenum param, ALint v)
{
    Src *p = src(s); if (!p) { err = AL_INVALID_NAME; return; }
    if (param == AL_LOOPING) p->looping = v != 0;
    else if (param == AL_BUFFER) { p->qhead = 0; p->qcount = 0; p->processed = 0; if (v) { p->queue[0] = (ALuint)v; p->qcount = 1; } }
}
void alSource3f(ALuint s, ALenum param, ALfloat x, ALfloat y, ALfloat z)
{
    (void)param; (void)x; (void)y; (void)z;
    if (!src(s)) err = AL_INVALID_NAME;
}
void alGetSourcei(ALuint s, ALenum param, ALint *v)
{
    Src *p = src(s); if (!p) { err = AL_INVALID_NAME; *v = 0; return; }
    if (param == AL_SOURCE_STATE) *v = p->state;
    else if (param == AL_BUFFERS_PROCESSED) *v = processed(p);
    else if (param == AL_BUFFERS_QUEUED) *v = p->qcount;
    else if (param == AL_LOOPING) *v = p->looping;
    else *v = 0;
}
void alGetSourcef(ALuint s, ALenum param, ALfloat *v)
{
    Src *p = src(s); if (!p) { err = AL_INVALID_NAME; *v = 0; return; }
    *v = param == AL_GAIN ? p->gain_q16 / 65536.0f : param == AL_PITCH ? p->pitch_q16 / 65536.0f : 0;
}

/* ---------------------------------------------------------------- the mixer */
static int sample_at(const Buf *b, unsigned frame, int ch)
{
    if (frame >= b->frames) frame = b->frames ? b->frames - 1 : 0;
    unsigned idx = frame * (unsigned)b->channels + (unsigned)(b->channels > 1 ? ch : 0);
    if (b->bits == 16) { short v; memcpy(&v, b->data + 2 * idx, 2); return v; }
    return ((int)b->data[idx] - 128) << 8;   /* 8-bit PCM is unsigned */
}
void oal_render(short *out, int frames)
{
    static int mix[2 * OAL_MAX_FRAMES];
    if (frames > OAL_MAX_FRAMES) frames = OAL_MAX_FRAMES;
    memset(mix, 0, sizeof(int) * 2 * (size_t)frames);
    for (int k = 1; k <= MAX_SOURCES; k++) {
        Src *p = &srcs[k];
        if (!p->used || p->state != AL_PLAYING) continue;
        int g = p->gain_q16;
        for (int f = 0; f < frames; f++) {
            if (p->processed >= p->qcount) {                      /* starved: stops, as OpenAL does */
                if (p->looping && p->qcount) { p->processed = 0; p->pos = 0; } else { p->state = AL_STOPPED; break; }
            }
            const Buf *b = &bufs[p->queue[(p->qhead + p->processed) % QUEUE_LEN]];
            unsigned fr = (unsigned)(p->pos >> 32);
            if (!b->used || fr >= b->frames) { p->processed++; p->pos = 0; f--; continue; }
            unsigned frac = (unsigned)(p->pos >> 16) & 0xFFFF;
            int l0 = sample_at(b, fr, 0), l1 = sample_at(b, fr + 1, 0);
            int r0 = sample_at(b, fr, 1), r1 = sample_at(b, fr + 1, 1);
            int l = l0 + (int)(((long long)(l1 - l0) * frac) >> 16), r = r0 + (int)(((long long)(r1 - r0) * frac) >> 16);
            mix[2 * f] += (int)(((long long)l * g) >> 16);
            mix[2 * f + 1] += (int)(((long long)r * g) >> 16);
            unsigned long long step = ((unsigned long long)b->freq * (unsigned)p->pitch_q16 << 16) / OAL_RATE;
            p->pos += step;
        }
    }
    for (int i = 0; i < 2 * frames; i++) { int v = mix[i]; out[i] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
}
