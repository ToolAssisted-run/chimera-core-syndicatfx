/* xlat.h - runtime for C translated from a static i386 ELF by elf2c.py.
 *
 * The guest's flat 32-bit address space is an arena: guest address a is xl_mem[a & XL_MASK].
 * Registers and flags live in the global xl; flags are lazy (the last flag-setting operation
 * and its operands), materialized only when read. Everything here is plain portable C: the
 * translated program runs the same on any host, and all of its state is in xl and the arena. */
#ifndef XLAT_H
#define XLAT_H
#include <stdint.h>
#include <string.h>

#ifndef XL_ARENA_BITS
#define XL_ARENA_BITS 28                 /* 256 MiB guest address space */
#endif
#define XL_ARENA_SIZE (1u << XL_ARENA_BITS)
#define XL_MASK (XL_ARENA_SIZE - 1u)

enum { XF_EXPLICIT, XF_ADD, XF_ADC, XF_SUB, XF_SBB, XF_LOGIC, XF_INC, XF_DEC, XF_NEG,
       XF_SHL, XF_SHR, XF_SAR, XF_MUL };

typedef struct XlCpu {
    uint32_t r[8];            /* eax ecx edx ebx esp ebp esi edi */
    uint32_t fa, fb, fr;      /* lazy flags: operands and result of the last flag-setting op */
    uint32_t fx;              /* XF_EXPLICIT: the flags word; XF_INC/DEC/ADC/SBB: carry in */
    uint8_t fop, fsz;         /* operation, operand size in bytes */
    uint8_t df;               /* direction flag */
    uint32_t ret;             /* return address popped by the last 'ret' (checked by the caller) */
    uint16_t gs;              /* gs selector; gs_base from set_thread_area */
    uint32_t gs_base;
    uint32_t eip;             /* last traced address (diagnostics only) */
    uint64_t icount;          /* translated blocks executed (a cycle-count proxy) */
    /* x87 */
    long double st[8]; uint8_t ftop; uint16_t fcw, fsw;
} XlCpu;

extern XlCpu xl;
extern uint8_t *xl_mem;

#define EAX xl.r[0]
#define ECX xl.r[1]
#define EDX xl.r[2]
#define EBX xl.r[3]
#define ESP xl.r[4]
#define EBP xl.r[5]
#define ESI xl.r[6]
#define EDI xl.r[7]

static inline uint8_t  rd8 (uint32_t a) { return xl_mem[a & XL_MASK]; }
static inline uint16_t rd16(uint32_t a) { uint16_t v; memcpy(&v, xl_mem + (a & XL_MASK), 2); return v; }
static inline uint32_t rd32(uint32_t a) { uint32_t v; memcpy(&v, xl_mem + (a & XL_MASK), 4); return v; }
static inline uint64_t rd64(uint32_t a) { uint64_t v; memcpy(&v, xl_mem + (a & XL_MASK), 8); return v; }
static inline void wr8 (uint32_t a, uint32_t v) { xl_mem[a & XL_MASK] = (uint8_t)v; }
static inline void wr16(uint32_t a, uint32_t v) { uint16_t w = (uint16_t)v; memcpy(xl_mem + (a & XL_MASK), &w, 2); }
static inline void wr32(uint32_t a, uint32_t v) { memcpy(xl_mem + (a & XL_MASK), &v, 4); }
static inline void wr64(uint32_t a, uint64_t v) { memcpy(xl_mem + (a & XL_MASK), &v, 8); }

static inline void push32(uint32_t v) { ESP -= 4; wr32(ESP, v); }
static inline uint32_t pop32(void) { uint32_t v = rd32(ESP); ESP += 4; return v; }
static inline void push16(uint32_t v) { ESP -= 2; wr16(ESP, v); }
static inline uint32_t pop16(void) { uint32_t v = rd16(ESP); ESP += 2; return v; }

#define SZMASK(sz) ((sz) == 1 ? 0xFFu : (sz) == 2 ? 0xFFFFu : 0xFFFFFFFFu)
#define SIGNBIT(sz) ((sz) == 1 ? 0x80u : (sz) == 2 ? 0x8000u : 0x80000000u)

/* flag-setting ops: record, return the (masked) result */
static inline uint32_t f_set(int op, int sz, uint32_t a, uint32_t b, uint32_t r) {
    xl.fop = (uint8_t)op; xl.fsz = (uint8_t)sz; xl.fa = a; xl.fb = b; xl.fr = r & SZMASK(sz); return xl.fr;
}
uint32_t xl_cf(void), xl_zf(void), xl_sf(void), xl_of(void), xl_pf(void), xl_af(void);
uint32_t xl_flags(void);                 /* materialize EFLAGS (CF PF AF ZF SF DF OF + bit 1) */
void xl_setflags(uint32_t f);            /* load EFLAGS (popf/sahf) */

