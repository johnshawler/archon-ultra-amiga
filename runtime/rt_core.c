/* Runtime core: INT services (DOS / DPMI / BIOS / mouse), port dispatch, string instructions, polling & IRQ delivery. */
#include "rt.h"
#include "layout.h"
#include "bda.h"
#ifdef HOSTED
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#endif

u8 *WIN; struct ctx *CTX; long rt_replay;
#ifdef HOSTED
static u64 tstop;                                   /* index of the instruction at which the trace stub next calls the slow handler */
#define rt_trn ((long long)(tstop - (u32)CTX->trem) + 1)     /* instructions counted including the one being executed (what the reference records), exact between stops */
static double tports;                                /* cycles charged for port reads, on top of one per instruction */
#endif
#ifdef HOSTED
double rt_cycles; extern void hw_set_exact(double us); extern void hw_use_exact(void);
#endif
u32 xl_tab[0x3d100];
extern const u8 xl_bits[]; extern const u16 xl_deltas[]; extern const u32 xl_first, xl_nstarts, xl_codesize;
extern void rt_dispatch_miss(void);
/* fill the x86-offset -> native-address dispatch table from the compact start/delta data */
void rt_build_xl_tab(void) {
    for (u32 i = 0; i < sizeof xl_tab / 4; i++) xl_tab[i] = (u32)rt_dispatch_miss;
    u32 native = xl_first, k = 0, n = 0;
    for (u32 off = 0; off < xl_codesize && n < xl_nstarts; off++) {
        if (xl_bits[off >> 3] & (1 << (off & 7))) { if (n) native += xl_deltas[n - 1]; xl_tab[off] = native; n++; }
    }
    (void)k;
}
#define HEAP_BASE 0x1e0000u
#define DOS_BASE 0x8eb0u
#define DOS_END 0xA0000u
/* DOS conventional memory (paragraph arithmetic copied from DOSBox/DOS4GW behaviour observed in the traces):
   low hole A [0x220, 0x861) serves INT 21h/48h, high area B [0x8eb, 0x9fff) serves DPMI 0100. */
static struct { u32 sel, seg, n; } dosrec[64]; static int ndosrec;
static u32 a_ptr = 0x220, a_end = 0x861, b_ptr = 0x8eb, b_end = 0x9fff;
u32 rt_heap_end = 0xFFF000u, rt_heap_reserve = 0;      /* harness mimics DOSBox's 16 MB; the Amiga build lowers these */
#define HEAP_END rt_heap_end
static u32 heap_ptr = HEAP_BASE;
#ifdef HOSTED
#define POLL_N 64
#else
#define POLL_N 128
#endif
#define QUANT_US 16
static u32 vtime; static int getenv_dbg = 1;

static u32 vec_pm[256];                   /* protected-mode interrupt vectors (x86 flat offsets) set by the program */
static u32 ldt_base[64]; static int ldt_n;
static u32 mouse_x, mouse_y, mouse_b;
static int trace_int = 1;
extern void hw_key(u8);
void rt_leave(void);

#define SEL_FLAT 0x178
static u32 sel_base(u32 sel) {
    if (sel == 0x28) return 0x1920; if (sel == 0x30) return 0x1880;
    if (sel == SEL_FLAT || sel == 0x170 || sel == 0 || sel == 0x20 || sel == 0xC8) return 0;
    if (sel >= 0x1d0 && ((sel - 0x1d0) >> 3) < (u32)ldt_n) return ldt_base[(sel - 0x1d0) >> 3];
    return 0;
}
static u32 new_sel(u32 base) { ldt_base[ldt_n] = base; return 0x1d0 + 8 * ldt_n++; }

static u32 halloc(u32 bytes) {
    if (bytes > HEAP_END) return 0;
    bytes = (bytes + 15) & ~15u;
    if (bytes > HEAP_END - heap_ptr) return 0;
    u32 p = heap_ptr; heap_ptr += bytes;
    for (u32 i = 0; i < bytes; i += 4096) { }
    return p;
}

void rt_c_setseg(int which, u32 sel) {
    u32 b = sel_base(sel); static const int idx[6] = {0, 4, 5, 3, 2, 1};   /* which -> sel[] slot (es,fs,gs,ds,ss,cs) */
    CTX->sel[idx[which]] = sel;
    switch (which) {
    case 0: CTX->es_base = b; break; case 1: CTX->fs_base = b; break; case 2: CTX->gs_base = b; break;
    case 3: if (b) plat_log("rt: ds <- sel %x base %x (non-flat ds unsupported)\n", sel, b); CTX->ds_base = b; break;
    case 4: if (b) plat_log("rt: ss <- sel %x base %x (non-flat ss unsupported)\n", sel, b); break;
    }
}

u32 rt_mk_eflags(u32 ccr) {
    u32 f = 0x202;
    if (ccr & CCR_C) f |= 1; if (ccr & CCR_Z) f |= 0x40; if (ccr & CCR_N) f |= 0x80; if (ccr & CCR_V) f |= 0x800;
    if (!CTX->iflag) f &= ~0x200u; if (CTX->df) f |= 0x400;
    return f;
}
u32 rt_set_eflags(u32 v) {
    CTX->iflag = (v >> 9) & 1; CTX->df = (v >> 10) & 1;
    u32 c = 0; if (v & 1) c |= CCR_C; if (v & 0x40) c |= CCR_Z; if (v & 0x80) c |= CCR_N; if (v & 0x800) c |= CCR_V; return c;
}

