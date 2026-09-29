/* ail32.c - the AIL/32 2.x API layer (Miles Design, 1991-92) that MAIN.EXE links and drives its music
 * driver with, ported to C from the routines in SyndicatFX's src/syndre.sx (0x3BB38..0x3C54D: find_proc,
 * call_driver, API_timer, the timer services, the driver services and the thunks to the driver).
 *
 * The driver is the original GAMEFM.DLL (the Ad Lib driver, "a32adlib"), loaded from the game's files
 * by ail32_dll_load() and run by the translation runtime's interpreter (xlat/xl_interp.c): its
 * functions are called through the function index at the start of its image, as call_driver does, and
 * its "serve" function is a timer callback, as AIL_init_driver makes it.
 *
 * The timer interrupt: MAIN.EXE hooks INT 8 with API_timer and programs the PIT at the shortest period
 * any timer asks for. Here the PIT is the core's (HC_PIT sets its divisor) and its interrupts are run
 * by ail32_service(): the core says which ticks are due, in order, and each runs API_timer's logic at
 * its time (so the driver's writes to the chip carry that time). The game's other callers of this layer
 * in DOS - the digital driver (GAMEDG.DLL, no timer) and the keyboard timer - are not the core's: the
 * digitized sounds are SyndicatFX's (bfsoundlib), the keyboard the port's. */
#include <stdlib.h>
#include <string.h>
#include "ail32.h"
#include "hostcall.h"

#define CURRENT_REV 0xD7
#define NDRV 16
#define NTIMERS 17                        /* 16, and 16 = the BIOS's clock, chained to */
#define BIOS_TIMER 16

