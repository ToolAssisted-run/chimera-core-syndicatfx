/* xlat_rt.c - runtime support for translated i386 code: flags, shifts, multiply/divide,
 * string instructions, dispatch by guest address. Portable C, no host state. */
#include "xlat.h"
#include <stdio.h>

XlCpu xl;
uint8_t *xl_mem;
uint32_t xl_tls_base[8];
void (*xl_hook)(uint32_t addr);

static uint32_t parity8(uint32_t v) { v &= 0xFF; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return !(v & 1); }

uint32_t xl_cf(void)
{
    uint32_t m = SZMASK(xl.fsz), a = xl.fa & m, b = xl.fb & m;
    switch (xl.fop) {
    case XF_EXPLICIT: return xl.fx & 1;
    case XF_ADD: return xl.fr < a;
    case XF_ADC: return ((uint64_t)a + b + xl.fx) > m;
    case XF_SUB: return a < b;
    case XF_SBB: return (uint64_t)a < (uint64_t)b + xl.fx;
    case XF_LOGIC: return 0;
    case XF_INC: case XF_DEC: return xl.fx;
    case XF_NEG: return a != 0;
    default: return 0;
    }
}
uint32_t xl_zf(void) { return xl.fop == XF_EXPLICIT ? (xl.fx >> 6) & 1 : xl.fr == 0; }
uint32_t xl_sf(void) { return xl.fop == XF_EXPLICIT ? (xl.fx >> 7) & 1 : (xl.fr & SIGNBIT(xl.fsz)) != 0; }
uint32_t xl_pf(void) { return xl.fop == XF_EXPLICIT ? (xl.fx >> 2) & 1 : parity8(xl.fr); }
uint32_t xl_of(void)
{
    uint32_t s = SIGNBIT(xl.fsz), m = SZMASK(xl.fsz), a = xl.fa & m, b = xl.fb & m, r = xl.fr;
    switch (xl.fop) {
    case XF_EXPLICIT: return (xl.fx >> 11) & 1;
    case XF_ADD: case XF_ADC: return (((a ^ r) & (b ^ r)) & s) != 0;
    case XF_SUB: case XF_SBB: return (((a ^ b) & (a ^ r)) & s) != 0;
    case XF_INC: return r == s;
    case XF_DEC: return a == s;
    case XF_NEG: return a == s;
    default: return 0;
    }
}
uint32_t xl_af(void)
{
    uint32_t a = xl.fa, b = xl.fb, r = xl.fr;
    switch (xl.fop) {
    case XF_EXPLICIT: return (xl.fx >> 4) & 1;
    case XF_ADD: case XF_ADC: case XF_SUB: case XF_SBB: case XF_INC: case XF_DEC: return ((a ^ b ^ r) & 0x10) != 0;
    case XF_NEG: return ((a ^ r) & 0x10) != 0;
    default: return 0;
    }
}
uint32_t xl_flags(void)
{
    return xl_cf() | 2u | (xl_pf() << 2) | (xl_af() << 4) | (xl_zf() << 6) | (xl_sf() << 7) | 0x200u
         | ((uint32_t)xl.df << 10) | (xl_of() << 11);
}
void xl_setflags(uint32_t f) { xl.fop = XF_EXPLICIT; xl.fx = f; xl.df = (f >> 10) & 1; }

static uint32_t mkflags(int sz, uint32_t r, uint32_t cf, uint32_t of, uint32_t af)
{
    r &= SZMASK(sz);
    return cf | 2u | (parity8(r) << 2) | (af << 4) | ((r == 0) << 6) | (((r & SIGNBIT(sz)) != 0) << 7) | (of << 11);
}
static void setx(uint32_t f) { xl.fop = XF_EXPLICIT; xl.fx = f; }
static void setcfof(uint32_t cf, uint32_t of) { uint32_t f = xl_flags(); setx((f & ~0x801u) | cf | (of << 11)); }

