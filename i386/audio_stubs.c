/* audio_stubs.c - vorbisfile as seen by bfsoundlib in the core build: the game has no Ogg music (its
 * CD audio option is off), so no stream ever opens. OpenAL is oal.c. */
#include <stddef.h>
#include <vorbis/vorbisfile.h>
int ov_open_callbacks(void *ds, OggVorbis_File *vf, const char *i, long n, ov_callbacks cb) { (void)ds; (void)vf; (void)i; (void)n; (void)cb; return -1; }
vorbis_info *ov_info(OggVorbis_File *vf, int link) { (void)vf; (void)link; return NULL; }
long ov_read(OggVorbis_File *vf, char *b, int l, int be, int w, int s, int *bs) { (void)vf; (void)b; (void)l; (void)be; (void)w; (void)s; (void)bs; return -1; }
int ov_clear(OggVorbis_File *vf) { (void)vf; return 0; }
int ov_pcm_seek(OggVorbis_File *vf, ogg_int64_t pos) { (void)vf; (void)pos; return -1; }
