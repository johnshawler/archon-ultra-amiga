import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'translator'))
from le import LE
le = LE(sys.argv[1]); out = sys.argv[2]
for n in (1, 2, 3): open(os.path.join(out, 'obj%d.bin' % n), 'wb').write(bytes(le.mem[n]))

import layout
b = layout.OBJ_BASES
sent = b[0] + 0x3c800; sent2 = b[0] + 0x3c810; be2 = int.from_bytes(sent2.to_bytes(4, 'little'), 'big')
be = int.from_bytes(sent.to_bytes(4, 'little'), 'big')
open(os.path.join(out, 'layout.h'), 'w').write('#define OBJ1_BASE 0x%x\n#define OBJ2_BASE 0x%x\n#define OBJ3_BASE 0x%x\n#define XL_BASE 0x%x\n#define XL_SENTINEL_OFF 0x3c800\n#define XL_SENTINEL_TRACE_OFF 0x3c810\n#define OBJ3_ESP_TOP 0x%x\n' % (b[0], b[1], b[2], b[0], b[2] + 0x33dd0))
open(os.path.join(out, 'defsyms.txt'), 'w').write('-Wa,--defsym,XL_BASE=0x%x -Wa,--defsym,XL_SENT_BE=0x%x -Wa,--defsym,XL_SENT_BE_TRACE=0x%x' % (b[0], be, be2))
