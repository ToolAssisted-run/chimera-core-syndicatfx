#!/usr/bin/env python3
"""elf2c.py - translate a static, non-PIE i386 ELF (linked with --emit-relocs) into portable C.

usage: elf2c.py IN.elf OUTDIR [--files N]

Code discovery is recursive descent from the ELF entry, FUNC symbols, direct call targets and
code pointers (immediates and data words that relocations mark as addresses into code). Indirect
jumps through tables (jmp *T(,r,4)) get their targets from the table's relocated words.
Each entry becomes a C function over the runtime in xlat.h: the guest stack, registers and flags
are emulated exactly; calls push the real return address and check it on return.

Output: xl_image.c (the loadable segments and process-start facts), xl_fns.h, xl_table.c (sorted
entry table for indirect branches), xl_code_NN.c (the functions)."""
import sys, os, struct, collections
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
import capstone
from capstone import x86 as X

ARGS = sys.argv[1:]
nfiles = 16
if '--files' in ARGS:
    i = ARGS.index('--files'); nfiles = int(ARGS[i + 1]); del ARGS[i:i + 2]
hook_names = []
export_syms = []
while '--export-sym' in ARGS:    # --export-sym SYMBOL: its address as XLSYM_<SYMBOL> in xl_image.c/h
    i = ARGS.index('--export-sym'); export_syms.append(ARGS[i + 1]); del ARGS[i:i + 2]
while '--hook' in ARGS:          # --hook SYMBOL: call xl_hook(addr) at that function's entry (probes/verification)
    i = ARGS.index('--hook'); hook_names.append(ARGS[i + 1]); del ARGS[i:i + 2]
inpath, outdir = ARGS[0], ARGS[1]
os.makedirs(outdir, exist_ok=True)

f = open(inpath, 'rb'); elf = ELFFile(f)
assert elf['e_machine'] == 'EM_386' and elf['e_type'] == 'ET_EXEC'

# ---------------------------------------------------------------- image
segs = []
for s in elf.iter_segments():
    if s['p_type'] == 'PT_LOAD':
        segs.append((s['p_vaddr'], s['p_memsz'], s.data(), s['p_flags']))
def img_byte(a):
    for va, msz, data, _ in segs:
        if va <= a < va + len(data): return data[a - va]
    return None
def img32(a):
    b = [img_byte(a + k) for k in range(4)]
    return None if None in b else struct.unpack('<I', bytes(b))[0]
exec_ranges = []
for sec in elf.iter_sections():
    if sec['sh_flags'] & 4 and sec['sh_type'] == 'SHT_PROGBITS':   # SHF_EXECINSTR
        exec_ranges.append((sec['sh_addr'], sec['sh_addr'] + sec['sh_size'], sec.data(), sec.name))
def in_exec(a): return any(lo <= a < hi for lo, hi, _, _ in exec_ranges)
def code_bytes(a, n=16):
    for lo, hi, data, _ in exec_ranges:
        if lo <= a < hi: return data[a - lo: min(hi, a + n) - lo]
    return b''

# ---------------------------------------------------------------- symbols, relocations
symtab = elf.get_section_by_name('.symtab')
sym_at = collections.defaultdict(list); func_syms = set()
for s in symtab.iter_symbols():
    if s['st_shndx'] in ('SHN_UNDEF', 'SHN_ABS') or not s.name: continue
    sym_at[s['st_value']].append(s.name)
    if s['st_info']['type'] == 'STT_FUNC' and in_exec(s['st_value']): func_syms.add(s['st_value'])
abs_relocs = {}          # site -> target (R_386_32 words: the value in the image is the final address)
for sec in elf.iter_sections():
    if isinstance(sec, RelocationSection):
        tgt_sec = elf.get_section(sec['sh_info'])
        if not (tgt_sec['sh_flags'] & 2): continue                   # SHF_ALLOC only
        for r in sec.iter_relocations():
            if r['r_info_type'] == 1:
                v = img32(r['r_offset'])
                if v is not None: abs_relocs[r['r_offset']] = v

# ---------------------------------------------------------------- decoding
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); md.detail = True
insns = {}               # addr -> insn
def decode(a):
    if a in insns: return insns[a]
    b = code_bytes(a)
    for i in md.disasm(b, a):
        insns[a] = i; return i
    return None

JCC = {X.X86_INS_JO: 0, X.X86_INS_JNO: 1, X.X86_INS_JB: 2, X.X86_INS_JAE: 3, X.X86_INS_JE: 4, X.X86_INS_JNE: 5,
       X.X86_INS_JBE: 6, X.X86_INS_JA: 7, X.X86_INS_JS: 8, X.X86_INS_JNS: 9, X.X86_INS_JP: 10, X.X86_INS_JNP: 11,
       X.X86_INS_JL: 12, X.X86_INS_JGE: 13, X.X86_INS_JLE: 14, X.X86_INS_JG: 15}
