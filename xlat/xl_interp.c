/* xl_interp.c - an interpreter for i386 code outside the static translation: code the program loads
 * at run time (a driver read from a data file), which elf2c never saw.
 *
 * It runs on the same machine state as the translated code - the registers and lazy flags in xl, the
 * arena, the same flag, shift, multiply, divide and string helpers - so the two call each other freely:
 * xl_call() hands it an address in a registered range, and it hands a call out of that range back to
 * xl_call(). It returns to its translated caller when a 'ret' pops the return address that caller
 * pushed. The embedder registers the ranges (xl_interp_range) and does the port I/O (xl_host_in/out).
 *
 * Flat 32-bit protected-mode code: segment registers read as the flat selector and load as nothing
 * (gs excepted, as in the translation); an unknown opcode is fatal, naming it. */
#include <stdio.h>
#include "xlat.h"

#define MAX_RANGES 4
static uint32_t rng_lo[MAX_RANGES], rng_hi[MAX_RANGES];
static int nrng;

void xl_interp_range(uint32_t lo, uint32_t hi)
{
    for (int k = 0; k < nrng; k++) if (rng_lo[k] == lo) { rng_hi[k] = hi; return; }
    if (nrng < MAX_RANGES) { rng_lo[nrng] = lo; rng_hi[nrng] = hi; nrng++; }
}
void xl_interp_reset(void) { nrng = 0; }
int xl_interp_owns(uint32_t a)
{
    for (int k = 0; k < nrng; k++) if (a >= rng_lo[k] && a < rng_hi[k]) return 1;
    return 0;
}

/* ---------------------------------------------------------------- decoding */
static uint32_t eip, at;            /* next byte, start of the instruction */
static uint8_t  f8(void)  { return rd8(eip++); }
static uint16_t f16(void) { uint16_t v = rd16(eip); eip += 2; return v; }
static uint32_t f32(void) { uint32_t v = rd32(eip); eip += 4; return v; }
static uint32_t fimm(int sz) { return sz == 1 ? f8() : sz == 2 ? f16() : f32(); }
static uint32_t sext(uint32_t v, int sz) { return sz == 1 ? (uint32_t)(int32_t)(int8_t)v : sz == 2 ? (uint32_t)(int32_t)(int16_t)v : v; }

static void fail(const char *what)
{
    char msg[160];
    snprintf(msg, sizeof msg, "interpreter: %s at %08x (bytes %02x %02x %02x %02x)", what, at, rd8(at), rd8(at + 1), rd8(at + 2), rd8(at + 3));
    xl_host_fatal(msg);
}

typedef struct { int mod, reg, rm, isreg, espbase; uint32_t ea; } Modrm;
static int seg_gs;
static void modrm(Modrm *m)
{
    uint8_t b = f8();
    m->mod = b >> 6; m->reg = (b >> 3) & 7; m->rm = b & 7; m->isreg = m->mod == 3; m->espbase = 0;
    if (m->isreg) return;
    uint32_t ea;
    if (m->rm == 4) {
        uint8_t sib = f8(); int sc = sib >> 6, idx = (sib >> 3) & 7, base = sib & 7;
        ea = (base == 5 && m->mod == 0) ? f32() : xl.r[base];
        m->espbase = base == 4;
        if (idx != 4) ea += xl.r[idx] << sc;
    } else if (m->rm == 5 && m->mod == 0) ea = f32();
    else ea = xl.r[m->rm];
    if (m->mod == 1) ea += (uint32_t)(int32_t)(int8_t)f8();
    else if (m->mod == 2) ea += f32();
    if (seg_gs) ea += xl.gs_base;
    m->ea = ea;
}

