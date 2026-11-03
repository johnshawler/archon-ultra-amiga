import sys, struct
d = open(sys.argv[1], 'rb').read(); o = open(sys.argv[2], 'rb').read()
def norm(v):
    if 0x16d000 <= v < 0x1aa000: return v - 0x16d000 + 0x110000
    if 0x1aa000 <= v < 0x1ac000: return v - 0x1aa000 + 0x150000
    if 0x1ac000 <= v < 0x1e0000: return v - 0x1ac000 + 0x160000
    return v
n = min(len(d), len(o))
ok = bytearray(n)
for i in range(n):
    if d[i] == o[i]: ok[i] = 1
for i in range(n - 3):
    a = struct.unpack_from('<I', d, i)[0]; b = struct.unpack_from('<I', o, i)[0]
    if a != b and norm(a) == b:
        for k in range(4): ok[i + k] = 1
for i in range(n - 2):
    a = (d[i] | (d[i+1] << 8) | (d[i+2] << 16)); b = (o[i] | (o[i+1] << 8) | (o[i+2] << 16))
    if a != b and norm(a) == b:
        for k in range(3): ok[i + k] = 1
for i in range(n - 1):
    a = (d[i] | (d[i+1] << 8)); b = (o[i] | (o[i+1] << 8))
    if a == b: pass
bad = [i for i in range(n) if not ok[i]]
print(len(bad), 'unexplained differing bytes of', n)
runs = []
for i in bad:
    if runs and i - runs[-1][1] <= 8: runs[-1][1] = i
    else: runs.append([i, i])
for a, b in runs[:int(sys.argv[3]) if len(sys.argv) > 3 else 50]:
    print('+%06x..%06x (x86 %x): dosbox %s | ours %s' % (a, b, int(sys.argv[4], 16) + a if len(sys.argv) > 4 else a, d[a:b + 1].hex()[:96], o[a:b + 1].hex()[:96]))
