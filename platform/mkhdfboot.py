#!/usr/bin/env python3
"""mkhdfboot.py out.hdf launch.bin payload.bin name=path ... : ONE bootable hard-disk image for the A1200.

Kickstart 3.1 boots hard disks through the filesystem (it does not run a partition bootblock), so the image carries one FFS volume DH0 (label ARCHON)
with S/Startup-Sequence, 'ArchonUltra' (an AmigaDOS executable = launch.bin + the stage-2 payload) and the game's data files.  The game reads its
data through AmigaDOS (SYS:name), so it works with any kind of drive (UAE hardfile, IDE, SCSI, CF ...).

Geometry: 512-byte blocks, 16 heads x 63 sectors = 1008 blocks per cylinder.  Cylinder 0 = RDB (RDSK block 0, PART block 1).
"""
import sys, struct

BS, SPT, HEADS = 512, 63, 16
CYL = SPT * HEADS
HT = 72


def csum_set(b, off=20):
    b[off:off + 4] = bytes(4)
    s = sum(struct.unpack('>128I', bytes(b))) & 0xFFFFFFFF
    b[off:off + 4] = struct.pack('>I', (-s) & 0xFFFFFFFF)


def put(b, off, v): struct.pack_into('>I', b, off, v & 0xFFFFFFFF)


def bstr(b, off, s, maxlen):
    b[off] = len(s); b[off + 1:off + 1 + len(s)] = s.encode()


def hashname(name, n=HT):
    h = len(name)
    for c in name.upper():
        h = (h * 13 + ord(c)) & 0x7FF
    return h % n


