import sys
a = [l.split() for l in open(sys.argv[1])]     # dosbox
b = [l.split() for l in open(sys.argv[2])]     # ours
a2 = []
for r in a:
    if a2 and a2[-1][0] == r[0] and len(a2) > 0: continue        # rep-string restarts in DOSBox
    a2.append(r)
a = a2
off = 0x16d000 - 0x110000
skip = set(int(x) for x in sys.argv[3].split(',')) if len(sys.argv) > 3 else set()   # register columns to ignore (1..7)
def norm(v):
    if 0x16d000 <= v < 0x1aa000: return v - 0x16d000 + 0x110000
    if 0x1aa000 <= v < 0x1ac000: return v - 0x1aa000 + 0x150000
    if 0x1ac000 <= v < 0x1e0000: return v - 0x1ac000 + 0x160000
    return v
raw = [list(r) for r in a]
for r in a:
    for j in range(1, 8): r[j] = '%08x' % norm(int(r[j], 16))
n = min(len(a), len(b))
raw2 = [];
for r in raw:
    if raw2 and raw2[-1][0] == r[0]: continue
    raw2.append(r)
raw = raw2
for k in range(n):
    ea = int(a[k][0], 16) - off; eb = int(b[k][0], 16)
    if ea != eb:
        print('EIP diverges at step', k, 'dosbox %x ours %x' % (ea, eb)); break
    bad = [j for j in range(1, 8) if j not in skip and int(a[k][j], 16) != int(b[k][j], 16) and not (0x190000 <= int(a[k][j], 16) < 0x194000 and 0x190000 <= int(b[k][j], 16) < 0x194000) and not (int(a[k][j], 16) >= 0x1a0000 and int(b[k][j], 16) >= 0x1a0000) and not ((int(raw[k][j], 16) & 0xFFFF) == (int(b[k][j], 16) & 0xFFFF) and int(a[k][j], 16) >> 16 in range(0x4, 0x20) and int(b[k][j], 16) >> 16 in range(0x1, 0x20))]
    if bad:
        print('REG diverge at step', k, 'eip %x' % eb, [(j, a[k][j], b[k][j]) for j in bad]); break
else:
    print('no divergence in', n, 'steps')
for j in range(max(0, k - 6), min(n, k + 1)): print(j, 'D', ' '.join(a[j]), '\n   O', ' '.join(b[j]))
