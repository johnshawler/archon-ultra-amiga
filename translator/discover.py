"""Control-flow discovery for obj1: recursive descent + trace seeds + fixup-based code pointers / jump tables."""
import sys, struct, collections, gzip, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from le import LE
from capstone import *
from capstone.x86 import *

def fixup_map(le):
    fx = {}
    for pg, s, kind, tobj, toff in le.fixups:
        for n, ob in enumerate(le.objs):
            if ob['pmi'] - 1 <= pg < ob['pmi'] - 1 + ob['npages']:
                fx[ob['base'] + (pg - (ob['pmi'] - 1)) * 4096 + s] = (kind, tobj, toff); break
    return fx

def discover(le, exec_seeds=(), extra=()):
    code = bytes(le.mem[1]); base = le.objs[0]['base']; size = len(code)
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
    fx_src = fixup_map(le)
    insns = {}; work = [base + le.eip] + list(exec_seeds) + list(extra); seen = set()
    indirect = []; tables = {}
    while work:
        a = work.pop()
        while base <= a < base + size:
            if a in insns: break
            off = a - base
            try: i = next(md.disasm(code[off:off + 16], a, 1))
            except StopIteration: break
            insns[a] = i
            g = set(i.groups); mn = i.mnemonic
            nxt = a + i.size
            if i.id in (X86_INS_RET, X86_INS_IRET, X86_INS_IRETD, X86_INS_HLT): break
            if (X86_GRP_JUMP in g) or (X86_GRP_CALL in g):
                op = i.operands[0] if i.operands else None
                if op is not None and op.type == X86_OP_IMM:
                    work.append(op.imm)
                    if mn == 'jmp': break
                elif op is not None:
                    indirect.append((a, i))
                    if op.type == X86_OP_MEM and op.mem.index != 0 and op.mem.scale == 4 and op.mem.base == 0:
                        t = op.mem.disp; ents = []
                        while t in fx_src and fx_src[t][1] == 1:
                            ents.append(fx_src[t][2] + base); t += 4
                        for e in ents: work.append(e)
                        tables[op.mem.disp] = ents
                    if mn == 'jmp': break
            a = nxt
    return insns, indirect, tables, fx_src

if __name__ == '__main__':
    le = LE(sys.argv[1])
    seeds = [int(l, 16) - 0x15D000 for l in gzip.open(sys.argv[2], 'rt') if l.strip()] if len(sys.argv) > 2 else []
    insns, ind, tables, fx = discover(le, seeds)
    print('seeds', len(seeds), 'instructions', len(insns), 'indirect sites', len(ind), 'jump tables', len(tables))
    i0, ind0, t0, _ = discover(le, [])
    print('entry-only descent: instructions', len(i0), 'jump tables', len(t0), '| executed seeds not reached statically:', sum(1 for s in seeds if s not in i0))
    print(collections.Counter(i.mnemonic for i in insns.values()).most_common(70))