u32 rt_dbg_exit_code, rt_dbg_exit_a, rt_dbg_exit_b;
void rt_c_unimpl(u32 where) { rt_dbg_exit_code = 3; rt_dbg_exit_a = where; plat_log("rt: UNIMPLEMENTED instruction at x86 %x (or bad jump)\n", where); plat_exit(3); }
void rt_c_badtarget(u32 off, u32 ret) { rt_dbg_exit_code = 4; rt_dbg_exit_a = off + XL_BASE; rt_dbg_exit_b = ret; plat_log("rt: indirect jump/call to unknown x86 target %x (native return %x)\n", off + XL_BASE, ret); plat_exit(4); }

/* ---- ports */
#ifdef HOSTED
static int getenv_dbg2(void) { static int v = -1; if (v < 0) v = getenv("PORTLOG") != 0; return v; }
static FILE *prf; static struct { long idx; u16 port; u8 val; int ok; } prc; static int pr_loaded;
static void pr_next(void) {
    char line[64]; prc.ok = 0;
    while (fgets(line, sizeof line, prf)) { long i; unsigned p, v; if (sscanf(line, "%ld %x %x", &i, &p, &v) == 3) { prc.idx = i; prc.port = p; prc.val = v; prc.ok = 1; return; } }
}
static void pr_load(void) { pr_loaded = 1; prf = fopen("portreads.txt", "r"); if (prf) pr_next(); }
#endif
u32 rt_dbg_port;
u32 rt_c_in(u32 port, int size) {
    rt_dbg_port = port;
#ifdef HOSTED
    if (rt_replay) {
        if (!pr_loaded) pr_load();
        while (prc.ok && prc.idx < rt_trn) pr_next();
        if (prc.ok && prc.idx == rt_trn && prc.port == port) { u32 v = prc.val; pr_next(); tports += 30; rt_cycles = (double)rt_trn + tports; hw_set_exact(rt_cycles / 30.0); return v; }
        tports += 30; rt_cycles = (double)rt_trn + tports; hw_set_exact(rt_cycles / 30.0);
    }
#endif
    int h; u32 v = vga_port_in(port, size, &h); if (h) return v;
    v = hw_port_in(port, size, &h);
#ifdef HOSTED
    if (rt_replay && (port == 0x42 || port == 0x201 || port == 0x40) && getenv_dbg2()) plat_log("PR %lld %x %x\n", rt_trn, port, v & 0xFF);
#endif
    if (h) return v;
    { static u8 seen[65536]; if (!seen[port & 0xFFFF]++) plat_log("rt: in %x (size %d) unhandled\n", port, size); }
    return 0xFFFFFFFFu;
}
void rt_c_out(u32 port, u32 val, int size) {
    val &= size == 1 ? 0xFF : size == 2 ? 0xFFFF : 0xFFFFFFFFu;
#ifdef HOSTED
    if (rt_replay) { rt_cycles += 22; hw_set_exact(rt_cycles / 30.0); }
#endif
    if (vga_port_out(port, val, size)) return;
    if (hw_port_out(port, val, size)) return;
    { static u8 seen[65536]; if (!seen[port & 0xFFFF]++) plat_log("rt: out %x,%x (size %d) unhandled\n", port, val, size); }
}

/* ---- string instructions */
static u32 vgaR(u32 a, int sz) { return vga_in_window(a) ? vga_read(a, sz) : (sz == 1 ? ld8(a) : sz == 2 ? ld16(a) : ld32(a)); }
static void memW(u32 a, u32 v, int sz) { if (vga_in_window(a)) vga_write(a, v, sz); else if (sz == 1) st8(a, v); else if (sz == 2) st16(a, v); else st32(a, v); }
static u32 flags_sub(u32 a, u32 b, int sz) {
    u64 mask = sz == 1 ? 0xFF : sz == 2 ? 0xFFFF : 0xFFFFFFFFu; u32 sh = sz * 8 - 1;
    u64 r = ((u64)a - b) & mask; u32 f = 0;
    if ((u64)(a & mask) < (u64)(b & mask)) f |= CCR_C;
    if (r == 0) f |= CCR_Z; if ((r >> sh) & 1) f |= CCR_N;
    if ((((a ^ b) & (a ^ (u32)r)) >> sh) & 1) f |= CCR_V;
    return f;
}
u32 rt_c_str(int op, struct regs *r) {
    int s = op % 3, kind = (op / 3) % 6, pfx = op / 18; int sz = 1 << s;
    int rep = pfx != 0; int step = CTX->df ? -sz : sz;
#ifdef HOSTED
    if (getenv("STRDBG") && rep && r->ecx >= 0x20 && r->ecx < 0x80) plat_log("STR op=%d esi=%x edi=%x ecx=%x df=%d dsb=%x esb=%x src0=%08x\n", op, r->esi, r->edi, r->ecx, CTX->df, CTX->ds_base, CTX->es_base, ld32(CTX->ds_base + r->esi));
#endif
    u32 flags = *(u16 *)((char *)r + 40) & 0xF; int cmpl = (kind == 3 || kind == 4); (void)cmpl;
    u32 dsb = CTX->ds_base, esb = CTX->es_base;
    for (;;) {
        if (rep && r->ecx == 0) break;
        switch (kind) {
        case 0: { u32 v = vgaR(dsb + r->esi, sz); memW(esb + r->edi, v, sz); r->esi += step; r->edi += step; break; }
        case 1: { memW(esb + r->edi, r->eax, sz); r->edi += step; break; }
        case 2: { u32 v = vgaR(dsb + r->esi, sz); if (sz == 1) r->eax = (r->eax & ~0xFFu) | v; else if (sz == 2) r->eax = (r->eax & ~0xFFFFu) | v; else r->eax = v; r->esi += step; break; }
        case 3: { u32 v = vgaR(esb + r->edi, sz); flags = flags_sub(r->eax, v, sz); r->edi += step; break; }
        case 4: { u32 a = vgaR(dsb + r->esi, sz), b = vgaR(esb + r->edi, sz); flags = flags_sub(a, b, sz); r->esi += step; r->edi += step; break; }
        case 5: { u32 v = vgaR(dsb + r->esi, sz); rt_c_out(r->edx & 0xFFFF, v, sz); r->esi += step; break; }
        }
        if (!rep) break;
        r->ecx--;
        if (pfx == 2 && !(flags & CCR_Z)) break;      /* repe stops when not equal */
        if (pfx == 3 && (flags & CCR_Z)) break;       /* repne stops when equal */
    }
    return flags;
}

