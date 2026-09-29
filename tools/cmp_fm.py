#!/usr/bin/env python3
"""cmp_fm.py ORACLE_FMTRACE CORE_FMTRACE: the FM chip's register writes of the original (DOSBox-X's
oracle-run --fmtrace: "emulated_ms port value frame" per port write) against the core's (sfx-run --sound
--fmtrace: "pit_clock register value [T]" per register write). Prints how many writes agree in order,
register and value, and - from the first key-on - how far their times drift apart, relative to it."""
import sys
def oracle(path):
    out = []; idx = {}
    for line in open(path):
        t, port, val, fr = line.split(); port = int(port, 16); val = int(val, 16); t = float(t)
        if port & 1 == 0: idx[port & ~1] = val | (0x100 if (port & 2) and port not in (0x228,) else 0)
        else: out.append((t * 1000.0, idx.get(port & ~1, 0), val))    # us
    return out
def core(path):
    out = []
    for line in open(path):
        p = line.split(); out.append((int(p[0]) * 1e6 / 1193182, int(p[1], 16), int(p[2], 16)))
    return out
o, c = oracle(sys.argv[1]), core(sys.argv[2])
print('oracle writes', len(o), 'core writes', len(c))
n = min(len(o), len(c)); first = None
for i in range(n):
    if o[i][1:] != c[i][1:]: first = i; break
print('identical (reg,val) prefix:', first if first is not None else n)
if first is not None:
    for i in range(max(0, first - 3), min(n, first + 8)): print(i, 'oracle %03x=%02x' % o[i][1:], ' core %03x=%02x' % c[i][1:])
# timing: find the first key-on (reg B0-B8 with bit 5) in each, compare times of later writes relative to it
def keyon(s):
    for i, (t, r, v) in enumerate(s):
        if 0xB0 <= r <= 0xB8 and v & 0x20: return i
k1, k2 = keyon(o), keyon(c)
print('first key-on: oracle #%d, core #%d' % (k1, k2))
if k1 is not None and k2 is not None:
    diffs = []
    m = min(len(o) - k1, len(c) - k2)
    same = 0
    for j in range(m):
        a, b = o[k1 + j], c[k2 + j]
        if a[1:] != b[1:]: print('diverge at key-on+%d: oracle %03x=%02x core %03x=%02x' % (j, a[1], a[2], b[1], b[2])); break
        same += 1
        diffs.append(((a[0] - o[k1][0]) - (b[0] - c[k2][0])) / 1000.0)
    print('matching writes after the first key-on:', same, 'of', m)
    if diffs: print('relative time difference (ms): min %.2f max %.2f last %.2f' % (min(diffs), max(diffs), diffs[-1]))