LOOPS = {X.X86_INS_JECXZ, X.X86_INS_JCXZ, X.X86_INS_LOOP, X.X86_INS_LOOPE, X.X86_INS_LOOPNE}
def is_term(i):
    return i.id in (X.X86_INS_JMP, X.X86_INS_RET, X.X86_INS_RETF, X.X86_INS_HLT, X.X86_INS_UD2, X.X86_INS_IRET, X.X86_INS_IRETD)
def imm_target(i):
    if i.operands and i.operands[0].type == X.X86_OP_IMM: return i.operands[0].imm & 0xFFFFFFFF
    return None
def table_jump(i):
    """jmp [T + reg*s] or jmp [T + reg] where T holds relocated code addresses -> T, else None"""
    if i.id != X.X86_INS_JMP or not i.operands or i.operands[0].type != X.X86_OP_MEM: return None
    m = i.operands[0].mem
    if (m.base != 0) + (m.index != 0) != 1 or not m.disp: return None
    t = m.disp & 0xFFFFFFFF
    return t if table_targets(t) else None
def table_targets(t):
    out = []; a = t
    while a in abs_relocs and in_exec(abs_relocs[a]):
        out.append(abs_relocs[a]); a += 4
    return out

entries = {elf['e_entry']} | set(func_syms)
code_ptr_sites = set()   # reloc sites that are immediates inside decoded instructions
seen_blocks = set(); jt_sites = set()
def explore(start):
    """decode everything reachable from start (intra- and inter-procedural); collect call targets as entries"""
    work = [start]
    while work:
        a = work.pop()
        if a in seen_blocks or not in_exec(a): continue
        seen_blocks.add(a)
        while True:
            i = decode(a)
            if i is None: break
            # relocations inside this instruction
            for k in range(i.size):
                s = a + k
                if s in abs_relocs:
                    tgt = abs_relocs[s]
                    if in_exec(tgt) and tgt not in abs_relocs and i.imm_offset and s == a + i.imm_offset and i.id not in (X.X86_INS_CALL, X.X86_INS_JMP):
                        code_ptr_sites.add(s)
                        if tgt not in entries: entries.add(tgt); work.append(tgt)
            t = imm_target(i)
            if i.id == X.X86_INS_CALL:
                if t is not None and t == a + i.size: pass
                elif t is not None and in_exec(t):
                    if t not in entries: entries.add(t)
                    work.append(t)
            elif i.id in JCC or i.id in LOOPS:
                work.append(t)
            elif i.id == X.X86_INS_JMP:
                if t is not None: work.append(t)
                tb = table_jump(i)
                if tb is not None:
                    for k, tt in enumerate(table_targets(tb)):
                        jt_sites.add(tb + 4 * k); work.append(tt)
            if is_term(i): break
            a += i.size
            if a in seen_blocks: break
    return

pending = list(entries)
while True:
    for e in pending: explore(e)
    # data words that point into code and are not jump-table entries: code pointers (function tables, vtables).
    # An address that is itself a relocated word is data (a table in the code section: no instruction starts
    # with an address), whatever points at it.
    new = []
    covered = set()
    for a, i in insns.items():
        covered.update(range(a, a + i.size))
    for s, tgt in abs_relocs.items():
        if s in jt_sites or s in covered or not in_exec(tgt) or tgt in abs_relocs: continue
        if tgt not in entries: entries.add(tgt); new.append(tgt)
    if not new: break
    pending = new
print('entries', len(entries), 'instructions', len(insns), file=sys.stderr)

# ---------------------------------------------------------------- functions
def successors(i):
    """intra-function successors of instruction i: list of (addr, kind); kind 'fall' or 'jump'"""
    out = []
    t = imm_target(i)
    if i.id in JCC or i.id in LOOPS: out = [(t, 'jump'), (i.address + i.size, 'fall')]
    elif i.id == X.X86_INS_JMP:
        if t is not None: out = [(t, 'jump')]
        else:
            tb = table_jump(i)
            if tb is not None: out = [(x, 'jump') for x in table_targets(tb)]
    elif is_term(i): out = []
    else: out = [(i.address + i.size, 'fall')]
    return out
def func_body(e):
    body = set(); work = [e]
    while work:
        a = work.pop()
        if a in body or a not in insns: continue
        if a != e and a in entries: continue          # another entry: reached as a tail call
        body.add(a)
        for s, _ in successors(insns[a]): work.append(s)
    return body