uint32_t op_shl(int sz, uint32_t a, uint32_t n)
{
    int bits = sz * 8; n &= 31; a &= SZMASK(sz);
    if (!n) return a;
    uint32_t r = (uint32_t)(((uint64_t)a << n) & SZMASK(sz));
    uint32_t cf = n <= (uint32_t)bits ? (uint32_t)(((uint64_t)a >> (bits - n)) & 1) : 0;
    setx(mkflags(sz, r, cf, ((r & SIGNBIT(sz)) != 0) ^ cf, 0));
    return r;
}
uint32_t op_shr(int sz, uint32_t a, uint32_t n)
{
    n &= 31; a &= SZMASK(sz);
    if (!n) return a;
    uint32_t r = (uint32_t)((uint64_t)a >> n);
    uint32_t cf = (uint32_t)(((uint64_t)a >> (n - 1)) & 1);
    setx(mkflags(sz, r, cf, (a & SIGNBIT(sz)) != 0, 0));
    return r;
}
uint32_t op_sar(int sz, uint32_t a, uint32_t n)
{
    n &= 31; a &= SZMASK(sz);
    if (!n) return a;
    int32_t sa = sz == 1 ? (int8_t)a : sz == 2 ? (int16_t)a : (int32_t)a;
    uint32_t cf = (uint32_t)((sa >> (n - 1)) & 1);
    uint32_t r = (uint32_t)(sa >> n) & SZMASK(sz);
    setx(mkflags(sz, r, cf, 0, 0));
    return r;
}
uint32_t op_rol(int sz, uint32_t a, uint32_t n)
{
    int bits = sz * 8; a &= SZMASK(sz);
    if (!(n & 31)) return a;
    n = (n & 31) % bits;
    uint32_t r = n ? ((a << n) | (a >> (bits - n))) & SZMASK(sz) : a;
    uint32_t cf = r & 1;
    setcfof(cf, ((r & SIGNBIT(sz)) != 0) ^ cf);
    return r;
}
uint32_t op_ror(int sz, uint32_t a, uint32_t n)
{
    int bits = sz * 8; a &= SZMASK(sz);
    if (!(n & 31)) return a;
    n = (n & 31) % bits;
    uint32_t r = n ? ((a >> n) | (a << (bits - n))) & SZMASK(sz) : a;
    uint32_t msb = (r & SIGNBIT(sz)) != 0, msb2 = (r >> (bits - 2)) & 1;
    setcfof(msb, msb ^ msb2);
    return r;
}
uint32_t op_rcl(int sz, uint32_t a, uint32_t n)
{
    int bits = sz * 8; a &= SZMASK(sz);
    n = (n & 31) % (bits + 1);
    if (!n) return a;
    uint64_t v = ((uint64_t)xl_cf() << bits) | a;          /* bits+1 wide */
    uint64_t wm = (1ull << (bits + 1)) - 1;
    v = ((v << n) | (v >> (bits + 1 - n))) & wm;
    uint32_t r = (uint32_t)(v & SZMASK(sz)), cf = (uint32_t)(v >> bits) & 1;
    setcfof(cf, ((r & SIGNBIT(sz)) != 0) ^ cf);
    return r;
}
uint32_t op_rcr(int sz, uint32_t a, uint32_t n)
{
    int bits = sz * 8; a &= SZMASK(sz);
    n = (n & 31) % (bits + 1);
    if (!n) return a;
    uint32_t cfin = xl_cf();
    uint64_t v = ((uint64_t)cfin << bits) | a;
    uint64_t wm = (1ull << (bits + 1)) - 1;
    v = ((v >> n) | (v << (bits + 1 - n))) & wm;
    uint32_t r = (uint32_t)(v & SZMASK(sz)), cf = (uint32_t)(v >> bits) & 1;
    setcfof(cf, ((r >> (bits - 1)) ^ (r >> (bits - 2))) & 1);
    return r;
}
uint32_t op_shld(int sz, uint32_t a, uint32_t b, uint32_t n)
{
    int bits = sz * 8; n &= 31; a &= SZMASK(sz); b &= SZMASK(sz);
    if (!n) return a;
    uint64_t v = ((uint64_t)a << bits) | b;                  /* a:b */
    uint32_t r = (uint32_t)((v << n) >> bits) & SZMASK(sz);
    uint32_t cf = (uint32_t)((v >> (2 * bits - n)) & 1);
    setx(mkflags(sz, r, cf, ((r ^ a) & SIGNBIT(sz)) != 0, 0));
    return r;
}
uint32_t op_shrd(int sz, uint32_t a, uint32_t b, uint32_t n)
{
    int bits = sz * 8; n &= 31; a &= SZMASK(sz); b &= SZMASK(sz);
    if (!n) return a;
    uint64_t v = ((uint64_t)b << bits) | a;                  /* b:a */
    uint32_t r = (uint32_t)(v >> n) & SZMASK(sz);
    uint32_t cf = (uint32_t)((v >> (n - 1)) & 1);
    setx(mkflags(sz, r, cf, ((r ^ a) & SIGNBIT(sz)) != 0, 0));
    return r;
}
void op_mul(int sz, uint32_t b)
{
    if (sz == 1) { uint32_t r = (EAX & 0xFF) * (b & 0xFF); EAX = (EAX & 0xFFFF0000u) | (r & 0xFFFF); uint32_t c = (r >> 8) != 0; setx(mkflags(1, r, c, c, 0)); }
    else if (sz == 2) { uint32_t r = (EAX & 0xFFFF) * (b & 0xFFFF); EAX = (EAX & 0xFFFF0000u) | (r & 0xFFFF); EDX = (EDX & 0xFFFF0000u) | (r >> 16); uint32_t c = (r >> 16) != 0; setx(mkflags(2, r, c, c, 0)); }
    else { uint64_t r = (uint64_t)EAX * b; EAX = (uint32_t)r; EDX = (uint32_t)(r >> 32); uint32_t c = EDX != 0; setx(mkflags(4, EAX, c, c, 0)); }
}
void op_imul1(int sz, uint32_t b)
{
    if (sz == 1) { int32_t r = (int8_t)EAX * (int8_t)b; EAX = (EAX & 0xFFFF0000u) | ((uint32_t)r & 0xFFFF); uint32_t c = r != (int8_t)r; setx(mkflags(1, (uint32_t)r, c, c, 0)); }
    else if (sz == 2) { int32_t r = (int16_t)EAX * (int16_t)b; EAX = (EAX & 0xFFFF0000u) | ((uint32_t)r & 0xFFFF); EDX = (EDX & 0xFFFF0000u) | (((uint32_t)r >> 16) & 0xFFFF); uint32_t c = r != (int16_t)r; setx(mkflags(2, (uint32_t)r, c, c, 0)); }
    else { int64_t r = (int64_t)(int32_t)EAX * (int32_t)b; EAX = (uint32_t)r; EDX = (uint32_t)((uint64_t)r >> 32); uint32_t c = r != (int32_t)r; setx(mkflags(4, EAX, c, c, 0)); }
}
uint32_t op_imul2(int sz, uint32_t a, uint32_t b)
{
    int64_t r; uint32_t c, res;
    if (sz == 2) { r = (int64_t)(int16_t)a * (int16_t)b; res = (uint32_t)r & 0xFFFF; c = r != (int16_t)r; }
    else { r = (int64_t)(int32_t)a * (int32_t)b; res = (uint32_t)r; c = r != (int32_t)r; }
    setx(mkflags(sz, res, c, c, 0));
    return res;
}
void op_div(int sz, uint32_t b, uint32_t at)
{
    char msg[64];
    if (sz == 1) { b &= 0xFF; uint32_t n = EAX & 0xFFFF; if (!b || n / b > 0xFF) goto de; EAX = (EAX & 0xFFFF0000u) | ((n % b) << 8) | (n / b); }
    else if (sz == 2) { b &= 0xFFFF; uint32_t n = ((EDX & 0xFFFF) << 16) | (EAX & 0xFFFF); if (!b || n / b > 0xFFFF) goto de;
        EAX = (EAX & 0xFFFF0000u) | (n / b); EDX = (EDX & 0xFFFF0000u) | (n % b); }
    else { uint64_t n = ((uint64_t)EDX << 32) | EAX; if (!b || n / b > 0xFFFFFFFFull) goto de; EAX = (uint32_t)(n / b); EDX = (uint32_t)(n % b); }
    return;
de: snprintf(msg, sizeof msg, "divide error at %08x", at); xl_host_fatal(msg);
}
void op_idiv(int sz, uint32_t b, uint32_t at)
{
    char msg[64];
    if (sz == 1) { int32_t d = (int8_t)b, n = (int16_t)EAX; if (!d || (n == -32768 && d == -1)) goto de; int32_t q = n / d, r = n % d; if (q != (int8_t)q) goto de;
        EAX = (EAX & 0xFFFF0000u) | (((uint32_t)r & 0xFF) << 8) | ((uint32_t)q & 0xFF); }
    else if (sz == 2) { int32_t d = (int16_t)b, n = (int32_t)(((EDX & 0xFFFF) << 16) | (EAX & 0xFFFF)); if (!d) goto de; int64_t q = (int64_t)n / d, r = (int64_t)n % d; if (q != (int16_t)q) goto de;
        EAX = (EAX & 0xFFFF0000u) | ((uint32_t)q & 0xFFFF); EDX = (EDX & 0xFFFF0000u) | ((uint32_t)r & 0xFFFF); }
    else { int64_t d = (int32_t)b, n = (int64_t)(((uint64_t)EDX << 32) | EAX); if (!d || (n == INT64_MIN && d == -1)) goto de; int64_t q = n / d, r = n % d; if (q != (int32_t)q) goto de;
        EAX = (uint32_t)q; EDX = (uint32_t)r; }
    return;
de: snprintf(msg, sizeof msg, "divide error at %08x", at); xl_host_fatal(msg);
}
void op_bt(uint32_t v, uint32_t bit) { uint32_t f = xl_flags(); setx((f & ~1u) | ((v >> (bit & 31)) & 1)); }
uint32_t op_bsf(int sz, uint32_t v, uint32_t old)
{
    uint32_t f = xl_flags() & ~0x40u; v &= SZMASK(sz);
    if (!v) { setx(f | 0x40); return old; }
    uint32_t i = 0; while (!((v >> i) & 1)) i++;
    setx(f); return i;
}
uint32_t op_bsr(int sz, uint32_t v, uint32_t old)
{
    uint32_t f = xl_flags() & ~0x40u; v &= SZMASK(sz);
    if (!v) { setx(f | 0x40); return old; }
    uint32_t i = sz * 8 - 1; while (!((v >> i) & 1)) i--;
    setx(f); return i;
}
void op_cpuid(void)
{
    if (EAX == 0) { EAX = 1; EBX = 0x756e6547; EDX = 0x49656e69; ECX = 0x6c65746e; }   /* GenuineIntel */
    else { EAX = 0x0633; EBX = 0; ECX = 0; EDX = 0x00008011; }                        /* i686: FPU TSC CMOV */
}
uint64_t xl_rdtsc(void) { return xl.icount * 16; }

