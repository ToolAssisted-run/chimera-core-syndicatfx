/* syndicatfx-driver.c - see syndicatfx-driver.h. */
#include "syndicatfx-driver.h"
#include "sfx-machine.h"
#include "game-state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the port's own menu texts (built from SyndicatFX's lang/*.po by util/textdat.py, see the Makefile) */
extern const unsigned char sfx_lang_eng[], sfx_lang_fre[], sfx_lang_ita[];
extern const unsigned sfx_lang_eng_size, sfx_lang_fre_size, sfx_lang_ita_size;

static uint8_t buttons[SFX_BUTTON_COUNT];
static uint64_t packed;   /* FrameAdvance's first 64 buttons, ORed in for that step only */
static int32_t axes[SFX_AXIS_COUNT] = {32768, 32768};
static HcInput input;
static int rendering = 1, have_video, ended;
static uint32_t bgra[640 * 480];
static int vid_w = 320, vid_h = 200;
static const int16_t *audio = (const int16_t *)bgra;   /* the last step's sound (none before a step) */
static int audio_n;
static char errbuf[256];

/* the files the port's installer copies to its "sound" directory as well (util/install) */
static const char *const sound_dir[] = {"INTRO.XMI", "ISNDS-0.DAT", "ISNDS-0.TAB", "ISNDS-1.DAT", "ISNDS-1.TAB", "GSOUND-0.DAT",
    "GSOUND-0.TAB", "SOUND-0.DAT", "SOUND-0.TAB", "SOUND-1.DAT", "SOUND-1.TAB", "SAMPLE.AD", "SAMPLE.OPL", "SYNGAME.XMI"};
#define NSOUND_DIR (int)(sizeof sound_dir / sizeof sound_dir[0])

int drv_init(const char *language, int sound, char *err, size_t errlen)
{
    static SfxFile files[SFX_FIRMWARE_COUNT + 1 + NSOUND_DIR];
    static char sound_names[NSOUND_DIR][48];
    static char names[SFX_FIRMWARE_COUNT + 1][48];
    for (int i = 0; i < SFX_FIRMWARE_COUNT; i++) {
        const SfxFirmware *fw = &sfx_firmware[i];
        size_t size = 0;
        void *b = drv_file_open(fw->name, &size);
        if (!b) { snprintf(err, errlen, "missing game file %s (from the Syndicate Plus CD, SYNDICAT\\DATA)", fw->name); return -1; }
        /* taken as it is: a file of the project's own may stand in for an original (the project pins its hash) */
        void *ro = drv_alloc_readonly(size ? size : 1); memcpy(ro, b, size); free(b);
        snprintf(names[i], sizeof names[i], "data/%s", fw->name);
        for (char *c = names[i]; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
        files[i].name = names[i]; files[i].data = ro; files[i].size = (uint32_t)size;
    }
    const char *arg = "0", *lname = "language/eng/guitext.dat";
    const unsigned char *ldata = sfx_lang_eng; unsigned lsize = sfx_lang_eng_size;
    for (unsigned k = 0; k < sizeof sfx_languages / sizeof sfx_languages[0]; k++)
        if (language && !strcmp(language, sfx_languages[k][0])) arg = sfx_languages[k][1];
    if (!strcmp(arg, "1")) { lname = "language/fre/guitext.dat"; ldata = sfx_lang_fre; lsize = sfx_lang_fre_size; }
    if (!strcmp(arg, "2")) { lname = "language/ita/guitext.dat"; ldata = sfx_lang_ita; lsize = sfx_lang_ita_size; }
    files[SFX_FIRMWARE_COUNT].name = lname; files[SFX_FIRMWARE_COUNT].data = ldata; files[SFX_FIRMWARE_COUNT].size = lsize;
    int nfiles = SFX_FIRMWARE_COUNT + 1;
    for (int k = 0; k < NSOUND_DIR; k++)
        for (int i = 0; i < SFX_FIRMWARE_COUNT; i++) {
            if (strcmp(sfx_firmware[i].name, sound_dir[k])) continue;
            snprintf(sound_names[k], sizeof sound_names[k], "sound/%s", names[i] + 5);
            files[nfiles] = files[i]; files[nfiles++].name = sound_names[k];
        }
    drv_readonly_done();
    /* -S: menus at their own 320x200; -s: no sound card */
    static const char *argv[] = {"syndicatfx", "-c", "0", "-S", "-s"};
    argv[2] = arg;
    if (sfx_init(files, nfiles, sound ? 4 : 5, argv) < 0) { snprintf(err, errlen, "%s", sfx_error()); return -1; }
    memset(&input, 0, sizeof input); memset(buttons, 0, sizeof buttons);
    have_video = 0; ended = 0; audio_n = 0;
    gamestate_from_game(0);
    return 0;
}

void drv_set_button(int index, int level) { if (index >= 0 && index < SFX_BUTTON_COUNT) buttons[index] = level != 0; }
void drv_set_axis(int index, int32_t value)
{
    if (index < 0 || index >= SFX_AXIS_COUNT) return;
    axes[index] = value < 0 ? 0 : value > 65535 ? 65535 : value;
}
void drv_set_rendering(int on) { rendering = on != 0; }
void drv_set_packed(uint64_t bits) { packed = bits; }

void drv_step(void)
{
    if (ended) { audio_n = 0; return; }
    memset(input.keys, 0, sizeof input.keys); input.buttons = 0;
    for (int i = 0; i < SFX_BUTTON_COUNT; i++) {
        if (!buttons[i] && !(i < 64 && ((packed >> i) & 1))) continue;
        if (sfx_buttons[i].mouse) input.buttons |= 1 << sfx_buttons[i].code;
        else input.keys[sfx_buttons[i].code] = 1;
    }
    /* the pointer: axis units over the picture the player is looking at (the last presented one) */
    const SfxVideo *v = sfx_video();
    input.mouse_x = (int)((int64_t)axes[0] * v->w / 65536);
    input.mouse_y = (int)((int64_t)axes[1] * v->h / 65536);
    gamestate_to_game();
    int rc = sfx_step(&input);
    if (rc) { ended = rc; snprintf(errbuf, sizeof errbuf, "%s", rc < 0 ? sfx_error() : "the game ended"); }
    audio = sfx_audio(&audio_n);
    gamestate_from_game(ended);
    v = sfx_video();
    vid_w = v->w; vid_h = v->h;
    if (rendering) {
        uint32_t pal[256];
        for (int c = 0; c < 256; c++) pal[c] = 0xFF000000u | (uint32_t)v->palette[4 * c] << 16 | (uint32_t)v->palette[4 * c + 1] << 8 | v->palette[4 * c + 2];
        for (int i = 0; i < vid_w * vid_h; i++) bgra[i] = pal[v->pixels[i]];
        have_video = 1;
    }
}

const uint32_t *drv_video(int *w, int *h) { if (w) *w = vid_w; if (h) *h = vid_h; return bgra; }   /* black until the first picture */
const int16_t *drv_audio(int *frames) { if (frames) *frames = audio_n; return audio; }
void drv_vsync(int *num, int *den)
{
    uint64_t us = sfx_step_us() ? sfx_step_us() : SFX_STEP_US;
    uint64_t a = 1000000, b = us;
    while (b) { uint64_t t = a % b; a = b; b = t; }
    *num = (int)(1000000 / a); *den = (int)(us / a);
}
int drv_input_was_read(void) { return sfx_input_was_read(); }
uint64_t drv_cycles(void) { return sfx_cycles(); }
int drv_ended(void) { return ended; }
const char *drv_error(void) { return errbuf; }