# ---------------------------------------------------------------- code generation
R32 = {X.X86_REG_EAX: 0, X.X86_REG_ECX: 1, X.X86_REG_EDX: 2, X.X86_REG_EBX: 3, X.X86_REG_ESP: 4, X.X86_REG_EBP: 5, X.X86_REG_ESI: 6, X.X86_REG_EDI: 7}
R16 = {X.X86_REG_AX: 0, X.X86_REG_CX: 1, X.X86_REG_DX: 2, X.X86_REG_BX: 3, X.X86_REG_SP: 4, X.X86_REG_BP: 5, X.X86_REG_SI: 6, X.X86_REG_DI: 7}
R8L = {X.X86_REG_AL: 0, X.X86_REG_CL: 1, X.X86_REG_DL: 2, X.X86_REG_BL: 3}
R8H = {X.X86_REG_AH: 0, X.X86_REG_CH: 1, X.X86_REG_DH: 2, X.X86_REG_BH: 3}
SEGS = {X.X86_REG_ES: 0, X.X86_REG_CS: 1, X.X86_REG_SS: 2, X.X86_REG_DS: 3, X.X86_REG_FS: 4, X.X86_REG_GS: 5}
MASK = {1: '0xFFu', 2: '0xFFFFu', 4: '0xFFFFFFFFu'}

class Unsupported(Exception): pass

def ea(m):
    parts = []
    if m.base: parts.append('xl.r[%d]' % R32[m.base])
    if m.index: parts.append('xl.r[%d]*%d' % (R32[m.index], m.scale) if m.scale != 1 else 'xl.r[%d]' % R32[m.index])
    if m.disp or not parts: parts.append('0x%xu' % (m.disp & 0xFFFFFFFF))
    e = '(uint32_t)(' + ' + '.join(parts) + ')'
    if m.segment == X.X86_REG_GS: e = '(uint32_t)(%s + xl.gs_base)' % e
    elif m.segment == X.X86_REG_FS: raise Unsupported('fs segment')
    return e
def reg_rd(r):
    if r in R32: return 'xl.r[%d]' % R32[r]
    if r in R16: return '(xl.r[%d] & 0xFFFFu)' % R16[r]
    if r in R8L: return '(xl.r[%d] & 0xFFu)' % R8L[r]
    if r in R8H: return '((xl.r[%d] >> 8) & 0xFFu)' % R8H[r]
    if r == X.X86_REG_GS: return '(uint32_t)xl.gs'
    if r in SEGS: return '0x2Bu'
    raise Unsupported('reg %d' % r)
def reg_wr(r, v):
    if r in R32: return 'xl.r[%d] = (uint32_t)(%s);' % (R32[r], v)
    if r in R16: return 'xl.r[{0}] = (xl.r[{0}] & 0xFFFF0000u) | ((uint32_t)({1}) & 0xFFFFu);'.format(R16[r], v)
    if r in R8L: return 'xl.r[{0}] = (xl.r[{0}] & 0xFFFFFF00u) | ((uint32_t)({1}) & 0xFFu);'.format(R8L[r], v)
    if r in R8H: return 'xl.r[{0}] = (xl.r[{0}] & 0xFFFF00FFu) | (((uint32_t)({1}) & 0xFFu) << 8);'.format(R8H[r], v)
    raise Unsupported('write reg %d' % r)
RD = {1: 'rd8', 2: 'rd16', 4: 'rd32'}; WR = {1: 'wr8', 2: 'wr16', 4: 'wr32'}

class Ctx:
    """per-instruction operand helper: memory addresses are computed once into A"""
    def __init__(self, i):
        self.i = i; self.pre = []; self.addr_done = False
    def addr(self, op):
        if not self.addr_done:
            self.pre.append('A = %s;' % ea(op.mem)); self.addr_done = True
        return 'A'
    def rd(self, op, size=None):
        sz = size or op.size
        if op.type == X.X86_OP_REG: return reg_rd(op.reg)
        if op.type == X.X86_OP_IMM: return '0x%xu' % (op.imm & int(MASK[sz][:-1], 16))
        if op.type == X.X86_OP_MEM: return '%s(%s)' % (RD[sz], self.addr(op))
        raise Unsupported('operand')
    def wr(self, op, v):
        if op.type == X.X86_OP_REG: return reg_wr(op.reg, v)
        if op.type == X.X86_OP_MEM: return '%s(%s, %s);' % (WR[op.size], self.addr(op), v)
        raise Unsupported('write operand')

CC = {'o': 0, 'no': 1, 'b': 2, 'c': 2, 'nae': 2, 'ae': 3, 'nb': 3, 'nc': 3, 'e': 4, 'z': 4, 'ne': 5, 'nz': 5,
      'be': 6, 'na': 6, 'a': 7, 'nbe': 7, 's': 8, 'ns': 9, 'p': 10, 'pe': 10, 'np': 11, 'po': 11,
      'l': 12, 'nge': 12, 'ge': 13, 'nl': 13, 'le': 14, 'ng': 14, 'g': 15, 'nle': 15}
