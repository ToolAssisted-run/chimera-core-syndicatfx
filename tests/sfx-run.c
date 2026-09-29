/* sfx-run.c - native test driver for the syndicatfx machine: scripted input per step, screenshots,
 * level-block dumps per game turn (the format of the oracle comparisons: "TURN", turn, step, len, block).
 *
 * usage: sfx-run --data CD_DATA_DIR --lang GUITEXT.DAT [--script FILE] [--steps N] [--turns FILE] [--quiet]
 *                [--sound] [--steplog] [--wav FILE]   (--sound: the Sound Blaster, as the core's default;
 *                --steplog: each step's length; --wav: the sound, 44100 Hz stereo)
 *
 * script lines: key STEP SCANCODE 0|1 | mouse STEP X Y | button STEP left|right|middle 0|1 | shot STEP FILE.ppm | end STEP */
#include "../waterbox/sfx-machine.h"
#include "xlat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>

static FILE *turnlog; static uint64_t cur_step;
static void on_turn(uint64_t t, const uint8_t *b, uint32_t len)
{
    if (!turnlog) return;
    uint32_t h[4]; memcpy(h, "TURN", 4); h[1] = (uint32_t)t; h[2] = (uint32_t)cur_step; h[3] = len;
    fwrite(h, 4, 4, turnlog); fwrite(b, 1, len, turnlog);
}
static int quiet;
/* --calls A B: every hooked function entry during steps A..B, with the guest return address */
static long calls_from, calls_to;
static void calls_hook(uint32_t addr)
{
    if ((long)cur_step >= calls_from && (long)cur_step <= calls_to)
        fprintf(stderr, "step %llu call %08x from %08x (%08x)\n", (unsigned long long)cur_step, addr, rd32(ESP), rd32(ESP + 4));
}
/* --pday: process_day calls per caller (return address) before the first game turn: the menu clock */
static uint32_t pd_last; static long pd_n;
static void pday_hook(uint32_t addr)
{
    (void)addr;
    if (sfx_turns()) return;
    uint32_t ret = rd32(ESP);
    if (ret != pd_last && pd_n) { fprintf(stderr, "pday ret=%08x count=%ld until step %llu\n", pd_last, pd_n, (unsigned long long)cur_step); pd_n = 0; }
    pd_last = ret; pd_n++;
}
static void on_log(const char *s, uint32_t n) { if (!quiet) fwrite(s, 1, n, stderr); }
static uint8_t *slurp(const char *p, uint32_t *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(l ? l : 1); if (fread(b, 1, l, f) != (size_t)l) { fclose(f); free(b); return NULL; }
    fclose(f); *n = (uint32_t)l; return b;
}
static void shot(const char *path)
{
    const SfxVideo *v = sfx_video(); FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", v->w, v->h);
    for (int i = 0; i < v->w * v->h; i++) { const uint8_t *c = v->palette + 4 * v->pixels[i]; fputc(c[0], f); fputc(c[1], f); fputc(c[2], f); }
    fclose(f);
}
typedef struct { long step; int kind, a, b; char path[256]; } Ev;

