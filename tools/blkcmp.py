#!/usr/bin/env python3
"""blkcmp.py dosbox/blocks.bin ours/blocks_ours.bin : compare the per-block checkpoints (executed-instruction hash, VGA memory hash, palette hash)."""
import sys, numpy as np
dt = np.dtype([('idx', '<u8'), ('eip', '<u4'), ('vga', '<u4'), ('dac', '<u4')])
a = np.fromfile(sys.argv[1], dtype=dt); b = np.fromfile(sys.argv[2], dtype=dt)
n = min(len(a), len(b))
print('blocks: dosbox %d, ours %d (compared %d, %d instructions each block %d..)' % (len(a), len(b), n, int(a['idx'][1] - a['idx'][0]) if n > 1 else 0, int(a['idx'][0]) if n else 0))
bad = [(k, f) for k in range(n) for f in ('idx', 'eip', 'vga', 'dac') if a[f][k] != b[f][k]]
firsts = {}
for k, f in bad: firsts.setdefault(f, k)
print('first mismatching block per field:', {f: (k, int(a['idx'][k])) for f, k in firsts.items()} or 'none')
print('mismatching blocks: eip %d, vga %d, palette %d' % tuple(sum(1 for k, f in bad if f == x) for x in ('eip', 'vga', 'dac')))
sys.exit(1 if bad else 0)