class FFS:
    """Minimal FFS ('DOS\\1') volume writer (one partition starting at partition block 0)."""
    def __init__(self, nblocks, label):
        self.n = nblocks; self.blocks = {}; self.root = nblocks // 2
        self.next_free = 2; self.used = {self.root}; self.label = label
        self.root_tab = [0] * HT
        self.date = (9000, 600, 0)

    def alloc(self):
        while self.next_free in self.used: self.next_free += 1
        b = self.next_free; self.used.add(b); self.next_free += 1; assert b < self.n; return b

    def _hdr(self, blk, name, parent, sectype, comment=''):
        b = bytearray(BS)
        put(b, 0, 2); put(b, 4, blk)
        d, m, t = self.date
        put(b, 420, d); put(b, 424, m); put(b, 428, t)
        bstr(b, 432, name, 30); put(b, 500, parent); put(b, 508, sectype)
        return b

    def _link(self, table_owner, table_off, tab, name, blk, blocks):
        h = hashname(name)
        # chain: new entry goes to the head of its hash slot, the old head becomes nextSameHash
        old = tab[h]; tab[h] = blk
        if old: put(blocks[blk], 496, old)

    def add_file(self, name, data, parent_blk, parent_tab, parent_blocks_ref):
        hdr_blk = self.alloc()
        nd = (len(data) + BS - 1) // BS
        data_blks = [self.alloc() for _ in range(nd)]
        h = self._hdr(hdr_blk, name, parent_blk, 0xFFFFFFFD)
        put(h, 16, data_blks[0] if nd else 0); put(h, 324, len(data))
        put(h, 8, min(nd, 72))
        for i, db in enumerate(data_blks[:72]): put(h, 308 - 4 * i, db)          # data block table is stored backwards
        rest = data_blks[72:]; prev = h; ext_blks = []
        while rest:
            eb = self.alloc(); chunk, rest = rest[:72], rest[72:]
            e = bytearray(BS); put(e, 0, 16); put(e, 4, eb); put(e, 8, len(chunk)); put(e, 500, hdr_blk); put(e, 508, 0xFFFFFFFD)
            for i, db in enumerate(chunk): put(e, 308 - 4 * i, db)
            put(prev, 504, eb); ext_blks.append((eb, e)); prev = e
        for eb, e in ext_blks:
            csum_set(e); self.blocks[eb] = e
        for i, db in enumerate(data_blks):                                       # FFS: data blocks hold 512 raw bytes
            chunk = data[i * BS:(i + 1) * BS]
            self.blocks[db] = bytearray(chunk + bytes(BS - len(chunk)))
        self.blocks[hdr_blk] = h
        return hdr_blk, h

    def build(self, files, dirs):
        """files: [(dirname or None, name, data)]"""
        dirblk = {}
        for d in dirs:
            blk = self.alloc(); dirblk[d] = (blk, [0] * HT, self._hdr(blk, d, self.root, 2))
        for d, name, data in files:
            if d is None: parent, tab = self.root, self.root_tab
            else: parent, tab = dirblk[d][0], dirblk[d][1]
            hb, h = self.add_file(name, data, parent, tab, None)
            hsh = hashname(name); old = tab[hsh]; tab[hsh] = hb
            if old: put(h, 496, old)
            csum_set(h); self.blocks[hb] = h
        for d, (blk, tab, b) in dirblk.items():
            for i, v in enumerate(tab): put(b, 24 + 4 * i, v)
            csum_set(b); self.blocks[blk] = b
            h = hashname(d); old = self.root_tab[h]; self.root_tab[h] = blk
            if old: put(b, 496, old); csum_set(b)
        # bitmap blocks: one per 4064 blocks (127 longs x 32), listed in the root block
        total = self.n - 2; npages = (total + 4063) // 4064
        bm_blocks = [self.alloc() for _ in range(npages)]
        for k, bm in enumerate(bm_blocks):
            words = [0xFFFFFFFF] * 127
            for j in range(127 * 32):
                i = k * 4064 + j
                if i >= total or (i + 2) in self.used:
                    words[j // 32] &= ~(1 << (j % 32)) & 0xFFFFFFFF
            bmb = bytearray(BS); struct.pack_into('>127I', bmb, 4, *words)
            put(bmb, 0, (-(sum(words) & 0xFFFFFFFF)) & 0xFFFFFFFF); self.blocks[bm] = bmb
        # root block
        r = bytearray(BS); put(r, 0, 2); put(r, 12, HT)
        for i, v in enumerate(self.root_tab): put(r, 24 + 4 * i, v)
        put(r, 312, 0xFFFFFFFF)
        for k, bm in enumerate(bm_blocks): put(r, 316 + 4 * k, bm)
        d, m, t = self.date
        for off in (472, 484): put(r, off, d); put(r, off + 4, m); put(r, off + 8, t)
        put(r, 420, d); put(r, 424, m); put(r, 428, t)
        bstr(r, 432, self.label, 30); put(r, 508, 1)
        csum_set(r); self.blocks[self.root] = r
        # boot block (not bootable code, only the DOS type; the system boots through the filesystem)
        bb = bytearray(2 * BS); bb[0:4] = b'DOS\1'; put(bb, 8, self.root); self.blocks[0] = bb[:BS]; self.blocks[1] = bb[BS:]

    def image(self):
        img = bytearray(self.n * BS)
        for k, b in self.blocks.items(): img[k * BS:k * BS + len(b)] = b
        return img


def hunk_exe(code):
    """AmigaDOS load file: one code hunk (relocatable-free)."""
    code = bytes(code) + bytes((-len(code)) % 4)
    n = len(code) // 4
    return struct.pack('>IIIIII', 0x3F3, 0, 1, 0, 0, n) + struct.pack('>II', 0x3E9, n) + code + struct.pack('>I', 0x3F2)


def rdb_blocks(cyls, part_lo, part_hi):
    def block(longs):
        longs = list(longs) + [0] * (128 - len(longs))
        n = longs[1]; longs[2] = 0; longs[2] = (-sum(longs[:n])) & 0xFFFFFFFF
        return struct.pack('>128I', *longs)
    def cstr(t, n): return list(struct.unpack('>%dI' % (n // 4), t.encode().ljust(n, b'\0')))
    rdb = [0x5244534B, 64, 0, 7, BS, 0x12, 0xFFFFFFFF, 1, 0xFFFFFFFF, 0xFFFFFFFF] + [0xFFFFFFFF] * 6 + \
          [cyls, SPT, HEADS, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] + \
          [0, CYL - 1, 1, cyls - 1, CYL, 0, 1, 0] + cstr('ARCHON', 8) + cstr('ULTRA-AMIGA', 16) + cstr('1.0', 4)
    rdb = rdb[:64] + [0] * (64 - len(rdb))
    nm = b'DH0'; name = list(struct.unpack('>8I', (bytes([len(nm)]) + nm).ljust(32, b'\0')))
    env = [16, 128, 0, HEADS, 1, SPT, 2, 0, 0, part_lo, part_hi, 30, 0, 0x7FFFFFFF, 0xFFFFFFFE, 0, 0x444F5301]
    part = [0x50415254, 64, 0, 7, 0xFFFFFFFF, 1, 0, 0, 0] + name + [0] * 15 + env
    part = part[:64] + [0] * (64 - len(part))
    return block(rdb), block(part)


def main():
    out, launchf, payf = sys.argv[1:4]
    files = [a.split('=', 1) for a in sys.argv[4:]]
    launch = bytearray(open(launchf, 'rb').read()); pay = open(payf, 'rb').read()
    struct.pack_into('>I', launch, len(launch) - 4, len(pay))                     # payload_len is the last long of the stub
    exe = hunk_exe(bytes(launch) + pay)
    contents = [(None, 'ArchonUltra', exe), ('s', 'Startup-Sequence', b'SYS:ArchonUltra\n')]
    for name, path in files: contents.append((None, name, open(path, 'rb').read()))
    need = 4 + sum(1 + (len(d) + BS - 1) // BS + (len(d) // BS // 72 + 1) for _, _, d in contents)
    need += need // 4064 + 4
    cyls_vol = (need + CYL - 1) // CYL + 1                                         # a spare cylinder
    nblocks = cyls_vol * CYL
    vol = FFS(nblocks, 'ARCHON')
    vol.build(contents, ['s'])
    part_lo, part_hi = 1, cyls_vol
    cyls = 1 + cyls_vol
    img = bytearray(cyls * CYL * BS)
    r, p = rdb_blocks(cyls, part_lo, part_hi)
    img[0:BS] = r; img[BS:2 * BS] = p
    v = vol.image(); img[CYL * BS:CYL * BS + len(v)] = v
    open(out, 'wb').write(img)
    print('%s: %d bytes, %d cylinders; volume DH0 (FFS) cylinders %d-%d, %d of %d blocks used' % (out, len(img), cyls, part_lo, part_hi, len(vol.used), nblocks))


main()
