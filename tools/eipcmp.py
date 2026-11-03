import sys, array
def load(p):
    a = array.array('I'); a.frombytes(open(p, 'rb').read()); return a
def collapse(a, maxp=6):
    out = []
    for v in a:
        out.append(v)
        for p in range(1, maxp + 1):
            n = len(out)
            if n >= 2 * p and out[n - p:] == out[n - 2 * p:n - p]:
                del out[n - p:]
                break
    return out
d = load(sys.argv[1]); o = load(sys.argv[2]); o.byteswap()
if len(sys.argv) > 3: m = int(sys.argv[3]); d = d[:m]; o = o[:m]
dc = collapse(d); oc = collapse(o)
n = min(len(dc), len(oc))
print('collapsed lengths', len(dc), len(oc))
for k in range(n):
    if dc[k] != oc[k]:
        print('EIP diverges at collapsed step', k, 'dosbox %x ours %x (obj1 offsets)' % (dc[k], oc[k]))
        print('context dosbox:', ' '.join('%x' % x for x in dc[max(0, k - 12):k + 4]))
        print('context ours  :', ' '.join('%x' % x for x in oc[max(0, k - 12):k + 4]))
        break
else:
    print('no divergence in', n, 'collapsed steps')
