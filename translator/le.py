"""LE (Linear Executable) loader for the DOS/4GW-bound ARCHON.EXE: objects, page data, fixups."""
import struct

class LE:
    def __init__(self, path):
        d = open(path, 'rb').read(); self.d = d
        o = d.find(b'LE\x00\x00', 0x20000)
        assert o > 0; self.o = o
        h = d[o:o + 0xC4]
        (self.cpu, self.os, self.ver, self.flags, self.npages, self.eip_obj, self.eip, self.esp_obj, self.esp,
         self.pagesize, self.lastpage, self.fixsize) = struct.unpack_from('<HHIIIIIIIIII', h, 8)
        (self.objtab, self.nobj, self.pagemap, _, _, self.resid) = struct.unpack_from('<IIIIII', h, 0x40)
        self.fixpgtab, self.fixrectab = struct.unpack_from('<II', h, 0x68)
        self.datapages = struct.unpack_from('<I', h, 0x80)[0]
        # bound-exe: object data lives at (file offset of first page) found by locating entry stub; verified against DOSBox
        self.objs = []
        for i in range(self.nobj):
            vs, base, fl, pmi, np_, _ = struct.unpack_from('<IIIIII', d, o + self.objtab + i * 24)
            self.objs.append(dict(size=vs, base=base, flags=fl, pmi=pmi, npages=np_))
        self.datastart = 0x37214          # file offset of page 1 (checked by the Watcom _start stub at +0x26D2E)
        self.pagesize = 4096
        self.mem = {}
        for n, ob in enumerate(self.objs):
            buf = bytearray(ob['npages'] * self.pagesize)
            for p in range(ob['npages']):
                pg = ob['pmi'] - 1 + p
                src = self.datastart + pg * self.pagesize
                buf[p * self.pagesize:(p + 1) * self.pagesize] = d[src:src + self.pagesize] if src < len(d) else b''
            self.mem[n + 1] = buf[:ob['size']] if ob['size'] <= len(buf) else buf
        self.fixups = self._parse_fixups()

    def _parse_fixups(self):
        d, o = self.d, self.o
        fp = [struct.unpack_from('<I', d, o + self.fixpgtab + i * 4)[0] for i in range(self.npages + 1)]
        out = []   # (page, src_off_in_page, kind, target_obj, target_off)
        for pg in range(self.npages):
            p = o + self.fixrectab + fp[pg]; end = o + self.fixrectab + fp[pg + 1]
            while p < end:
                st, fl = d[p], d[p + 1]; p += 2
                src = None
                if st & 0x20:  # source list
                    cnt = d[p]; p += 1
                else:
                    src = struct.unpack_from('<h', d, p)[0]; p += 2
                tt = fl & 3
                assert tt == 0, ('only internal refs expected', hex(p), hex(st), hex(fl))
                if fl & 0x40: tobj = struct.unpack_from('<H', d, p)[0]; p += 2
                else: tobj = d[p]; p += 1
                toff = 0
                if (st & 0xF) != 2:
                    if fl & 0x10: toff = struct.unpack_from('<I', d, p)[0]; p += 4
                    else: toff = struct.unpack_from('<H', d, p)[0]; p += 2
                if st & 0x20:
                    srcs = [struct.unpack_from('<h', d, p + 2 * i)[0] for i in range(cnt)]; p += 2 * cnt
                else: srcs = [src]
                if fl & 0x04: p += 4 if fl & 0x20 else 2   # additive value
                for s in srcs: out.append((pg, s, st & 0xF, tobj, toff))
        return out

if __name__ == '__main__':
    import sys, collections
    le = LE(sys.argv[1])
    print('objects', [(hex(o['base']), hex(o['size']), o['pmi'], o['npages']) for o in le.objs])
    print('fixups', len(le.fixups), collections.Counter((f[2], f[3]) for f in le.fixups))
    code = le.mem[1]; print('entry bytes', code[le.eip:le.eip + 8].hex())