/* ---- string instructions (flat model: es = ds) ---- */
static uint32_t acc(int sz) { return EAX & SZMASK(sz); }
static void setacc(int sz, uint32_t v) { if (sz == 4) EAX = v; else EAX = (EAX & ~SZMASK(sz)) | (v & SZMASK(sz)); }
static uint32_t rdn(int sz, uint32_t a) { return sz == 1 ? rd8(a) : sz == 2 ? rd16(a) : rd32(a); }
static void wrn(int sz, uint32_t a, uint32_t v) { if (sz == 1) wr8(a, v); else if (sz == 2) wr16(a, v); else wr32(a, v); }
void xl_movs(int sz, int rep)
{
    int32_t d = xl.df ? -sz : sz;
    if (!rep) { wrn(sz, EDI, rdn(sz, ESI)); ESI += d; EDI += d; return; }
    while (ECX) { wrn(sz, EDI, rdn(sz, ESI)); ESI += d; EDI += d; ECX--; }
}
void xl_stos(int sz, int rep)
{
    int32_t d = xl.df ? -sz : sz; uint32_t v = acc(sz);
    if (!rep) { wrn(sz, EDI, v); EDI += d; return; }
    while (ECX) { wrn(sz, EDI, v); EDI += d; ECX--; }
}
void xl_lods(int sz, int rep)
{
    int32_t d = xl.df ? -sz : sz;
    if (!rep) { setacc(sz, rdn(sz, ESI)); ESI += d; return; }
    while (ECX) { setacc(sz, rdn(sz, ESI)); ESI += d; ECX--; }
}
void xl_cmps(int sz, int rep)
{
    int32_t d = xl.df ? -sz : sz;
    if (!rep) { op_sub(sz, rdn(sz, ESI), rdn(sz, EDI)); ESI += d; EDI += d; return; }
    while (ECX) {
        op_sub(sz, rdn(sz, ESI), rdn(sz, EDI)); ESI += d; EDI += d; ECX--;
        if (rep == 1 ? !xl_zf() : xl_zf()) break;
    }
}
void xl_scas(int sz, int rep)
{
    int32_t d = xl.df ? -sz : sz;
    if (!rep) { op_sub(sz, acc(sz), rdn(sz, EDI)); EDI += d; return; }
    while (ECX) {
        op_sub(sz, acc(sz), rdn(sz, EDI)); EDI += d; ECX--;
        if (rep == 1 ? !xl_zf() : xl_zf()) break;
    }
}