def sext(v, sz):
    return '(uint32_t)(int32_t)(int8_t)(%s)' % v if sz == 1 else '(uint32_t)(int32_t)(int16_t)(%s)' % v if sz == 2 else v

def translate(i, body, fname):
    """C lines for instruction i inside function body"""
    c = Ctx(i); ops = i.operands; mn = i.mnemonic; iid = i.id; out = []
    nxt = i.address + i.size
    def goto(t, out):
        if t in body: out.append('goto L_%08x;' % t)
        elif t in entries: out.append('F_%08x(); return;' % t)
        else: out.append('xl.eip = 0x%08xu; xl_call(0x%08xu); return;' % (i.address, t))
    sz = ops[0].size if ops else 4
    if mn.startswith('lock '): mn = mn[5:]
    # ---- data movement
    if iid == X.X86_INS_MOV:
        d, s = ops
        if d.type == X.X86_OP_REG and d.reg in SEGS:
            out.append('xl_segload(0x%08xu, %d, %s);' % (i.address, SEGS[d.reg], c.rd(s, 2)))
        else:
            out.append(c.wr(d, c.rd(s)))
    elif iid in (X.X86_INS_MOVZX, X.X86_INS_MOVSX):
        d, s = ops
        v = c.rd(s)
        out.append(c.wr(d, sext(v, s.size) if iid == X.X86_INS_MOVSX else v))
    elif iid == X.X86_INS_LEA:
        out.append(reg_wr(ops[0].reg, ea(ops[1].mem).replace(' + xl.gs_base', '')))
    elif iid == X.X86_INS_PUSH:
        s = ops[0]
        if s.type == X.X86_OP_IMM: v = '0x%xu' % (s.imm & 0xFFFFFFFF); psz = 4 if s.size != 2 else 2
        elif s.type == X.X86_OP_REG and s.reg in SEGS: v = reg_rd(s.reg); psz = 4
        else: v = c.rd(s); psz = s.size
        out.append('{ uint32_t v = %s; push%d(v); }' % (v, psz * 8))
    elif iid == X.X86_INS_POP:
        d = ops[0]; psz = 2 if d.size == 2 else 4
        if d.type == X.X86_OP_REG and d.reg in SEGS: out.append('xl_segload(0x%08xu, %d, pop32());' % (i.address, SEGS[d.reg]))
        else: out.append('{ uint32_t v = pop%d(); %s }' % (psz * 8, c.wr(d, 'v')))
    elif iid == X.X86_INS_XCHG:
        d, s = ops
        if d.type == X.X86_OP_REG and s.type == X.X86_OP_REG and d.reg == s.reg: pass
        else:
            out.append('{ uint32_t t1 = %s, t2 = %s; %s %s }' % (c.rd(d), c.rd(s), c.wr(d, 't2'), c.wr(s, 't1')))
    elif iid == X.X86_INS_CMPXCHG:
        d, s = ops
        acc = reg_rd({1: X.X86_REG_AL, 2: X.X86_REG_AX, 4: X.X86_REG_EAX}[d.size])
        out.append('{ uint32_t t = %s; op_sub(%d, %s, t); if (xl_zf()) { %s } else { %s } }' % (
            c.rd(d), d.size, acc, c.wr(d, c.rd(s)), reg_wr({1: X.X86_REG_AL, 2: X.X86_REG_AX, 4: X.X86_REG_EAX}[d.size], 't')))
    elif iid == X.X86_INS_XADD:
        d, s = ops
        out.append('{ uint32_t t = %s, u = %s; uint32_t r = op_add(%d, t, u); %s %s }' % (c.rd(d), c.rd(s), d.size, c.wr(s, 't'), c.wr(d, 'r')))
    elif iid == X.X86_INS_BSWAP:
        r = ops[0].reg
        out.append('{ uint32_t v = %s; %s }' % (reg_rd(r), reg_wr(r, '(v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24)')))
    elif iid == X.X86_INS_CDQ: out.append('EDX = (EAX & 0x80000000u) ? 0xFFFFFFFFu : 0;')
    elif iid == X.X86_INS_CWDE: out.append('EAX = (uint32_t)(int32_t)(int16_t)EAX;')
    elif iid == X.X86_INS_CBW: out.append('EAX = (EAX & 0xFFFF0000u) | ((uint32_t)(int16_t)(int8_t)EAX & 0xFFFFu);')
    elif iid == X.X86_INS_CWD: out.append('EDX = (EDX & 0xFFFF0000u) | ((EAX & 0x8000u) ? 0xFFFFu : 0);')
    # ---- arithmetic / logic
    elif iid in (X.X86_INS_ADD, X.X86_INS_SUB, X.X86_INS_ADC, X.X86_INS_SBB, X.X86_INS_CMP):
        d, s = ops
        fn = {X.X86_INS_ADD: 'op_add', X.X86_INS_SUB: 'op_sub', X.X86_INS_ADC: 'op_adc', X.X86_INS_SBB: 'op_sbb', X.X86_INS_CMP: 'op_sub'}[iid]
        sv = c.rd(s)
        if s.type == X.X86_OP_IMM and s.size < d.size: sv = '0x%xu' % (s.imm & int(MASK[d.size][:-1], 16))
        e = '%s(%d, %s, %s)' % (fn, d.size, c.rd(d), sv)
        out.append(e + ';' if iid == X.X86_INS_CMP else c.wr(d, e))
    elif iid in (X.X86_INS_AND, X.X86_INS_OR, X.X86_INS_XOR, X.X86_INS_TEST):
        d, s = ops
        o = {X.X86_INS_AND: '&', X.X86_INS_OR: '|', X.X86_INS_XOR: '^', X.X86_INS_TEST: '&'}[iid]
        sv = c.rd(s)
        if s.type == X.X86_OP_IMM: sv = '0x%xu' % (s.imm & int(MASK[d.size][:-1], 16))
        if iid == X.X86_INS_XOR and d.type == X.X86_OP_REG and s.type == X.X86_OP_REG and d.reg == s.reg:
            out.append(c.wr(d, 'op_logic(%d, 0)' % d.size))
        else:
            e = 'op_logic(%d, %s %s %s)' % (d.size, c.rd(d), o, sv)
            out.append(e + ';' if iid == X.X86_INS_TEST else c.wr(d, e))
    elif iid == X.X86_INS_NOT: out.append(c.wr(ops[0], '~%s' % c.rd(ops[0])))
    elif iid == X.X86_INS_NEG: out.append(c.wr(ops[0], 'op_neg(%d, %s)' % (sz, c.rd(ops[0]))))
    elif iid == X.X86_INS_INC: out.append(c.wr(ops[0], 'op_inc(%d, %s)' % (sz, c.rd(ops[0]))))
    elif iid == X.X86_INS_DEC: out.append(c.wr(ops[0], 'op_dec(%d, %s)' % (sz, c.rd(ops[0]))))
    elif iid in (X.X86_INS_SHL, X.X86_INS_SAL, X.X86_INS_SHR, X.X86_INS_SAR, X.X86_INS_ROL, X.X86_INS_ROR, X.X86_INS_RCL, X.X86_INS_RCR):
        fn = {X.X86_INS_SHL: 'op_shl', X.X86_INS_SAL: 'op_shl', X.X86_INS_SHR: 'op_shr', X.X86_INS_SAR: 'op_sar',
              X.X86_INS_ROL: 'op_rol', X.X86_INS_ROR: 'op_ror', X.X86_INS_RCL: 'op_rcl', X.X86_INS_RCR: 'op_rcr'}[iid]
        n = c.rd(ops[1], 1) if len(ops) > 1 else '1u'
        out.append(c.wr(ops[0], '%s(%d, %s, %s)' % (fn, sz, c.rd(ops[0]), n)))
    elif iid in (X.X86_INS_SHLD, X.X86_INS_SHRD):
        fn = 'op_shld' if iid == X.X86_INS_SHLD else 'op_shrd'
        out.append(c.wr(ops[0], '%s(%d, %s, %s, %s)' % (fn, sz, c.rd(ops[0]), c.rd(ops[1]), c.rd(ops[2], 1))))
    elif iid == X.X86_INS_MUL: out.append('op_mul(%d, %s);' % (sz, c.rd(ops[0])))
    elif iid == X.X86_INS_IMUL:
        if len(ops) == 1: out.append('op_imul1(%d, %s);' % (sz, c.rd(ops[0])))
        elif len(ops) == 2: out.append(c.wr(ops[0], 'op_imul2(%d, %s, %s)' % (sz, c.rd(ops[0]), c.rd(ops[1]))))
        else:
            k = ops[2].imm & 0xFFFFFFFF
            out.append(c.wr(ops[0], 'op_imul2(%d, %s, 0x%xu)' % (sz, c.rd(ops[1]), k)))
    elif iid == X.X86_INS_DIV: out.append('op_div(%d, %s, 0x%08xu);' % (sz, c.rd(ops[0]), i.address))
    elif iid == X.X86_INS_IDIV: out.append('op_idiv(%d, %s, 0x%08xu);' % (sz, c.rd(ops[0]), i.address))
    elif iid in (X.X86_INS_BT, X.X86_INS_BTS, X.X86_INS_BTR, X.X86_INS_BTC):
        d, s = ops; bits = d.size * 8
        if d.type == X.X86_OP_MEM and s.type == X.X86_OP_REG:
            c.pre.append('A = %s + (uint32_t)(((int32_t)%s >> %d) * %d);' % (ea(d.mem), reg_rd(s.reg), 5 if bits == 32 else 4, d.size)); c.addr_done = True
            bit = '(%s & %du)' % (reg_rd(s.reg), bits - 1)
        else:
            bit = '(%s & %du)' % (c.rd(s), bits - 1)
        v = c.rd(d)
        body_ = '{ uint32_t v = %s, b = %s; op_bt(v, b); ' % (v, bit)
        if iid == X.X86_INS_BTS: body_ += c.wr(d, 'v | (1u << b)')
        elif iid == X.X86_INS_BTR: body_ += c.wr(d, 'v & ~(1u << b)')
        elif iid == X.X86_INS_BTC: body_ += c.wr(d, 'v ^ (1u << b)')
        out.append(body_ + ' }')
    elif iid in (X.X86_INS_BSF, X.X86_INS_BSR):
        d, s = ops
        out.append(c.wr(d, '%s(%d, %s, %s)' % ('op_bsf' if iid == X.X86_INS_BSF else 'op_bsr', d.size, c.rd(s), c.rd(d))))
    elif mn.startswith('set'):
        out.append(c.wr(ops[0], 'xl_cond(%d)' % CC[mn[3:]]))
    elif mn.startswith('cmov'):
        out.append('if (xl_cond(%d)) { %s }' % (CC[mn[4:]], c.wr(ops[0], c.rd(ops[1]))))
    # ---- flags
    elif iid == X.X86_INS_CLD: out.append('xl.df = 0;')
    elif iid == X.X86_INS_STD: out.append('xl.df = 1;')
    elif iid == X.X86_INS_CLC: out.append('xl_setflags(xl_flags() & ~1u);')
    elif iid == X.X86_INS_STC: out.append('xl_setflags(xl_flags() | 1u);')
    elif iid == X.X86_INS_CMC: out.append('xl_setflags(xl_flags() ^ 1u);')
    elif iid in (X.X86_INS_PUSHFD, X.X86_INS_PUSHF): out.append('push32(xl_flags());')
    elif iid in (X.X86_INS_POPFD, X.X86_INS_POPF): out.append('xl_setflags(pop32());')
    elif iid == X.X86_INS_LAHF: out.append('EAX = (EAX & 0xFFFF00FFu) | ((xl_flags() & 0xD5u) << 8);')
    elif iid == X.X86_INS_SAHF: out.append('xl_setflags((xl_flags() & ~0xD5u) | ((EAX >> 8) & 0xD5u));')
    elif iid in (X.X86_INS_CLI, X.X86_INS_STI, X.X86_INS_NOP, X.X86_INS_ENDBR32, X.X86_INS_PAUSE, X.X86_INS_WAIT): pass
    elif iid == X.X86_INS_CPUID: out.append('op_cpuid();')
    elif iid == X.X86_INS_RDTSC: out.append('{ uint64_t t = xl_rdtsc(); EAX = (uint32_t)t; EDX = (uint32_t)(t >> 32); }')
    # ---- strings
    elif iid in (X.X86_INS_MOVSB, X.X86_INS_MOVSW, X.X86_INS_MOVSD, X.X86_INS_STOSB, X.X86_INS_STOSW, X.X86_INS_STOSD,
                 X.X86_INS_LODSB, X.X86_INS_LODSW, X.X86_INS_LODSD, X.X86_INS_CMPSB, X.X86_INS_CMPSW, X.X86_INS_CMPSD,
                 X.X86_INS_SCASB, X.X86_INS_SCASW, X.X86_INS_SCASD):
        name = capstone.Cs.insn_name(md, iid)
        kind, ch = name[:4], name[4]
        esz = {'b': 1, 'w': 2, 'd': 4}[ch]
        pre = i.prefix[0]
        rep = 0 if pre == 0 else (2 if pre == 0xF2 else 1)
        out.append('xl_%s(%d, %d);' % (kind, esz, rep))
    # ---- control flow
    elif iid in JCC:
        t = imm_target(i); tail = []
        goto(t, tail)
        out.append('if (xl_cond(%d)) { %s }' % (JCC[iid], ' '.join(tail)))
    elif iid in LOOPS:
        t = imm_target(i); tail = []; goto(t, tail)
        if iid == X.X86_INS_JECXZ: out.append('if (ECX == 0) { %s }' % ' '.join(tail))
        elif iid == X.X86_INS_JCXZ: out.append('if ((ECX & 0xFFFFu) == 0) { %s }' % ' '.join(tail))
        elif iid == X.X86_INS_LOOP: out.append('if (--ECX != 0) { %s }' % ' '.join(tail))
        elif iid == X.X86_INS_LOOPE: out.append('if (--ECX != 0 && xl_zf()) { %s }' % ' '.join(tail))
        else: out.append('if (--ECX != 0 && !xl_zf()) { %s }' % ' '.join(tail))
    elif iid == X.X86_INS_JMP:
        t = imm_target(i)
        if t is not None: goto(t, out)
        else:
            tb = table_jump(i)
            v = c.rd(ops[0])
            if tb is not None:
                tg = sorted(set(table_targets(tb)))
                cases = []
                for x in tg:
                    tail = []; goto(x, tail); cases.append('case 0x%08xu: %s' % (x, ' '.join(tail)))
                out.append('{ uint32_t T = %s; switch (T) { %s default: xl.eip = 0x%08xu; xl_call(T); return; } }' % (v, ' '.join(cases), i.address))
            else:
                intra = sorted(x for x in body if x in jump_targets_global)
                out.append('{ uint32_t T = %s; xl.eip = 0x%08xu; xl_call(T); return; }' % (v, i.address))
    elif iid == X.X86_INS_CALL:
        t = imm_target(i)
        if t is not None and t == nxt:
            out.append('push32(0x%08xu);' % nxt)      # get-pc idiom (call to the next instruction): a push
        elif t is not None:
            if t in entries: call = 'F_%08x();' % t
            else: call = 'xl.eip = 0x%08xu; xl_call(0x%08xu);' % (i.address, t)
            out.append('push32(0x%08xu); %s if (xl.ret != 0x%08xu) xl_badret(0x%08xu, 0x%08xu);' % (nxt, call, nxt, nxt, i.address))
        else:
            out.append('{ uint32_t T = %s; push32(0x%08xu); xl.eip = 0x%08xu; xl_call(T); if (xl.ret != 0x%08xu) xl_badret(0x%08xu, 0x%08xu); }' % (
                c.rd(ops[0]), nxt, i.address, nxt, nxt, i.address))
    elif iid == X.X86_INS_RET:
        if ops: out.append('xl.ret = pop32(); ESP += %du; return;' % (ops[0].imm & 0xFFFF))
        else: out.append('xl.ret = pop32(); return;')
    elif iid == X.X86_INS_LEAVE: out.append('ESP = EBP; EBP = pop32();')
    elif iid == X.X86_INS_ENTER:
        if ops[1].imm != 0: raise Unsupported('enter with nesting')
        out.append('push32(EBP); EBP = ESP; ESP -= %du;' % (ops[0].imm & 0xFFFF))
    elif iid == X.X86_INS_INT: out.append('xl_int(0x%08xu, %d);' % (i.address, ops[0].imm & 0xFF))
    elif iid == X.X86_INS_INT3: out.append('xl_int(0x%08xu, 3);' % i.address)
    elif iid in (X.X86_INS_HLT, X.X86_INS_UD2):
        out.append('xl_unimpl(0x%08xu, "%s"); return;' % (i.address, mn))
    elif 0xD8 <= i.opcode[0] <= 0xDF:
        mem = [o for o in ops if o.type == X.X86_OP_MEM]
        a = ea(mem[0].mem) if mem else '0'
        out.append('xl_x87(0x%08xu, 0x%02x, %s, 0x%02x);' % (i.address, i.opcode[0], a, i.modrm))
    elif iid in (X.X86_INS_WAIT,): pass
    elif iid == X.X86_INS_PUSHAL:
        out.append('{ uint32_t s = ESP; push32(EAX); push32(ECX); push32(EDX); push32(EBX); push32(s); push32(EBP); push32(ESI); push32(EDI); }')
    elif iid == X.X86_INS_POPAL:
        out.append('EDI = pop32(); ESI = pop32(); EBP = pop32(); ESP += 4; EBX = pop32(); EDX = pop32(); ECX = pop32(); EAX = pop32();')
    else:
        raise Unsupported(mn + ' ' + i.op_str)
    return c.pre + out

