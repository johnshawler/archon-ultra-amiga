"""Whole-program driver: discovery + liveness + translation -> 68k assembly for obj1."""
import sys, os, gzip, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from le import LE
from discover import fixup_map
from capstone import *
from capstone.x86 import *
import xl
from xl import X, Unsupported, CF, ZF, SF, OF, ALL, COND, COND_ALIAS

BASE = 0x10000

def decode_all(le, seeds):
    code = bytes(le.mem[1]); size = len(code)
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
    fx = fixup_map(le)
    ins = {}; cov = bytearray(size)
    def dec(a):
        off = a - BASE
        try: return next(md.disasm(code[off:off + 16], a, 1))
        except StopIteration: return None
    # trusted: executed instruction starts
    for a in sorted(seeds):
        i = dec(a)
        if i is None: continue
        ins[a] = i
        for k in range(i.size): cov[a - BASE + k] = 1
    trusted = set(ins)
    # static descent from entry, call/jump targets and fixup targets, not overlapping trusted bytes
    roots = [BASE + le.eip] + [t[2] + BASE for s, t in fx.items() if t[1] == 1]
    work = list(roots) + [i.operands[0].imm for i in list(ins.values()) if i.operands and i.operands[0].type == X86_OP_IMM and (X86_GRP_JUMP in i.groups or X86_GRP_CALL in i.groups)]
    seen = set()
    while work:
        a = work.pop()
        while BASE <= a < BASE + size:
            if a in ins:
                if a in seen: break
                seen.add(a)
            else:
                i = dec(a)
                if i is None: break
                # overlap with a trusted instruction at a different alignment?
                if any(cov[a - BASE + k] for k in range(i.size)): break
                ins[a] = i; seen.add(a)
                for k in range(i.size): cov[a - BASE + k] = 2
            i = ins[a]
            g = set(i.groups); mn = i.mnemonic
            nxt = a + i.size
            if i.id in (X86_INS_RET, X86_INS_IRET, X86_INS_IRETD, X86_INS_HLT): break
            if (X86_GRP_JUMP in g) or (X86_GRP_CALL in g):
                op = i.operands[0] if i.operands else None
                if op is not None and op.type == X86_OP_IMM:
                    work.append(op.imm)
                    if mn == 'jmp': break
                elif op is not None:
                    if op.type == X86_OP_MEM and op.mem.index != 0 and op.mem.scale == 4 and op.mem.base == 0:
                        t = op.mem.disp
                        while t in fx and fx[t][1] == 1:
                            work.append(fx[t][2] + BASE); t += 4
                    if mn == 'jmp': break
            a = nxt
    return ins, trusted

FLAG_USE = {}
for c, (b, f) in COND.items(): FLAG_USE['j' + c] = f; FLAG_USE['set' + c] = f
for c, t in COND_ALIAS.items(): FLAG_USE['j' + c] = COND[t][1]; FLAG_USE['set' + c] = COND[t][1]
FLAG_USE.update({'adc': CF, 'sbb': CF, 'rcl': CF, 'rcr': CF, 'cmc': CF, 'pushfd': ALL, 'pushf': ALL, 'lahf': ALL})
FLAG_KILL = {}
for m in ('add', 'sub', 'and', 'or', 'xor', 'cmp', 'test', 'neg', 'adc', 'sbb', 'div', 'idiv', 'imul', 'mul', 'popfd', 'sahf', 'scasb', 'scasw', 'scasd', 'cmpsb', 'cmpsw', 'cmpsd', 'int', 'bt', 'bts', 'btr', 'btc', 'bsf', 'bsr'):
    FLAG_KILL[m] = ALL
FLAG_KILL.update({'inc': ZF | SF | OF, 'dec': ZF | SF | OF, 'clc': CF, 'stc': CF, 'cmc': CF})

