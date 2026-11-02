"""Per-instruction tests: translated 68k code (qemu-m68k) vs Unicorn x86 reference. Usage: xltest.py [filter]"""
import sys, os, struct, random, subprocess, tempfile, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import xl
from xl import *
import keystone, unicorn
from unicorn.x86_const import *
from capstone import *

ks = keystone.Ks(keystone.KS_ARCH_X86, keystone.KS_MODE_32)
md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
MEMA, MEMB = 0x14000, 0x14F00     # data block and stack block (256 bytes each)
FL2CCR = {CF: 1, OF: 2, ZF: 4, SF: 8}
XF = {CF: 0x1, ZF: 0x40, SF: 0x80, OF: 0x800}
REGS = ['eax', 'ecx', 'edx', 'ebx', 'ebp', 'esi', 'edi', 'esp']
UREG = [UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_ESP]

def assemble(txt):
    enc, n = ks.asm(txt)
    return bytes(enc)

def regs_in_ea(i):
    used = set()
    for op in i.operands:
        if op.type == X86_OP_MEM:
            if op.mem.base: used.add(i.reg_name(op.mem.base))
            if op.mem.index: used.add(i.reg_name(op.mem.index))
    return used

def gen_vector(i, rnd):
    regs = [rnd.choice([rnd.getrandbits(32), rnd.getrandbits(8), rnd.getrandbits(16), 0, 1, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0x80, 0x8000, 0xFF, 0xFFFF]) for _ in range(7)]
    esp = MEMB + 0x80 + rnd.randrange(0, 4) * 4
    regs.append(esp)
    used = regs_in_ea(i)
    for op in i.operands:
        if op.type == X86_OP_MEM:
            m = op.mem
            base = i.reg_name(m.base) if m.base else None
            idx = i.reg_name(m.index) if m.index else None
            target = MEMA + 0x80 + rnd.randrange(-0x20, 0x20)
            if base == 'esp' or idx == 'esp':
                pass
            elif base and idx:
                iv = rnd.randrange(0, 8); regs[REGS.index(idx)] = iv
                regs[REGS.index(base)] = (target - m.disp - iv * m.scale) & 0xFFFFFFFF
            elif base:
                regs[REGS.index(base)] = (target - m.disp) & 0xFFFFFFFF
            elif idx:
                regs[REGS.index(idx)] = ((target - m.disp) // m.scale) & 0xFFFFFFFF
    if i.mnemonic == 'leave': regs[4] = MEMB + 0x40 + rnd.randrange(0, 8) * 4
    flags = rnd.getrandbits(4)  # bits in 68k CCR order C V Z N
    memA = bytes(rnd.getrandbits(8) for _ in range(256))
    memB = bytes(rnd.getrandbits(8) for _ in range(256))
    return regs, flags, memA, memB

def ref_run(code, regs, flags, memA, memB):
    uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
    uc.mem_map(0x10000, 0x10000)
    uc.mem_map(0x100000, 0x1000)
    uc.mem_write(0x100000, code + b'\x90' * 16)
    uc.mem_write(MEMA, memA); uc.mem_write(MEMB, memB)
    for r, v in zip(UREG, regs): uc.reg_write(r, v)
    ef = 0x202
    for xf, cc in ((CF, 1), (OF, 2), (ZF, 4), (SF, 8)):
        if flags & cc: ef |= XF[xf]
    uc.reg_write(UC_X86_REG_EFLAGS, ef)
    try:
        uc.emu_start(0x100000, 0x100000 + len(code), count=1)
    except unicorn.UcError as e:
        return None
    out = [uc.reg_read(r) for r in UREG]
    eflags = uc.reg_read(UC_X86_REG_EFLAGS)
    fl = 0
    for xf, cc in ((CF, 1), (OF, 2), (ZF, 4), (SF, 8)):
        if eflags & XF[xf]: fl |= cc
    return out, fl, bytes(uc.mem_read(MEMA, 256)), bytes(uc.mem_read(MEMB, 256))

HARNESS = r'''
	.text
	.globl _start
_start:
	lea win,%a5
	lea ctx,%a6
	lea invec,%a2
	lea outvec,%a3
	move.l #NVEC,ncount
1:
	lea 36(%a2),%a0
	lea (MEMA,%a5),%a1
	move.l #63,%d7
2:	move.l (%a0)+,(%a1)+
	dbra %d7,2b
	lea (MEMB,%a5),%a1
	move.l #63,%d7
3:	move.l (%a0)+,(%a1)+
	dbra %d7,3b
	movem.l (%a2),%d0-%d6
	move.l 28(%a2),%d7
	lea (%a5,%d7.l),%sp
	move.l 32(%a2),%d7
	lea 548(%a2),%a2
	move.w %d7,%ccr
	| ---- begin translated code
CODE
	| ---- end
	move.w %ccr,%d7
	move.w %d7,2(%a3)
	movem.l %d0-%d6,4(%a3)
	move.l %sp,%d7
	sub.l %a5,%d7
	move.l %d7,32(%a3)
	lea 36(%a3),%a3
	lea (MEMA,%a5),%a0
	lea (MEMB,%a5),%a1
	move.l #63,%d7
4:	move.l (%a0)+,(%a3)+
	dbra %d7,4b
	move.l #63,%d7
5:	move.l (%a1)+,(%a3)+
	dbra %d7,5b
	subq.l #1,ncount
	bne 1b
	| write(1,outvec,len)
	move.l #4,%d0
	move.l #1,%d1
	lea outvec,%a0
	move.l %a0,%d2
	move.l %a3,%d3
	sub.l %d2,%d3
	trap #0
	| exit(0)
	moveq #1,%d0
	moveq #0,%d1
	trap #0
	.data
	.align 4
invec:
	.incbin "INVEC"
	.bss
	.align 4
ncount:	.space 4
ctx:	.space 64
outvec:	.space OUTSZ
win:	.space 0x20000
'''
REC_OUT = 4 + 32 + 512 + 4   # words: flags(+pad), regs, esp, memA, memB, pad   (see harness)

def run_test(name, asm_txt, nvec=40, seed=1, flagmask=ALL, live=ALL, verbose=False):
    code = assemble(asm_txt)
    i = next(md.disasm(code, 0x100000))
    xt = xl.X(lambda a: 'L%x' % a)
    try:
        lines = xt.insn(i, live)
    except Unsupported as e:
        return ('UNSUPPORTED', str(e))
    rnd = random.Random(seed)
    vecs = [gen_vector(i, rnd) for _ in range(nvec)]
    refs = [ref_run(code, *v) for v in vecs]
    # drop vectors where reference faulted
    keep = [(v, r) for v, r in zip(vecs, refs) if r is not None]
    if not keep: return ('NOREF', '')
    ind = bytearray()
    for (regs, flags, ma, mb), r in keep:
        for k in range(7): ind += struct.pack('>I', regs[k])
        ind += struct.pack('>I', regs[7]) + struct.pack('>I', flags) + ma + mb
    tmp = tempfile.mkdtemp()
    open(tmp + '/in.bin', 'wb').write(ind)
    src = HARNESS.replace('CODE', '\n'.join(lines)).replace('INVEC', tmp + '/in.bin').replace('NVEC', str(len(keep))) \
        .replace('MEMA_ADDR', str(MEMA)).replace('MEMA', str(MEMA)).replace('MEMB', str(MEMB)).replace('OUTSZ', str(548 * len(keep) + 64))
    open(tmp + '/t.s', 'w').write(src)
    p = subprocess.run(['m68k-linux-gnu-gcc', '-m68030', '-nostdlib', '-static', '-x', 'assembler', tmp + '/t.s', '-o', tmp + '/t'], capture_output=True, text=True)
    if p.returncode:
        return ('ASMERR', p.stderr[:600] + '\n' + '\n'.join(lines))
    q = subprocess.run(['qemu-m68k', '-cpu', 'm68030', tmp + '/t'], capture_output=True, timeout=60)
    out = q.stdout
    rec = 548
    if len(out) != rec * len(keep):
        return ('RUNERR', 'rc=%d outlen=%d expected=%d %s' % (q.returncode, len(out), rec * len(keep), q.stderr[:200]) + '\n' + '\n'.join(lines))
    bad = []
    for k, ((regs, flags, ma, mb), (rr, rf, rma, rmb)) in enumerate(keep):
        o = out[k * rec:(k + 1) * rec]
        ccr = struct.unpack('>H', o[2:4])[0] & 0xF
        got = list(struct.unpack('>7I', o[4:32])) + [struct.unpack('>I', o[32:36])[0]]
        gma = o[36:36 + 256]; gmb = o[36 + 256:36 + 512]
        ok = True; why = []
        for r in range(8):
            if rr[r] != got[r]: ok = False; why.append('%s ref=%08x got=%08x' % (REGS[r], rr[r], got[r]))
        fm = 0
        for xf, cc in ((CF, 1), (OF, 2), (ZF, 4), (SF, 8)):
            if flagmask & xf: fm |= cc
        if (ccr ^ rf) & fm: ok = False; why.append('flags ref=%x got=%x mask=%x' % (rf, ccr, fm))
        if rma != gma: ok = False; why.append('memA differs')
        lo = min(rr[7], got[7]) - MEMB if min(rr[7], got[7]) >= MEMB else 0
        lo = max(0, lo)
        lo = max(lo, 0) if rr[7] == got[7] else lo
        if rmb[max(lo, min(rr[7], got[7]) - MEMB if True else 0):] != gmb[max(lo, min(rr[7], got[7]) - MEMB):] and rr[7] == got[7] or (rr[7] != got[7] and rmb[rr[7] - MEMB:] != gmb[rr[7] - MEMB:]): ok = False; why.append('memB(stack) differs')
        if not ok:
            bad.append((regs, flags, why))
    if bad:
        regs, flags, why = bad[0]
        return ('FAIL', '%d/%d bad. e.g. in regs=%s flags=%x: %s\n%s' % (len(bad), len(keep), ['%08x' % r for r in regs], flags, why, '\n'.join(lines)))
    return ('ok', '%d vectors' % len(keep))

if __name__ == '__main__':
    from tests_list import TESTS
    filt = sys.argv[1] if len(sys.argv) > 1 else ''
    res = collections.Counter()
    for t in TESTS:
        txt, mask = t if isinstance(t, tuple) else (t, ALL)
        if filt not in txt: continue
        st, msg = run_test(txt, txt, flagmask=mask)
        res[st] += 1
        if st != 'ok': print('%-8s %s\n   %s' % (st, txt, msg.replace('\n', '\n   ')))
        else: print('ok       %s' % txt)
    print(dict(res))
