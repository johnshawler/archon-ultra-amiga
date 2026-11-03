"""Whole-program driver: discovery + liveness + translation -> 68k assembly for obj1."""
import sys, os, gzip, collections, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from le import LE
from discover import fixup_map
from capstone import *
from capstone.x86 import *
import xl
from xl import X, Unsupported, CF, ZF, SF, OF, ALL, COND, COND_ALIAS

import layout
BASE = layout.OBJ_BASES[0]

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
    strong = set(trusted) | set(roots) | set(i.operands[0].imm for i in list(ins.values()) if i.operands and i.operands[0].type == X86_OP_IMM and X86_GRP_CALL in i.groups)
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
                if any((a + k) in strong for k in range(1, i.size)): break      # would swallow a known entry point
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
    if i.mnemonic.startswith('rep') and ('scas' in i.mnemonic or 'cmps' in i.mnemonic):
        kill = 0; use = ALL            # count 0 leaves the flags untouched
    return use, kill

def ds_states(ins, entries):
    """Forward analysis: which instructions run with a possibly non-flat DS (after mov/pop/lds ds until the matching restore)."""
    def isdsload(i):
        if i.mnemonic == 'mov' and i.operands[0].type == X86_OP_REG and i.reg_name(i.operands[0].reg) == 'ds': return True
        if i.mnemonic == 'pop' and i.operands[0].type == X86_OP_REG and i.reg_name(i.operands[0].reg) == 'ds': return True
        return i.mnemonic == 'lds'
    def ispushds(i): return i.mnemonic == 'push' and i.operands[0].type == X86_OP_REG and i.reg_name(i.operands[0].reg) == 'ds'
    def ispopds(i): return i.mnemonic == 'pop' and i.operands[0].type == X86_OP_REG and i.reg_name(i.operands[0].reg) == 'ds'
    seen = {}
    work = [(e, 'F', ()) for e in entries if e in ins]
    U = set()
    while work:
        a, st, stk = work.pop()
        while a in ins:
            key = (st, stk)
            if key in seen.setdefault(a, set()): break
            seen[a].add(key)
            i = ins[a]
            if st == 'U': U.add(a)
            # transfer
            if ispushds(i): stk = stk + (st,)
            elif ispopds(i):
                if stk: st = stk[-1]; stk = stk[:-1]
                else: st = 'U'
            elif isdsload(i): st = 'U'
            g = set(i.groups); mn = i.mnemonic
            if i.id in (X86_INS_RET, X86_INS_IRET, X86_INS_IRETD, X86_INS_HLT): break
            nxt = a + i.size
            if (X86_GRP_JUMP in g) and i.operands and i.operands[0].type == X86_OP_IMM:
                work.append((i.operands[0].imm, st, stk))
                if mn == 'jmp': break
            elif (X86_GRP_JUMP in g) and mn == 'jmp': break
            a = nxt
    return U

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
        if i.id in (X86_INS_RET,): out = ALL     # ret preserves the flags: whatever the caller does with them is live
        new_in = use | (out & ~kill)
        live_out[a] = out
        if new_in != live_in[a]:
            live_in[a] = new_in
            for p in pred[a]:
                if p not in inq: work.append(p); inq.add(p)
    return live_out

