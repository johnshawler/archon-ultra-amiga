#!/usr/bin/env python3
"""mkhdf.py out.hdf name=path ... : raw data disk read by the Amiga build through the Gayle IDE port.  Sector 0: 'ARCH', nfiles, 32-byte entries
(name[16], lba, size big-endian); file data follows, each file starting on a sector boundary."""
import sys, struct, os
out = sys.argv[1]; files = [a.split('=', 1) for a in sys.argv[2:]]
ent = b''; lba = 1; data = b''
for name, path in files:
    d = open(path, 'rb').read()
    ent += name.encode().ljust(16, b'\0') + struct.pack('>II', lba, len(d)) + bytes(8)
    pad = (-len(d)) % 512; data += d + bytes(pad); lba += (len(d) + pad) // 512
hdr = b'ARCH' + bytes([len(files)]) + bytes(11) + ent
hdr += bytes(512 - len(hdr))
img = hdr + data
img += bytes((-len(img)) % (512 * 63 * 16))        # whole cylinders (16 heads x 63 sectors)
open(out, 'wb').write(img); print('%s: %d bytes' % (out, len(img)))