static inline uint32_t op_add(int sz, uint32_t a, uint32_t b) { return f_set(XF_ADD, sz, a, b, a + b); }
static inline uint32_t op_sub(int sz, uint32_t a, uint32_t b) { return f_set(XF_SUB, sz, a, b, a - b); }
static inline uint32_t op_adc(int sz, uint32_t a, uint32_t b) { uint32_t c = xl_cf(); uint32_t r = f_set(XF_ADC, sz, a, b, a + b + c); xl.fx = c; return r; }
static inline uint32_t op_sbb(int sz, uint32_t a, uint32_t b) { uint32_t c = xl_cf(); uint32_t r = f_set(XF_SBB, sz, a, b, a - b - c); xl.fx = c; return r; }
static inline uint32_t op_logic(int sz, uint32_t r) { return f_set(XF_LOGIC, sz, 0, 0, r); }
static inline uint32_t op_inc(int sz, uint32_t a) { uint32_t c = xl_cf(); uint32_t r = f_set(XF_INC, sz, a, 1, a + 1); xl.fx = c; return r; }
static inline uint32_t op_dec(int sz, uint32_t a) { uint32_t c = xl_cf(); uint32_t r = f_set(XF_DEC, sz, a, 1, a - 1); xl.fx = c; return r; }
static inline uint32_t op_neg(int sz, uint32_t a) { return f_set(XF_NEG, sz, a, 0, 0u - a); }
uint32_t op_shl(int sz, uint32_t a, uint32_t n);
uint32_t op_shr(int sz, uint32_t a, uint32_t n);
uint32_t op_sar(int sz, uint32_t a, uint32_t n);
uint32_t op_rol(int sz, uint32_t a, uint32_t n);
uint32_t op_ror(int sz, uint32_t a, uint32_t n);
uint32_t op_rcl(int sz, uint32_t a, uint32_t n);
uint32_t op_rcr(int sz, uint32_t a, uint32_t n);
uint32_t op_shld(int sz, uint32_t a, uint32_t b, uint32_t n);
uint32_t op_shrd(int sz, uint32_t a, uint32_t b, uint32_t n);
void op_mul(int sz, uint32_t b);          /* unsigned edx:eax = eax * b (by size) */
void op_imul1(int sz, uint32_t b);        /* signed one-operand form */
uint32_t op_imul2(int sz, uint32_t a, uint32_t b);  /* two/three-operand form */
void op_div(int sz, uint32_t b, uint32_t at);
void op_idiv(int sz, uint32_t b, uint32_t at);
void op_bt(uint32_t v, uint32_t bit);     /* CF = bit */
uint32_t op_bsf(int sz, uint32_t v, uint32_t old);
uint32_t op_bsr(int sz, uint32_t v, uint32_t old);
void op_cpuid(void);

/* conditions (x86 cc numbering) */
static inline int xl_cond(int cc) {
    switch (cc) {
    case 0x0: return xl_of();                  case 0x1: return !xl_of();
    case 0x2: return xl_cf();                  case 0x3: return !xl_cf();
    case 0x4: return xl_zf();                  case 0x5: return !xl_zf();
    case 0x6: return xl_cf() || xl_zf();       case 0x7: return !xl_cf() && !xl_zf();
    case 0x8: return xl_sf();                  case 0x9: return !xl_sf();
    case 0xA: return xl_pf();                  case 0xB: return !xl_pf();
    case 0xC: return xl_sf() != xl_of();       case 0xD: return xl_sf() == xl_of();
    case 0xE: return xl_zf() || (xl_sf() != xl_of());
    default:  return !xl_zf() && (xl_sf() == xl_of());
    }
}

/* string instructions (rep handled in the helpers) */
void xl_movs(int sz, int rep);
void xl_stos(int sz, int rep);
void xl_lods(int sz, int rep);
void xl_cmps(int sz, int rep);            /* rep: 0 none, 1 repe, 2 repne */
void xl_scas(int sz, int rep);

/* x87 subset */
void xl_x87(uint32_t addr, int op, uint32_t ea, int modrm);

/* control flow outside the static translation */
typedef void (*XlFn)(void);
void xl_call(uint32_t target);           /* call a translated entry by guest address */
void xl_badret(uint32_t expected, uint32_t at);
void xl_unimpl(uint32_t addr, const char *what);
void xl_segload(uint32_t addr, int seg, uint32_t sel);
void xl_int(uint32_t addr, int n);        /* int n; n = 0x80 is the Linux i386 syscall */
uint64_t xl_rdtsc(void);

extern void (*xl_hook)(uint32_t addr);   /* optional probe at hooked function entries (elf2c --hook) */

/* code outside the static translation (xl_interp.c): xl_call() interprets an address in a registered
 * range, until it returns to its caller */
void xl_interp(uint32_t target);
void xl_interp_range(uint32_t lo, uint32_t hi);
int xl_interp_owns(uint32_t a);
void xl_interp_reset(void);

/* provided by the embedder */
void xl_host_syscall(void);               /* eax = nr, ebx.. = args; result in eax */
void xl_host_fatal(const char *msg);
uint32_t xl_host_in(uint32_t port, int sz);             /* in (interpreted code only) */
void xl_host_out(uint32_t port, uint32_t v, int sz);    /* out */

extern const uint32_t xl_entry_addrs[];
extern const XlFn xl_entry_fns[];
extern const uint32_t xl_entry_count;
#endif
