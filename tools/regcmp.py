import sys
def norm(v):
    if 0x16d000 <= v < 0x1aa000: return v - 0x16d000 + 0x110000
    if 0x1aa000 <= v < 0x1ac000: return v - 0x1aa000 + 0x150000
    if 0x1ac000 <= v < 0x1e0000: return v - 0x1ac000 + 0x160000
    return v
def same(x, y):
    nx = norm(x)
    if nx == y or x == y: return True
    if 0x190000 <= nx < 0x194000 and 0x190000 <= y < 0x194000: return True     # stack pointers differ by frame layout
    if (x & 0xFFFF) == (y & 0xFFFF) and 0x10 <= (x >> 16) <= 0x20 and 0x10 <= (y >> 16) <= 0x20: return True   # stale relocated upper half
    return False
a = [l.split() for l in open(sys.argv[1])]
a2 = []
for r in a:
    if a2 and a2[-1][0] == r[0]: continue
    a2.append(r)
b = [l.split() for l in open(sys.argv[2])]
names = ['eax', 'ecx', 'edx', 'ebx', 'ebp', 'esi', 'edi']
taint = set()
for k in range(min(len(a2), len(b))):
    eip_d = int(a2[k][0], 16) - 0x16d000; eip_o = int(b[k][0], 16) - 0x110000
    if eip_d != eip_o: print('eip diverge', k); break
    for j in range(1, 8):
        x = int(a2[k][j], 16); y = int(b[k][j], 16)
        if not same(x, y):
            if names[j - 1] not in taint:
                print('first mismatch', names[j - 1], 'at step', k, 'eip %x' % eip_o, 'dosbox %08x ours %08x' % (x, y))
                taint.add(names[j - 1])
        else:
            pass
    if len(taint) >= int(sys.argv[3]) if len(sys.argv) > 3 else False: break