static uint32_t rreg(int r, int sz)
{
    if (sz == 4) return xl.r[r];
    if (sz == 2) return xl.r[r] & 0xFFFF;
    return r < 4 ? xl.r[r] & 0xFF : (xl.r[r - 4] >> 8) & 0xFF;
}
static void wreg(int r, int sz, uint32_t v)
{
    if (sz == 4) xl.r[r] = v;
    else if (sz == 2) xl.r[r] = (xl.r[r] & 0xFFFF0000u) | (v & 0xFFFF);
    else if (r < 4) xl.r[r] = (xl.r[r] & ~0xFFu) | (v & 0xFF);
    else xl.r[r - 4] = (xl.r[r - 4] & ~0xFF00u) | ((v & 0xFF) << 8);
}
static uint32_t rdm(uint32_t a, int sz) { return sz == 1 ? rd8(a) : sz == 2 ? rd16(a) : rd32(a); }
static void wrm(uint32_t a, int sz, uint32_t v) { if (sz == 1) wr8(a, v); else if (sz == 2) wr16(a, v); else wr32(a, v); }
static uint32_t rm_rd(const Modrm *m, int sz) { return m->isreg ? rreg(m->rm, sz) : rdm(m->ea, sz); }
static void rm_wr(const Modrm *m, int sz, uint32_t v) { if (m->isreg) wreg(m->rm, sz, v); else wrm(m->ea, sz, v); }
static void push(int sz, uint32_t v) { if (sz == 2) push16(v); else push32(v); }
static uint32_t pop(int sz) { return sz == 2 ? pop16() : pop32(); }

static uint32_t alu(int op, int sz, uint32_t a, uint32_t b)
{
    switch (op) {
    case 0: return op_add(sz, a, b);
    case 1: return op_logic(sz, a | b);
    case 2: return op_adc(sz, a, b);
    case 3: return op_sbb(sz, a, b);
    case 4: return op_logic(sz, a & b);
    case 5: return op_sub(sz, a, b);
    case 6: return op_logic(sz, a ^ b);
    default: op_sub(sz, a, b); return a;           /* cmp */
    }
}
static uint32_t shift(int op, int sz, uint32_t a, uint32_t n)
{
    switch (op) {
    case 0: return op_rol(sz, a, n);
    case 1: return op_ror(sz, a, n);
    case 2: return op_rcl(sz, a, n);
    case 3: return op_rcr(sz, a, n);
    case 4: case 6: return op_shl(sz, a, n);
    case 5: return op_shr(sz, a, n);
    default: return op_sar(sz, a, n);
    }
}

/* ---------------------------------------------------------------- execution */
/* a call to target from the interpreted instruction ending at 'next' */
static void call_to(uint32_t target, uint32_t next)
{
    push32(next);
    if (xl_interp_owns(target)) { eip = target; return; }
    xl.eip = at;
    xl_call(target);                                   /* translated: runs to its 'ret' */
    if (xl.ret != next) xl_badret(next, at);
    eip = next;
}
static void jump_to(uint32_t target)
{
    if (!xl_interp_owns(target)) fail("jump out of the interpreted code");
    eip = target;
}