jump_targets_global = set()

def gen_function(e):
    body = func_body(e)
    lines = ['void F_%08x(void)' % e, '{', '    uint32_t A; (void)A;']
    if e in hook_addrs: lines.append('    if (xl_hook) xl_hook(0x%08xu);' % e)
    order = sorted(body)
    # labels needed: jump targets within the body, and the entry
    labels = {e}
    for a in order:
        i = insns[a]
        for s, kind in successors(i):
            if s in body and (kind == 'jump' or s != a + i.size): labels.add(s)
        if (a + i.size) not in body and not is_term(i) and successors(i):
            pass
    lines.append('    goto L_%08x;' % e)
    prev_end = None; unsupported = 0
    for idx, a in enumerate(order):
        i = insns[a]
        if prev_end is not None and prev_end != a:
            pass
        if a in labels or prev_end != a:
            lines.append('L_%08x:' % a)
        if prev_end != a or a in labels:
            pass
        try:
            code = translate(i, body, e)
        except Unsupported as ex:
            code = ['xl_unimpl(0x%08xu, "%s %s"); return;' % (a, i.mnemonic, i.op_str.replace('"', "'"))]
            unsupported += 1
        lines.append('    /* %08x %s %s */ xl.icount++;' % (a, i.mnemonic, i.op_str.replace('*/', '* /')))
        lines.extend('    ' + x for x in code)
        # fallthrough at the end of a straight-line run
        nxt = a + i.size
        succ = successors(i)
        if any(k == 'fall' for _, k in succ):
            nidx = order[idx + 1] if idx + 1 < len(order) else None
            if nidx != nxt:
                if nxt in body: lines.append('    goto L_%08x;' % nxt)
                elif nxt in entries: lines.append('    F_%08x(); return;' % nxt)
                else: lines.append('    xl.eip = 0x%08xu; xl_call(0x%08xu); return;' % (a, nxt))
        prev_end = nxt
    lines.append('}')
    return lines, unsupported

