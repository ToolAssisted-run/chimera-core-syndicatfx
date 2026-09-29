/* sfx-machine.c - see sfx-machine.h. */
#include "sfx-machine.h"
#include "coro.h"
#include "xlat.h"
#include "xl_image.h"
#include "opl3.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

/* guest address space (XL_ARENA_BITS = 26: 64 MiB) */
#define HEAP_TOP   0x02000000u      /* brk area ends here */
#define MMAP_BASE  0x02000000u
#define MMAP_TOP   0x03E00000u
#define STACK_TOP  0x03FF0000u
#define PAGE 4096u

extern uint32_t xl_tls_base[8];
void (*sfx_on_turn)(uint64_t, const uint8_t *, uint32_t);
void (*sfx_on_log)(const char *, uint32_t);
void (*sfx_on_fm_write)(uint64_t, uint16_t, uint8_t, int);

enum { ST_READY, ST_RUNNING, ST_ENDED, ST_FATAL };
static int state;
static char errmsg[256];
static Coro prog;
static const HcInput *cur_in;
static uint64_t steps, turns, clock_us, step_icount0, last_step_us, step_reads, step_io_us;
/* Virtual time inside a step: the game's logic never reads the clock (one turn per step), only waits
 * that spin on it do (the FLI player's frame delay). So each read within a step advances the clock by
 * 1 ms, and executed instructions buy time too (1000 per microsecond) so that no wait can hang; a port
 * access takes 1 us (an ISA bus cycle - the music driver times the FM chip's timers so). */
#define INSTR_PER_US 1000u
static uint64_t now_us(void)
{
    uint64_t by_instr = (xl.icount - step_icount0) / INSTR_PER_US, by_reads = step_reads * 1000u;
    return clock_us + (by_instr > by_reads ? by_instr : by_reads) + step_io_us;
}
static uint64_t clock_read(void) { step_reads++; return now_us(); }

/* A step's length is its virtual time, at least SFX_STEP_US; it is fixed when the program asks for the
 * step's sound (HC_AUDIO_FRAMES, just before the step ends) or else when the step ends. Its sound is
 * that many frames at 44100 Hz (the remainder carried), which the program mixes and hands over. */
static int step_fixed, audio_frames, audio_given;
static uint64_t audio_acc;   /* sample-time remainder, in samples*1e6 */
static int16_t audio_out[2 * SFX_AUDIO_MAX];
static void fix_step(void)
{
    if (step_fixed) return;
    uint64_t el = now_us() - clock_us;
    last_step_us = el > SFX_STEP_US ? el : SFX_STEP_US;
    audio_acc += last_step_us * 44100u;
    audio_frames = (int)(audio_acc / 1000000u); audio_acc %= 1000000u;
    if (audio_frames > SFX_AUDIO_MAX) audio_frames = SFX_AUDIO_MAX;
    step_fixed = 1;
}

/* ---------------------------------------------------------------- the PIT and the FM chip
 * The PIT's channel 0 interrupts at its divisor's period (1193182 Hz input clock) once the program
 * programs it (HC_PIT); the program runs each interrupt when told it is due (HC_PIT_TICK: at the step's
 * end, and whenever the music's API is called, so the interrupts keep their order with the program's
 * own writes). Times here are in PIT clocks since the program started.
 *
 * The FM chip is the Sound Blaster's (an OPL3 in OPL2 mode, Nuked OPL3) at the Ad Lib's ports
 * (0x388..0x38B, and the card's own 0x220..0x223, 0x228/0x229): its timers and status for the
 * driver's detection, and every register write queued with its time - an interrupt's writes carry the
 * interrupt's - so the step's sound is rendered with each write where it happened. */
#define PIT_HZ 1193182u
static uint64_t us2clk(uint64_t us) { return us * PIT_HZ / 1000000u; }
static int pit_armed, in_tick;
static uint64_t n_ticks, n_opl_writes;
static uint64_t pit_next, pit_period, tick_clk;
static uint64_t step_end_clk(void) { return us2clk(clock_us + last_step_us); }
static int pit_tick(void)
{
    uint64_t limit = step_fixed ? step_end_clk() : us2clk(now_us());
    in_tick = 0;
    if (!pit_armed || (step_fixed ? pit_next >= limit : pit_next > limit)) return 0;
    tick_clk = pit_next; pit_next += pit_period; in_tick = 1; n_ticks++;
    return 1;
}

