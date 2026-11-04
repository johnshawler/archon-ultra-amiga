#!/usr/bin/env python3
"""mkhdfboot.py out.hdf boot_patched.bin payload.bin name=path ... : ONE bootable hard-disk image.
Layout (512-byte blocks, 16 heads x 63 sectors = 1008 blocks per cylinder): cylinder 0 = RDB (RDSK block 0, PART block 1);
partition DH0 'DOS\\0', bootable, from cylinder 1: 2 boot blocks (the bootblock), the boot payload, then the data table ('ARCH', file table,
files).  The bootblock reads the payload with the boot device's own IO request at an absolute byte offset (patched into it here); the game finds the
data table by scanning for the 'ARCH' signature.  The partition holds no DOS filesystem: it is only booted, never mounted for files."""
import sys, struct
out, bootf, payf = sys.argv[1:4]; files = [a.split('=', 1) for a in sys.argv[4:]]
BS, SPT, HEADS = 512, 63, 16; CYL = SPT * HEADS
boot = bytearray(open(bootf, 'rb').read()); assert boot[16:20] == b'ARCH' and len(boot) <= 1024
pay = open(payf, 'rb').read()
part_blk = CYL                                            # first block of the partition
payload_blk = part_blk + 2
pay_blocks = (len(pay) + BS - 1) // BS
table_blk = payload_blk + pay_blocks
ent = b''; lba = 1; data = b''
for name, path in files:
    d = open(path, 'rb').read()
    ent += name.encode().ljust(16, b'\0') + struct.pack('>II', lba, len(d)) + bytes(8)
    pad = (-len(d)) % BS; data += d + bytes(pad); lba += (len(d) + pad) // BS
hdr = b'ARCH' + bytes([len(files)]) + bytes(11) + ent; hdr += bytes(BS - len(hdr))
blob = hdr + data
end_blk = table_blk + len(blob) // BS
cyls = (end_blk + CYL - 1) // CYL
total = cyls * CYL
# bootblock: payload offset patched, DOS\0 + checksum
struct.pack_into('>I', boot, 24, payload_blk * BS)
bb = bytearray(boot + bytes(1024 - len(boot))); bb[0:4] = b'DOS\0'; bb[4:8] = bytes(4)
s = 0
for i in range(0, 1024, 4):
    s += struct.unpack('>I', bb[i:i + 4])[0]
    if s > 0xFFFFFFFF: s = (s & 0xFFFFFFFF) + 1
bb[4:8] = struct.pack('>I', (~s) & 0xFFFFFFFF)
def block(longs):
    longs = list(longs) + [0] * (128 - len(longs)); 
    n = longs[1]; longs[2] = 0; longs[2] = (-sum(longs[:n])) & 0xFFFFFFFF
    return struct.pack('>128I', *longs)
def bstr(name):
    b = bytes([len(name)]) + name.encode(); return list(struct.unpack('>8I', b.ljust(32, b'\0')))
def cstr(t, n): return list(struct.unpack('>%dI' % (n // 4), t.encode().ljust(n, b'\0')))
rdb = [0x5244534B, 64, 0, 7, BS, 0x12, 0xFFFFFFFF, 1, 0xFFFFFFFF, 0xFFFFFFFF] + [0xFFFFFFFF] * 6 + \
      [cyls, SPT, HEADS, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] + \
      [0, CYL - 1, 1, cyls - 1, CYL, 0, 1, 0] + cstr('ARCHON', 8) + cstr('ULTRA-AMIGA', 16) + cstr('1.0', 4) + [0] * 4
rdb = rdb[:64] + [0] * (64 - len(rdb))
env = [16, 128, 0, HEADS, 1, SPT, 2, 0, 0, 1, cyls - 1, 30, 0, 0x7FFFFFFF, 0xFFFFFFFE, 0, 0x444F5300] 
part = [0x50415254, 64, 0, 7, 0xFFFFFFFF, 1, 0, 0, 0] + bstr('DH0') + [0] * 15 + env + [0] * 15
part = part[:64] + [0] * (64 - len(part))
img = bytearray(total * BS)
img[0:512] = block(rdb); img[512:1024] = block(part)
img[part_blk * BS:part_blk * BS + 1024] = bb
img[payload_blk * BS:payload_blk * BS + len(pay)] = pay
img[table_blk * BS:table_blk * BS + len(blob)] = blob
open(out, 'wb').write(img); print('%s: %d bytes, %d cylinders, boot partition at block %d, payload at byte %d, data table at block %d' % (out, len(img), cyls, part_blk, payload_blk * BS, table_blk))
