"""x86-32 (protected mode, flat) -> 68020/030 assembly (GNU as, MIT syntax) instruction translator.

Conventions (see docs/DESIGN.md):
  eax=d0 ecx=d1 edx=d2 ebx=d3 ebp=d4 esi=d5 edi=d6      d7 = scratch (also byte-swap temp)
  esp = a7 (native pointer: a5 + x86 esp)               a5 = window base (x86 linear address L lives at a5+L)
  a6 = runtime context                                  a0..a3 = address temps, a4 = spare
  Memory is stored little-endian exactly as on the PC; every 16/32-bit access byte-swaps (ror.w #8 / swap / ror.w #8).
  Flags: x86 CF/ZF/SF/OF <-> 68k C/Z/N/V directly (sub/cmp/add/neg/logic have identical semantics); only flags that are
  live after an instruction are made exact.
"""
import re
from capstone import *
from capstone.x86 import *

CF, ZF, SF, OF = 1, 2, 4, 8
ALL = CF | ZF | SF | OF

class Unsupported(Exception):
    pass

GPR = {'eax': 'd0', 'ecx': 'd1', 'edx': 'd2', 'ebx': 'd3', 'ebp': 'd4', 'esi': 'd5', 'edi': 'd6'}
R16 = {'ax': 'd0', 'cx': 'd1', 'dx': 'd2', 'bx': 'd3', 'bp': 'd4', 'si': 'd5', 'di': 'd6'}
R8L = {'al': 'd0', 'cl': 'd1', 'dl': 'd2', 'bl': 'd3'}
R8H = {'ah': 'd0', 'ch': 'd1', 'dh': 'd2', 'bh': 'd3'}
SEGREGS = ('es', 'cs', 'ss', 'ds', 'fs', 'gs')
SZ = {1: 'b', 2: 'w', 4: 'l'}

# x86 condition -> (68k branch mnemonic suffix, flags read)
COND = {
    'e': ('eq', ZF), 'ne': ('ne', ZF), 'b': ('cs', CF), 'ae': ('cc', CF), 'be': ('ls', CF | ZF), 'a': ('hi', CF | ZF),
    's': ('mi', SF), 'ns': ('pl', SF), 'l': ('lt', SF | OF), 'ge': ('ge', SF | OF), 'le': ('le', SF | OF | ZF),
    'g': ('gt', SF | OF | ZF), 'o': ('vs', OF), 'no': ('vc', OF),
}
COND_ALIAS = {'z': 'e', 'nz': 'ne', 'c': 'b', 'nc': 'ae', 'nae': 'b', 'nb': 'ae', 'na': 'be', 'nbe': 'a', 'nge': 'l', 'nl': 'ge',
              'ng': 'le', 'nle': 'g'}

def swap32(r):
    return [f'ror.w #8,%{r}', f'swap %{r}', f'ror.w #8,%{r}']

def swap16(r):
    return [f'ror.w #8,%{r}']

def swapped_imm(v, size):
    if size == 4: v &= 0xFFFFFFFF; return int.from_bytes(v.to_bytes(4, 'little'), 'big')
    if size == 2: v &= 0xFFFF; return int.from_bytes(v.to_bytes(2, 'little'), 'big')
    return v & 0xFF

def hexs(v):
    return '0x%x' % (v & 0xFFFFFFFF)

CTX = {'poll': 0, 'if': 4, 'es': 8, 'fs': 12, 'gs': 16, 'ds': 20, 'df': 24, 'tmp': 28}

