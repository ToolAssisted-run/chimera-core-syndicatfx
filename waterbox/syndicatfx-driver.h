/* syndicatfx-driver.h - the core's host, shared by the native reference (run-native) and the guest
 * (wbx-entry.c): firmware check, the button/axis wire, the step, picture and sound. */
#ifndef SYNDICATFX_DRIVER_H
#define SYNDICATFX_DRIVER_H
#include <stdint.h>
#include <stddef.h>
#include "sfx-tables.h"

/* supplied by the embedder: open a mounted file by exact name (NULL if absent), and memory for the
 * game files that is never written again (sealed memory in the guest) */
void *drv_file_open(const char *name, size_t *size);   /* returns malloc'd bytes */
void *drv_alloc_readonly(size_t size);
void  drv_readonly_done(void);                           /* all read-only data is written */

int  drv_init(const char *language, int sound, char *err, size_t errlen);
void drv_set_button(int index, int level);
void drv_set_axis(int index, int32_t value);
void drv_set_packed(uint64_t bits);                     /* FrameAdvance's packed buttons, this step only */
void drv_step(void);
void drv_set_rendering(int on);
const uint32_t *drv_video(int *w, int *h);              /* BGRA (black until the first picture) */
const int16_t *drv_audio(int *frames);                  /* stereo frames of the step just run */
void drv_vsync(int *num, int *den);                     /* the length of the step just run */
int  drv_input_was_read(void);
uint64_t drv_cycles(void);
int  drv_ended(void);                                   /* 1 program ended, -1 fatal (message in err of drv_error) */
const char *drv_error(void);
#endif