# ---------------------------------------------------------------- output
hook_addrs = set()
for s_ in symtab.iter_symbols():
    if s_.name in hook_names: hook_addrs.add(s_['st_value'])
ents = sorted(e for e in entries if e in insns)
with open(os.path.join(outdir, 'xl_fns.h'), 'w') as h:
    h.write('/* generated by elf2c.py - do not edit */\n#include "xlat.h"\n')
    for e in ents: h.write('void F_%08x(void);\n' % e)
with open(os.path.join(outdir, 'xl_table.c'), 'w') as t:
    t.write('/* generated by elf2c.py - do not edit */\n#include "xl_fns.h"\n')
    t.write('const uint32_t xl_entry_count = %d;\n' % len(ents))
    t.write('const uint32_t xl_entry_addrs[] = {\n' + ''.join('0x%08xu,\n' % e for e in ents) + '};\n')
    t.write('const XlFn xl_entry_fns[] = {\n' + ''.join('F_%08x,\n' % e for e in ents) + '};\n')
tot_unsup = 0; files = [[] for _ in range(nfiles)]
for k, e in enumerate(ents):
    lines, u = gen_function(e); tot_unsup += u
    files[k % nfiles].append('\n'.join(lines))
for k, fl in enumerate(files):
    with open(os.path.join(outdir, 'xl_code_%02d.c' % k), 'w') as o:
        o.write('/* generated by elf2c.py - do not edit */\n#include "xl_fns.h"\n\n')
        o.write('\n\n'.join(fl)); o.write('\n')