def flag_sets(i):
    m = i.mnemonic.split(' ')[-1]
    use = FLAG_USE.get(m, 0); kill = FLAG_KILL.get(m, 0)
    if m in ('shl', 'shr', 'sar', 'sal'):
        c = i.operands[1] if len(i.operands) > 1 else None
        kill = (CF | ZF | SF | OF) if (c is None or c.type == X86_OP_IMM) else 0
    if m in ('rol', 'ror'):
        c = i.operands[1] if len(i.operands) > 1 else None
        kill = (CF | OF) if (c is None or c.type == X86_OP_IMM) else 0
    if m in ('rcl', 'rcr'): kill = 0
    if i.mnemonic.startswith('repe') or i.mnemonic.startswith('repne'):
        if 'scas' in i.mnemonic or 'cmps' in i.mnemonic: kill = ALL
    if m in ('scasb', 'scasw', 'scasd', 'cmpsb', 'cmpsw', 'cmpsd') and i.mnemonic.startswith('rep'): kill = ALL
    return use, kill

def liveness(ins):
    succ = {}
    for a, i in ins.items():
        g = set(i.groups); mn = i.mnemonic.split(' ')[-1]
        nxt = a + i.size; s = []
        term = i.id in (X86_INS_RET, X86_INS_IRET, X86_INS_IRETD, X86_INS_HLT)
        if (X86_GRP_JUMP in g) and i.operands and i.operands[0].type == X86_OP_IMM:
            if i.operands[0].imm in ins: s.append(i.operands[0].imm)
            if mn != 'jmp' and nxt in ins: s.append(nxt)
        elif (X86_GRP_JUMP in g) and mn == 'jmp':
            pass
        elif not term and nxt in ins:
            s.append(nxt)
        succ[a] = s
    live_in = {a: 0 for a in ins}; live_out = {a: 0 for a in ins}
    changed = True
    order = sorted(ins, reverse=True)
    pred = collections.defaultdict(list)
    for a, ss in succ.items():
        for t in ss: pred[t].append(a)
    work = collections.deque(order)
    inq = set(order)
    while work:
        a = work.popleft(); inq.discard(a)
        i = ins[a]
        out = 0
        for t in succ[a]: out |= live_in[t]
        use, kill = flag_sets(i)
        if X86_GRP_CALL in i.groups: out = 0       # flags dead across calls (Watcom/cdecl)
        new_in = use | (out & ~kill)
        live_out[a] = out
        if new_in != live_in[a]:
            live_in[a] = new_in
            for p in pred[a]:
                if p not in inq: work.append(p); inq.add(p)
    return live_out

def main(exe, seeds_path, outs, stats=True):
    le = LE(exe)
    seeds = [int(l, 16) - 0x15D000 for l in gzip.open(seeds_path, 'rt') if l.strip()]
    ins, trusted = decode_all(le, seeds)
    live = liveness(ins)
    label = lambda a: 'L%x' % a
    # branch/call targets must exist as labels
    lines = ['\t.text', '\t.even']
    unsup = collections.Counter(); unsup_trusted = collections.Counter(); n_ok = 0
    targets = set()
    for a, i in ins.items():
        if i.operands and i.operands[0].type == X86_OP_IMM and ((X86_GRP_JUMP in i.groups) or (X86_GRP_CALL in i.groups)):
            targets.add(i.operands[0].imm)
    xt = X(label)
    starts = []
    for a in sorted(ins):
        i = ins[a]
        lines.append(f'{label(a)}:')
        try:
            body = xt.insn(i, live[a])
            lines.extend(body); n_ok += 1
        except Unsupported as e:
            key = i.mnemonic + ' ' + i.op_str
            (unsup_trusted if a in trusted else unsup)[i.mnemonic] += 1
            lines.append(f'\tjbsr rt_unimpl\n\t.long 0x{a:x}')
        except Exception as e:
            unsup_trusted['EXC ' + i.mnemonic + ' ' + str(e)[:40]] += 1
            lines.append(f'\tjbsr rt_unimpl\n\t.long 0x{a:x}')
        starts.append(a)
    # targets outside the decoded set
    missing = sorted(t for t in targets if t not in ins and BASE <= t < BASE + len(le.mem[1]))
    for t in missing:
        lines.append(f'{label(t)}:\n\tjbsr rt_badjump\n\t.long 0x{t:x}')
    open(outs, 'w').write('\n'.join(lines) + '\n')
    open(outs + '.starts', 'w').write('\n'.join('%x' % a for a in starts) + '\n')
    if stats:
        print('instructions', len(ins), 'trusted', len(trusted), 'translated ok', n_ok, 'branch targets missing', len(missing))
        print('unsupported (trusted/executed):', unsup_trusted.most_common())
        print('unsupported (static only):', unsup.most_common(30))
    return ins, live

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3])
