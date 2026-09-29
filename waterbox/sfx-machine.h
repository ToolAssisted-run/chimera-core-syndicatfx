/* sfx-machine.h - the syndicatfx machine: the translated i386 program, its 32-bit arena, a Linux i386
 * syscall layer over an in-memory file system, and the step boundary at the game's input read.
 * Plain C over guest memory only (used by the native reference and the miniBox guest alike). */
#ifndef SFX_MACHINE_H
#define SFX_MACHINE_H
#include <stdint.h>
#define HOSTCALL_NO_I386
#include "../i386/hostcall.h"

typedef struct SfxFile { const char *name; const uint8_t *data; uint32_t size; } SfxFile;  /* "data/game01.dat" */

#define SFX_STEP_US 62500u          /* one step = 1/16 s: the port's pacing (game_update, 16 fps) */
#define SFX_AUDIO_MAX 65536         /* sound frames a step can have */

int  sfx_init(const SfxFile *files, int nfiles, int argc, const char *const *argv);
int  sfx_step(const HcInput *in);   /* runs to the next input read: 0 ok, 1 program ended, -1 fatal */
const char *sfx_error(void);

typedef struct SfxVideo { int w, h; const uint8_t *pixels; const uint8_t *palette; /* 256 x RGBA */ } SfxVideo;
const SfxVideo *sfx_video(void);    /* the last presented picture */
int  sfx_input_was_read(void);      /* the step ended at an input read (always, unless the program ended) */
uint64_t sfx_steps(void);
uint64_t sfx_step_us(void);         /* the virtual length of the step just run: >= SFX_STEP_US */
const int16_t *sfx_audio(int *frames);
void sfx_sound_counts(uint64_t *ticks, uint64_t *fm_writes, uint32_t *pit_divisor);   /* diagnostics */   /* its sound: 44100 Hz stereo, exactly the step's time (silence without) */
uint64_t sfx_turns(void);           /* game loop turns so far */
uint64_t sfx_cycles(void);          /* translated instructions executed */
uint8_t *sfx_arena(void);           /* the 32-bit guest address space */
uint32_t sfx_arena_size(void);
uint32_t sfx_level_block(uint32_t *len);   /* guest address of the level block (0 before the first turn) */
extern void (*sfx_on_turn)(uint64_t turn, const uint8_t *block, uint32_t len);   /* optional (verification) */
extern void (*sfx_on_log)(const char *text, uint32_t len);                        /* optional: stdout/stderr */
extern void (*sfx_on_fm_write)(uint64_t pit_clock, uint16_t reg, uint8_t val, int in_interrupt);   /* optional */
#endif