# image
phdr_addr = None
for s in elf.iter_segments():
    if s['p_type'] == 'PT_LOAD' and s['p_offset'] <= elf['e_phoff'] < s['p_offset'] + s['p_filesz']:
        phdr_addr = s['p_vaddr'] + elf['e_phoff'] - s['p_offset']
with open(os.path.join(outdir, 'xl_image.c'), 'w') as o:
    o.write('/* generated by elf2c.py - do not edit */\n#include <stdint.h>\n#include "xl_image.h"\n')
    for k, (va, msz, data, fl) in enumerate(segs):
        o.write('static const uint8_t seg%d[%d] = {' % (k, max(1, len(data))))
        o.write(','.join(str(b) for b in data) if data else '0'); o.write('};\n')
    o.write('const XlSegment xl_segments[] = {\n')
    for k, (va, msz, data, fl) in enumerate(segs):
        o.write('  {0x%08xu, 0x%xu, 0x%xu, %d, seg%d},\n' % (va, msz, len(data), fl, k))
    o.write('};\nconst uint32_t xl_segment_count = %d;\n' % len(segs))
    o.write('const uint32_t xl_elf_entry = 0x%08xu, xl_elf_phdr = 0x%08xu, xl_elf_phnum = %d, xl_elf_phent = %d;\n' % (
        elf['e_entry'], phdr_addr or 0, elf['e_phnum'], elf['e_phentsize']))
    symv = {s_.name: s_['st_value'] for s_ in symtab.iter_symbols() if s_.name in export_syms}
    for nm in export_syms:
        if nm not in symv: sys.exit('--export-sym: no symbol ' + nm)
        o.write('const uint32_t XLSYM_%s = 0x%08xu;\n' % (nm, symv[nm]))
with open(os.path.join(outdir, 'xl_image.h'), 'w') as o:
    o.write('/* generated by elf2c.py - do not edit */\n#include <stdint.h>\n'
            'typedef struct { uint32_t vaddr, memsz, filesz, flags; const uint8_t *data; } XlSegment;\n'
            'extern const XlSegment xl_segments[]; extern const uint32_t xl_segment_count;\n'
            'extern const uint32_t xl_elf_entry, xl_elf_phdr, xl_elf_phnum, xl_elf_phent;\n'
            + ''.join('extern const uint32_t XLSYM_%s;\n' % nm for nm in export_syms))
print('functions', len(ents), 'unsupported instruction sites', tot_unsup, file=sys.stderr)
