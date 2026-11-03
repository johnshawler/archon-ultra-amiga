import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'translator'))
from le import LE
from capstone import *
le = LE('/tmp/claude-0/-home-user/58ef198a-5062-53ff-9850-553625a9f153/scratchpad/au/ARCHON.EXE')
code = bytes(le.mem[1]); b = le.objs[0]['base']; md = Cs(CS_ARCH_X86, CS_MODE_32)
for a in sys.argv[1:]:
    a = int(a, 16) + (b if int(a, 16) < b else 0)
    i = next(md.disasm(code[a - b:a - b + 16], a))
    print(hex(a), i.mnemonic, i.op_str)