int main(int argc, char **argv)
{
    const char *data = NULL, *lang = NULL, *script = NULL, *turns = NULL, *wav = NULL; long steps = 1000; int sound = 0, steplog = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data")) data = argv[++i];
        else if (!strcmp(argv[i], "--lang")) lang = argv[++i];
        else if (!strcmp(argv[i], "--script")) script = argv[++i];
        else if (!strcmp(argv[i], "--steps")) steps = atol(argv[++i]);
        else if (!strcmp(argv[i], "--turns")) turns = argv[++i];
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (!strcmp(argv[i], "--sound")) sound = 1;
        else if (!strcmp(argv[i], "--steplog")) steplog = 1;
        else if (!strcmp(argv[i], "--wav")) wav = argv[++i];
        else if (!strcmp(argv[i], "--pday")) xl_hook = pday_hook;
        else if (!strcmp(argv[i], "--calls")) { xl_hook = calls_hook; calls_from = atol(argv[++i]); calls_to = atol(argv[++i]); }
    }
    if (!data || !lang) { fprintf(stderr, "usage: sfx-run --data DIR --lang GUITEXT.DAT ...\n"); return 2; }
    SfxFile files[600]; int nf = 0;
    DIR *d = opendir(data); struct dirent *e;
    while (d && (e = readdir(d)) && nf < 598) {
        if (e->d_name[0] == '.') continue;
        char p[1024], nm[300]; snprintf(p, sizeof p, "%s/%s", data, e->d_name);
        snprintf(nm, sizeof nm, "data/%s", e->d_name); for (char *c = nm; *c; c++) *c = (char)tolower(*c);
        uint32_t n; uint8_t *b = slurp(p, &n); if (!b) continue;
        files[nf].name = strdup(nm); files[nf].data = b; files[nf].size = n; nf++;
        /* the port's installer copies the sound files to "sound/" too (util/install) */
        static const char *const snd[] = {"intro.xmi", "isnds-0.dat", "isnds-0.tab", "isnds-1.dat", "isnds-1.tab", "gsound-0.dat",
            "gsound-0.tab", "sound-0.dat", "sound-0.tab", "sound-1.dat", "sound-1.tab", "sample.ad", "sample.opl", "syngame.xmi"};
        for (unsigned k = 0; k < sizeof snd / sizeof snd[0]; k++)
            if (!strcmp(nm + 5, snd[k])) { files[nf] = files[nf - 1]; snprintf(p, sizeof p, "sound/%s", snd[k]); files[nf++].name = strdup(p); }
    }
    if (d) closedir(d);
    { uint32_t n; uint8_t *b = slurp(lang, &n); if (!b) { fprintf(stderr, "cannot read %s\n", lang); return 2; }
      files[nf].name = "language/eng/guitext.dat"; files[nf].data = b; files[nf].size = n; nf++; }
    Ev evs[4096]; int ne = 0;
    if (script) {
        FILE *f = fopen(script, "r"); char line[512];
        while (f && fgets(line, sizeof line, f) && ne < 4096) {
            char c[32], s1[256] = "", s2[64] = ""; long st;
            if (sscanf(line, "%31s %ld %255s %63s", c, &st, s1, s2) < 2 || c[0] == '#') continue;
            Ev *v = &evs[ne]; v->step = st; v->path[0] = 0;
            if (!strcmp(c, "key")) { v->kind = 0; v->a = atoi(s1); v->b = atoi(s2); }
            else if (!strcmp(c, "mouse")) { v->kind = 1; v->a = atoi(s1); v->b = atoi(s2); }
            else if (!strcmp(c, "button")) { v->kind = 2; v->a = !strcmp(s1, "left") ? 0 : !strcmp(s1, "right") ? 1 : 2; v->b = atoi(s2); }
            else if (!strcmp(c, "shot")) { v->kind = 3; snprintf(v->path, sizeof v->path, "%s", s1); }
            else if (!strcmp(c, "end")) { steps = st; continue; }
            else continue;
            ne++;
        }
        if (f) fclose(f);
    }
    if (turns) turnlog = fopen(turns, "wb");
    sfx_on_turn = on_turn; sfx_on_log = on_log;
    const char *pargv[] = {"syndicatfx", "-c", "0", "-S", "-s"};
    if (sfx_init(files, nf, sound ? 4 : 5, pargv) < 0) { fprintf(stderr, "init: %s\n", sfx_error()); return 1; }
    HcInput in; memset(&in, 0, sizeof in);
    FILE *wf = wav ? fopen(wav, "wb") : NULL; uint32_t wav_frames = 0;
    if (wf) fwrite((uint8_t[44]){0}, 1, 44, wf);
    clock_t t0 = clock(); int rc = 0;
    for (cur_step = 0; (long)cur_step < steps; cur_step++) {
        for (int k = 0; k < ne; k++) {
            Ev *v = &evs[k]; if (v->step != (long)cur_step) continue;
            if (v->kind == 0 && v->a >= 0 && v->a < HC_KEYS) in.keys[v->a] = (unsigned char)v->b;
            else if (v->kind == 1) { in.mouse_x = v->a; in.mouse_y = v->b; }
            else if (v->kind == 2) { if (v->b) in.buttons |= 1 << v->a; else in.buttons &= ~(1 << v->a); }
        }
        rc = sfx_step(&in);
        if (wf) { int n; const int16_t *a = sfx_audio(&n); fwrite(a, 4, (size_t)n, wf); wav_frames += (uint32_t)n; }
        if (steplog) fprintf(stderr, "step %llu us %llu turns %llu instr %llu\n", (unsigned long long)cur_step, (unsigned long long)sfx_step_us(),
                             (unsigned long long)sfx_turns(), (unsigned long long)sfx_cycles());
        for (int k = 0; k < ne; k++) if (evs[k].step == (long)cur_step && evs[k].kind == 3) shot(evs[k].path);
        if (rc) break;
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    fprintf(stderr, "sfx-run: %s after %llu steps, %llu turns, %llu instructions, %.2f s (%.1f steps/s)\n",
            rc < 0 ? sfx_error() : rc > 0 ? "program ended" : "ok", (unsigned long long)sfx_steps(), (unsigned long long)sfx_turns(),
            (unsigned long long)sfx_cycles(), secs, secs > 0 ? sfx_steps() / secs : 0.0);
    if (turnlog) fclose(turnlog);
    if (wf) {
        uint32_t data_len = wav_frames * 4, h[11] = {0x46464952, 36 + data_len, 0x45564157, 0x20746d66, 16, 0x00020001, 44100, 44100 * 4, 0x00100004, 0x61746164, data_len};
        fseek(wf, 0, SEEK_SET); fwrite(h, 4, 11, wf); fclose(wf);
    }
    if (xl_hook && pd_n) fprintf(stderr, "pday ret=%08x count=%ld (last)\n", pd_last, pd_n);
    return rc < 0 ? 1 : 0;
}