static opl3_chip opl;
static int opl_used;
static uint8_t opl_index, opl_t1, opl_t2, opl_ctrl, opl_status;
static uint16_t opl_bank;
static uint64_t opl_t1_start, opl_t2_start;
typedef struct { uint64_t clk; uint16_t reg; uint8_t val; } OplWrite;
#define OPLQ 16384
static OplWrite oplq[OPLQ];
static int oplq_n;
static uint64_t oplq_last;
static void opl_write(uint16_t reg, uint8_t v)
{
    uint64_t now = now_us();
    if (reg == 2) opl_t1 = v;
    else if (reg == 3) opl_t2 = v;
    else if (reg == 4) {
        if (v & 0x80) opl_status = 0;
        else { opl_ctrl = v; if (v & 1) opl_t1_start = now; if (v & 2) opl_t2_start = now; }
    }
    uint64_t clk = in_tick ? tick_clk : us2clk(now);
    if (step_fixed && clk > step_end_clk()) clk = step_end_clk();
    if (clk < oplq_last) clk = oplq_last;
    oplq_last = clk;
    if (oplq_n == OPLQ) { OPL3_WriteReg(&opl, oplq[0].reg, oplq[0].val); memmove(oplq, oplq + 1, sizeof oplq[0] * (OPLQ - 1)); oplq_n--; }
    oplq[oplq_n].clk = clk; oplq[oplq_n].reg = reg; oplq[oplq_n].val = v; oplq_n++;
    opl_used = 1; n_opl_writes++;
    if (sfx_on_fm_write) sfx_on_fm_write(clk, reg, v, in_tick);
}
static uint8_t opl_read_status(void)
{
    uint64_t now = now_us();
    if ((opl_ctrl & 1) && !(opl_ctrl & 0x40) && now >= opl_t1_start + (256u - opl_t1) * 80u) opl_status |= 0xC0;
    if ((opl_ctrl & 2) && !(opl_ctrl & 0x20) && now >= opl_t2_start + (256u - opl_t2) * 320u) opl_status |= 0xA0;
    return opl_status;
}
static int opl_port(uint32_t port) { return (port >= 0x388 && port <= 0x38B) || (port >= 0x220 && port <= 0x223) || port == 0x228 || port == 0x229; }
uint32_t xl_host_in(uint32_t port, int sz)
{
    (void)sz;
    step_io_us++;
    return (opl_port(port) && !(port & 1)) ? opl_read_status() : 0xFF;
}
void xl_host_out(uint32_t port, uint32_t v, int sz)
{
    (void)sz;
    step_io_us++;
    if (!opl_port(port)) return;
    if (!(port & 1)) { opl_index = (uint8_t)v; opl_bank = (port & 2) && port != 0x228 ? 0x100 : 0; }
    else opl_write((uint16_t)(opl_bank | opl_index), (uint8_t)v);
}
/* the step's FM sound, added to what audio_out holds: writes up to each frame's time, then the frame */
static void render_fm(void)
{
    if (!opl_used) return;
    uint64_t start = us2clk(clock_us);
    int k = 0;
    for (int f = 0; f < audio_frames; f++) {
        uint64_t t = start + (uint64_t)f * PIT_HZ / 44100u;
        while (k < oplq_n && oplq[k].clk <= t) { OPL3_WriteReg(&opl, oplq[k].reg, oplq[k].val); k++; }
        int16_t s[2];
        OPL3_GenerateResampled(&opl, s);
        for (int c = 0; c < 2; c++) {
            int v = audio_out[2 * f + c] + s[c];
            audio_out[2 * f + c] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
    memmove(oplq, oplq + k, sizeof oplq[0] * (size_t)(oplq_n - k)); oplq_n -= k;   /* the next step's */
}
static uint32_t brk_start, brk_cur, level_addr, level_len;
static uint8_t mmap_used[(MMAP_TOP - MMAP_BASE) / PAGE];
static int prog_argc; static const char *const *prog_argv;

/* last presented picture */
static SfxVideo video;
static uint8_t vid_pixels[640 * 480], vid_pal[256 * 4];

/* ---------------------------------------------------------------- in-memory file system */
#define MAXFILES 1024
#define MAXFD 64
typedef struct VFile {
    char name[128];                 /* normalized, lowercase, relative to /game */
    const uint8_t *ro; uint32_t size;  /* mounted (read-only) data, or */
    uint8_t *rw; uint32_t cap;      /* a writable file living in guest memory */
    int used, dir;
} VFile;
static VFile files[MAXFILES];
static struct { int file; uint32_t pos; int flags; int used; uint32_t dirpos; } fds[MAXFD];

static void normalize(const char *in, char *out, size_t n)
{
    char tmp[256]; size_t k = 0;
    const char *p = in;
    if (!strncmp(p, "/game", 5) && (p[5] == '/' || !p[5])) p += 5;
    while (*p == '/') p++;
    for (; *p && k + 1 < sizeof tmp; p++) {
        char c = *p == '\\' ? '/' : *p;
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c == '/' && k && tmp[k - 1] == '/') continue;
        tmp[k++] = c;
    }
    while (k && tmp[k - 1] == '/') k--;
    tmp[k] = 0;
    /* drop "./" components */
    char *s = tmp, *d = out; size_t left = n - 1;
    while (*s && left) {
        if (s[0] == '.' && (s[1] == '/' || !s[1])) { s += s[1] ? 2 : 1; continue; }
        while (*s && *s != '/' && left) { *d++ = *s++; left--; }
        if (*s == '/' && left) { *d++ = *s++; left--; }
    }
    if (d > out && d[-1] == '/') d--;
    *d = 0;
}
static int vfind(const char *norm)
{
    for (int k = 0; k < MAXFILES; k++) if (files[k].used && !strcmp(files[k].name, norm)) return k;
    return -1;
}
static int vnew(const char *norm, int dir)
{
    for (int k = 0; k < MAXFILES; k++) if (!files[k].used) {
        memset(&files[k], 0, sizeof files[k]);
        snprintf(files[k].name, sizeof files[k].name, "%s", norm);
        files[k].used = 1; files[k].dir = dir; return k;
    }
    return -1;
}
static int is_dir(const char *norm)
{
    if (!*norm) return 1;
    int k = vfind(norm);
    if (k >= 0) return files[k].dir;
    size_t l = strlen(norm);
    for (int j = 0; j < MAXFILES; j++)
        if (files[j].used && !strncmp(files[j].name, norm, l) && files[j].name[l] == '/') return 1;
    return 0;
}
static uint32_t fsize(int k) { return files[k].size; }

/* ---------------------------------------------------------------- guest memory helpers */
static char *gstr(uint32_t a) { return (char *)xl_mem + (a & XL_MASK); }
static void gcopy_out(uint32_t a, const void *src, uint32_t n) { for (uint32_t i = 0; i < n; i++) wr8(a + i, ((const uint8_t *)src)[i]); }
static void gcopy_in(void *dst, uint32_t a, uint32_t n) { for (uint32_t i = 0; i < n; i++) ((uint8_t *)dst)[i] = (uint8_t)rd8(a + i); }

static void log_text(const char *s, uint32_t n) { if (sfx_on_log) sfx_on_log(s, n); }

void xl_host_fatal(const char *msg)
{
    snprintf(errmsg, sizeof errmsg, "%s", msg);
    state = ST_FATAL;
    for (;;) coro_yield(&prog);
}

/* ---------------------------------------------------------------- syscalls */
static int32_t sys_open(uint32_t path, uint32_t flags)
{
    char norm[128]; normalize(gstr(path), norm, sizeof norm);
    int acc = flags & 3, creat = (flags & 0x40) != 0, trunc = (flags & 0x200) != 0;
    int k = vfind(norm);
    if (k < 0 && is_dir(norm)) { k = vnew(norm, 1); }
    if (k < 0) {
        if (!creat) return -2;                               /* ENOENT */
        k = vnew(norm, 0); if (k < 0) return -24;
    }
    if (files[k].dir && acc != 0) return -21;                /* EISDIR */
    if (acc != 0 && files[k].ro) {                          /* writing a mounted file: copy it into guest memory */
        uint32_t n = files[k].size; uint8_t *b = malloc(n ? n : 1); memcpy(b, files[k].ro, n);
        files[k].rw = b; files[k].cap = n; files[k].ro = NULL;
    }
    if (trunc && acc != 0) files[k].size = 0;
    for (int f = 3; f < MAXFD; f++) if (!fds[f].used) {
        fds[f].used = 1; fds[f].file = k; fds[f].pos = (flags & 0x400) ? files[k].size : 0; fds[f].flags = (int)flags; fds[f].dirpos = 0;
        return f;
    }
    return -24;                                              /* EMFILE */
}
static int32_t sys_read(uint32_t fd, uint32_t buf, uint32_t n)
{
    if (fd >= MAXFD || !fds[fd].used) return fd == 0 ? 0 : -9;
    int k = fds[fd].file; if (files[k].dir) return -21;
    uint32_t sz = fsize(k), pos = fds[fd].pos;
    if (pos >= sz) return 0;
    if (n > sz - pos) n = sz - pos;
    gcopy_out(buf, (files[k].ro ? files[k].ro : files[k].rw) + pos, n);
    fds[fd].pos += n; return (int32_t)n;
}
static int32_t sys_write(uint32_t fd, uint32_t buf, uint32_t n)
{
    if (fd == 1 || fd == 2) { log_text(gstr(buf), n); return (int32_t)n; }
    if (fd >= MAXFD || !fds[fd].used) return -9;
    int k = fds[fd].file; uint32_t end = fds[fd].pos + n;
    if (fds[fd].flags & 0x400) { fds[fd].pos = files[k].size; end = fds[fd].pos + n; }
    if (end > files[k].cap) { uint32_t c = end + 4096; files[k].rw = realloc(files[k].rw, c); memset(files[k].rw + files[k].cap, 0, c - files[k].cap); files[k].cap = c; }
    for (uint32_t i = 0; i < n; i++) files[k].rw[fds[fd].pos + i] = rd8(buf + i);
    fds[fd].pos = end; if (end > files[k].size) files[k].size = end;
    return (int32_t)n;
}
static void stat64_out(uint32_t a, int k, int dir)
{
    for (uint32_t i = 0; i < 96; i++) wr8(a + i, 0);
    wr32(a + 12, (uint32_t)(k + 2)); wr32(a + 16, dir ? 040755u : 0100644u); wr32(a + 20, 1);
    wr64(a + 44, dir ? 4096u : fsize(k)); wr32(a + 52, 4096); wr64(a + 56, dir ? 8 : (fsize(k) + 511) / 512);
    wr32(a + 64, 788918400u); wr32(a + 72, 788918400u); wr32(a + 80, 788918400u);   /* 1995-01-01 */
    wr64(a + 88, (uint64_t)(k + 2));
}
static int32_t sys_stat(uint32_t path, uint32_t out)
{
    char norm[128]; normalize(gstr(path), norm, sizeof norm);
    int k = vfind(norm);
    if (k >= 0) { stat64_out(out, k, files[k].dir); return 0; }
    if (is_dir(norm)) { stat64_out(out, MAXFILES, 1); return 0; }
    return -2;
}
static int32_t sys_getdents64(uint32_t fd, uint32_t buf, uint32_t n)
{
    if (fd >= MAXFD || !fds[fd].used || !files[fds[fd].file].dir) return -20;   /* ENOTDIR */
    const char *dn = files[fds[fd].file].name; size_t dl = strlen(dn);
    uint32_t off = 0, idx = 0;
    const char *names[MAXFILES]; int types[MAXFILES]; int cnt = 0;
    for (int k = 0; k < MAXFILES; k++) {                   /* direct children, in table order */
        if (!files[k].used) continue;
        const char *nm = files[k].name, *rest;
        if (dl) { if (strncmp(nm, dn, dl) || nm[dl] != '/') continue; rest = nm + dl + 1; } else rest = nm;
        if (!*rest || strchr(rest, '/')) continue;
        names[cnt] = rest; types[cnt] = files[k].dir ? 4 : 8; cnt++;
    }
    for (idx = fds[fd].dirpos; idx < (uint32_t)cnt; idx++) {
        uint32_t nl = (uint32_t)strlen(names[idx]), rl = (19 + nl + 1 + 7) & ~7u;
        if (off + rl > n) break;
        wr64(buf + off, idx + 1); wr64(buf + off + 8, idx + 1); wr16(buf + off + 16, rl); wr8(buf + off + 18, (uint32_t)types[idx]);
        for (uint32_t i = 0; i <= nl; i++) wr8(buf + off + 19 + i, (uint8_t)names[idx][i]);
        off += rl;
    }
    fds[fd].dirpos = idx;
    return (int32_t)off;
}
static int32_t sys_mmap(uint32_t len, uint32_t flags)
{
    if (!(flags & 0x20)) return -19;                         /* anonymous only (ENODEV) */
    uint32_t np = (len + PAGE - 1) / PAGE, run = 0, total = (MMAP_TOP - MMAP_BASE) / PAGE;
    for (uint32_t p = 0; p < total; p++) {
        run = mmap_used[p] ? 0 : run + 1;
        if (run == np) {
            uint32_t first = p + 1 - np;
            for (uint32_t q = first; q <= p; q++) mmap_used[q] = 1;
            uint32_t a = MMAP_BASE + first * PAGE;
            memset(xl_mem + a, 0, (size_t)np * PAGE);
            return (int32_t)a;
        }
    }
    return -12;                                              /* ENOMEM */
}
static int32_t sys_munmap(uint32_t a, uint32_t len)
{
    if (a < MMAP_BASE || a >= MMAP_TOP) return 0;
    uint32_t first = (a - MMAP_BASE) / PAGE, np = (len + PAGE - 1) / PAGE;
    for (uint32_t q = first; q < first + np && q < (MMAP_TOP - MMAP_BASE) / PAGE; q++) mmap_used[q] = 0;
    return 0;
}
static int input_read;
static void do_step_yield(uint32_t in_addr)
{
    coro_yield(&prog);                                       /* the step ends at the read ... */
    if (in_addr) gcopy_out(in_addr, cur_in, sizeof *cur_in); /* ... and the next step's input is what it returns */
    input_read = 1;
}

void xl_host_syscall(void)
{
    uint32_t nr = EAX, a1 = EBX, a2 = ECX, a3 = EDX, a4 = ESI, a5 = EDI; int32_t r = -38;   /* ENOSYS */
    switch (nr) {
    /* ---- host calls */
    case HC_STEP: do_step_yield(a1); r = 0; break;
    case HC_PRESENT: {
        int w = (int)a2, h = (int)a3, pitch = (int)a4;
        if (w > 640) w = 640; if (h > 480) h = 480;
        for (int y = 0; y < h; y++) memcpy(vid_pixels + y * w, xl_mem + ((a1 + (uint32_t)(y * pitch)) & XL_MASK), (size_t)w);
        for (int c = 0; c < 256; c++) for (int k = 0; k < 4; k++) vid_pal[c * 4 + k] = rd8(a5 + c * 4 + k);
        video.w = w; video.h = h; r = 0; break; }
    case HC_TURN:
        level_addr = a1; level_len = a2; turns++;
        if (sfx_on_turn) sfx_on_turn(turns - 1, xl_mem + (a1 & XL_MASK), a2);
        r = 0; break;
    case HC_LOG: log_text(gstr(a1), a2); r = 0; break;
    case HC_TICKS: r = (int32_t)(clock_read() / 1000); break;
    case HC_DELAY: r = 0; break;
    case HC_AUDIO_FRAMES: fix_step(); r = audio_frames; break;
    case HC_CODE: xl_interp_range(a1, a1 + a2); r = 0; break;
    case HC_PIT: pit_period = a1 ? a1 : 65536u; pit_next = us2clk(now_us()) + pit_period; pit_armed = 1; r = 0; break;
    case HC_PIT_TICK: r = pit_tick(); break;
    case HC_AUDIO:
        if (step_fixed && (int32_t)a2 > 0 && (int32_t)a2 <= audio_frames) { gcopy_in(audio_out, a1, a2 * 4u); audio_given = (int)a2; r = 0; }
        else r = -22;
        break;
    /* ---- Linux i386 */
    case 1: case 252: state = ST_ENDED; for (;;) coro_yield(&prog);
    case 3: r = sys_read(a1, a2, a3); break;
    case 4: r = sys_write(a1, a2, a3); break;
    case 5: r = sys_open(a1, a2); break;
    case 295: r = (int32_t)a1 == -100 ? sys_open(a2, a3) : -38; break;          /* openat(AT_FDCWD, ...) */
    case 6: if (a1 < MAXFD && fds[a1].used) { fds[a1].used = 0; r = 0; } else r = a1 <= 2 ? 0 : -9; break;
    case 10: { char norm[128]; normalize(gstr(a1), norm, sizeof norm); int k = vfind(norm);
        if (k < 0 || files[k].dir) r = -2; else { free(files[k].rw); files[k].used = 0; r = 0; } break; }
    case 38: { char n1[128], n2[128]; normalize(gstr(a1), n1, sizeof n1); normalize(gstr(a2), n2, sizeof n2);
        int k = vfind(n1); if (k < 0) { r = -2; break; } int j = vfind(n2); if (j >= 0 && j != k) { free(files[j].rw); files[j].used = 0; }
        snprintf(files[k].name, sizeof files[k].name, "%s", n2); r = 0; break; }
    case 33: { char norm[128]; normalize(gstr(a1), norm, sizeof norm); r = (vfind(norm) >= 0 || is_dir(norm)) ? 0 : -2; break; }
    case 39: { char norm[128]; normalize(gstr(a1), norm, sizeof norm); if (is_dir(norm)) r = -17; else { vnew(norm, 1); r = 0; } break; }
    case 40: r = 0; break;                                                        /* rmdir */
    case 19: { if (a1 >= MAXFD || !fds[a1].used) { r = -9; break; } int64_t p = a3 == 0 ? (int32_t)a2 : a3 == 1 ? (int64_t)fds[a1].pos + (int32_t)a2 : (int64_t)fsize(fds[a1].file) + (int32_t)a2;
        if (p < 0) { r = -22; break; } fds[a1].pos = (uint32_t)p; r = (int32_t)p; break; }
    case 140: { if (a1 >= MAXFD || !fds[a1].used) { r = -9; break; } int64_t o = (int64_t)(((uint64_t)a2 << 32) | a3);
        int64_t p = a5 == 0 ? o : a5 == 1 ? (int64_t)fds[a1].pos + o : (int64_t)fsize(fds[a1].file) + o;
        if (p < 0) { r = -22; break; } fds[a1].pos = (uint32_t)p; wr64(a4, (uint64_t)p); r = 0; break; }
    case 145: case 146: {                                                         /* readv / writev */
        int32_t tot = 0;
        for (uint32_t k = 0; k < a3; k++) {
            uint32_t base = rd32(a2 + 8 * k), len = rd32(a2 + 8 * k + 4);
            if (!len) continue;
            int32_t got = nr == 146 ? sys_write(a1, base, len) : sys_read(a1, base, len);
            if (got < 0) { if (!tot) tot = got; break; }
            tot += got; if ((uint32_t)got < len) break;
        }
        r = tot; break; }
    case 195: case 196: r = sys_stat(a1, a2); break;
    case 197: if (a1 < MAXFD && fds[a1].used) { stat64_out(a2, fds[a1].file, files[fds[a1].file].dir); r = 0; }
              else if (a1 <= 2) { stat64_out(a2, MAXFILES, 0); wr32(a2 + 16, 020620u); r = 0; } else r = -9; break;
    case 300: r = (int32_t)a1 == -100 || gstr(a2)[0] == '/' ? sys_stat(a2, a3) : -38; break;
    case 383: r = -38; break;                                                     /* statx: use stat64 */
    case 220: r = sys_getdents64(a1, a2, a3); break;
    case 183: { const char *cwd = "/game"; gcopy_out(a1, cwd, 6); r = 6; break; }
    case 12: r = 0; break;                                                        /* chdir */
    case 45:
        if (a1 >= brk_start && a1 < HEAP_TOP) { if (a1 > brk_cur) memset(xl_mem + brk_cur, 0, a1 - brk_cur); brk_cur = a1; }
        r = (int32_t)brk_cur; break;
    case 90: r = -38; break;                                                      /* old mmap */
    case 192: r = sys_mmap(a2, a4); break;
    case 91: r = sys_munmap(a1, a2); break;
    case 163: r = -38; break;                                                     /* mremap */
    case 125: case 219: r = 0; break;                                             /* mprotect, madvise */
    case 54: r = -25; break;                                                      /* ioctl: ENOTTY */
    case 221: r = 0; break;                                                       /* fcntl64 */
    case 243: { uint32_t e = rd32(a1), base = rd32(a1 + 4);
        if (e == 0xFFFFFFFFu) { e = 6; wr32(a1, e); }
        xl_tls_base[e & 7] = base; r = 0; break; }
    case 258: case 20: case 224: r = 1000; break;                                 /* set_tid_address, getpid, gettid */
    case 174: case 175: case 119: case 173: r = 0; break;                         /* signals */
    case 238: case 270: xl_host_fatal("the program aborted"); break;              /* tkill, tgkill */
    case 240: case 422: r = (a2 & 127) == 0 ? -11 : 0; break;                     /* futex: WAIT -> EAGAIN, WAKE -> 0 */
    case 403: { uint64_t t = clock_read(), s = 788918400ull + t / 1000000u, ns = (t % 1000000u) * 1000u; wr64(a2, s); wr64(a2 + 8, ns); r = 0; break; }
    case 265: { uint64_t t = clock_read(); wr32(a2, (uint32_t)(788918400ull + t / 1000000u)); wr32(a2 + 4, (uint32_t)((t % 1000000u) * 1000u)); r = 0; break; }
    case 78: { uint64_t t = clock_read(); wr32(a1, (uint32_t)(788918400ull + t / 1000000u)); wr32(a1 + 4, (uint32_t)(t % 1000000u)); r = 0; break; }
    case 13: { uint32_t t = (uint32_t)(788918400ull + clock_read() / 1000000u); if (a1) wr32(a1, t); r = (int32_t)t; break; }
    case 122: { static const char f[6][65] = {"Linux", "chimera", "5.0", "#1", "i686", ""};
        for (int k = 0; k < 6; k++) gcopy_out(a1 + 65 * k, f[k], 65); r = 0; break; }
    case 158: r = 0; break;                                                       /* sched_yield */
    case 162: case 407: r = 0; break;                                             /* nanosleep: the step owns time */
    case 85: r = -22; break;                                                      /* readlink */
    default: {
        char m[64]; int l = snprintf(m, sizeof m, "[syscall %u unimplemented]\n", nr); log_text(m, (uint32_t)l); r = -38; }
    }
    EAX = (uint32_t)r;
}

/* ---------------------------------------------------------------- process */
static void program_main(void *arg)
{
    (void)arg;
    xl.eip = xl_elf_entry;
    xl_call(xl_elf_entry);
    state = ST_ENDED;
}

int sfx_init(const SfxFile *mount, int nfiles, int argc, const char *const *argv)
{
    memset(files, 0, sizeof files); memset(fds, 0, sizeof fds); memset(mmap_used, 0, sizeof mmap_used);
    memset(&xl, 0, sizeof xl); memset(xl_tls_base, 0, sizeof(uint32_t) * 8);
    steps = turns = clock_us = step_icount0 = last_step_us = step_reads = step_io_us = 0; audio_acc = 0; audio_frames = audio_given = step_fixed = 0;
    xl_interp_reset(); pit_armed = in_tick = 0; n_ticks = n_opl_writes = 0; pit_next = pit_period = tick_clk = 0;
    OPL3_Reset(&opl, 44100); opl_used = 0; opl_index = opl_t1 = opl_t2 = opl_ctrl = opl_status = 0; opl_bank = 0;
    opl_t1_start = opl_t2_start = 0; oplq_n = 0; oplq_last = 0; level_addr = level_len = 0; errmsg[0] = 0;
    for (int k = 0; k < nfiles; k++) {
        char norm[128]; normalize(mount[k].name, norm, sizeof norm);
        int f = vnew(norm, 0); if (f < 0) { snprintf(errmsg, sizeof errmsg, "too many files"); return -1; }
        files[f].ro = mount[k].data; files[f].size = mount[k].size;
    }
    if (!xl_mem) {
        xl_mem = mmap(NULL, XL_ARENA_SIZE + 64, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (xl_mem == MAP_FAILED) { xl_mem = NULL; snprintf(errmsg, sizeof errmsg, "arena allocation failed"); return -1; }
    }
    memset(xl_mem, 0, XL_ARENA_SIZE + 64);
    uint32_t end = 0;
    for (uint32_t k = 0; k < xl_segment_count; k++) {
        const XlSegment *s = &xl_segments[k];
        memcpy(xl_mem + s->vaddr, s->data, s->filesz);
        if (s->vaddr + s->memsz > end) end = s->vaddr + s->memsz;
    }
    brk_start = brk_cur = (end + PAGE - 1) & ~(PAGE - 1);
    /* the process stack: argv/envp strings, then argc, argv, envp, auxv */
    static const char *const env[] = {"HOME=/game", "PATH=/", "LANG=C", NULL};
    uint32_t sp = STACK_TOP, argp[16], envp[8]; int ne = 0;
    prog_argc = argc > 16 ? 16 : argc; prog_argv = argv;
    for (int k = prog_argc - 1; k >= 0; k--) { uint32_t l = (uint32_t)strlen(argv[k]) + 1; sp -= l; gcopy_out(sp, argv[k], l); argp[k] = sp; }
    for (int k = 0; env[k]; k++) { uint32_t l = (uint32_t)strlen(env[k]) + 1; sp -= l; gcopy_out(sp, env[k], l); envp[ne++] = sp; }
    sp -= 16; uint32_t rnd = sp; for (int k = 0; k < 16; k++) wr8(rnd + k, (uint32_t)(0xA5 ^ (k * 37)));
    uint32_t aux[][2] = {{3, xl_elf_phdr}, {4, xl_elf_phent}, {5, xl_elf_phnum}, {6, PAGE}, {9, xl_elf_entry},
                         {11, 1000}, {12, 1000}, {13, 1000}, {14, 1000}, {23, 0}, {25, rnd}, {0, 0}};
    int naux = (int)(sizeof aux / sizeof aux[0]);
    uint32_t words = (uint32_t)(1 + prog_argc + 1 + ne + 1 + 2 * naux);
    sp = (sp - words * 4) & ~15u;
    uint32_t p = sp;
    wr32(p, (uint32_t)prog_argc); p += 4;
    for (int k = 0; k < prog_argc; k++) { wr32(p, argp[k]); p += 4; } wr32(p, 0); p += 4;
    for (int k = 0; k < ne; k++) { wr32(p, envp[k]); p += 4; } wr32(p, 0); p += 4;
    for (int k = 0; k < naux; k++) { wr32(p, aux[k][0]); wr32(p + 4, aux[k][1]); p += 8; }
    ESP = sp; xl.fcw = 0x37F;
    if (prog.stack) munmap(prog.stack, prog.stack_size);
    if (coro_init(&prog, 16u << 20, program_main, NULL) < 0) { snprintf(errmsg, sizeof errmsg, "coroutine stack allocation failed"); return -1; }
    state = ST_READY; video.w = 320; video.h = 200; video.pixels = vid_pixels; video.palette = vid_pal;
    return 0;
}

int sfx_step(const HcInput *in)
{
    if (state == ST_ENDED) return 1;
    if (state == ST_FATAL) return -1;
    cur_in = in; state = ST_RUNNING; input_read = 0; step_fixed = 0; audio_given = 0;
    coro_resume(&prog);
    fix_step();
    if (audio_given < audio_frames) memset(audio_out + 2 * audio_given, 0, (size_t)(audio_frames - audio_given) * 4u);
    render_fm(); in_tick = 0;
    steps++; clock_us += last_step_us; step_icount0 = xl.icount; step_reads = 0; step_io_us = 0;
    if (prog.done && state == ST_RUNNING) state = ST_ENDED;
    return state == ST_FATAL ? -1 : state == ST_ENDED ? 1 : 0;
}

const char *sfx_error(void) { return errmsg; }
const SfxVideo *sfx_video(void) { return &video; }
int sfx_input_was_read(void) { return input_read; }
uint64_t sfx_steps(void) { return steps; }
uint64_t sfx_step_us(void) { return last_step_us; }
void sfx_sound_counts(uint64_t *ticks, uint64_t *fm_writes, uint32_t *pit_divisor) { *ticks = n_ticks; *fm_writes = n_opl_writes; *pit_divisor = pit_armed ? (uint32_t)pit_period : 0; }
const int16_t *sfx_audio(int *frames) { if (frames) *frames = audio_frames; return audio_out; }
uint64_t sfx_turns(void) { return turns; }
uint64_t sfx_cycles(void) { return xl.icount; }
uint8_t *sfx_arena(void) { return xl_mem; }
uint32_t sfx_arena_size(void) { return XL_ARENA_SIZE; }
uint32_t sfx_level_block(uint32_t *len) { if (len) *len = level_len; return level_addr; }