/* ---- polling, virtual time, interrupt delivery */
u32 vga_frame_count;
extern void hw_advance(u32);
extern void plat_vsync(u32 frame);
extern long rt_replay;
#ifndef HOSTED
extern void plat_poll(void);
#endif
void rt_c_poll_time(void) {
    static u32 last_frame_t;
    if (rt_replay) return;
#ifndef HOSTED
    plat_poll();                                   /* input, display, audio; the clock below is real time */
    hw_advance(plat_time_us());
#else
    vtime += QUANT_US; hw_advance(vtime);
    if (vtime - last_frame_t >= 14268) { last_frame_t += 14268; vga_frame_count++; plat_vsync(vga_frame_count); }
#endif
}
u32 rt_dbg_irq, rt_dbg_a, rt_dbg_b, rt_dbg_n5;
u32 rt_dbg_iflag;
u32 rt_c_poll(struct regs *r) {
    rt_dbg_iflag = CTX->iflag;
    rt_dbg_irq = HEAP_END;
#ifdef HOSTED
    { extern u32 rt_dbg_pc; static int en = -1, cnt; static u32 lo, hi; if (en < 0) { const char *e = getenv("POLLREGS"); en = e != 0; if (e) { lo = strtoul(e, (char **)&e, 16); hi = lo + 0x20; } }
      if (en && rt_dbg_pc >= lo && rt_dbg_pc < hi && cnt < 400) { cnt++; plat_log("pollregs pc %x eax %x ecx %x edx %x ebx %x ebp %x esi %x edi %x iflag %d\n", rt_dbg_pc, r->eax, r->ecx, r->edx, r->ebx, r->ebp, r->esi, r->edi, (int)CTX->iflag); } }
#endif
    if (rt_replay) { CTX->poll = 1000000; return 0; }
    CTX->poll = POLL_N;
    rt_c_poll_time();
    if (CTX->iflag && hw_irq_pending()) {
        { u32 ef = rt_mk_eflags(CTX->ccr_stub); CTX->tmp[0] = (ef << 24) | ((ef & 0xFF00) << 8) | ((ef >> 8) & 0xFF00) | (ef >> 24); }
        int vec = hw_irq_ack(); rt_dbg_n5 = (rt_dbg_n5 & 0xFFFF0000u) + 0x10000u + (vec & 0xFFFF);
        if (vec >= 0 && vec_pm[vec]) {
            u32 off = vec_pm[vec] - XL_BASE; if (off < 0x3d000 + 0x1000) { u32 t = xl_tab[off]; if (t) return t; }
            plat_log("rt: IRQ vector %x handler %x not translated\n", vec, vec_pm[vec]);
        }
    }
    return 0;
}

/* ---- DOS / DPMI / BIOS / mouse */
static u32 psp = 0x1920;
static struct { char name[64]; long h; } files[32];
static void set_cf(int e, struct regs *r, u32 err) { if (e) r->eax = (r->eax & 0xFFFF0000u) | err; }
static const char *winstr(u32 a) { return (const char *)(WIN + CTX->ds_base + a); }

