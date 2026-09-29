/* vorbisfile.h - the part of libvorbisfile's API bfsoundlib compiles against. The core build has no
 * Ogg music (the CD audio replacement tracks): every call fails. */
#ifndef STUB_VORBISFILE_H
#define STUB_VORBISFILE_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
typedef int64_t ogg_int64_t;
typedef struct vorbis_info { int version; int channels; long rate; long bitrate_upper, bitrate_nominal, bitrate_lower, bitrate_window; void *codec_setup; } vorbis_info;
typedef struct { size_t (*read_func)(void *, size_t, size_t, void *); int (*seek_func)(void *, ogg_int64_t, int);
                 int (*close_func)(void *); long (*tell_func)(void *); } ov_callbacks;
static const ov_callbacks OV_CALLBACKS_DEFAULT = {0, 0, 0, 0};
typedef struct OggVorbis_File { void *datasource; char opaque[1024]; } OggVorbis_File;
int ov_open_callbacks(void *datasource, OggVorbis_File *vf, const char *initial, long ibytes, ov_callbacks callbacks);
vorbis_info *ov_info(OggVorbis_File *vf, int link);
long ov_read(OggVorbis_File *vf, char *buffer, int length, int bigendianp, int word, int sgned, int *bitstream);
int ov_clear(OggVorbis_File *vf);
int ov_pcm_seek(OggVorbis_File *vf, ogg_int64_t pos);
#endif
