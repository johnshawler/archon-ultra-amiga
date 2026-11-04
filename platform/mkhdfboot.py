#!/usr/bin/env python3
"""mkhdfboot.py out.hdf launch.bin payload.bin name=path ... : ONE bootable hard-disk image for the A1200.

Kickstart 3.1 boots hard disks through the filesystem (it does not run a partition bootblock), so the image carries a small OFS volume DH0 with
S/Startup-Sequence and 'ArchonUltra', an AmigaDOS executable = launch.bin (the takeover code) + the stage-2 payload.  The game's data (ARCH table, the
PKG files, ...) lives in the raw blocks after that volume, outside any partition; the game finds it by scanning for the 'ARCH' signature.

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


class OFS:
    """Minimal OFS volume writer (one partition starting at partition block 0)."""
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
        nd = (len(data) + 487) // 488
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
        for i, db in enumerate(data_blks):
            blk = bytearray(BS); chunk = data[i * 488:(i + 1) * 488]
            put(blk, 0, 8); put(blk, 4, hdr_blk); put(blk, 8, i + 1); put(blk, 12, len(chunk)); put(blk, 16, data_blks[i + 1] if i + 1 < nd else 0)
            blk[24:24 + len(chunk)] = chunk
            csum_set(blk); self.blocks[db] = blk
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
        # bitmap block
        bm = self.alloc(); self.used.add(bm)
        words = [0xFFFFFFFF] * 127
        total = self.n - 2
        for i in range(total):
            blk = i + 2
            if blk in self.used:
                words[i // 32] &= ~(1 << (i % 32)) & 0xFFFFFFFF
        # blocks beyond the end of the partition are not free
        for i in range(total, 127 * 32):
            words[i // 32] &= ~(1 << (i % 32)) & 0xFFFFFFFF
        bmb = bytearray(BS); struct.pack_into('>127I', bmb, 4, *words)
        s = sum(words) & 0xFFFFFFFF; put(bmb, 0, (-s) & 0xFFFFFFFF); self.blocks[bm] = bmb
        # root block
        r = bytearray(BS); put(r, 0, 2); put(r, 12, HT)
        for i, v in enumerate(self.root_tab): put(r, 24 + 4 * i, v)
        put(r, 312, 0xFFFFFFFF); put(r, 316, bm)
        d, m, t = self.date
        for off in (472, 484): put(r, off, d); put(r, off + 4, m); put(r, off + 8, t)
        put(r, 420, d); put(r, 424, m); put(r, 428, t)
        bstr(r, 432, self.label, 30); put(r, 508, 1)
        csum_set(r); self.blocks[self.root] = r
        # boot block (not bootable code, only the DOS type; the system boots through the filesystem)
        bb = bytearray(2 * BS); bb[0:4] = b'DOS\0'; put(bb, 8, self.root); self.blocks[0] = bb[:BS]; self.blocks[1] = bb[BS:]

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
    env = [16, 128, 0, HEADS, 1, SPT, 2, 0, 0, part_lo, part_hi, 30, 0, 0x7FFFFFFF, 0xFFFFFFFE, 0, 0x444F5300]
    part = [0x50415254, 64, 0, 7, 0xFFFFFFFF, 1, 0, 0, 0] + name + [0] * 15 + env
    part = part[:64] + [0] * (64 - len(part))
    return block(rdb), block(part)


def main():
    out, launchf, payf = sys.argv[1:4]
    files = [a.split('=', 1) for a in sys.argv[4:]]
    launch = bytearray(open(launchf, 'rb').read()); pay = open(payf, 'rb').read()
    struct.pack_into('>I', launch, len(launch) - 4, len(pay))                     # payload_len is the last long of the stub
    exe = hunk_exe(bytes(launch) + pay)
    # the volume: only what the boot needs; ~2 cylinders
    nblocks = 2 * CYL
    vol = OFS(nblocks, 'ARCHON')
    vol.build([(None, 'ArchonUltra', exe), ('s', 'Startup-Sequence', b'SYS:ArchonUltra\n')], ['s'])
    part_lo, part_hi = 1, 2
    data_blk = (1 + 2) * CYL                                                       # raw data region starts at cylinder 3
    ent = b''; lba = 1; data = b''
    for name, path in files:
        d = open(path, 'rb').read()
        ent += name.encode().ljust(16, b'\0') + struct.pack('>II', lba, len(d)) + bytes(8)
        pad = (-len(d)) % BS; data += d + bytes(pad); lba += (len(d) + pad) // BS
    hdr = b'ARCH' + bytes([len(files)]) + bytes(11) + ent; hdr += bytes(BS - len(hdr))
    blob = hdr + data
    cyls = (data_blk + len(blob) // BS + CYL - 1) // CYL
    img = bytearray(cyls * CYL * BS)
    r, p = rdb_blocks(cyls, part_lo, part_hi)
    img[0:BS] = r; img[BS:2 * BS] = p
    v = vol.image(); img[CYL * BS:CYL * BS + len(v)] = v
    img[data_blk * BS:data_blk * BS + len(blob)] = blob
    open(out, 'wb').write(img)
    print('%s: %d bytes, %d cylinders; volume DH0 cylinders %d-%d, data table at block %d' % (out, len(img), cyls, part_lo, part_hi, data_blk))


main()