/* ---- control flow ---- */
void xl_call(uint32_t target)
{
    uint32_t lo = 0, hi = xl_entry_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (xl_entry_addrs[mid] < target) lo = mid + 1; else hi = mid;
    }
    if (lo < xl_entry_count && xl_entry_addrs[lo] == target) { xl_entry_fns[lo](); return; }
    if (xl_interp_owns(target)) { xl_interp(target); return; }
    char msg[80]; snprintf(msg, sizeof msg, "indirect branch to untranslated %08x (from %08x)", target, xl.eip);
    xl_host_fatal(msg);
}
void xl_badret(uint32_t expected, uint32_t at)
{
    char msg[96]; snprintf(msg, sizeof msg, "return to %08x where %08x expected (call at %08x)", xl.ret, expected, at);
    xl_host_fatal(msg);
}
void xl_unimpl(uint32_t addr, const char *what)
{
    char msg[128]; snprintf(msg, sizeof msg, "unimplemented instruction at %08x: %s", addr, what);
    xl_host_fatal(msg);
}
void xl_segload(uint32_t addr, int seg, uint32_t sel)
{
    if (seg == 5) { xl.gs = (uint16_t)sel; xl.gs_base = xl_tls_base[(sel >> 3) & 7]; return; }   /* gs */
    (void)addr;   /* ds/es/ss/fs loads are no-ops in the flat model */
}
void xl_int(uint32_t addr, int n)
{
    if (n == 0x80) { xl_host_syscall(); return; }
    char msg[64]; snprintf(msg, sizeof msg, "int %02x at %08x", n, addr); xl_host_fatal(msg);
}