static int int21(struct regs *r) {
    u8 ah = (r->eax >> 8) & 0xFF, al = r->eax & 0xFF;
    switch ((r->eax >> 8) & 0xFF) {
    case 0x30: r->eax = 5; r->ebx = 0x5048ff00u; return 0;
    case 0x25: vec_pm[al] = r->edx; plat_log("rt: set vector %02x = %x (int21)\n", al, r->edx); return 0;
    case 0x35: r->ebx = vec_pm[al] ? vec_pm[al] : 0xD09; return 0;
    case 0x3D: {
        for (int i = 5; i < 32; i++) if (!files[i].name[0]) {
            long h = plat_file_open(winstr(r->edx)); if (h < 0) { set_cf(1, r, 2); return 1; }
            files[i].h = h; files[i].name[0] = 1; r->eax = i; return 0; }
        set_cf(1, r, 4); return 1; }
    case 0x3E: { u32 b = r->ebx & 0xFFFF; if (b < 32 && files[b].name[0]) { plat_file_close(files[b].h); files[b].name[0] = 0; } return 0; }
    case 0x3F: { u32 b = r->ebx & 0xFFFF; if (b >= 32 || !files[b].name[0]) { set_cf(1, r, 6); return 1; }
        long n = plat_file_read(files[b].h, WIN + CTX->ds_base + r->edx, r->ecx); if (r->ecx != 8) plat_log("rt: read h=%u n=%u -> %ld to %x(+%x) first=%02x%02x%02x%02x\n", b, r->ecx, n, r->edx, CTX->ds_base, WIN[CTX->ds_base + r->edx], WIN[CTX->ds_base + r->edx + 1], WIN[CTX->ds_base + r->edx + 2], WIN[CTX->ds_base + r->edx + 3]); r->eax = n < 0 ? 0 : n; return 0; }
    case 0x42: { u32 b = r->ebx & 0xFFFF; if (b >= 32 || !files[b].name[0]) { set_cf(1, r, 6); return 1; }
        long off = (long)(s32)(((r->ecx & 0xFFFF) << 16) | (r->edx & 0xFFFF)); long p = plat_file_seek(files[b].h, off, al); plat_log("rt: seek h=%u off=%ld wh=%d -> %ld\n", b, off, al, p);
        r->edx = (r->edx & 0xFFFF0000u) | ((p >> 16) & 0xFFFF); r->eax = (r->eax & 0xFFFF0000u) | (p & 0xFFFF); return 0; }
    case 0x44: if (al == 0x0D) {          /* generic IOCTL for block devices; CL=0x66: get media ID (DOSBox reports serial 0x1234) */
            u32 b = CTX->ds_base + r->edx; plat_log("rt: ioctl 440D cx=%x buf=%x\n", r->ecx, b);
            if ((r->ecx & 0xFF) == 0x66) { st16(b, 0); st32(b + 2, 0x1234); const char *lab = "NO NAME    FAT16   "; for (int k = 0; k < 19; k++) WIN[b + 6 + k] = lab[k]; return 0; }
            return 0; }
        if (al == 0) { u32 dv = (r->ebx & 0xFFFF) < 5 ? 0x80D3 : 0x0000; r->edx = (r->edx & 0xFFFF0000u) | dv; r->eax = (r->eax & 0xFFFF0000u) | dv; } return 0;
    case 0x48: { u32 n = r->ebx & 0xFFFF;
        if (a_ptr + n > a_end) { set_cf(1, r, 8); r->ebx = (r->ebx & 0xFFFF0000u) | (a_end - a_ptr); return 1; }
        u32 seg = a_ptr; a_ptr += n + 1; r->eax = (r->eax & 0xFFFF0000u) | seg; return 0; }
    case 0x49: return 0;
    case 0x4A: return 0;
    case 0x4C: plat_log("rt: program exit code %d\n", al); rt_leave(); return 0;
    case 0x2A: r->ecx = 1994; r->edx = (3 << 8) | 14; r->eax = (r->eax & ~0xFF) | 1; return 0;
    case 0x2C: r->ecx = (12 << 8) | 0; r->edx = 0; return 0;
    case 0x2B: case 0x2D: r->eax &= ~0xFFu; return 0;
    case 0x19: r->eax &= ~0xFF; return 0;
    case 0x0E: return 0;
    case 0x47: WIN[r->esi] = 0; return 0;
    case 0xFF: if ((r->edx & 0xFFFF) == 0x78) { r->eax = 0x4734ffffu; return 0; } break;
    case 0x62: r->ebx = (r->ebx & ~0xFFFF) | 0x24; return 0;
    case 0x40: { u32 b = r->ebx & 0xFFFF; if (b == 1 || b == 2) { r->eax = r->ecx; return 0; } set_cf(1, r, 5); return 1; }
    case 0x4E: case 0x3C: case 0x41: case 0x43: set_cf(1, r, 2); return 1;
    case 0x1A: return 0; case 0x2F: r->ebx = 0x80; return 0;
    }
    plat_log("rt: int21 ah=%02x al=%02x unhandled (ebx=%x ecx=%x edx=%x)\n", ah, al, r->ebx, r->ecx, r->edx);
    set_cf(1, r, 1); return 1;
}

