/* audio_stubs.c - OpenAL and vorbisfile as seen by bfsoundlib in the core build: there is no device,
 * so bfsoundlib reports sound as unavailable (the game runs as with -s). */
#include <stddef.h>
#include <AL/al.h>
#include <AL/alc.h>
#include <vorbis/vorbisfile.h>
ALCdevice *alcOpenDevice(const ALCchar *name) { (void)name; return NULL; }
ALCboolean alcCloseDevice(ALCdevice *d) { (void)d; return ALC_TRUE; }
ALCcontext *alcCreateContext(ALCdevice *d, const ALCint *a) { (void)d; (void)a; return NULL; }
void alcDestroyContext(ALCcontext *c) { (void)c; }
ALCcontext *alcGetCurrentContext(void) { return NULL; }
ALCenum alcGetError(ALCdevice *d) { (void)d; return ALC_INVALID_DEVICE; }
ALCboolean alcMakeContextCurrent(ALCcontext *c) { (void)c; return ALC_FALSE; }
void alBufferData(ALuint b, ALenum f, const ALvoid *d, ALsizei s, ALsizei r) { (void)b; (void)f; (void)d; (void)s; (void)r; }
void alDeleteBuffers(ALsizei n, const ALuint *b) { (void)n; (void)b; }
void alDeleteSources(ALsizei n, const ALuint *s) { (void)n; (void)s; }
void alGenBuffers(ALsizei n, ALuint *b) { for (ALsizei i = 0; i < n; i++) b[i] = 0; }
void alGenSources(ALsizei n, ALuint *s) { for (ALsizei i = 0; i < n; i++) s[i] = 0; }
ALenum alGetError(void) { return AL_INVALID_OPERATION; }
void alGetSourcef(ALuint s, ALenum p, ALfloat *v) { (void)s; (void)p; *v = 0; }
void alGetSourcei(ALuint s, ALenum p, ALint *v) { (void)s; (void)p; *v = 0; }
void alSourcef(ALuint s, ALenum p, ALfloat v) { (void)s; (void)p; (void)v; }
void alSourcei(ALuint s, ALenum p, ALint v) { (void)s; (void)p; (void)v; }
void alSource3f(ALuint s, ALenum p, ALfloat a, ALfloat b, ALfloat c) { (void)s; (void)p; (void)a; (void)b; (void)c; }
void alSourcePause(ALuint s) { (void)s; }
void alSourcePlay(ALuint s) { (void)s; }
void alSourceStop(ALuint s) { (void)s; }
void alSourceQueueBuffers(ALuint s, ALsizei n, const ALuint *b) { (void)s; (void)n; (void)b; }
void alSourceUnqueueBuffers(ALuint s, ALsizei n, ALuint *b) { (void)s; (void)n; (void)b; }
int ov_open_callbacks(void *ds, OggVorbis_File *vf, const char *i, long n, ov_callbacks cb) { (void)ds; (void)vf; (void)i; (void)n; (void)cb; return -1; }
vorbis_info *ov_info(OggVorbis_File *vf, int link) { (void)vf; (void)link; return NULL; }
long ov_read(OggVorbis_File *vf, char *b, int l, int be, int w, int s, int *bs) { (void)vf; (void)b; (void)l; (void)be; (void)w; (void)s; (void)bs; return -1; }
int ov_clear(OggVorbis_File *vf) { (void)vf; return 0; }
int ov_pcm_seek(OggVorbis_File *vf, ogg_int64_t pos) { (void)vf; (void)pos; return -1; }
