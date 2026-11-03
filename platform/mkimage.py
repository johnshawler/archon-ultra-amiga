#!/usr/bin/env python3
"""mkimage.py <elf> <outdir> <boot.bin> <stage2.bin> : build the ADF (bootblock + stage2 + LZ4 image) from the linked ELF."""
import sys, struct, subprocess, os
elf, out, bootbin, s2bin = sys.argv[1:5]
nm = subprocess.run(['m68k-linux-gnu-nm', elf], capture_output=True, text=True).stdout
sym = {l.split()[2]: int(l.split()[0], 16) for l in nm.splitlines() if len(l.split()) == 3}
raw_p = os.path.join(out, 'image.raw')
subprocess.check_call(['m68k-linux-gnu-objcopy', '-O', 'binary', elf, raw_p])
raw = bytearray(open(raw_p, 'rb').read())
assert len(raw) == sym['_raw_end'], (len(raw), sym['_raw_end'])
rel = subprocess.run(['m68k-linux-gnu-readelf', '-rW', elf], capture_output=True, text=True).stdout
offs = []
for l in rel.splitlines():
    p = l.split()
    if len(p) >= 3 and p[2] == 'R_68K_32': offs.append(int(p[0], 16))
offs = sorted(set(offs)); assert len(offs) * 4 + 4 <= 0x20000, len(offs)
for o in offs: assert o + 4 <= sym['_reloc_start'], hex(o)
rs = sym['_reloc_start']
for k, o in enumerate(offs): struct.pack_into('>I', raw, rs + 4 * k, o)
bss_len = sym['_end'] - sym['_raw_end']
import lz4.block
comp = lz4.block.compress(bytes(raw), mode='high_compression', compression=12, store_size=False)
s2 = bytearray(open(s2bin, 'rb').read())
comp_off = (len(s2) + 3) & ~3
payload = bytearray(s2) + bytes(comp_off - len(s2)) + comp
struct.pack_into('>7I', payload, 4, comp_off, len(comp), len(raw), bss_len, sym['_start'], rs, len(offs))
pl = bytes(payload) + bytes((-len(payload)) % 512)
bb = bytearray(open(bootbin, 'rb').read()); assert bb[16:20] == b'ARCH'
struct.pack_into('>I', bb, 20, len(pl))
tmp = os.path.join(out, 'boot_patched.bin'); open(tmp, 'wb').write(bb)
open(os.path.join(out, 'payload.bin'), 'wb').write(pl)
here = os.path.dirname(os.path.abspath(__file__))
subprocess.check_call([sys.executable, os.path.join(here, 'mkadf.py'), os.path.join(out, 'archon.adf'), tmp, os.path.join(out, 'payload.bin')])
print('image: raw %d (relocs %d, bss %d) -> lz4 %d; payload %d bytes' % (len(raw), len(offs), bss_len, len(comp), len(pl)))