static int int31(struct regs *r) {
    u32 ax = r->eax & 0xFFFF;
    switch (ax) {
    case 0x0000: { u32 n = r->ecx & 0xFFFF; u32 s = new_sel(0); for (u32 i = 1; i < n; i++) new_sel(0); r->eax = (r->eax & ~0xFFFF) | s; return 0; }
    case 0x0001: return 0;
    case 0x0002: { u32 seg = r->ebx & 0xFFFF; r->eax = (r->eax & ~0xFFFF) | new_sel(seg << 4); return 0; }
    case 0x0006: { u32 b = sel_base(r->ebx & 0xFFFF); r->ecx = b >> 16; r->edx = b & 0xFFFF; return 0; }
    case 0x0007: { u32 s = r->ebx & 0xFFFF; u32 b = ((r->ecx & 0xFFFF) << 16) | (r->edx & 0xFFFF); if (s >= 0x1d0) ldt_base[(s - 0x1d0) >> 3] = b; return 0; }
    case 0x0008: return 0; case 0x0009: return 0; case 0x000B: return 0; case 0x000C: return 0;
    case 0x0100: { u32 n = r->ebx & 0xFFFF; u32 p = (b_ptr + n <= b_end) ? b_ptr * 16u : 0; if (p) b_ptr += n + 1; if (!p) { set_cf(1, r, 8); r->ebx = (r->ebx & 0xFFFF0000u) | (b_end - b_ptr); return 1; }
        for (u32 i = 0; i < n * 16u; i++) WIN[p + i] = 0;                    /* fresh conventional memory is zero in the reference (DOS/4GW) */
        { u32 sel = new_sel(p); if (ndosrec < 64) { dosrec[ndosrec].sel = sel; dosrec[ndosrec].seg = p >> 4; dosrec[ndosrec].n = n; ndosrec++; }
          r->eax = (r->eax & ~0xFFFF) | (p >> 4); r->edx = (r->edx & ~0xFFFF) | sel; } return 0; }
    case 0x0101: { u32 sel = r->edx & 0xFFFF;
        for (int k = ndosrec - 1; k >= 0; k--) if (dosrec[k].sel == sel) { if (dosrec[k].seg + dosrec[k].n + 1 == b_ptr) b_ptr = dosrec[k].seg; dosrec[k] = dosrec[--ndosrec]; break; }
        return 0; }
    case 0x0200: r->ecx = 0; r->edx = 0; return 0;
    case 0x0201: return 0;
    case 0x0204: r->ecx = (r->ecx & ~0xFFFF) | SEL_FLAT; r->edx = vec_pm[r->ebx & 0xFF]; return 0;
    case 0x0205: vec_pm[r->ebx & 0xFF] = r->edx; plat_log("rt: set vector %02x = %x (dpmi)\n", r->ebx & 0xFF, r->edx); return 0;
    case 0x0300: {            /* simulate real-mode interrupt: ES:EDI = register structure */
        u32 st = r->edi + CTX->es_base; u8 n = r->ebx & 0xFF;
        struct regs rr; rr.edi = ld32(st); rr.esi = ld32(st + 4); rr.ebp = ld32(st + 8); rr.ebx = ld32(st + 16); rr.edx = ld32(st + 20); rr.ecx = ld32(st + 24); rr.eax = ld32(st + 28);
        u16 es = ld16(st + 0x22), ds = ld16(st + 0x24);
        u32 save_ds = CTX->ds_base; CTX->ds_base = (u32)ds << 4;      /* real-mode ds:dx pointers */
        int c = rt_c_int(n, &rr);
        CTX->ds_base = save_ds; (void)es;
        st32(st, rr.edi); st32(st + 4, rr.esi); st32(st + 8, rr.ebp); st32(st + 16, rr.ebx); st32(st + 20, rr.edx); st32(st + 24, rr.ecx); st32(st + 28, rr.eax);
        st16(st + 0x20, c ? 1 : 0); return 0; }
    case 0x0500: { u32 st = r->edi + CTX->es_base; u32 room = (HEAP_END > heap_ptr + rt_heap_reserve) ? HEAP_END - heap_ptr - rt_heap_reserve : 0; u32 avail = (room & ~0xFFFu) + 0x1000; st32(st, avail); for (int k = 4; k < 0x30; k += 4) st32(st + k, 0xFFFFFFFFu); st32(st + 0x1c, avail >> 12); return 0; }
    case 0x0501: { u32 sz = ((r->ebx & 0xFFFF) << 16) | (r->ecx & 0xFFFF); u32 p = sz ? halloc(sz) : 0; rt_dbg_a = sz; rt_dbg_b = heap_ptr; rt_dbg_n5++; plat_log("rt: dpmi alloc %x -> %x\n", sz, p);
        if (!p) { set_cf(1, r, 0x8012); return 1; }
        static u32 hn; r->ebx = (r->ebx & 0xFFFF0000u) | (p >> 16); r->ecx = (r->ecx & 0xFFFF0000u) | (p & 0xFFFF); r->esi = (r->esi & 0xFFFF0000u) | 0x4001; r->edi = (r->edi & 0xFFFF0000u) | (0x59c7 + 0x20 * hn++); return 0; }
    case 0x0502: return 0;
    case 0x0600: case 0x0601: case 0x0602: case 0x0603: return 0;
    case 0x0900: r->eax = (r->eax & ~0xFF) | (CTX->iflag ? 1 : 0); CTX->iflag = 0; return 0;
    case 0x0901: r->eax = (r->eax & ~0xFF) | (CTX->iflag ? 1 : 0); CTX->iflag = 1; return 0;
    case 0x0902: r->eax = (r->eax & ~0xFF) | (CTX->iflag ? 1 : 0); return 0;
    case 0x0400: r->eax = 0x0090; r->ebx = 7; r->ecx = 0; r->edx = 0; return 0;
    }
    plat_log("rt: int31 ax=%04x unhandled (ebx=%x ecx=%x edx=%x)\n", ax, r->ebx, r->ecx, r->edx);
    set_cf(1, r, 0x8001); return 1;
}

