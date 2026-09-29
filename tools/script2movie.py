#!/usr/bin/env python3
"""script2movie.py IN.script STEPS > OUT.movie: an sfx-run step script (SDL scancodes held, picture pixels)
into a gate movie: one line per step, the keys held as the harness's tokens, x=/y= when the mouse moves.
'# size W H' lines in the script give the picture size its pixels are in (default 320x200)."""
import sys, math
sys.path.insert(0, __file__.rsplit('/', 2)[0] + '/waterbox')
from tables import BUTTONS
CHAR = {'Key Up': 'U', 'Key Down': 'D', 'Key Left': 'L', 'Key Right': 'R', 'Key Enter': 'N', 'Key Space': '_',
        'Key Backspace': 'B', 'Key Escape': 'X', 'Key LeftShift': 'S', 'Mouse Left Button': '[', 'Mouse Right Button': ']'}
def token(name):
    if name in CHAR: return CHAR[name]
    k = name[4:]
    if len(k) == 1: return k.lower()
    if k.startswith('F') and k[1:].isdigit(): return ' ' + k + ' '
    return None
by_sc = {b[2]: b[0] for b in BUTTONS if b[1] == 'key'}
events, W, H = [], 320, 200
for line in open(sys.argv[1]):
    p = line.split()
    if not p: continue
    if p[0] == '#' and len(p) >= 4 and p[1] == 'size': W, H = int(p[2]), int(p[3]); continue
    if p[0].startswith('#'): continue
    events.append((int(p[1]), p, W, H))
held, lines = {}, []
for st in range(int(sys.argv[2])):
    axis = ''
    for s, p, w, h in events:
        if s != st: continue
        if p[0] == 'key': held[by_sc[int(p[2])]] = int(p[3])
        elif p[0] == 'button': held['Mouse Left Button' if p[2] == 'left' else 'Mouse Right Button'] = int(p[3])
        elif p[0] == 'mouse': axis = ' x=%d y=%d' % (math.ceil(int(p[2]) * 65536 / w), math.ceil(int(p[3]) * 65536 / h))
    keys = ''.join(token(n) for n, v in held.items() if v and token(n))
    lines.append((keys.strip() + axis).strip())
print('\n'.join(lines))
