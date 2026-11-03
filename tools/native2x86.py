import sys, subprocess, bisect
nm = subprocess.run(['m68k-linux-gnu-nm', '-n', sys.argv[1]], capture_output=True, text=True).stdout.split('\n')
syms = [(int(l.split()[0], 16), l.split()[2]) for l in nm if len(l.split()) == 3 and l.split()[2].startswith('L') and all(c in '0123456789abcdef' for c in l.split()[2][1:])]
addrs = [s[0] for s in syms]
for a in sys.argv[2:]:
    i = bisect.bisect_right(addrs, int(a, 16)) - 1
    print(a, '->', syms[i][1] if i >= 0 else '?')
