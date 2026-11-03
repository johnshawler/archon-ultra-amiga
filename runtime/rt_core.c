/* Runtime core: INT services (DOS / DPMI / BIOS / mouse), port dispatch, string instructions, polling & IRQ delivery. */
#include "rt.h"
#include "layout.h"
#include "bda.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>

u8 *WIN; struct ctx *CTX; long rt_replay; long rt_trn; double rt_cycles; extern void hw_set_exact(double us); extern void hw_use_exact(void);
extern u32 xl_tab[];
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
#define POLL_N 64
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
    bytes = (bytes + 15) & ~15u;
    if (heap_ptr + bytes > HEAP_END) return 0;
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

void rt_c_unimpl(u32 where) { plat_log("rt: UNIMPLEMENTED instruction at x86 %x (or bad jump)\n", where); plat_exit(3); }
void rt_c_badtarget(u32 off, u32 ret) { plat_log("rt: indirect jump/call to unknown x86 target %x (native return %x)\n", off + XL_BASE, ret); plat_exit(4); }

/* ---- ports */
static int getenv_dbg2(void) { static int v = -1; if (v < 0) v = getenv("PORTLOG") != 0; return v; }
static struct { long idx; u16 port; u8 val; } *prd; static long npr, prp; static int pr_loaded;
static void pr_load(void) {
    pr_loaded = 1; FILE *f = fopen("portreads.txt", "r"); if (!f) return;
    long cap = 600000; prd = malloc(cap * sizeof *prd); char line[64];
    while (fgets(line, sizeof line, f) && npr < cap) { long i; unsigned p, v; if (sscanf(line, "%ld %x %x", &i, &p, &v) == 3) { prd[npr].idx = i; prd[npr].port = p; prd[npr].val = v; npr++; } }
    fclose(f);
}
u32 rt_c_in(u32 port, int size) {
    if (rt_replay) {
        if (!pr_loaded) pr_load();
        while (prp < npr && prd[prp].idx < rt_trn) prp++;
        if (prp < npr && prd[prp].idx == rt_trn && prd[prp].port == port) { u32 v = prd[prp++].val; rt_cycles += 30; hw_set_exact(rt_cycles / 30.0); return v; }
    }
    if (rt_replay) { rt_cycles += 30; hw_set_exact(rt_cycles / 30.0); }
    int h; u32 v = vga_port_in(port, size, &h); if (h) return v;
    v = hw_port_in(port, size, &h);
    if (rt_replay && (port == 0x42 || port == 0x201 || port == 0x40) && getenv_dbg2()) plat_log("PR %ld %x %x\n", rt_trn, port, v & 0xFF);
    if (h) return v;
    { static u8 seen[65536]; if (!seen[port & 0xFFFF]++) plat_log("rt: in %x (size %d) unhandled\n", port, size); }
    return 0xFFFFFFFFu;
}
void rt_c_out(u32 port, u32 val, int size) {
    val &= size == 1 ? 0xFF : size == 2 ? 0xFFFF : 0xFFFFFFFFu;
    if (rt_replay) { rt_cycles += 22; hw_set_exact(rt_cycles / 30.0); }
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
    if (getenv("STRDBG") && rep && r->ecx >= 0x20 && r->ecx < 0x80) plat_log("STR op=%d esi=%x edi=%x ecx=%x df=%d dsb=%x esb=%x src0=%08x\n", op, r->esi, r->edi, r->ecx, CTX->df, CTX->ds_base, CTX->es_base, ld32(CTX->ds_base + r->esi));
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
void rt_c_poll_time(void) {
    static u32 last_frame_t;
    if (rt_replay) return;
    vtime += QUANT_US; hw_advance(vtime);
    if (vtime - last_frame_t >= 14268) { last_frame_t += 14268; vga_frame_count++; plat_vsync(vga_frame_count); }
}
u32 rt_c_poll(struct regs *r) {
    if (rt_replay) { CTX->poll = 1000000; return 0; }
    CTX->poll = POLL_N;
    rt_c_poll_time();
    if (CTX->iflag && hw_irq_pending()) {
        { u32 ef = rt_mk_eflags(CTX->ccr_stub); CTX->tmp[0] = (ef << 24) | ((ef & 0xFF00) << 8) | ((ef >> 8) & 0xFF00) | (ef >> 24); }
        int vec = hw_irq_ack();
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
    case 0x3D: { static int nth; nth++; if (nth == 4) { FILE *f = fopen("obj3_ours.bin", "wb"); fwrite(WIN + OBJ3_BASE, 1, 0x33dd0, f); fclose(f); }
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
    case 0x0500: { u32 st = r->edi + CTX->es_base; u32 avail = ((HEAP_END - heap_ptr - rt_heap_reserve) & ~0xFFFu) + 0x1000; st32(st, avail); for (int k = 4; k < 0x30; k += 4) st32(st + k, 0xFFFFFFFFu); st32(st + 0x1c, avail >> 12); return 0; }
    case 0x0501: { u32 sz = ((r->ebx & 0xFFFF) << 16) | (r->ecx & 0xFFFF); u32 p = sz ? halloc(sz) : 0; plat_log("rt: dpmi alloc %x -> %x\n", sz, p);
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
static int int33(struct regs *r) {
    u32 ax = r->eax & 0xFFFF;
    switch (ax) {
    case 0: r->eax = 0xFFFF; r->ebx = 2; return 0;
    case 1: case 2: case 4: case 7: case 8: case 0x0A: case 0x0F: case 0x1D: return 0;
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

int rt_c_int(u32 vec, struct regs *r) {
    int c;
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
    ctx->sel[0] = 0x28; ctx->sel[1] = 0x170; ctx->sel[2] = 0x178; ctx->sel[3] = 0x178; ctx->sel[4] = 0; ctx->sel[5] = 0x20;
    vga_init(); hw_init();
    for (int i = 0; i < 5; i++) { files[i].name[0] = 1; }
    CTX->es_base = 0x1920;
    for (int k = 0; k < 256; k++) WIN[0x400 + k] = bda_init[k];
    st16(psp + 0x2c, 0x30); WIN[psp + 0x80] = 0; WIN[psp + 0x81] = 0xD;
    { const char *env = "PATH=Z:\\\0COMSPEC=Z:\\COMMAND.COM\0BLASTER=A220 I7 D1 H5 T6"; u32 p = 0x1880; { const char *q = env; for (int k = 0; k < 3; k++) { while (*q) WIN[p++] = *q++; WIN[p++] = 0; q++; } } WIN[p++] = 0; st16(p, 1); p += 2;
      const char *pn = "C:\\ARCHON.EXE"; for (const char *q = pn; *q; q++) WIN[p++] = *q; WIN[p++] = 0; }
}
void rt_mouse_set(u32 x, u32 y, u32 b) { mouse_x = x; mouse_y = y; mouse_b = b; }

#include <stdio.h>
static FILE *ftr;
#define trn rt_trn
static struct { long idx; int vec; } ev[60000]; static int nev, evp; static int replay_loaded;
extern void hw_replay_time(u32 us);
u32 rt_c_trace(u32 addr, struct regs *r) {
    if (!replay_loaded) {
        replay_loaded = 1; FILE *f = fopen("irq_sched.txt", "r");
        if (f) { char line[128]; while (fgets(line, sizeof line, f) && nev < 60000) { long i; int v, t; if (line[0] != '#' && sscanf(line, "%ld %d %d", &i, &v, &t) == 3) { ev[nev].idx = i; ev[nev].vec = v; nev++; } } fclose(f); rt_replay = 1; }
        ftr = fopen("iteip_ours.bin", "wb"); if (rt_replay) hw_use_exact();
    }
    while (evp < nev && ev[evp].idx == trn) {
        int vec = ev[evp++].vec;
        if (vec == 8 && !vec_pm[8]) { st32(0x46C, ld32(0x46C) + 1); continue; }
        if (vec_pm[vec]) {
            if (vec == 0x70) { extern void hw_rtc_flag(void); hw_rtc_flag(); }
            u32 off = vec_pm[vec] - XL_BASE; u32 t = xl_tab[off];
            if (t) { u32 ef = rt_mk_eflags(CTX->ccr_stub); CTX->tmp[0] = (ef << 24) | ((ef & 0xFF00) << 8) | ((ef >> 8) & 0xFF00) | (ef >> 24); return t; }
        }
    }
    { static long dumpidx = -2; if (dumpidx == -2) { const char *e = getenv("DUMPIDX"); dumpidx = e ? atol(e) : -1; }
      if (dumpidx >= 0 && trn == dumpidx) { FILE *f = fopen("obj3_idx.bin", "wb"); fwrite(WIN + OBJ3_BASE, 1, 0x33dd0, f); fclose(f); f = fopen("vga_idx.bin", "wb"); fwrite(vga_mem, 1, 262144, f); fwrite(vga_dac, 1, 768, f); fclose(f); { u32 st, pt; int c4; extern void vga_get_display(u32 *, u32 *, int *); vga_get_display(&st, &pt, &c4); f = fopen("vga_disp.txt", "w"); fprintf(f, "%u %u %d\n", st, pt, c4); fclose(f); } f = fopen("heap_idx.bin", "wb"); fwrite(WIN + 0x1e0000, 1, 0x700000, f); fclose(f); f = fopen("low_idx.bin", "wb"); fwrite(WIN, 1, 0xA0000, f); fclose(f); plat_exit(0); } }
    { static u32 wp[8]; static int nwp = -1; static int hits[8];
      if (nwp < 0) { nwp = 0; const char *e = getenv("WATCHPC"); while (e && *e && nwp < 8) { wp[nwp++] = strtoul(e, (char **)&e, 16); if (*e == ',') e++; } }
      for (int k = 0; k < nwp; k++) if (wp[k] == addr && hits[k]++ < 400) plat_log("WATCH %x idx=%ld eax=%08x ecx=%08x edx=%08x ebx=%08x ebp=%08x esi=%08x edi=%08x\n", addr, trn, r->eax, r->ecx, r->edx, r->ebx, r->ebp, r->esi, r->edi); }
    { static long regn = -2; static FILE *fr; if (regn == -2) { const char *e = getenv("REGTRACE"); regn = e ? atol(e) : -1; if (regn > 0) fr = fopen("itrace_ours.log", "w"); }
      if (trn < regn) fprintf(fr, "%08x %08x %08x %08x %08x %08x %08x %08x\n", addr, r->eax, r->ecx, r->edx, r->ebx, r->ebp, r->esi, r->edi); else if (fr && trn == regn) { fclose(fr); fr = 0; } }
    if (trn < 30000000) { u32 v = addr - XL_BASE; fwrite(&v, 4, 1, ftr); }
    trn++; rt_cycles += 1; hw_set_exact(rt_cycles / 30.0);
    if ((trn & 63) == 0) hw_advance((u32)(trn / 30));
    if (trn == 30000000) { fflush(ftr); plat_exit(0); }
    return 0;
}