extern void vga_set_mode13(void);
static int int10(struct regs *r) {
    u8 ah = (r->eax >> 8) & 0xFF;
    if (ah == 0x00) { if ((r->eax & 0xFF) == 0x13) vga_set_mode13(); return 0; }
    if (ah == 0x0F) { r->eax = (r->eax & ~0xFFFF) | 0x5003 | (0 << 8); r->ebx &= ~0xFF00u; return 0; }
    if (ah == 0x1A) { r->eax = (r->eax & ~0xFFFF) | 0x1A; r->ebx = (r->ebx & ~0xFFFF) | 8; return 0; }
    if (ah == 0x12 || ah == 0x11 || ah == 0x10 || ah == 0x01 || ah == 0x02 || ah == 0x05) return 0;
    plat_log("rt: int10 ax=%04x unhandled\n", r->eax & 0xFFFF); return 0;
}
#ifdef HOSTED
extern long rt_replay;
static struct { u32 axin, ax, bx, cx, dx; } m33[20000]; static int nm33, pm33, m33_loaded;
#endif
static int int33(struct regs *r) {
    u32 ax = r->eax & 0xFFFF;
#ifdef HOSTED
    if (rt_replay) {            /* replay the reference emulator's mouse driver results, in call order */
        if (!m33_loaded) { m33_loaded = 1; FILE *f = fopen("int33.log", "r"); if (f) { long i; unsigned a, b, c, d, e, g, h, k;
            while (nm33 < 20000 && fscanf(f, "%ld %x %x %x %x %x %x %x %x", &i, &a, &b, &c, &d, &e, &g, &h, &k) == 9) { if (i == 0) continue; m33[nm33].axin = a; m33[nm33].ax = e; m33[nm33].bx = g; m33[nm33].cx = h; m33[nm33].dx = k; nm33++; } fclose(f); } }
        if (pm33 < nm33) {
            if (m33[pm33].axin != ax) plat_log("replay: int33 call %d ax=%04x, reference %04x\n", pm33, ax, m33[pm33].axin);
            r->eax = (r->eax & ~0xFFFFu) | m33[pm33].ax; r->ebx = (r->ebx & ~0xFFFFu) | m33[pm33].bx; r->ecx = (r->ecx & ~0xFFFFu) | m33[pm33].cx; r->edx = (r->edx & ~0xFFFFu) | m33[pm33].dx;
            pm33++; return 0;
        }
    }
#endif
    switch (ax) {
    case 0: r->eax = 0xFFFF; r->ebx = 2; return 0;
    case 4: { mouse_x = r->ecx & 0xFFFF; mouse_y = r->edx & 0xFFFF; if (mouse_x > 639) mouse_x = 639; if (mouse_y > 199) mouse_y = 199; return 0; }
    case 1: case 2: case 7: case 8: case 0x0A: case 0x0F: case 0x1D: return 0;
    case 3: r->ebx = mouse_b; r->ecx = mouse_x; r->edx = mouse_y; return 0;
    case 0x0B: r->ecx = 0; r->edx = 0; return 0;
    case 0x24: r->ebx = 0x0626; r->ecx = 0x0400; return 0;
    case 0x0C: return 0;
    }
    plat_log("rt: int33 ax=%04x unhandled\n", ax); return 0;
}
static int int1a(struct regs *r) {
    u8 ah = (r->eax >> 8) & 0xFF;
    if (ah == 0) { r->ecx = 0; r->edx = vtime / 55000; r->eax &= ~0xFF; return 0; }
    if (ah == 2) { r->ecx = 0x1200; r->edx = 0; return 0; }
    if (ah == 4) { r->ecx = 0x1994; r->edx = 0x0314; return 0; }
    return 0;
}

u32 rt_dbg_last, rt_dbg_nint, rt_dbg_hist[32];
int rt_c_int(u32 vec, struct regs *r) {
    int c; rt_dbg_last = (vec << 8) | ((r->eax >> 8) & 0xFF); rt_dbg_hist[rt_dbg_nint & 31] = (vec << 16) | (r->eax & 0xFFFF); rt_dbg_nint++;
    switch (vec) {
    case 0x21: c = int21(r); break;
    case 0x31: c = int31(r); break;
    case 0x10: c = int10(r); break;
    case 0x33: c = int33(r); break;
    case 0x1A: c = int1a(r); break;
    default: plat_log("rt: INT %x unhandled\n", vec); c = 0;
    }
    return c;
}

void rt_init(u8 *win, struct ctx *ctx) {
    WIN = win; CTX = ctx; ctx->poll = POLL_N; ctx->iflag = 1;
#ifdef HOSTED
    ctx->trem = 1; ctx->thash = 2166136261u; tstop = 0;
#endif
    ctx->sel[0] = 0x28; ctx->sel[1] = 0x170; ctx->sel[2] = 0x178; ctx->sel[3] = 0x178; ctx->sel[4] = 0; ctx->sel[5] = 0x20;
    vga_init(); hw_init();
    for (int i = 0; i < 5; i++) { files[i].name[0] = 1; }
    CTX->es_base = 0x1920;
    for (int k = 0; k < 256; k++) WIN[0x400 + k] = bda_init[k];
    st16(psp + 0x2c, 0x30); WIN[psp + 0x80] = 0; WIN[psp + 0x81] = 0xD;
    { const char *env = "PATH=Z:\\\0COMSPEC=Z:\\COMMAND.COM\0BLASTER=A220 I7 D1 H5 T6"; u32 p = 0x1880; { const char *q = env; for (int k = 0; k < 3; k++) { while (*q) WIN[p++] = *q++; WIN[p++] = 0; q++; } } WIN[p++] = 0; st16(p, 1); p += 2;
      const char *pn = "C:\\ARCHON.EXE"; for (const char *q = pn; *q; q++) WIN[p++] = *q; WIN[p++] = 0; }
}
/* relative movement from the platform (the driver clamps the logical pointer; INT 33h function 4 repositions it) */
void rt_mouse_move(int dx, int dy, u32 b) { int x = (int)mouse_x + dx, y = (int)mouse_y + dy; if (x < 0) x = 0; if (x > 639) x = 639; if (y < 0) y = 0; if (y > 199) y = 199; mouse_x = (u32)x; mouse_y = (u32)y; mouse_b = b; }
void rt_mouse_set(u32 x, u32 y, u32 b) { mouse_x = x; mouse_y = y; mouse_b = b; }