void xl_interp(uint32_t target)
{
    uint32_t entry_esp = ESP;       /* holds the translated caller's return address */
    uint32_t save_eip = eip, save_at = at;
    eip = target;
    for (;;) {
        at = eip; xl.icount++;
        int osz = 4, rep = 0, asz16 = 0; seg_gs = 0;
        uint8_t op;
        for (;;) {                                     /* prefixes */
            op = f8();
            if (op == 0x66) osz = 2;
            else if (op == 0x67) asz16 = 1;
            else if (op == 0xF3) rep = 1;
            else if (op == 0xF2) rep = 2;
            else if (op == 0x65) seg_gs = 1;
            else if (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E || op == 0x64 || op == 0xF0) ;
            else break;
        }
        if (asz16 && op != 0xE3) fail("16-bit addressing");
        Modrm m; uint32_t v, t;
        int sz = (op & 1) ? osz : 1;                   /* for the ops whose low bit picks byte/full */
        switch (op) {
        /* ---- arithmetic: op Eb,Gb / Ev,Gv / Gb,Eb / Gv,Ev / AL,Ib / eAX,Iv */
        case 0x00: case 0x01: case 0x08: case 0x09: case 0x10: case 0x11: case 0x18: case 0x19:
        case 0x20: case 0x21: case 0x28: case 0x29: case 0x30: case 0x31: case 0x38: case 0x39:
            modrm(&m); v = alu(op >> 3, sz, rm_rd(&m, sz), rreg(m.reg, sz));
            if ((op >> 3) != 7) rm_wr(&m, sz, v);
            break;
        case 0x02: case 0x03: case 0x0A: case 0x0B: case 0x12: case 0x13: case 0x1A: case 0x1B:
        case 0x22: case 0x23: case 0x2A: case 0x2B: case 0x32: case 0x33: case 0x3A: case 0x3B:
            modrm(&m); v = alu(op >> 3, sz, rreg(m.reg, sz), rm_rd(&m, sz));
            if ((op >> 3) != 7) wreg(m.reg, sz, v);
            break;
        case 0x04: case 0x05: case 0x0C: case 0x0D: case 0x14: case 0x15: case 0x1C: case 0x1D:
        case 0x24: case 0x25: case 0x2C: case 0x2D: case 0x34: case 0x35: case 0x3C: case 0x3D:
            v = alu(op >> 3, sz, rreg(0, sz), fimm(sz));
            if ((op >> 3) != 7) wreg(0, sz, v);
            break;
        case 0x06: case 0x0E: case 0x16: case 0x1E: push(osz, 0x2B); break;        /* push es/cs/ss/ds */
        case 0x07: case 0x17: case 0x1F: pop(osz); break;                          /* pop es/ss/ds */
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
            wreg(op & 7, osz, op_inc(osz, rreg(op & 7, osz))); break;
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
            wreg(op & 7, osz, op_dec(osz, rreg(op & 7, osz))); break;
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
            push(osz, rreg(op & 7, osz)); break;
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            v = pop(osz); wreg(op & 7, osz, v); break;
        case 0x60: { uint32_t sp = ESP; for (int r = 0; r < 8; r++) push(osz, r == 4 ? sp : rreg(r, osz)); break; }
        case 0x61: for (int r = 7; r >= 0; r--) { v = pop(osz); if (r != 4) wreg(r, osz, v); } break;
        case 0x68: push(osz, fimm(osz)); break;
        case 0x6A: push(osz, sext(f8(), 1)); break;
        case 0x69: modrm(&m); v = rm_rd(&m, osz); wreg(m.reg, osz, op_imul2(osz, v, fimm(osz))); break;
        case 0x6B: modrm(&m); v = rm_rd(&m, osz); wreg(m.reg, osz, op_imul2(osz, v, sext(f8(), 1))); break;
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F:
            t = sext(f8(), 1); if (xl_cond(op & 15)) jump_to(eip + t); break;
        case 0x80: case 0x81: case 0x83:
            sz = op == 0x80 ? 1 : osz; modrm(&m);
            t = op == 0x83 ? sext(f8(), 1) : fimm(sz);
            v = alu(m.reg, sz, rm_rd(&m, sz), t);
            if (m.reg != 7) rm_wr(&m, sz, v);
            break;
        case 0x84: case 0x85: modrm(&m); op_logic(sz, rm_rd(&m, sz) & rreg(m.reg, sz)); break;
        case 0x86: case 0x87: modrm(&m); v = rm_rd(&m, sz); rm_wr(&m, sz, rreg(m.reg, sz)); wreg(m.reg, sz, v); break;
        case 0x88: case 0x89: modrm(&m); rm_wr(&m, sz, rreg(m.reg, sz)); break;
        case 0x8A: case 0x8B: modrm(&m); wreg(m.reg, sz, rm_rd(&m, sz)); break;
        case 0x8C: modrm(&m); rm_wr(&m, m.isreg ? osz : 2, m.reg == 5 ? xl.gs : 0x2B); break;
        case 0x8D: modrm(&m); if (m.isreg) fail("lea of a register"); wreg(m.reg, osz, m.ea - (seg_gs ? xl.gs_base : 0)); break;
        case 0x8E: modrm(&m); xl_segload(at, m.reg == 5 ? 5 : m.reg, rm_rd(&m, 2)); break;
        case 0x8F: modrm(&m); v = pop(osz); if (m.espbase) m.ea += (uint32_t)osz; rm_wr(&m, osz, v); break;   /* esp: after the pop */
        case 0x90: break;
        case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
            v = rreg(0, osz); wreg(0, osz, rreg(op & 7, osz)); wreg(op & 7, osz, v); break;
        case 0x98: if (osz == 2) wreg(0, 2, sext(EAX & 0xFF, 1)); else EAX = sext(EAX & 0xFFFF, 2); break;
        case 0x99: if (osz == 2) wreg(2, 2, (EAX & 0x8000) ? 0xFFFF : 0); else EDX = (EAX & 0x80000000u) ? 0xFFFFFFFFu : 0; break;
        case 0x9C: push(osz, xl_flags()); break;
        case 0x9D: v = pop(osz); if (osz == 2) v = (xl_flags() & 0xFFFF0000u) | v; xl_setflags(v); break;
        case 0x9E: xl_setflags((xl_flags() & ~0xD5u) | ((EAX >> 8) & 0xD5u)); break;
        case 0x9F: EAX = (EAX & 0xFFFF00FFu) | ((xl_flags() & 0xD5u) << 8); break;
        case 0xA0: case 0xA1: t = f32(); wreg(0, sz, rdm(t + (seg_gs ? xl.gs_base : 0), sz)); break;
        case 0xA2: case 0xA3: t = f32(); wrm(t + (seg_gs ? xl.gs_base : 0), sz, rreg(0, sz)); break;
        case 0xA4: case 0xA5: xl_movs(sz, rep != 0); break;
        case 0xA6: case 0xA7: xl_cmps(sz, rep); break;
        case 0xA8: case 0xA9: op_logic(sz, rreg(0, sz) & fimm(sz)); break;
        case 0xAA: case 0xAB: xl_stos(sz, rep != 0); break;
        case 0xAC: case 0xAD: xl_lods(sz, rep != 0); break;
        case 0xAE: case 0xAF: xl_scas(sz, rep); break;
        case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
            wreg(op & 7, 1, f8()); break;
        case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
            wreg(op & 7, osz, fimm(osz)); break;
        case 0xC0: case 0xC1: modrm(&m); v = rm_rd(&m, sz); t = f8(); rm_wr(&m, sz, shift(m.reg, sz, v, t)); break;
        case 0xD0: case 0xD1: modrm(&m); rm_wr(&m, sz, shift(m.reg, sz, rm_rd(&m, sz), 1)); break;
        case 0xD2: case 0xD3: modrm(&m); rm_wr(&m, sz, shift(m.reg, sz, rm_rd(&m, sz), ECX & 0xFF)); break;
        case 0xC2: case 0xC3: {
            uint32_t n = op == 0xC2 ? f16() : 0;
            if (ESP == entry_esp) { xl.ret = pop32(); ESP += n; eip = save_eip; at = save_at; return; }
            eip = pop32(); ESP += n;
            if (!xl_interp_owns(eip)) fail("return out of the interpreted code");
            break; }
        case 0xC6: case 0xC7: modrm(&m); rm_wr(&m, sz, fimm(sz)); break;
        case 0xC8: { uint32_t n = f16(); if (f8()) fail("enter with nesting"); push32(EBP); EBP = ESP; ESP -= n; break; }
        case 0xC9: ESP = EBP; EBP = pop(osz); break;
        case 0xD7: wreg(0, 1, rd8(EBX + (EAX & 0xFF))); break;
        case 0xE0: case 0xE1: case 0xE2: {
            t = sext(f8(), 1); ECX--;
            int go = ECX != 0 && (op == 0xE2 || (op == 0xE1 ? xl_zf() : !xl_zf()));
            if (go) jump_to(eip + t);
            break; }
        case 0xE3: t = sext(f8(), 1); if ((asz16 ? ECX & 0xFFFF : ECX) == 0) jump_to(eip + t); break;
        case 0xE4: case 0xE5: t = f8(); wreg(0, sz, xl_host_in(t, sz)); break;
        case 0xE6: case 0xE7: t = f8(); xl_host_out(t, rreg(0, sz), sz); break;
        case 0xEC: case 0xED: wreg(0, sz, xl_host_in(EDX & 0xFFFF, sz)); break;
        case 0xEE: case 0xEF: xl_host_out(EDX & 0xFFFF, rreg(0, sz), sz); break;
        case 0xE8: t = f32(); call_to(eip + t, eip); break;
        case 0xE9: t = f32(); jump_to(eip + t); break;
        case 0xEB: t = sext(f8(), 1); jump_to(eip + t); break;
        case 0xF5: xl_setflags(xl_flags() ^ 1u); break;
        case 0xF6: case 0xF7:
            modrm(&m); v = rm_rd(&m, sz);
            switch (m.reg) {
            case 0: case 1: op_logic(sz, v & fimm(sz)); break;
            case 2: rm_wr(&m, sz, ~v); break;
            case 3: rm_wr(&m, sz, op_neg(sz, v)); break;
            case 4: op_mul(sz, v); break;
            case 5: op_imul1(sz, v); break;
            case 6: op_div(sz, v, at); break;
            default: op_idiv(sz, v, at); break;
            }
            break;
        case 0xF8: xl_setflags(xl_flags() & ~1u); break;
        case 0xF9: xl_setflags(xl_flags() | 1u); break;
        case 0xFA: case 0xFB: break;                                           /* cli, sti */
        case 0xFC: xl.df = 0; break;
        case 0xFD: xl.df = 1; break;
        case 0xFE: modrm(&m); v = rm_rd(&m, 1);
            if (m.reg == 0) rm_wr(&m, 1, op_inc(1, v)); else if (m.reg == 1) rm_wr(&m, 1, op_dec(1, v)); else fail("opcode FE");
            break;
        case 0xFF: modrm(&m);
            switch (m.reg) {
            case 0: rm_wr(&m, osz, op_inc(osz, rm_rd(&m, osz))); break;
            case 1: rm_wr(&m, osz, op_dec(osz, rm_rd(&m, osz))); break;
            case 2: call_to(rm_rd(&m, 4), eip); break;
            case 4: jump_to(rm_rd(&m, 4)); break;
            case 6: push(osz, rm_rd(&m, osz)); break;
            default: fail("far call/jump");
            }
            break;
        case 0x0F: {
            uint8_t op2 = f8();
            if (op2 >= 0x80 && op2 <= 0x8F) { t = osz == 2 ? sext(f16(), 2) : f32(); if (xl_cond(op2 & 15)) jump_to(eip + t); break; }
            if (op2 >= 0x90 && op2 <= 0x9F) { modrm(&m); rm_wr(&m, 1, (uint32_t)xl_cond(op2 & 15)); break; }
            if (op2 >= 0xC8 && op2 <= 0xCF) { v = xl.r[op2 & 7]; xl.r[op2 & 7] = (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24); break; }
            switch (op2) {
            case 0xB6: case 0xB7: modrm(&m); wreg(m.reg, osz, rm_rd(&m, op2 == 0xB6 ? 1 : 2)); break;
            case 0xBE: case 0xBF: { int ssz = op2 == 0xBE ? 1 : 2; modrm(&m); wreg(m.reg, osz, sext(rm_rd(&m, ssz), ssz)); break; }
            case 0xAF: modrm(&m); wreg(m.reg, osz, op_imul2(osz, rreg(m.reg, osz), rm_rd(&m, osz))); break;
            case 0xA4: case 0xAC: modrm(&m); t = f8();
                rm_wr(&m, osz, op2 == 0xA4 ? op_shld(osz, rm_rd(&m, osz), rreg(m.reg, osz), t) : op_shrd(osz, rm_rd(&m, osz), rreg(m.reg, osz), t)); break;
            case 0xA5: case 0xAD: modrm(&m); t = ECX & 0xFF;
                rm_wr(&m, osz, op2 == 0xA5 ? op_shld(osz, rm_rd(&m, osz), rreg(m.reg, osz), t) : op_shrd(osz, rm_rd(&m, osz), rreg(m.reg, osz), t)); break;
            case 0xBC: modrm(&m); wreg(m.reg, osz, op_bsf(osz, rm_rd(&m, osz), rreg(m.reg, osz))); break;
            case 0xBD: modrm(&m); wreg(m.reg, osz, op_bsr(osz, rm_rd(&m, osz), rreg(m.reg, osz))); break;
            case 0xA3: case 0xAB: case 0xB3: case 0xBB: case 0xBA: {
                int kind; modrm(&m);
                if (op2 == 0xBA) { kind = m.reg - 4; if (kind < 0) fail("0F BA"); t = f8() & (osz * 8 - 1); }
                else { kind = op2 == 0xA3 ? 0 : op2 == 0xAB ? 1 : op2 == 0xB3 ? 2 : 3; t = rreg(m.reg, osz);
                       if (!m.isreg) { m.ea += (uint32_t)(((int32_t)t >> 5) * 4); } t &= osz * 8 - 1; }
                v = rm_rd(&m, osz); op_bt(v, t);
                if (kind == 1) rm_wr(&m, osz, v | (1u << t));
                else if (kind == 2) rm_wr(&m, osz, v & ~(1u << t));
                else if (kind == 3) rm_wr(&m, osz, v ^ (1u << t));
                break; }
            default: fail("unimplemented opcode 0F");
            }
            break; }
        default: fail("unimplemented opcode");
        }
    }
}
