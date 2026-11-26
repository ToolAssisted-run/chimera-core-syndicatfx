/* syndicatfx-driver.c - see syndicatfx-driver.h. */
#include "syndicatfx-driver.h"
#include "sfx-machine.h"
#include "game-state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A text a person reads is a key of languages/en.json, each of its
 * arguments after a TEXT_ARG (chimera docs/translations.md). TEXT_ARG is a
 * string of its own so the byte after it is never read as more of its hex. */
#define TEXT_ARG "\x1f"

/* the port's own menu texts (built from SyndicatFX's lang/*.po by util/textdat.py, see the Makefile) */
extern const unsigned char sfx_lang_eng[], sfx_lang_fre[], sfx_lang_ita[];
extern const unsigned sfx_lang_eng_size, sfx_lang_fre_size, sfx_lang_ita_size;

static uint8_t buttons[SFX_BUTTON_COUNT];
static uint64_t packed;   /* FrameAdvance's first 64 buttons, ORed in for that step only */
static int32_t axes[SFX_AXIS_COUNT] = {32768, 32768};
static HcInput input;
static int rendering = 1, have_video, ended;
/* Every picture the core gives is 640x480, a mission's size, so the picture
 * keeps one size all the time (owner, 2026-11-26). A menu's 320x200 picture is
 * scaled up to it: each pixel is 2 wide, and its rows are 2 or 3 high in turn
 * (480 / 200 = 2.4), which keeps the 4:3 shape a 320x200 screen had. Only the
 * picture changes: the game still runs its menus at 320x200 (-S below). */
#define OUT_W 640
#define OUT_H 480
static uint32_t bgra[OUT_W * OUT_H];
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
        if (!b) { snprintf(err, errlen, "error.missing_file" TEXT_ARG "%s", fw->name); return -1; }
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
    /* -S: the game runs its menus at their own 320x200 (the picture is scaled
     * up above, not by the game); -s: no sound card */
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
    if (rc) { ended = rc; snprintf(errbuf, sizeof errbuf, "%s", rc < 0 ? sfx_error() : "error.game_ended"); }
    audio = sfx_audio(&audio_n);
    gamestate_from_game(ended);
    v = sfx_video();
    if (rendering) {
        uint32_t pal[256];
        for (int c = 0; c < 256; c++) pal[c] = 0xFF000000u | (uint32_t)v->palette[4 * c] << 16 | (uint32_t)v->palette[4 * c + 1] << 8 | v->palette[4 * c + 2];
        if (v->w == OUT_W && v->h == OUT_H) {
            for (int i = 0; i < OUT_W * OUT_H; i++) bgra[i] = pal[v->pixels[i]];
        } else if (v->w > 0 && v->h > 0 && v->w <= OUT_W && v->h <= OUT_H) {
            /* each picture pixel takes the game pixel it falls on */
            for (int y = 0; y < OUT_H; y++) {
                const uint8_t *src = v->pixels + (size_t)(y * v->h / OUT_H) * v->w;
                uint32_t *dst = bgra + (size_t)y * OUT_W;
                for (int x = 0; x < OUT_W; x++) dst[x] = pal[src[x * v->w / OUT_W]];
            }
        }
        have_video = 1;
    }
}

const uint32_t *drv_video(int *w, int *h) { if (w) *w = OUT_W; if (h) *h = OUT_H; return bgra; }   /* black until the first picture */
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