typedef void (*TimerFn)(void);
typedef uint32_t (*DrvFn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

static uint32_t index_base[NDRV];         /* each driver's function index: {number, address} ..., -1 */
static int32_t assigned_timer[NDRV];
static uint32_t driver_active[NDRV];
static uint16_t active_timers, timer_busy;
static TimerFn timer_callback[NTIMERS];
static uint16_t timer_status[NTIMERS];    /* 0 free, 1 registered (stopped), 2 running */
static uint32_t timer_elapsed[NTIMERS], timer_value[NTIMERS], timer_period;
static int32_t current_timer, timer_handle;
static uint32_t PIT_divisor;

/* ---------------------------------------------------------------- the timer services */
static void bios_caller(void) { }         /* the BIOS's INT 8: nothing of it here */

static void set_PIT_divisor(uint32_t d) { PIT_divisor = d; hc_call(HC_PIT, (long)d, 0, 0, 0, 0); }
static void set_PIT_period(uint32_t us) { set_PIT_divisor(us < 0xD68D ? (uint32_t)((uint64_t)us * 10000u / 8380u) : 0); }
static void program_timers(void)
{
    uint32_t temp = 0xFFFFFFFFu;
    for (int i = 0; i <= BIOS_TIMER; i++)
        if (timer_status[i] != 0 && timer_value[i] < temp) temp = timer_value[i];
    if (temp == timer_period) return;
    current_timer = -1;
    timer_period = temp;
    set_PIT_period(temp);
    memset(timer_elapsed, 0, sizeof timer_elapsed);
}
static void init_DDA_arrays(void)
{
    timer_period = 0xFFFFFFFFu;
    memset(timer_status, 0, sizeof timer_status);
    memset(timer_elapsed, 0, sizeof timer_elapsed);
    memset(timer_value, 0, sizeof timer_value);
}
static void set_timer_period(int32_t t, uint32_t us)
{
    uint16_t saved = timer_status[t];
    timer_status[t] = 1;
    timer_value[t] = us; timer_elapsed[t] = 0;
    program_timers();
    timer_status[t] = saved;
}
static void set_timer_frequency(int32_t t, uint32_t hz) { set_timer_period(t, 1000000u / hz); }
static void start_timer(int32_t t) { if (timer_status[t] == 1) timer_status[t] = 2; }
static int32_t register_timer(TimerFn fn)
{
    int32_t i;
    for (i = 0; i < 16; i++) if (timer_status[i] == 0) break;
    if (i == 16) return -1;
    timer_status[i] = 1; timer_callback[i] = fn; timer_value[i] = 0xFFFFFFFFu;
    if (++active_timers == 1) {
        init_DDA_arrays();
        timer_status[BIOS_TIMER] = 1;
        timer_callback[BIOS_TIMER] = bios_caller;             /* hook_timer_process */
        set_timer_period(BIOS_TIMER, 0xD68D);
        start_timer(BIOS_TIMER);
        timer_status[i] = 1; timer_value[i] = 0xFFFFFFFFu;
    }
    return i;
}
static void release_timer_handle(int32_t t)
{
    if (t == -1 || timer_status[t] == 0) return;
    timer_status[t] = 0;
    if (--active_timers == 0) set_PIT_divisor(0);                 /* and unhook */
}
static void release_all_timers(void) { for (int32_t t = 15; t >= 0; t--) release_timer_handle(t); }

/* API_timer: one PIT interrupt */
static void api_timer(void)
{
    if (timer_busy) return;
    timer_busy = 1;
    for (current_timer = 0; current_timer <= BIOS_TIMER; current_timer++) {
        int32_t i = current_timer;
        if (timer_status[i] != 2) continue;
        uint32_t e = timer_elapsed[i] + timer_period;
        if (e < timer_value[i]) { timer_elapsed[i] = e; continue; }
        timer_elapsed[i] = e - timer_value[i];
        if (current_timer >= BIOS_TIMER) timer_busy = 0;
        timer_callback[i]();
    }
    timer_busy = 0;
}
void ail32_service(void) { while (hc_call(HC_PIT_TICK, 0, 0, 0, 0, 0)) api_timer(); }

/* ---------------------------------------------------------------- the driver services */
static uint32_t find_proc(uint32_t n, HDRIVER h)
{
    if ((uint32_t)h >= NDRV || !index_base[h]) return 0;
    for (const uint32_t *p = (const uint32_t *)index_base[h];; p += 2) {
        if (p[0] == n) return p[1];
        if (p[0] == 0xFFFFFFFFu) return 0;
    }
}
static uint32_t call_driver(uint32_t n, HDRIVER h, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
    uint32_t fn = find_proc(n, h);
    return fn ? ((DrvFn)fn)((uint32_t)h, a, b, c, d, e) : 0;
}
static uint32_t interrupt_divisor(void) { return PIT_divisor; }   /* AIL_interrupt_divisor, for the driver */

void ail32_startup(void)
{
    active_timers = 0; timer_busy = 0;
    memset(index_base, 0, sizeof index_base);
    memset(assigned_timer, 0xFF, sizeof assigned_timer);
    memset(driver_active, 0, sizeof driver_active);
}
AIL32_DESC *ail32_describe_driver(HDRIVER h)
{
    ail32_service();
    return (AIL32_DESC *)call_driver(0x64, h, (uint32_t)interrupt_divisor, 0, 0, 0, 0);
}
HDRIVER ail32_register_driver(void *driver_base)
{
    HDRIVER cur;
    for (cur = 0; cur < NDRV; cur++) if (!index_base[cur]) break;
    if (cur == NDRV) return -1;
    const uint32_t *base = driver_base;
    if (base[1] != 0x79706F43u) return -1;                          /* "Copy"right */
    index_base[cur] = base[0];
    AIL32_DESC *desc = ail32_describe_driver(cur);
    if (!desc || desc->min_API_version > CURRENT_REV) return -1;
    return cur;
}
int32_t ail32_detect_device(HDRIVER h, uint32_t io, uint32_t irq, uint32_t dma, uint32_t drq)
{
    ail32_service();
    return (int32_t)call_driver(0x65, h, io, irq, dma, drq, 0);
}
void ail32_init_driver(HDRIVER h, uint32_t io, uint32_t irq, uint32_t dma, uint32_t drq)
{
    ail32_service();
    if ((uint32_t)h >= NDRV) return;
    timer_handle = -1;
    AIL32_DESC *desc = ail32_describe_driver(h);
    uint32_t rate = desc->service_rate;
    if (rate != 0xFFFFFFFFu) {
        uint32_t serve = find_proc(0x67, h);
        if (serve) {
            int32_t t = register_timer((TimerFn)serve);
            assigned_timer[h] = t; timer_handle = t;
            set_timer_frequency(t, rate);
        }
    }
    call_driver(0x66, h, io, irq, dma, drq, 0);
    driver_active[h] = 1;
    if (timer_handle != -1) start_timer(timer_handle);
}
static void shutdown_driver(HDRIVER h, const char *msg)
{
    if ((uint32_t)h >= NDRV) return;
    uint32_t was = driver_active[h]; driver_active[h] = 0;
    if (!was) return;
    if (assigned_timer[h] != -1) release_timer_handle(assigned_timer[h]);
    call_driver(0x68, h, (uint32_t)msg, 0, 0, 0, 0);
}
void ail32_shutdown(const char *msg)
{
    ail32_service();
    for (HDRIVER cur = 0; cur < NDRV; cur++) {
        if (!index_base[cur]) continue;
        if (assigned_timer[cur] != -1) release_timer_handle(assigned_timer[cur]);
        shutdown_driver(cur, msg);
    }
    release_all_timers();
}

/* the thunks: the driver's own functions */
#define THUNK(n, h, a, b, c, d) (ail32_service(), call_driver((n), (h), (a), (b), (c), (d), 0))
uint32_t ail32_state_table_size(HDRIVER h) { return THUNK(0x96, h, 0, 0, 0, 0); }
HSEQUENCE ail32_register_sequence(HDRIVER h, void *xmid, uint32_t num, void *state, void *ctrl)
{ return (HSEQUENCE)THUNK(0x97, h, (uint32_t)xmid, num, (uint32_t)state, (uint32_t)ctrl); }
uint32_t ail32_default_timbre_cache_size(HDRIVER h) { return THUNK(0x99, h, 0, 0, 0, 0); }
void ail32_define_timbre_cache(HDRIVER h, void *cache, uint32_t size) { THUNK(0x9A, h, (uint32_t)cache, size, 0, 0); }
uint32_t ail32_timbre_request(HDRIVER h, HSEQUENCE s) { return THUNK(0x9B, h, (uint32_t)s, 0, 0, 0); }
void ail32_install_timbre(HDRIVER h, uint32_t bank, uint32_t patch, void *src) { THUNK(0x9C, h, bank, patch, (uint32_t)src, 0); }
void ail32_start_sequence(HDRIVER h, HSEQUENCE s) { THUNK(0xAA, h, (uint32_t)s, 0, 0, 0); }
void ail32_stop_sequence(HDRIVER h, HSEQUENCE s) { THUNK(0xAB, h, (uint32_t)s, 0, 0, 0); }
void ail32_resume_sequence(HDRIVER h, HSEQUENCE s) { THUNK(0xAD, h, (uint32_t)s, 0, 0, 0); }
uint32_t ail32_sequence_status(HDRIVER h, HSEQUENCE s) { return THUNK(0xAE, h, (uint32_t)s, 0, 0, 0); }

/* ---------------------------------------------------------------- DLL_load */
static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t u16(const uint8_t *p) { return p[0] | p[1] << 8; }
void *ail32_dll_load(const void *image, uint32_t size)
{
    const uint8_t *d = image;
    if (size < 0x40) return NULL;
    uint32_t lx = u32(d + 0x3C);
    if (lx + 0xB0 > size || d[lx] != 'L' || d[lx + 1] != 'X') return NULL;
    const uint8_t *h = d + lx;
    uint32_t pagesize = u32(h + 0x28), pageshift = u32(h + 0x2C), objtab = u32(h + 0x40), nobj = u32(h + 0x44);
    uint32_t pgtab = u32(h + 0x48), fpt = u32(h + 0x68), frt = u32(h + 0x6C), datapages = u32(h + 0x80);
    if (nobj == 0 || nobj > 8) return NULL;
    uint32_t base_off[8], total = 0;
    for (uint32_t i = 0; i < nobj; i++) { base_off[i] = total; total += (u32(h + objtab + 24 * i) + 0xFFFu) & ~0xFFFu; }
    uint8_t *mem = malloc(total);
    if (!mem) return NULL;
    memset(mem, 0, total);
    for (uint32_t i = 0; i < nobj; i++) {
        const uint8_t *o = h + objtab + 24 * i;
        uint32_t vsize = u32(o), pgidx = u32(o + 12), pgcnt = u32(o + 16);
        for (uint32_t p = 0; p < pgcnt; p++) {
            uint32_t pg = pgidx + p;                                /* 1-based */
            const uint8_t *pe = h + pgtab + 8 * (pg - 1);
            uint32_t src = datapages + (u32(pe) << pageshift), n = u16(pe + 4), dst = base_off[i] + p * pagesize;
            if (dst + n > base_off[i] + ((vsize + 0xFFFu) & ~0xFFFu) || src + n > size) { free(mem); return NULL; }
            memcpy(mem + dst, d + src, n);
            /* this page's fixups */
            const uint8_t *q = h + frt + u32(h + fpt + 4 * (pg - 1)), *end = h + frt + u32(h + fpt + 4 * pg);
            while (q < end) {
                uint8_t st = q[0], fl = q[1]; q += 2;
                int nsrc = 1; int32_t one = 0;
                if (st & 0x20) nsrc = *q++; else { one = (int16_t)u16(q); q += 2; }
                if ((fl & 3) != 0) { free(mem); return NULL; }            /* imports: none in a driver */
                uint32_t tobj = (fl & 0x40) ? u16(q) : *q; q += (fl & 0x40) ? 2 : 1;
                uint32_t toff = 0;
                if ((st & 0xF) != 2) { toff = (fl & 0x10) ? u32(q) : u16(q); q += (fl & 0x10) ? 4 : 2; }
                if (tobj == 0 || tobj > nobj) { free(mem); return NULL; }
                uint32_t target = (uint32_t)(uintptr_t)mem + base_off[tobj - 1] + toff;
                for (int k = 0; k < nsrc; k++) {
                    int32_t so = (st & 0x20) ? (int16_t)u16(q + 2 * k) : one;
                    int64_t at = (int64_t)dst + so;
                    if (at < 0 || at + 4 > (int64_t)total) continue;
                    uint8_t *w = mem + at;
                    switch (st & 0xF) {
                    case 7: memcpy(w, &target, 4); break;                             /* 32-bit offset */
                    case 8: { uint32_t rel = target - ((uint32_t)(uintptr_t)w + 4); memcpy(w, &rel, 4); break; }
                    case 5: { uint16_t v = (uint16_t)target; memcpy(w, &v, 2); break; }
                    case 2: { uint16_t v = 0x2B; memcpy(w, &v, 2); break; }           /* the flat selector */
                    default: free(mem); return NULL;
                    }
                }
                if (st & 0x20) q += 2 * nsrc;
            }
        }
    }
    hc_call(HC_CODE, (long)(uintptr_t)mem, (long)total, 0, 0, 0);    /* the interpreter runs it */
    return mem;
}
