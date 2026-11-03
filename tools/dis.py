import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'translator'))
from le import LE
from capstone import *
le = LE(os.environ.get('ARCHON_EXE', '/tmp/claude-0/-home-user/58ef198a-5062-53ff-9850-553625a9f153/scratchpad/au/ARCHON.EXE'))
code = bytes(le.mem[1]); md = Cs(CS_ARCH_X86, CS_MODE_32)
a = int(sys.argv[1], 16); n = int(sys.argv[2]) if len(sys.argv) > 2 else 12
for i in list(md.disasm(code[a - le.objs[0]['base']:a - le.objs[0]['base'] + n * 8], a))[:n]: print(hex(i.address), i.mnemonic, i.op_str)