def main(exe, seeds_path, outs, stats=True):
    le = LE(exe)
    seeds = [int(l, 16) - 0x16d000 + BASE for l in gzip.open(seeds_path, 'rt') if l.strip()]
    ins, trusted = decode_all(le, seeds)
    live = liveness(ins)
    label = lambda a: 'L%x' % a
    vga_rd = set()
    try:
        for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'traces', 'cc4', 'vga_sites.log')):
            p = l.split(); a = int(p[0], 16) - 0x16d000 + BASE
            if a in ins and ins[a].mnemonic == 'mov': vga_rd.add(a)
    except IOError: pass
    xl.VGA_MOV_SITES = vga_rd
    entries = set([BASE + le.eip] + [i.operands[0].imm for i in ins.values() if i.operands and i.operands[0].type == X86_OP_IMM and X86_GRP_CALL in i.groups] + [t[2] + BASE for t in fixup_map(le).values() if t[1] == 1] + list(trusted))
    dsU = ds_states(ins, entries)
    heads = set()
    for a, i in ins.items():
        if i.operands and i.operands[0].type == X86_OP_IMM and ((X86_GRP_JUMP in i.groups) or i.mnemonic.startswith('loop')) and i.operands[0].imm <= a and i.operands[0].imm in ins:
            heads.add(i.operands[0].imm)
    # branch/call targets must exist as labels
    lines = ['\t.text', '\t.even']
    unsup = collections.Counter(); unsup_trusted = collections.Counter(); n_ok = 0
    targets = set()
    for a, i in ins.items():
        if i.operands and i.operands[0].type == X86_OP_IMM and ((X86_GRP_JUMP in i.groups) or (X86_GRP_CALL in i.groups)):
            targets.add(i.operands[0].imm)
    xt = X(label, 'xl_tab', BASE)
    starts = []
    for a in sorted(ins):
        i = ins[a]
        lines.append(f'{label(a)}:')
        if os.environ.get('XL_TRACE'):
            lines.append(f'\tjbsr rt_trace\n\t.long 0x{a:x}')
        if a in heads:
            use, kill = flag_sets(i)
            if (use | (live[a] & ~kill)) == 0:
                lines.append('\tsubq.l #1,(%a6)\n\tbpl 9f\n\tjbsr rt_poll\n9:')
        try:
            xt.ds_dyn = a in dsU
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
    txt = '\n'.join(lines) + '\n'
    defined = set(re.findall(r'^(L[0-9a-f]+):', txt, re.M))
    refd = set(re.findall(r'\b(L[0-9a-f]+)\b', txt))
    for u in sorted(refd - defined):
        txt += f'{u}:\n\tjbsr rt_badjump\n\t.long 0x{int(u[1:], 16) & 0xFFFFFFFF:x}\n'
    open(outs, 'w').write(txt)
    open(outs + '.starts', 'w').write('\n'.join('%x' % a for a in starts) + '\n')
    ent = BASE + le.eip
    first = starts[0]
    # compact dispatch data: bitmap of instruction starts (one bit per code byte) + 16-bit native deltas between consecutive starts
    size = len(le.mem[1]); bm = bytearray((size + 7) // 8)
    for a in starts: bm[(a - BASE) >> 3] |= 1 << ((a - BASE) & 7)
    tab = ['\t.data', '\t.even', '\t.globl xl_bits', 'xl_bits:']
    for k in range(0, len(bm), 32): tab.append('\t.byte ' + ','.join(str(v) for v in bm[k:k + 32]))
    tab += ['\t.even', '\t.globl xl_deltas', 'xl_deltas:']
    for k in range(1, len(starts), 8):
        tab.append('\t.word ' + ','.join(f'{label(starts[j])}-{label(starts[j - 1])}' for j in range(k, min(k + 8, len(starts)))))
    tab += ['\t.even', '\t.globl xl_entry', f'xl_entry:\t.long {label(ent)}', '\t.globl xl_first', f'xl_first:\t.long {label(first)}',
            '\t.globl xl_nstarts', f'xl_nstarts:\t.long {len(starts)}', '\t.globl xl_codesize', f'xl_codesize:\t.long {size}']
    open(outs, 'a').write('\n'.join(tab) + '\n')
    if stats:
        print('instructions', len(ins), 'trusted', len(trusted), 'translated ok', n_ok, 'branch targets missing', len(missing))
        print('unsupported (trusted/executed):', unsup_trusted.most_common())
        print('unsupported (static only):', unsup.most_common(30))
    return ins, live

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3])
