/* oal.h - the core's OpenAL (oal.c): the mix of the playing sources, rendered on demand. */
#ifndef OAL_H
#define OAL_H
#define OAL_RATE 44100
#define OAL_MAX_FRAMES 65536
int oal_active(void);                    /* a device is open (bfsoundlib started sound) */
void oal_render(short *out, int frames); /* the next frames, interleaved stereo */
#endif