class X:
    """Translator for one instruction."""

    def __init__(self, label_of, dispatch_sym='xl_tab', code_base=0x10000, df_known=0):
        self.label_of = label_of      # vaddr -> assembler label
        self.dispatch = dispatch_sym
        self.base = code_base
        self.df = df_known
        self.out = []
        self.warn = []

    # ------------------------------------------------------------------ helpers
    def e(self, s):
        self.out.append('\t' + s)

    def regop(self, i, op):
        n = i.reg_name(op.reg)
        return n

    def opsize(self, op):
        return op.size

    def r(self, name, size=None):
        """-> (dreg, kind) kind: 'l','w','b','h' (high byte)"""
        if name in GPR: return GPR[name], 'l'
        if name in R16: return R16[name], 'w'
        if name in R8L: return R8L[name], 'b'
        if name in R8H: return R8H[name], 'h'
        raise Unsupported('reg ' + name)

    # memory operand -> (setup lines, ea string).  uses a0 (and a1)
    def mem(self, i, op, extra=0):
        m = op.mem
        seg = i.reg_name(m.segment) if m.segment else None
        base = i.reg_name(m.base) if m.base else None
        idx = i.reg_name(m.index) if m.index else None
        sc = m.scale
        disp = m.disp + extra
        pre = []
        if seg in ('fs', 'gs', 'es', 'cs', 'ss') and seg in ('fs', 'gs', 'es'):
            # non-flat segment bases are supported through the context (rt keeps es/fs/gs bases); ds/ss/cs flat
            pre.append(f'move.l {CTX[seg]}(%a6),%d7') if False else None
            segadd = seg
        else:
            segadd = None
        if segadd:
            # a0 = a5 + segbase ; then add base/index
            pre.append(f'lea (%a5),%a0')
            pre.append(f'adda.l {CTX[segadd]}(%a6),%a0')
            if base == 'esp':
                raise Unsupported('seg+esp')
            if base and idx:
                pre.append(f'adda.l %{GPR[base]},%a0')
                return pre, f'({hexs(disp)},%a0,%{GPR[idx]}.l*{sc})'
            if base:
                return pre, f'({hexs(disp)},%a0,%{GPR[base]}.l)'
            if idx:
                return pre, f'({hexs(disp)},%a0,%{GPR[idx]}.l*{sc})'
            return pre, f'({hexs(disp)},%a0)'
        if base == 'esp':
            if idx:
                return pre, f'({hexs(disp)},%sp,%{GPR[idx]}.l*{sc})'
            return pre, f'({hexs(disp)},%sp)'
        if idx == 'esp':
            raise Unsupported('esp index')
        if base and idx:
            pre.append(f'lea (%a5,%{GPR[base]}.l),%a0')
            return pre, f'({hexs(disp)},%a0,%{GPR[idx]}.l*{sc})'
        if base:
            return pre, f'({hexs(disp)},%a5,%{GPR[base]}.l)'
        if idx:
            return pre, f'({hexs(disp)},%a5,%{GPR[idx]}.l*{sc})'
        return pre, f'({hexs(disp)},%a5)'

    def load_mem(self, i, op, dst, size):
        """swapped load of memory operand into data register dst (dst may be d7). Returns nothing, appends code."""
        pre, ea = self.mem(i, op)
        for l in pre: self.e(l)
        self.e(f'move.{SZ[size]} {ea},%{dst}')
        if size == 4:
            for l in swap32(dst): self.e(l)
        elif size == 2:
            for l in swap16(dst): self.e(l)

    def store_mem(self, i, op, src, size, scratch='d7'):
        """store data register src to memory operand, byte swapping through scratch (src preserved unless src==scratch)."""
        pre, ea = self.mem(i, op)
        for l in pre: self.e(l)
        if size == 1:
            self.e(f'move.b %{src},{ea}'); return
        if src != scratch:
            self.e(f'move.{SZ[size]} %{src},%{scratch}')
        for l in (swap32(scratch) if size == 4 else swap16(scratch)): self.e(l)
        self.e(f'move.{SZ[size]} %{scratch},{ea}')

    def protected(self, live, body, clobber_mask=ALL):
        """wrap body (callable appending code) so that flags in `live` survive when body clobbers them."""
        if live & clobber_mask:
            self.e('move.w %ccr,28(%a6)')
            body()
            self.e('move.w 28(%a6),%ccr')
        else:
            body()

    # ------------------------------------------------------------------ operand read/write at a given size
    def src_val(self, i, op, size, scratch='d7'):
        """Return a 68k source operand string for op (reg, imm or memory loaded+swapped into scratch)."""
        if op.type == X86_OP_IMM:
            v = op.imm & ((1 << (8 * size)) - 1)
            return f'#{hexs(v)}'
        if op.type == X86_OP_REG:
            n = i.reg_name(op.reg)
            d, k = self.r(n)
            if k == 'h':
                self.e(f'move.w %{d},%{scratch}'); self.e(f'lsr.w #8,%{scratch}')
                return f'%{scratch}'
            return f'%{d}'
        if op.type == X86_OP_MEM:
            self.load_mem(i, op, scratch, size)
            return f'%{scratch}'
        raise Unsupported('operand')

    # ------------------------------------------------------------------ the instruction dispatch
    def insn(self, i, live):
        """Translate capstone insn i; live = x86 flags live after it. Returns list of lines."""
        self.out = []
        m = i.mnemonic
        prefix = ''
        if m.startswith('rep'):
            prefix, m = m.split(' ', 1)
        key = m.replace('.', '_')
        # xchg ah,al
        if m == 'xchg' and [i.reg_name(o.reg) for o in i.operands if o.type == X86_OP_REG] in (['ah', 'al'], ['al', 'ah'], ['ch', 'cl'], ['cl', 'ch'], ['dh', 'dl'], ['dl', 'dh'], ['bh', 'bl'], ['bl', 'bh']):
            r = self.r(i.reg_name(i.operands[0].reg))[0]
            self.protected(live, lambda: self.e(f'rol.w #8,%{r}')); return self.out
        # high-byte register as destination of a read-modify-write op: rotate the parent so it becomes a low byte
        if m in ('inc', 'dec', 'not', 'neg', 'shl', 'shr', 'sar', 'sal', 'rol', 'ror', 'add', 'sub', 'and', 'or', 'xor', 'adc', 'sbb', 'cmp', 'test', 'mov') or m.startswith('set'):
            d0 = i.operands[0] if i.operands else None
            if d0 is not None and d0.type == X86_OP_REG and i.reg_name(d0.reg) in R8H:
                hn = i.reg_name(d0.reg); parent = R8H[hn]
                lowname = {'ah': 'al', 'ch': 'cl', 'dh': 'dl', 'bh': 'bl'}[hn]
                other_h = any(o.type == X86_OP_REG and i.reg_name(o.reg) in R8H for o in i.operands[1:])
                same_parent = any(o.type == X86_OP_REG and i.reg_name(o.reg) in (lowname,) for o in i.operands[1:])
                if not other_h and not same_parent and m not in ('mov',):
                    lowid = {'al': X86_REG_AL, 'cl': X86_REG_CL, 'dl': X86_REG_DL, 'bl': X86_REG_BL}[lowname]
                    d0._cs_op = None if False else None
                    d0.value.reg = lowid
                    self.e(f'rol.w #8,%{parent}')
                    sub = X(self.label_of, self.dispatch, self.base, self.df)
                    body = sub.insn(i, live)
                    self.out += body
                    # undo rotate without disturbing the result flags
                    if live & ALL:
                        self.e('move.w %ccr,28(%a6)'); self.e(f'rol.w #8,%{parent}'); self.e('move.w 28(%a6),%ccr')
                    else:
                        self.e(f'rol.w #8,%{parent}')
                    return self.out
        if any(o.type == X86_OP_REG and i.reg_name(o.reg) == 'esp' for o in i.operands) and m in ('mov', 'sub', 'add', 'and', 'lea', 'xchg', 'or', 'cmp', 'sub'):
            self.esp_insn(i, live, m); return self.out
        f = getattr(self, 'x_' + key, None)
        if f is None:
            if m.startswith('j') and m[1:] in COND or m[1:] in COND_ALIAS:
                f = self.x_jcc
            elif m.startswith('set'):
                f = self.x_setcc
            else:
                raise Unsupported(m)
        f(i, live, prefix)
        return self.out

    def esp_insn(self, i, live, m):
        ops = i.operands
        d = ops[0]; sreg = ops[1] if len(ops) > 1 else None
        dn = i.reg_name(d.reg) if d.type == X86_OP_REG else None
        def esp_to(r):
            self.e(f'move.l %sp,%{r}'); self.e(f'sub.l %a5,%{r}')
        if m == 'mov':
            if dn == 'esp':
                # esp <- reg / mem / imm
                if sreg.type == X86_OP_REG: 
                    r, k = self.r(i.reg_name(sreg.reg)); self.e(f'lea (%a5,%{r}.l),%sp')
                elif sreg.type == X86_OP_MEM:
                    self.load_mem(i, sreg, 'd7', 4); self.e('lea (%a5,%d7.l),%sp')
                else:
                    self.e(f'lea ({hexs(sreg.imm)},%a5),%sp')
                return
            # dest not esp: source is esp
            if d.type == X86_OP_REG:
                r, k = self.r(dn)
                self.protected(live, lambda: esp_to(r))
            else:
                def b():
                    esp_to('d7'); self.store_mem(i, d, 'd7', 4, 'd7')
                self.protected(live, b)
            return
        if dn == 'esp' and m in ('sub', 'add') and sreg.type == X86_OP_IMM:
            v = sreg.imm if m == 'add' else -sreg.imm
            if not (live & ALL):
                self.e(f'lea ({hexs(v)},%sp),%sp'); return
            esp_to('d7'); self.e(f'{m}.l #{hexs(sreg.imm)},%d7'); self.e('move.w %ccr,28(%a6)'); self.e('lea (%a5,%d7.l),%sp'); self.e('move.w 28(%a6),%ccr'); return
        if dn == 'esp' and m == 'and' and sreg.type == X86_OP_IMM:
            esp_to('d7'); self.e(f'and.l #{hexs(sreg.imm)},%d7'); self.e('move.w %ccr,28(%a6)'); self.e('lea (%a5,%d7.l),%sp'); self.e('move.w 28(%a6),%ccr'); return
        if dn == 'esp' and m in ('sub', 'add', 'and', 'or') and sreg.type == X86_OP_REG:
            r, k = self.r(i.reg_name(sreg.reg))
            esp_to('d7'); self.e(f'{m}.l %{r},%d7'); self.e('move.w %ccr,28(%a6)'); self.e('lea (%a5,%d7.l),%sp'); self.e('move.w 28(%a6),%ccr'); return
        if m == 'lea' and dn == 'esp':
            mm = sreg.mem
            base = i.reg_name(mm.base) if mm.base else None
            if base and not mm.index and base != 'esp':
                self.e(f'lea ({hexs(mm.disp)},%a5,%{GPR[base]}.l),%sp'); return
            raise Unsupported('lea esp complex')
        if m == 'cmp':
            esp_to('d7')
            if sreg.type == X86_OP_IMM: self.e(f'cmp.l #{hexs(sreg.imm)},%d7')
            elif sreg.type == X86_OP_MEM:
                self.e('movem.l %d6,32(%a6)'); self.load_mem(i, sreg, 'd6', 4); self.e('cmp.l %d6,%d7'); self.e('movem.l 32(%a6),%d6')
            else:
                r, k = self.r(i.reg_name(sreg.reg)); self.e(f'cmp.l %{r},%d7')
            return
        raise Unsupported('reg esp')


    # ------------------------------------------------------------------ moves
    def x_mov(self, i, live, pfx):
        d, s = i.operands[0], i.operands[1]
        size = d.size
        def body():
            if d.type == X86_OP_REG:
                dn = i.reg_name(d.reg)
                if dn in SEGREGS: return self.mov_seg(i, d, s)
                dr, k = self.r(dn)
                if s.type == X86_OP_REG and i.reg_name(s.reg) in SEGREGS:
                    self.e(f'moveq #0,%{dr}' if False else f'move.w #0x178,%{dr}'); return
                if k == 'h':
                    # ah <- src : rol.w #8 ; move.b ; rol.w #8
                    sv = self.src_val(i, s, 1, 'd7')
                    self.e(f'rol.w #8,%{dr}'); self.e(f'move.b {sv},%{dr}'); self.e(f'rol.w #8,%{dr}'); return
                if s.type == X86_OP_MEM:
                    self.load_mem(i, s, dr, size)
                elif s.type == X86_OP_IMM:
                    self.e(f'move.{SZ[size]} #{hexs(s.imm & ((1 << 8 * size) - 1))},%{dr}')
                else:
                    sn = i.reg_name(s.reg); sr, sk = self.r(sn)
                    if sk == 'h':
                        self.e(f'move.w %{sr},%d7'); self.e(f'lsr.w #8,%d7'); self.e(f'move.b %d7,%{dr}')
                    elif dr != sr:
                        self.e(f'move.{SZ[size]} %{sr},%{dr}')
                return
            # memory destination
            if s.type == X86_OP_IMM:
                v = s.imm & ((1 << 8 * size) - 1)
                pre, ea = self.mem(i, d)
                for l in pre: self.e(l)
                self.e(f'move.{SZ[size]} #{hexs(swapped_imm(v, size))},{ea}')
            else:
                sn = i.reg_name(s.reg); sr, sk = self.r(sn)
                if sk == 'h':
                    self.e(f'move.w %{sr},%d7'); self.e('lsr.w #8,%d7'); sr = 'd7'
                self.store_mem(i, d, sr, size)
        self.protected(live, body, ALL)

    def mov_seg(self, i, d, s):
        dn = i.reg_name(d.reg)
        if dn == 'cs': raise Unsupported('mov cs')
        if s.type == X86_OP_REG:
            sr, k = self.r(i.reg_name(s.reg))
            self.e(f'move.w %{sr},%d7')
        else:
            self.load_mem(i, s, 'd7', 2)
        self.e(f'jbsr rt_setseg_{dn}')   # selector in d7.w

    def x_movzx(self, i, live, pfx):
        d, s = i.operands
        dr, _ = self.r(i.reg_name(d.reg))
        ds_ = d.size
        def body():
            if s.type == X86_OP_MEM:
                self.load_mem(i, s, 'd7', s.size)
                srcr = 'd7'; ssz = s.size
            else:
                sr, k = self.r(i.reg_name(s.reg)); ssz = s.size
                if k == 'h':
                    self.e(f'move.w %{sr},%d7'); self.e('lsr.w #8,%d7'); srcr = 'd7'
                else:
                    srcr = sr
            if ds_ == 4:
                self.e(f'moveq #0,%{dr}')
                self.e(f'move.{SZ[ssz]} %{srcr},%{dr}')
            else:  # 16-bit dest from byte: only low word written
                self.e(f'moveq #0,%d7') if srcr != 'd7' else None
                if srcr != 'd7':
                    self.e(f'move.b %{srcr},%d7')
                    self.e(f'and.w #0xff,%d7')
                else:
                    self.e('and.w #0xff,%d7')
                self.e(f'move.w %d7,%{dr}')
        self.protected(live, body)

    def x_movsx(self, i, live, pfx):
        d, s = i.operands
        dr, _ = self.r(i.reg_name(d.reg)); ds_ = d.size; ssz = s.size
        def body():
            if s.type == X86_OP_MEM:
                self.load_mem(i, s, 'd7', ssz); srcr = 'd7'
            else:
                sr, k = self.r(i.reg_name(s.reg))
                if k == 'h':
                    self.e(f'move.w %{sr},%d7'); self.e('lsr.w #8,%d7'); srcr = 'd7'
                else: srcr = sr
            if ds_ == 4:
                if ssz == 1:
                    self.e(f'move.b %{srcr},%{dr}') if srcr != dr else None
                    self.e(f'extb.l %{dr}')
                else:
                    self.e(f'move.w %{srcr},%{dr}') if srcr != dr else None
                    self.e(f'ext.l %{dr}')
            else:
                self.e(f'move.b %{srcr},%d7'); self.e('ext.w %d7'); self.e(f'move.w %d7,%{dr}')
        self.protected(live, body)

    def x_lea(self, i, live, pfx):
        d, s = i.operands
        dr, _ = self.r(i.reg_name(d.reg))
        m = s.mem
        base = i.reg_name(m.base) if m.base else None
        idx = i.reg_name(m.index) if m.index else None
        sc = m.scale; disp = m.disp
        if d.size != 4: raise Unsupported('lea16')
        if base == 'esp' or idx == 'esp':
            # value = (a7 - a5) + disp
            self.e('move.l %sp,%d7') if False else None
        # value computation without flags: use lea on address regs
        def comp():
            if base == 'esp':
                self.e(f'lea ({hexs(disp)},%sp),%a0'); self.e(f'move.l %a0,%{dr}'); self.e('sub.l %a5,%' + dr)
                return
            if base and idx:
                self.e(f'movea.l %{GPR[base]},%a0')
                self.e(f'lea ({hexs(disp)},%a0,%{GPR[idx]}.l*{sc}),%a0')
            elif base:
                self.e(f'movea.l %{GPR[base]},%a0'); self.e(f'lea ({hexs(disp)},%a0),%a0')
            elif idx:
                self.e(f'movea.l %{GPR[idx]},%a0') if sc == 1 else None
                if sc == 1:
                    self.e(f'lea ({hexs(disp)},%a0),%a0')
                else:
                    self.e(f'suba.l %a0,%a0'); self.e(f'lea ({hexs(disp)},%a0,%{GPR[idx]}.l*{sc}),%a0')
            else:
                self.e(f'move.l #{hexs(disp)},%{dr}'); return
            self.e(f'move.l %a0,%{dr}')
        # move.l sets flags -> protect
        self.protected(live, comp)

    def x_xchg(self, i, live, pfx):
        a, b = i.operands; size = a.size
        def body():
            if a.type == X86_OP_REG and b.type == X86_OP_REG:
                ar, ak = self.r(i.reg_name(a.reg)); br, bk = self.r(i.reg_name(b.reg))
                if size == 4: self.e(f'exg %{ar},%{br}')
                elif size == 2:
                    self.e(f'move.w %{ar},%d7'); self.e(f'move.w %{br},%{ar}'); self.e(f'move.w %d7,%{br}')
                else:
                    if ak == 'h' or bk == 'h': raise Unsupported('xchg h')
                    self.e(f'move.b %{ar},%d7'); self.e(f'move.b %{br},%{ar}'); self.e(f'move.b %d7,%{br}')
                return
            reg, mem = (a, b) if a.type == X86_OP_REG else (b, a)
            rr, rk = self.r(i.reg_name(reg.reg))
            if rk == 'h': raise Unsupported('xchg h')
            self.load_mem(i, mem, 'd7', size)
            self.store_mem(i, mem, rr, size, 'a?') if False else None
            # store reg to mem (needs second temp for swap): use stack
            self.e(f'move.l %{rr},-(%sp)')
            pre, ea = self.mem(i, mem, extra=0)
            # recompute ea relative to unchanged a7? mem operand with esp base would be off by 4; handle
            if (i.reg_name(mem.mem.base) if mem.mem.base else None) == 'esp':
                raise Unsupported('xchg esp')
            for l in pre: self.e(l)
            self.e(f'move.l (%sp)+,%{rr}') if False else None
            self.e(f'move.l (%sp),%a1') if False else None
            # byte swap reg copy in a1-free way: do swap on stack copy via d-reg trick
            self.e(f'exg %d7,%{rr}')            # d7=reg value, rr=mem value
            sw = swap32 if size == 4 else swap16
            if size == 1:
                self.e(f'move.b %d7,{ea}')
            else:
                for l in sw('d7'): self.e(l)
                self.e(f'move.{SZ[size]} %d7,{ea}')
            self.e('addq.l #4,%sp')
        self.protected(live, body)

    def x_cwde(self, i, live, pfx):
        self.protected(live, lambda: self.e('ext.l %d0'))

    def x_cbw(self, i, live, pfx):
        self.protected(live, lambda: self.e('ext.w %d0'))

    def x_cdq(self, i, live, pfx):
        def b():
            self.e('move.l %d0,%d2'); self.e('smi %d2'); self.e('extb.l %d2')
        self.protected(live, b)

    def x_cwd(self, i, live, pfx):
        def b():
            self.e('move.w %d0,%d7'); self.e('smi %d7'); self.e('extb.l %d7'); self.e('move.w %d7,%d2')
        self.protected(live, b)

    # ------------------------------------------------------------------ ALU
    ALU = {'add': 'add', 'sub': 'sub', 'and': 'and', 'or': 'or', 'xor': 'eor', 'cmp': 'cmp'}

    def alu(self, i, live, op68, wb=True, test=False):
        d, s = i.operands; size = d.size
        sz = SZ[size]
        # destination register
        if d.type == X86_OP_REG:
            dn = i.reg_name(d.reg); dr, dk = self.r(dn)
            if dk == 'h':
                # operate on swapped copy
                self.e(f'rol.w #8,%{dr}')
                sv = self.src_val(i, s, 1, 'd7')
                self.alu_emit(op68, 'b', sv, f'%{dr}', s)
                if wb: pass
                # rol back must not disturb flags
                self.protected(live, lambda: self.e(f'rol.w #8,%{dr}'))
                return
            sv = self.src_val(i, s, size, 'd7')
            self.alu_emit(op68, sz, sv, f'%{dr}', s)
            return
        # memory destination
        if size == 1 and op68 == 'cmp':
            self.load_mem(i, d, 'd7', 1)
            if s.type == X86_OP_IMM: self.e(f'cmp.b #{hexs(s.imm & 0xFF)},%d7')
            else:
                sr, sk = self.r(i.reg_name(s.reg))
                if sk == 'h': raise Unsupported('cmp mem,ah')
                self.e(f'cmp.b %{sr},%d7')
            return
        if size == 1:
            sv = self.src_val(i, s, 1, 'd7') if s.type != X86_OP_MEM else self.src_val(i, s, 1, 'd7')
            pre, ea = self.mem(i, d)
            for l in pre: self.e(l)
            self.alu_emit(op68, 'b', sv, ea, s)
            return
        # 16/32-bit memory destination: load swapped to d7, op with reg/imm source, store back (unless cmp/test)
        if s.type == X86_OP_MEM: raise Unsupported('mem,mem')
        self.load_mem(i, d, 'd7', size)
        if s.type == X86_OP_IMM:
            sv = f'#{hexs(s.imm & ((1 << 8 * size) - 1))}'
        else:
            sr, sk = self.r(i.reg_name(s.reg))
            if sk == 'h': raise Unsupported('mem op ah')
            sv = f'%{sr}'
        self.alu_emit(op68, sz, sv, '%d7', s)
        if wb:
            # store back with flags protected
            def st():
                pre, ea = self.mem(i, d)
                for l in pre: self.e(l)
                for l in (swap32('d7') if size == 4 else swap16('d7')): self.e(l)
                self.e(f'move.{sz} %d7,{ea}')
            self.protected(live, st)

    def alu_emit(self, op68, sz, sv, dst, s):
        if op68 == 'eor':
            if sv.startswith('#'):
                self.e(f'eori.{sz} {sv},{dst}')
            else:
                self.e(f'eor.{sz} {sv},{dst}')
        elif op68 == 'cmp':
            if sv.startswith('#'): self.e(f'cmpi.{sz} {sv},{dst}') if not dst.startswith('%d') else self.e(f'cmp.{sz} {sv},{dst}')
            else: self.e(f'cmp.{sz} {sv},{dst}')
        elif op68 in ('and', 'or', 'add', 'sub'):
            if sv.startswith('#'):
                self.e(f'{op68}i.{sz} {sv},{dst}')
            elif sv.startswith('%d'):
                self.e(f'{op68}.{sz} {sv},{dst}')
            else:
                self.e(f'{op68}.{sz} {sv},{dst}')
        else:
            raise Unsupported(op68)

    def x_add(self, i, live, pfx): self.alu(i, live, 'add')
    def x_sub(self, i, live, pfx): self.alu(i, live, 'sub')
    def x_and(self, i, live, pfx): self.alu(i, live, 'and')
    def x_or(self, i, live, pfx): self.alu(i, live, 'or')
    def x_xor(self, i, live, pfx): self.alu(i, live, 'eor')
    def x_cmp(self, i, live, pfx): self.alu(i, live, 'cmp', wb=False)

    def x_test(self, i, live, pfx):
        d, s = i.operands; size = d.size; sz = SZ[size]
        if d.type == X86_OP_REG and s.type == X86_OP_REG and d.reg == s.reg:
            dr, k = self.r(i.reg_name(d.reg))
            if k == 'h':
                self.e(f'move.w %{dr},%d7'); self.e('lsr.w #8,%d7'); self.e('tst.b %d7')
            else:
                self.e(f'tst.{sz} %{dr}')
            return
        # general test: and into scratch
        if d.type == X86_OP_MEM:
            self.load_mem(i, d, 'd7', size); a = '%d7'
        else:
            dr, k = self.r(i.reg_name(d.reg))
            if k == 'h': self.e(f'move.w %{dr},%d7'); self.e('lsr.w #8,%d7'); a = '%d7'
            else: self.e(f'move.{sz} %{dr},%d7'); a = '%d7'
        if s.type == X86_OP_IMM:
            self.e(f'andi.{sz} #{hexs(s.imm & ((1 << 8 * size) - 1))},{a}')
        else:
            if s.type == X86_OP_MEM:
                self.load_mem(i, s, 'd6', size) if False else None
                raise Unsupported('test x,mem')
            sr, k = self.r(i.reg_name(s.reg))
            if k == 'h': raise Unsupported('test h')
            self.e(f'and.{sz} %{sr},{a}')

    def x_inc(self, i, live, pfx): self.incdec(i, live, 'addq')
    def x_dec(self, i, live, pfx): self.incdec(i, live, 'subq')

    def incdec(self, i, live, op):
        d = i.operands[0]; size = d.size; sz = SZ[size]
        def go():
            if d.type == X86_OP_REG:
                dr, k = self.r(i.reg_name(d.reg))
                if k == 'h': raise Unsupported('inc ah')
                self.e(f'{op}.{sz} #1,%{dr}')
            elif size == 1:
                pre, ea = self.mem(i, d)
                for l in pre: self.e(l)
                self.e(f'{op}.b #1,{ea}')
            else:
                self.load_mem(i, d, 'd7', size)
                self.e(f'{op}.{sz} #1,%d7')
                def st():
                    pre, ea = self.mem(i, d)
                    for l in pre: self.e(l)
                    for l in (swap32('d7') if size == 4 else swap16('d7')): self.e(l)
                    self.e(f'move.{sz} %d7,{ea}')
                self.protected(live, st)
        if live & CF:
            # preserve CF across inc/dec
            self.e('scs -(%sp)')
            go()
            self.e('move.w %ccr,%d7'); self.e('and.w #0x1e,%d7'); self.e('tst.b (%sp)+'); self.e('beq 9f'); self.e('or.w #1,%d7')
            self.e('9: move.w %d7,%ccr')
        else:
            go()

    def x_neg(self, i, live, pfx):
        d = i.operands[0]; size = d.size; sz = SZ[size]
        if d.type == X86_OP_REG:
            dr, k = self.r(i.reg_name(d.reg))
            if k == 'h': raise Unsupported('neg h')
            self.e(f'neg.{sz} %{dr}')
        elif size == 1:
            pre, ea = self.mem(i, d)
            for l in pre: self.e(l)
            self.e(f'neg.b {ea}')
        else:
            self.load_mem(i, d, 'd7', size)
            self.e(f'neg.{sz} %d7')
            def st():
                pre, ea = self.mem(i, d)
                for l in pre: self.e(l)
                for l in (swap32('d7') if size == 4 else swap16('d7')): self.e(l)
                self.e(f'move.{sz} %d7,{ea}')
            self.protected(live, st)

    def x_not(self, i, live, pfx):
        d = i.operands[0]; size = d.size; sz = SZ[size]
        def go():
            if d.type == X86_OP_REG:
                dr, k = self.r(i.reg_name(d.reg))
                if k == 'h': raise Unsupported('not h')
                self.e(f'not.{sz} %{dr}')
            elif size == 1:
                pre, ea = self.mem(i, d)
                for l in pre: self.e(l)
                self.e(f'not.b {ea}')
            else:
                self.load_mem(i, d, 'd7', size); self.e(f'not.{sz} %d7')
                pre, ea = self.mem(i, d)
                for l in pre: self.e(l)
                for l in (swap32('d7') if size == 4 else swap16('d7')): self.e(l)
                self.e(f'move.{sz} %d7,{ea}')
        self.protected(live, go)

    # carry-dependent
    def setx_from_c(self):
        self.e('scs -(%sp)'); self.e('neg.b (%sp)'); self.e('lea 2(%sp),%sp')

    def adcsbb(self, i, live, op):
        d, s = i.operands; size = d.size; sz = SZ[size]
        self.setx_from_c()
        if d.type == X86_OP_REG:
            dr, k = self.r(i.reg_name(d.reg))
            if k == 'h': raise Unsupported('adc h')
            if s.type == X86_OP_IMM:
                self.e(f'move.{sz} #{hexs(s.imm & ((1 << 8 * size) - 1))},%d7'); sv = '%d7'
            elif s.type == X86_OP_MEM:
                self.load_mem(i, s, 'd7', size); sv = '%d7'
            else:
                sr, sk = self.r(i.reg_name(s.reg))
                if sk == 'h': raise Unsupported('adc h')
                sv = f'%{sr}'
            self.e('ori.b #4,%ccr')
            self.e(f'{op}.{sz} {sv},%{dr}')
            return
        # memory destination: d7 = loaded mem value; source reg/imm; X (set above) survives loads
        self.load_mem(i, d, 'd7', size)
        if s.type == X86_OP_IMM:
            self.e('movem.l %d6,32(%a6)')
            self.e(f'move.{sz} #{hexs(s.imm & ((1 << 8 * size) - 1))},%d6')
            self.e('ori.b #4,%ccr')
            self.e(f'{op}.{sz} %d6,%d7')
            self.e('movem.l 32(%a6),%d6')
        else:
            sr, sk = self.r(i.reg_name(s.reg))
            if sk == 'h': raise Unsupported('adc mem,ah')
            self.e('ori.b #4,%ccr')
            self.e(f'{op}.{sz} %{sr},%d7')
        def st():
            pre, ea = self.mem(i, d)
            for l in pre: self.e(l)
            if size == 4:
                for l in swap32('d7'): self.e(l)
            elif size == 2:
                for l in swap16('d7'): self.e(l)
            self.e(f'move.{sz} %d7,{ea}')
        self.protected(live, st)

    def x_adc(self, i, live, pfx): self.adcsbb(i, live, 'addx')
    def x_sbb(self, i, live, pfx): self.adcsbb(i, live, 'subx')

    def x_cmc(self, i, live, pfx):
        self.e('move.w %ccr,%d7'); self.e('eori.w #1,%d7'); self.e('move.w %d7,%ccr')
    def x_clc(self, i, live, pfx):
        self.e('and.b #0x1e,%ccr') if False else self.e('andi.b #0x1e,%ccr')
    def x_stc(self, i, live, pfx):
        self.e('ori.b #1,%ccr')

    # shifts / rotates
    def shift(self, i, live, op68, rot=False, x=False):
        d = i.operands[0]; size = d.size; sz = SZ[size]
        cnt = i.operands[1] if len(i.operands) > 1 else None
        if d.type == X86_OP_REG:
            dr, k = self.r(i.reg_name(d.reg))
            if k == 'h': raise Unsupported('shift ah')
            tgt = f'%{dr}'; mem = False
        else:
            if size == 1:
                tgt = None
            mem = True
        if cnt is None: n = 1; imm = True
        elif cnt.type == X86_OP_IMM: n = cnt.imm & 31; imm = True
        else: imm = False
        def emit_shift(tg):
            if imm:
                if n == 0: return
                if n <= 8: self.e(f'{op68}.{sz} #{n},{tg}')
                else:
                    self.e(f'moveq #{n},%d7'); self.e(f'{op68}.{sz} %d7,{tg}')
            else:
                self.e('move.l %d1,%d7'); self.e('and.l #31,%d7')
                if op68 in ('rol', 'ror', 'roxl', 'roxr') and size < 4:
                    pass
                self.e(f'{op68}.{sz} %d7,{tg}')
        if x: self.setx_from_c()
        if not mem:
            emit_shift(tgt); return
        # memory operand: d7 = value, count in imm or cl (via d6 saved in ctx)
        self.load_mem(i, d, 'd7', size)
        if imm:
            emit_shift('%d7')
        else:
            self.e('movem.l %d6,32(%a6)'); self.e('move.l %d1,%d6'); self.e('and.l #31,%d6'); self.e(f'{op68}.{sz} %d6,%d7'); self.e('movem.l 32(%a6),%d6')
        def st():
            pre, ea = self.mem(i, d)
            for l in pre: self.e(l)
            if size == 4:
                for l in swap32('d7'): self.e(l)
            elif size == 2:
                for l in swap16('d7'): self.e(l)
            self.e(f'move.{sz} %d7,{ea}')
        self.protected(live, st)

    def x_shl(self, i, live, pfx): self.shift(i, live, 'lsl')
    def x_sal(self, i, live, pfx): self.shift(i, live, 'lsl')
    def x_shr(self, i, live, pfx): self.shift(i, live, 'lsr')
    def x_sar(self, i, live, pfx): self.shift(i, live, 'asr')
    def x_rol(self, i, live, pfx): self.shift(i, live, 'rol')
    def x_ror(self, i, live, pfx): self.shift(i, live, 'ror')
    def x_rcl(self, i, live, pfx): self.shift(i, live, 'roxl', x=True)
    def x_rcr(self, i, live, pfx): self.shift(i, live, 'roxr', x=True)

    # multiply / divide
    def x_imul(self, i, live, pfx):
        ops = i.operands
        if len(ops) == 1:
            s = ops[0]; size = s.size
            if size != 4: raise Unsupported('imul1 8/16')
            sv = self.src_val(i, s, 4, 'd7')
            self.e(f'muls.l {sv},%d2:%d0')
            if live & (CF | OF):
                self.e('tst.l %d0'); self.e('smi %d7'); self.e('extb.l %d7'); self.e('cmp.l %d7,%d2'); self.e('beq 8f')
                self.e('ori.b #3,%ccr'); self.e('bra 9f'); self.e('8: andi.b #0x1c,%ccr'); self.e('9:')
            return
        d = ops[0]; dr, _ = self.r(i.reg_name(d.reg)); size = d.size
        if len(ops) == 2: a, b = d, ops[1]
        else: a, b = ops[1], ops[2]
        if size == 4:
            if len(ops) == 2:
                sv = self.src_val(i, b, 4, 'd7')
                self.e(f'muls.l {sv},%{dr}'); self.cf_from_v(live)
            else:
                sv = self.src_val(i, a, 4, 'd7')
                if sv != f'%{dr}': self.e(f'move.l {sv},%{dr}')
                self.e(f'muls.l #{hexs(b.imm)},%{dr}'); self.cf_from_v(live)
        else:
            if len(ops) == 2:
                sv = self.src_val(i, b, 2, 'd6') if False else None
                if b.type == X86_OP_MEM: self.load_mem(i, b, 'd7', 2); sv = '%d7'
                else:
                    sr, k = self.r(i.reg_name(b.reg)); sv = f'%{sr}'
                self.e(f'move.w %{dr},%d6') if False else None
                # d6 may be edi: use stack
                self.e(f'move.w {sv},-(%sp)') if False else None
                self.e(f'move.w %{dr},%d7') if sv != '%d7' else None
                if sv == '%d7':
                    self.e(f'move.l %d7,-(%sp)'); self.e(f'move.w %{dr},%d7'); self.e('muls.w (%sp)+,%d7') if False else None
                    self.e('muls.w 2(%sp),%d7'); self.e('addq.l #4,%sp')
                else:
                    self.e(f'muls.w {sv},%d7')
                self.e(f'move.w %d7,%{dr}')
                self.imul16_flags(live)
            else:
                self.e(f'move.w %d7,%d7') if False else None
                if a.type == X86_OP_MEM: self.load_mem(i, a, 'd7', 2)
                else:
                    sr, k = self.r(i.reg_name(a.reg)); self.e(f'move.w %{sr},%d7')
                self.e(f'muls.w #{b.imm & 0xFFFF if b.imm >= 0 else (b.imm & 0xFFFF)},%d7')
                self.e(f'move.w %d7,%{dr}')
                self.imul16_flags(live)

    def imul16_flags(self, live):
        if live & (CF | OF):
            self.e('movea.w %d7,%a0'); self.e('cmpa.l %d7,%a0'); self.e('beq 8f'); self.e('ori.b #3,%ccr'); self.e('bra 9f')
            self.e('8: andi.b #0x1c,%ccr'); self.e('9:')

    def cf_from_v(self, live):
        if live & CF:
            self.e('bvc 9f'); self.e('ori.b #1,%ccr'); self.e('9:')

    def byte_src_to_ctx(self, i, s, signed=False):
        """source byte operand -> ctx word at 32(%a6) (zero or sign extended)."""
        if s.type == X86_OP_MEM:
            self.load_mem(i, s, 'd7', 1)
        else:
            r, k = self.r(i.reg_name(s.reg))
            if k == 'h': self.e(f'move.w %{r},%d7'); self.e('lsr.w #8,%d7')
            else: self.e(f'move.b %{r},%d7')
        if signed:
            self.e('ext.w %d7'); self.e('move.w %d7,32(%a6)')
        else:
            self.e('clr.w 32(%a6)'); self.e('move.b %d7,33(%a6)')

    def x_mul(self, i, live, pfx):
        s = i.operands[0]
        if s.size == 1:
            self.byte_src_to_ctx(i, s)
            self.e('moveq #0,%d7'); self.e('move.b %d0,%d7'); self.e('mulu.w 32(%a6),%d7'); self.e('move.w %d7,%d0')
            if live & (CF | OF):
                self.e('andi.w #0xff00,%d7'); self.e('beq 9f'); self.e('ori.b #3,%ccr'); self.e('9:')
            return
        if s.size == 2:
            if s.type == X86_OP_MEM: self.load_mem(i, s, 'd7', 2); sv = '%d7'
            else: sv = '%' + self.r(i.reg_name(s.reg))[0]
            self.e('move.w %d0,%d6') if False else None
            self.e('movem.l %d6,36(%a6)'); self.e('moveq #0,%d6'); self.e('move.w %d0,%d6'); self.e(f'mulu.w {sv},%d6')
            self.e('move.w %d6,%d0'); self.e('swap %d6'); self.e('move.w %d6,%d2'); 
            if live & (CF | OF):
                self.e('tst.w %d2'); self.e('beq 9f'); self.e('ori.b #3,%ccr'); self.e('9:')
            self.e('movem.l 36(%a6),%d6')
            return
        self.mul32(i, live)

    def x_div(self, i, live, pfx):
        s = i.operands[0]
        if s.size == 4:
            sv = self.src_val(i, s, 4, 'd7'); self.e(f'divu.l {sv},%d2:%d0'); return
        if s.size == 2:
            if s.type == X86_OP_MEM: self.load_mem(i, s, 'd7', 2); sv = 32
            else: sv = None
            # dividend dx:ax in d6 (saved)
            self.e('movem.l %d6,36(%a6)')
            if s.type == X86_OP_MEM:
                self.e('move.w %d7,32(%a6)'); dv = '32(%a6)'
            else:
                dv = '%' + self.r(i.reg_name(s.reg))[0]
            self.e('move.w %d2,%d6'); self.e('swap %d6'); self.e('move.w %d0,%d6'); self.e(f'divu.w {dv},%d6')
            self.e('move.w %d6,%d0'); self.e('swap %d6'); self.e('move.w %d6,%d2'); self.e('movem.l 36(%a6),%d6')
            return
        self.byte_src_to_ctx(i, s)
        self.e('moveq #0,%d7'); self.e('move.w %d0,%d7'); self.e('divu.w 32(%a6),%d7')
        self.e('move.b %d7,%d0'); self.e('swap %d7'); self.e('rol.w #8,%d0'); self.e('move.b %d7,%d0'); self.e('rol.w #8,%d0')

    def x_idiv(self, i, live, pfx):
        s = i.operands[0]
        if s.size == 4:
            sv = self.src_val(i, s, 4, 'd7'); self.e(f'divs.l {sv},%d2:%d0'); return
        raise Unsupported('idiv 8/16')

    def mul32(self, i, live):
        s = i.operands[0]
        sv = self.src_val(i, s, 4, 'd7')
        self.e(f'mulu.l {sv},%d2:%d0')
        if live & (CF | OF):
            self.e('tst.l %d2'); self.e('beq 9f'); self.e('ori.b #3,%ccr'); self.e('9:')

    # --- misc added after the first test round
    def x_lss(self, i, live, pfx):
        d, m = i.operands
        if i.reg_name(d.reg) != 'esp': raise Unsupported('lss')
        self.load_mem(i, m, 'd7', 4)
        self.e('lea (%a5,%d7.l),%sp')
        # selector word at +4 ignored (ss stays flat)

    def x_push16(self, i, live, pfx): pass

    def x_in(self, i, live, pfx):
        d, p = i.operands; size = d.size
        def body():
            self.port_arg(p)
            self.e(f'jbsr rt_in{size * 8}')
            r, k = self.r(i.reg_name(d.reg))
            self.e(f'move.{SZ[size]} %d7,%{r}')
        self.protected(live, body)

    def x_out(self, i, live, pfx):
        p, v = i.operands; size = v.size
        def body():
            self.port_arg(p)
            r, k = self.r(i.reg_name(v.reg))
            self.e(f'move.{SZ[size]} %{r},%d7')
            self.e(f'jbsr rt_out{size * 8}')
        self.protected(live, body)

    def port_arg(self, p):
        if p.type == X86_OP_IMM:
            self.e(f'movea.l #{p.imm & 0xFFFF},%a0')
        else:
            self.e('movea.w %d2,%a0')

    def x_int(self, i, live, pfx):
        self.e(f'jbsr rt_int_{i.operands[0].imm & 0xFF:02x}')

    def x_int3(self, i, live, pfx):
        self.e('jbsr rt_int3')

    def string(self, i, live, pfx, kind, size):
        p = ''
        if pfx:
            p = 'rep_' if pfx in ('rep', 'repne') and kind != 'scas' else (pfx + '_')
        nm = f'rt_{p}{kind}{"bwd"[{1: 0, 2: 1, 4: 2}[size]]}'
        self.e(f'jbsr {nm}')

    def x_outsb(self, i, live, pfx): self.string(i, live, pfx, 'outs', 1)
    def x_outsw(self, i, live, pfx): self.string(i, live, pfx, 'outs', 2)
    def x_outsd(self, i, live, pfx): self.string(i, live, pfx, 'outs', 4)
    def x_cmpsb(self, i, live, pfx): self.string(i, live, pfx, 'cmps', 1)
    def x_cmpsw(self, i, live, pfx): self.string(i, live, pfx, 'cmps', 2)
    def x_cmpsd(self, i, live, pfx): self.string(i, live, pfx, 'cmps', 4)

    # ------------------------------------------------------------------ stack
    def x_push(self, i, live, pfx):
        s = i.operands[0]
        if s.type == X86_OP_IMM:
            self.e(f'pea {hexs(swapped_imm(s.imm, 4))}'); return
        def body():
            if s.type == X86_OP_REG:
                n = i.reg_name(s.reg)
                if n in SEGREGS:
                    self.e('pea 0x78010000') if n in ('ds', 'es', 'ss') else self.e('pea 0x00000000'); return
                if n == 'esp':
                    self.e('move.l %sp,%d7'); self.e('sub.l %a5,%d7')
                else:
                    r, k = self.r(n)
                    if k == 'w':
                        self.e(f'move.w %{r},%d7') if False else None
                        raise Unsupported('push16')
                    self.e(f'move.l %{r},%d7')
            else:
                if s.size != 4: raise Unsupported('push16 mem')
                self.load_mem(i, s, 'd7', 4)
            for l in swap32('d7'): self.e(l)
            self.e('move.l %d7,-(%sp)')
        self.protected(live, body)

    def x_pop(self, i, live, pfx):
        d = i.operands[0]
        def body():
            if d.type == X86_OP_REG:
                n = i.reg_name(d.reg)
                if n in SEGREGS:
                    self.e('move.l (%sp)+,%d7');
                    for l in swap32('d7'): self.e(l)
                    self.e(f'jbsr rt_setseg_{n}'); return
                r, k = self.r(n)
                if k != 'l': raise Unsupported('pop16')
                self.e(f'move.l (%sp)+,%{r}')
                for l in swap32(r): self.e(l)
            else:
                self.e('move.l (%sp)+,%d7')
                for l in swap32('d7'): self.e(l)
                pre, ea = self.mem(i, d)
                for l in pre: self.e(l)
                for l in swap32('d7'): self.e(l)
                self.e(f'move.l %d7,{ea}')
        self.protected(live, body)

    def x_leave(self, i, live, pfx):
        # mov esp,ebp ; pop ebp
        def body():
            self.e('lea (%a5,%d4.l),%sp')
            self.e('move.l (%sp)+,%d4')
            for l in swap32('d4'): self.e(l)
        self.protected(live, body)

    def x_pushal(self, i, live, pfx):
        def body():
            self.e('move.l %sp,%a0')
            for r in ('d0', 'd1', 'd2', 'd3'):
                self.e(f'move.l %{r},%d7')
                for l in swap32('d7'): self.e(l)
                self.e('move.l %d7,-(%sp)')
            self.e('move.l %a0,%d7'); self.e('sub.l %a5,%d7')
            for l in swap32('d7'): self.e(l)
            self.e('move.l %d7,-(%sp)')
            for r in ('d4', 'd5', 'd6'):
                self.e(f'move.l %{r},%d7')
                for l in swap32('d7'): self.e(l)
                self.e('move.l %d7,-(%sp)')
        self.protected(live, body)
    x_pushad = x_pushal

    def x_popal(self, i, live, pfx):
        def body():
            for r in ('d6', 'd5', 'd4'):
                self.e(f'move.l (%sp)+,%{r}')
                for l in swap32(r): self.e(l)
            self.e('addq.l #4,%sp')
            for r in ('d3', 'd2', 'd1', 'd0'):
                self.e(f'move.l (%sp)+,%{r}')
                for l in swap32(r): self.e(l)
        self.protected(live, body)
    x_popad = x_popal

    def x_pushfd(self, i, live, pfx):
        self.e('jbsr rt_pushfd')
    def x_popfd(self, i, live, pfx):
        self.e('jbsr rt_popfd')
    def x_cli(self, i, live, pfx):
        self.e('clr.b 4(%a6)')
    def x_sti(self, i, live, pfx):
        self.e('move.b #1,4(%a6)')
    def x_cld(self, i, live, pfx):
        self.e('clr.b 24(%a6)')
    def x_std(self, i, live, pfx):
        self.e('move.b #1,24(%a6)')
    def x_nop(self, i, live, pfx):
        pass

    # ------------------------------------------------------------------ control flow
    def x_call(self, i, live, pfx):
        t = i.operands[0]
        if t.type == X86_OP_IMM:
            self.e(f'jbsr {self.label_of(t.imm)}')
        else:
            self.indirect(i, t, 'jsr')

    def x_jmp(self, i, live, pfx):
        t = i.operands[0]
        if t.type == X86_OP_IMM:
            self.e(f'jbra {self.label_of(t.imm)}')
        else:
            self.indirect(i, t, 'jmp')

    def indirect(self, i, t, how):
        if t.type == X86_OP_REG:
            r, k = self.r(i.reg_name(t.reg)); self.e(f'move.l %{r},%d7')
        else:
            self.load_mem(i, t, 'd7', 4)
        self.e(f'sub.l #{hexs(self.base)},%d7')
        self.e(f'lea {self.dispatch},%a0')
        self.e('move.l (%a0,%d7.l*4),%a0')
        self.e(f'{how} (%a0)')

    def x_ret(self, i, live, pfx):
        if i.operands:
            self.e(f'rtd #{i.operands[0].imm}')
        else:
            self.e('rts')

    def x_iretd(self, i, live, pfx):
        self.e('move.l (%sp)+,%a0'); self.e('addq.l #8,%sp'); self.e('jmp (%a0)')
    x_iret = x_iretd

    def x_jcc(self, i, live, pfx):
        c = i.mnemonic[1:]
        c = COND_ALIAS.get(c, c)
        t = i.operands[0]
        self.e(f'jb{COND[c][0]} {self.label_of(t.imm)}')

    def x_setcc(self, i, live, pfx):
        c = i.mnemonic[3:]; c = COND_ALIAS.get(c, c)
        d = i.operands[0]
        cc = COND[c][0]
        def body():
            if d.type == X86_OP_REG:
                dr, k = self.r(i.reg_name(d.reg))
                if k == 'h': raise Unsupported('setcc ah')
                self.e(f's{cc} %{dr}')
                # 68k Scc yields 0xFF for true; x86 wants 1
                self.e(f'neg.b %{dr}') if False else None
                self.e(f'and.b #1,%{dr}')
            else:
                pre, ea = self.mem(i, d)
                for l in pre: self.e(l)
                self.e(f's{cc} {ea}'); self.e(f'and.b #1,{ea}')
        # Scc itself preserves flags; and.b clobbers them -> protect if live
        self.protected(live & ~0, body)

    def x_loop(self, i, live, pfx):
        def go():
            self.e('subq.l #1,%d1'); self.e(f'jbne {self.label_of(i.operands[0].imm)}')
        go()

    def x_jecxz(self, i, live, pfx):
        self.e('tst.l %d1'); self.e(f'jbeq {self.label_of(i.operands[0].imm)}')


    def x_stosb(self, i, live, pfx): self.string(i, live, pfx, 'stos', 1)
    def x_stosw(self, i, live, pfx): self.string(i, live, pfx, 'stos', 2)
    def x_stosd(self, i, live, pfx): self.string(i, live, pfx, 'stos', 4)
    def x_movsb(self, i, live, pfx): self.string(i, live, pfx, 'movs', 1)
    def x_movsw(self, i, live, pfx): self.string(i, live, pfx, 'movs', 2)
    def x_movsd(self, i, live, pfx): self.string(i, live, pfx, 'movs', 4)
    def x_lodsb(self, i, live, pfx): self.string(i, live, pfx, 'lods', 1)
    def x_lodsw(self, i, live, pfx): self.string(i, live, pfx, 'lods', 2)
    def x_lodsd(self, i, live, pfx): self.string(i, live, pfx, 'lods', 4)
    def x_scasb(self, i, live, pfx): self.string(i, live, pfx, 'scas', 1)
    def x_scasw(self, i, live, pfx): self.string(i, live, pfx, 'scas', 2)
    def x_scasd(self, i, live, pfx): self.string(i, live, pfx, 'scas', 4)