/* ---- x87, decoded by opcode (D8..DF) and ModRM; ea = memory operand address for the memory forms.
 * Arithmetic is done in the host's 80-bit long double (x86-64: the same x87 format and rounding). */
#include <math.h>
#define ST(i) xl.st[(xl.ftop + (i)) & 7]
static void fpush(long double v) { xl.ftop = (xl.ftop - 1) & 7; ST(0) = v; }
static void fpop(void) { xl.ftop = (xl.ftop + 1) & 7; }
static long double ld_m32(uint32_t a) { float f; uint32_t v = rd32(a); memcpy(&f, &v, 4); return f; }
static long double ld_m64(uint32_t a) { double d; uint64_t v = rd64(a); memcpy(&d, &v, 8); return d; }
static long double ld_m80(uint32_t a) { long double x = 0; for (int k = 0; k < 10; k++) ((uint8_t *)&x)[k] = rd8(a + k); return x; }
static void st_m32(uint32_t a, long double x) { float f = (float)x; uint32_t v; memcpy(&v, &f, 4); wr32(a, v); }
static void st_m64(uint32_t a, long double x) { double d = (double)x; uint64_t v; memcpy(&v, &d, 8); wr64(a, v); }
static void st_m80(uint32_t a, long double x) { for (int k = 0; k < 10; k++) wr8(a + k, ((uint8_t *)&x)[k]); }
static long double fround(long double x)
{
    switch ((xl.fcw >> 10) & 3) {
    case 1: return floorl(x);
    case 2: return ceill(x);
    case 3: return truncl(x);
    default: { long double f = floorl(x), d = x - f;
        if (d > 0.5L || (d == 0.5L && fmodl(f, 2.0L) != 0)) f += 1; return f; }
    }
}
static int64_t fist_val(long double x, int bits)
{
    long double r = fround(x);
    long double lim = bits == 16 ? 32768.0L : bits == 32 ? 2147483648.0L : 9223372036854775808.0L;
    if (!(r >= -lim && r < lim)) return bits == 16 ? -32768 : bits == 32 ? INT32_MIN : INT64_MIN;
    return (int64_t)r;
}
static void fcom_sw(long double a, long double b)
{
    uint16_t c = isnan(a) || isnan(b) ? 0x4500 : a > b ? 0 : a < b ? 0x0100 : 0x4000;   /* C3 C2 C0 */
    xl.fsw = (uint16_t)((xl.fsw & ~0x4700) | c);
}
static void fcomi_fl(long double a, long double b)
{
    uint32_t f = xl_flags() & ~0x8D5u;   /* clear OF SF ZF AF PF CF */
    if (isnan(a) || isnan(b)) f |= 0x45; else if (a < b) f |= 1; else if (a == b) f |= 0x40;
    xl_setflags(f);
}
static long double arith(int op, long double a, long double b)   /* D8-style op on (st0-ish a, operand b) */
{
    switch (op) {
    case 0: return a + b; case 1: return a * b; case 4: return a - b; case 5: return b - a;
    case 6: return a / b; default: return b / a;
    }
}
static uint16_t fsw_now(void) { return (uint16_t)((xl.fsw & ~0x3800) | ((xl.ftop & 7) << 11)); }
void xl_x87(uint32_t addr, int op, uint32_t ea, int modrm)
{
    int reg = (modrm >> 3) & 7, i = modrm & 7, mem = modrm < 0xC0;
    char msg[64];
    if (mem) {
        switch (op) {
        case 0xD8: case 0xDC: case 0xDA: case 0xDE: {
            long double m = op == 0xD8 ? ld_m32(ea) : op == 0xDC ? ld_m64(ea) : op == 0xDA ? (long double)(int32_t)rd32(ea) : (long double)(int16_t)rd16(ea);
            if (reg == 2 || reg == 3) { fcom_sw(ST(0), m); if (reg == 3) fpop(); }
            else ST(0) = arith(reg, ST(0), m);
            return; }
        case 0xD9:
            if (reg == 0) { fpush(ld_m32(ea)); return; }
            if (reg == 2) { st_m32(ea, ST(0)); return; }
            if (reg == 3) { st_m32(ea, ST(0)); fpop(); return; }
            if (reg == 5) { xl.fcw = rd16(ea); return; }
            if (reg == 7) { wr16(ea, xl.fcw); return; }
            break;
        case 0xDB:
            if (reg == 0) { fpush((long double)(int32_t)rd32(ea)); return; }
            if (reg == 1) { wr32(ea, (uint32_t)(ST(0) >= 2147483648.0L || ST(0) < -2147483648.0L || isnan(ST(0)) ? INT32_MIN : (int32_t)truncl(ST(0)))); fpop(); return; }
            if (reg == 2 || reg == 3) { wr32(ea, (uint32_t)fist_val(ST(0), 32)); if (reg == 3) fpop(); return; }
            if (reg == 5) { fpush(ld_m80(ea)); return; }
            if (reg == 7) { st_m80(ea, ST(0)); fpop(); return; }
            break;
        case 0xDD:
            if (reg == 0) { fpush(ld_m64(ea)); return; }
            if (reg == 2) { st_m64(ea, ST(0)); return; }
            if (reg == 3) { st_m64(ea, ST(0)); fpop(); return; }
            if (reg == 7) { wr16(ea, fsw_now()); return; }
            break;
        case 0xDF:
            if (reg == 0) { fpush((long double)(int16_t)rd16(ea)); return; }
            if (reg == 2 || reg == 3) { wr16(ea, (uint32_t)fist_val(ST(0), 16)); if (reg == 3) fpop(); return; }
            if (reg == 5) { fpush((long double)(int64_t)rd64(ea)); return; }
            if (reg == 7) { wr64(ea, (uint64_t)fist_val(ST(0), 64)); fpop(); return; }
            break;
        }
    } else {
        switch (op) {
        case 0xD8:
            if (reg == 2 || reg == 3) { fcom_sw(ST(0), ST(i)); if (reg == 3) fpop(); return; }
            ST(0) = arith(reg, ST(0), ST(i)); return;
        case 0xD9:
            if (reg == 0) { long double v = ST(i); fpush(v); return; }
            if (reg == 1) { long double t = ST(0); ST(0) = ST(i); ST(i) = t; return; }
            switch (modrm) {
            case 0xD0: return;
            case 0xE0: ST(0) = -ST(0); return;
            case 0xE1: ST(0) = fabsl(ST(0)); return;
            case 0xE4: fcom_sw(ST(0), 0.0L); return;
            case 0xE8: fpush(1.0L); return;
            case 0xEE: fpush(0.0L); return;
            case 0xF8: { long double q = ST(0) / ST(1); ST(0) = ST(0) - truncl(q) * ST(1); xl.fsw &= ~0x0400; return; }  /* fprem (full) */
            case 0xFA: ST(0) = sqrtl(ST(0)); return;
            case 0xFC: ST(0) = fround(ST(0)); return;
            case 0xFD: ST(0) = ldexpl(ST(0), (int)truncl(ST(1))); return;
            }
            break;
        case 0xDA: if (modrm == 0xE9) { fcom_sw(ST(0), ST(1)); fpop(); fpop(); return; } break;
        case 0xDB:
            if (modrm == 0xE2) { xl.fsw &= 0x7F00; return; }
            if (modrm == 0xE3) { xl.fcw = 0x37F; xl.fsw = 0; xl.ftop = 0; return; }
            if (reg == 5 || reg == 6) { fcomi_fl(ST(0), ST(i)); return; }
            break;
        case 0xDC:   /* st(i) = st(i) op st(0), with the reversed pairs of the encoding */
            if (reg == 0) { ST(i) = ST(i) + ST(0); return; }
            if (reg == 1) { ST(i) = ST(i) * ST(0); return; }
            if (reg == 4) { ST(i) = ST(0) - ST(i); return; }
            if (reg == 5) { ST(i) = ST(i) - ST(0); return; }
            if (reg == 6) { ST(i) = ST(0) / ST(i); return; }
            if (reg == 7) { ST(i) = ST(i) / ST(0); return; }
            break;
        case 0xDD:
            if (reg == 0) return;                                  /* ffree */
            if (reg == 2) { ST(i) = ST(0); return; }
            if (reg == 3) { ST(i) = ST(0); fpop(); return; }
            if (reg == 4 || reg == 5) { fcom_sw(ST(0), ST(i)); if (reg == 5) fpop(); return; }
            break;
        case 0xDE:
            if (modrm == 0xD9) { fcom_sw(ST(0), ST(1)); fpop(); fpop(); return; }
            if (reg == 0) { ST(i) = ST(i) + ST(0); fpop(); return; }
            if (reg == 1) { ST(i) = ST(i) * ST(0); fpop(); return; }
            if (reg == 4) { ST(i) = ST(0) - ST(i); fpop(); return; }
            if (reg == 5) { ST(i) = ST(i) - ST(0); fpop(); return; }
            if (reg == 6) { ST(i) = ST(0) / ST(i); fpop(); return; }
            if (reg == 7) { ST(i) = ST(i) / ST(0); fpop(); return; }
            break;
        case 0xDF:
            if (modrm == 0xE0) { EAX = (EAX & 0xFFFF0000u) | fsw_now(); return; }
            if (reg == 5 || reg == 6) { fcomi_fl(ST(0), ST(i)); fpop(); return; }
            break;
        }
    }
    snprintf(msg, sizeof msg, "x87 %02x %02x at %08x", op, modrm, addr);
    xl_host_fatal(msg);
}