#ifdef HOSTED
#include <stdio.h>
#include <string.h>
static FILE *ftr;
static FILE *evf; static char evlp[48], evlc[48], evln[48];
static struct { long idx; int vec; int untraced; int ok; int rep; u32 repo; } evc;
static int ut_active; static u32 ut_addr; static long ut_trn; static int replay_loaded;
static int rep_pending; static u32 rep_addr, rep_resume;
static unsigned blkn; static FILE *fblk; static int hashlog; static u64 stepn = 2048;
/* one instruction joins the rolling hash; at every checkpoint the hash, the VGA memory and the palette are recorded */
static void acct(u64 idx, u32 v) {
    u32 h = (CTX->thash ^ v) * 16777619u;
    if (blkn && ((idx + 1) % blkn) == 0) {
        unsigned long long ix = idx + 1; u32 vh = 2166136261u, dh = 2166136261u;
        for (u32 i = 0; i < 262144; i++) vh = (vh ^ vga_mem[i]) * 16777619u;
        for (u32 i = 0; i < 768; i++) dh = (dh ^ vga_dac[i]) * 16777619u;
        fwrite(&ix, 8, 1, fblk); fwrite(&h, 4, 1, fblk); fwrite(&vh, 4, 1, fblk); fwrite(&dh, 4, 1, fblk);
        h = 2166136261u;
    }
    CTX->thash = h;
    if (hashlog && ((idx + 1) % stepn) == 0) plat_log("HASH %llu %08x\n", (unsigned long long)(idx + 1), h);
}
static void ev_next(void) {                        /* streaming: next hardware event; 'untraced' = it sits between two software-interrupt records at the same index */
    evc.ok = 0;
    while (evlc[0]) {
        long i; int v, t; unsigned ro = 0;
        if (evlc[0] != '#' && sscanf(evlc, "%ld %d %d R%x", &i, &v, &t, &ro) >= 3) {
            long a1 = -1, a2 = -1; sscanf(evlp, "# sw %ld", &a1); sscanf(evln, "# sw %ld", &a2);
            evc.idx = i; evc.vec = v; evc.untraced = (a1 == i && a2 == i); evc.ok = 1;
            evc.rep = strchr(evlc, 'R') != 0; evc.repo = ro;           /* the interrupt arrived in the middle of a rep string instruction (its address) */
        }
        memcpy(evlp, evlc, sizeof evlp); memcpy(evlc, evln, sizeof evlc);
        if (!fgets(evln, sizeof evln, evf)) evln[0] = 0;
        if (evc.ok) return;
    }
}
extern void hw_replay_time(u32 us);
u32 rt_c_trace(u32 addr, struct regs *r) {
    u64 trn = tstop;                                  /* a slow stop is entered with trem == 0: this instruction's index is tstop */
    static long long lockn = -1; static int noeip = -1; static int dbgmode; static long long ewlo = -1, ewhi; static FILE *few;
    if (!replay_loaded) {
        replay_loaded = 1; evf = fopen("irq_sched.txt", "r");
        if (evf) { evlp[0] = 0; if (!fgets(evlc, sizeof evlc, evf)) evlc[0] = 0; if (!fgets(evln, sizeof evln, evf)) evln[0] = 0; ev_next(); rt_replay = 1; }
        ftr = fopen("iteip_ours.bin", "wb"); if (rt_replay) hw_use_exact();
        { const char *e = getenv("LOCKN"); lockn = e ? atoll(e) : 30000000; }
        noeip = getenv("NOEIP") ? 1 : 0; { const char *bb = getenv("BLKN"); blkn = bb ? (unsigned)atoi(bb) : 0; if (blkn) fblk = fopen("blocks_ours.bin", "wb"); }
        { const char *e = getenv("EIPWIN"); if (e) sscanf(e, "%lld:%lld", &ewlo, &ewhi); }
        hashlog = getenv("HASHLOG") != 0; { const char *e = getenv("STEP"); if (e) stepn = (u64)atoll(e); if (!stepn) stepn = 2048; }
        dbgmode = getenv("WATCHPC") || getenv("REGTRACE") || getenv("DUMPIDX");
    }
    if (ut_active && addr == ut_addr) { ut_active = 0; trn = ut_trn; tstop = trn; }
    if (rep_pending && addr == rep_resume) { rep_pending = 0; acct(trn, rep_addr - XL_BASE); trn++; tstop = trn; }      /* the interrupted rep instruction is traced again when it resumes */       /* an untraced (DOSBox-internal) handler run has returned: its instructions are not counted */
    while (evc.ok && (u64)evc.idx == trn) {
        int vec = evc.vec, unt = evc.untraced, erep = evc.rep; u32 erepo = evc.repo; ev_next();
        if (vec == 8 && !vec_pm[8]) { st32(0x46C, ld32(0x46C) + 1); continue; }
        if (vec_pm[vec]) {
            if (vec == 0x70) { extern void hw_rtc_flag(void); hw_rtc_flag(); }
            u32 off = vec_pm[vec] - XL_BASE; u32 t = xl_tab[off];
            if (t) {
                if (unt && !ut_active) { ut_active = 1; ut_addr = addr; ut_trn = trn; }
                if (erep && !unt) { rep_pending = 1; rep_addr = erepo + XL_BASE; rep_resume = addr; }
                u32 ef = rt_mk_eflags(CTX->ccr_stub); CTX->tmp[0] = (ef << 24) | ((ef & 0xFF00) << 8) | ((ef >> 8) & 0xFF00) | (ef >> 24);
                tstop = trn; CTX->trem = 1;                  /* the handler's first instruction is index trn again: stop there too */
                return t;
            }
        }
    }
    { static long long dumpidx = -2; if (dumpidx == -2) { const char *e = getenv("DUMPIDX"); dumpidx = e ? atoll(e) : -1; }
      if (dumpidx >= 0 && (long long)trn == dumpidx) { FILE *f = fopen("obj3_idx.bin", "wb"); fwrite(WIN + OBJ3_BASE, 1, 0x33dd0, f); fclose(f); f = fopen("vga_idx.bin", "wb"); fwrite(vga_mem, 1, 262144, f); fwrite(vga_dac, 1, 768, f); fclose(f); { u32 st, pt; int c4; extern void vga_get_display(u32 *, u32 *, int *); vga_get_display(&st, &pt, &c4); f = fopen("vga_disp.txt", "w"); fprintf(f, "%u %u %d\n", st, pt, c4); fclose(f); } f = fopen("heap_idx.bin", "wb"); fwrite(WIN + 0x1e0000, 1, 0x700000, f); fclose(f); f = fopen("low_idx.bin", "wb"); fwrite(WIN, 1, 0xA0000, f); fclose(f); plat_exit(0); } }
    { static u32 wp[8]; static int nwp = -1; static int hits[8];
      if (nwp < 0) { nwp = 0; const char *e = getenv("WATCHPC"); while (e && *e && nwp < 8) { wp[nwp++] = strtoul(e, (char **)&e, 16); if (*e == ',') e++; } }
      for (int k = 0; k < nwp; k++) if (wp[k] == addr && hits[k]++ < 400) plat_log("WATCH %x idx=%lld eax=%08x ecx=%08x edx=%08x ebx=%08x ebp=%08x esi=%08x edi=%08x\n", addr, (long long)trn, r->eax, r->ecx, r->edx, r->ebx, r->ebp, r->esi, r->edi); }
    { static long long regn = -2; static FILE *fr; if (regn == -2) { const char *e = getenv("REGTRACE"); regn = e ? atoll(e) : -1; if (regn > 0) fr = fopen("itrace_ours.log", "w"); }
      if ((long long)trn < regn) fprintf(fr, "%08x %08x %08x %08x %08x %08x %08x %08x\n", addr, r->eax, r->ecx, r->edx, r->ebx, r->ebp, r->esi, r->edi); else if (fr && (long long)trn == regn) { fclose(fr); fr = 0; } }
    u32 v = addr - XL_BASE;
    if ((long long)trn < lockn && !noeip) fwrite(&v, 4, 1, ftr);
    if (ewlo >= 0 && (long long)trn + 1 > ewlo && (long long)trn + 1 <= ewhi) { if (!few) few = fopen("eipwin_ours.bin", "wb"); fwrite(&v, 4, 1, few); if ((long long)trn + 1 == ewhi) fflush(few); }
    if (!ut_active) acct(trn, v);                      /* the instruction being executed joins the rolling hash (the fast path does this for all the others) */
    u64 T = trn + 1;                                   /* index of the next instruction */
    rt_cycles = (double)T + tports; hw_set_exact(rt_cycles / 30.0);
    hw_advance((u32)(T / 30));
    if ((long long)T == lockn) { fflush(ftr); if (fblk) fflush(fblk); plat_exit(0); }
    /* next stop: an event, a checkpoint, the end, or soon (device time must advance); every instruction while debugging, writing the EIP file or inside an untraced handler */
    u64 S = ((T + stepn) / stepn) * stepn - 1;            /* stops where (index + 1) is a multiple of the step (2048 by default) */
    if (ut_active || rep_pending || dbgmode || !noeip) S = T;
    if (ewlo >= 0 && (long long)T <= ewhi) { if ((long long)T + 1 > ewlo) S = T; else if ((u64)ewlo - 1 >= T && (u64)ewlo - 1 < S) S = (u64)ewlo - 1; }
    if (evc.ok && (u64)evc.idx < S && (u64)evc.idx >= T) S = (u64)evc.idx;
    if (blkn) { u64 nb = ((T + 1 + blkn - 1) / blkn) * blkn - 1; if (nb >= T && nb < S) S = nb; }
    if (lockn > 0 && (u64)lockn - 1 >= T && (u64)lockn - 1 < S) S = (u64)lockn - 1;
    tstop = S; CTX->trem = (s32)(S - trn);
    return 0;
}

#endif
