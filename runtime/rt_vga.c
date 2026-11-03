/* VGA model: unchained ("mode X") planar memory with latches and write modes, CRTC/sequencer/GC/DAC registers.
   Memory layout is DOSBox-style interleaved: plane p, offset o -> vga_mem[o*4+p] (so chain-4 linear byte X == vga_mem[X]). */
#include "rt.h"

u8 vga_mem[262144];
u8 vga_dac[768];
static u8 seq[8], crtc[32], gc[16], attr[32];
static u8 seq_idx, crtc_idx, gc_idx, attr_idx, attr_flip, dac_wr_idx, dac_rd_idx, dac_wr_phase, dac_rd_phase, dac_mask = 0xFF, misc = 0x63;
static u32 latch;
int vga_mode13 = 0;          /* set by int 10h set mode */
u32 vga_frame = 0;

#define MAPMASK (seq[2] & 15)
#define CHAIN4 (seq[4] & 8)
#define WMODE (gc[5] & 3)
#define RMODE ((gc[5] >> 3) & 1)

int vga_in_window(u32 a) { return a >= 0xA0000 && a < 0xB0000; }

void vga_init(void) {
    for (int i = 0; i < 256; i++) { vga_dac[i * 3] = i & 63; vga_dac[i * 3 + 1] = i & 63; vga_dac[i * 3 + 2] = i & 63; }
    seq[2] = 0xF; seq[4] = 0xE; gc[8] = 0xFF; gc[5] = 0x10;
}

void vga_set_mode13(void) {
    for (u32 i = 0; i < sizeof vga_mem; i++) vga_mem[i] = 0;
    seq[2] = 0xF; seq[4] = 0xE; gc[5] = 0x40; gc[8] = 0xFF; gc[6] = 5; gc[3] = 0; gc[1] = 0; gc[0] = 0;
    crtc[0x13] = 0x28; crtc[0x14] = 0x40; crtc[0x17] = 0xA3; crtc[0x0C] = crtc[0x0D] = 0; crtc[9] = 0x41;
    vga_mode13 = 1;
}

static inline u32 expand(u8 v) { return v * 0x01010101u; }

static u32 logic_op(u32 val) {
    switch ((gc[3] >> 3) & 3) { case 1: return val & latch; case 2: return val | latch; case 3: return val ^ latch; default: return val; }
}

static void write_planar(u32 off, u8 val) {
    u32 pm = expand(0), m = MAPMASK; u32 mm = 0;
    for (int p = 0; p < 4; p++) if (m & (1 << p)) mm |= 0xFFu << (8 * p);
    u32 v, bm = expand(gc[8]);
    switch (WMODE) {
    case 0: {
        u32 r = gc[3] & 7; u8 rv = r ? ((val >> r) | (val << (8 - r))) : val;
        u32 sr = 0; u32 esr = gc[1] & 15;
        u32 fullsr = 0, fullesr = 0;
        for (int p = 0; p < 4; p++) { if (gc[0] & (1 << p)) fullsr |= 0xFFu << (8 * p); if (esr & (1 << p)) fullesr |= 0xFFu << (8 * p); }
        v = (expand(rv) & ~fullesr) | (fullsr & fullesr);
        v = logic_op(v); v = (v & bm) | (latch & ~bm); (void)sr; (void)pm; break; }
    case 1: v = latch; break;
    case 2: { u32 f = 0; for (int p = 0; p < 4; p++) if (val & (1 << p)) f |= 0xFFu << (8 * p); v = logic_op(f); v = (v & bm) | (latch & ~bm); break; }
    default: {
        u32 r = gc[3] & 7; u8 rv = r ? ((val >> r) | (val << (8 - r))) : val; u32 b = rv & gc[8]; u32 fullsr = 0;
        for (int p = 0; p < 4; p++) if (gc[0] & (1 << p)) fullsr |= 0xFFu << (8 * p);
        u32 bmm = expand(b); v = logic_op(fullsr); v = (v & bmm) | (latch & ~bmm); break; }
    }
    u32 *dst = (u32 *)(void *)&vga_mem[(off & 0xFFFF) * 4];
    u32 old = vga_mem[(off & 0xFFFF) * 4] | (vga_mem[(off & 0xFFFF) * 4 + 1] << 8) | (vga_mem[(off & 0xFFFF) * 4 + 2] << 16) | ((u32)vga_mem[(off & 0xFFFF) * 4 + 3] << 24);
    u32 nw = (old & ~mm) | (v & mm); (void)dst;
    u8 *q = &vga_mem[(off & 0xFFFF) * 4]; q[0] = nw; q[1] = nw >> 8; q[2] = nw >> 16; q[3] = nw >> 24;
}

static u8 read_planar(u32 off) {
    u8 *q = &vga_mem[(off & 0xFFFF) * 4];
    latch = q[0] | (q[1] << 8) | (q[2] << 16) | ((u32)q[3] << 24);
    if (RMODE == 0) return q[gc[4] & 3];
    u8 cc = gc[2] & 15, dc = gc[7] & 15, r = 0;
    for (int b = 0; b < 8; b++) {
        int ok = 1;
        for (int p = 0; p < 4; p++) if (dc & (1 << p)) { int bit = (q[p] >> b) & 1; if (bit != ((cc >> p) & 1)) ok = 0; }
        if (ok) r |= 1 << b;
    }
    return r;
}

static u32 base_off(u32 a) {
    u32 m = (gc[6] >> 2) & 3; u32 o = a - 0xA0000;
    if (m == 1) return o; if (m == 2) return o - 0x10000; if (m == 3) return o - 0x18000; return o;
}

static u8 rd8(u32 a) {
    u32 o = base_off(a);
    if (CHAIN4) { return vga_mem[o & 0x3FFFF]; }
    return read_planar(o);
}
static void wr8(u32 a, u8 v) {
    u32 o = base_off(a);
    if (CHAIN4) { vga_mem[o & 0x3FFFF] = v; return; }
    write_planar(o, v);
}
u32 vga_read(u32 a, int size) {
    u32 r = 0; for (int i = 0; i < size; i++) r |= (u32)rd8(a + i) << (8 * i); return r;
}
void vga_write(u32 a, u32 v, int size) {
    for (int i = 0; i < size; i++) wr8(a + i, v >> (8 * i));
}

/* ---- ports */
extern u32 vga_vretrace_state(void);
int vga_port_out(u32 port, u32 v, int size) {
    if (size > 1) { vga_port_out(port, v & 0xFF, 1); vga_port_out(port + 1, (v >> 8) & 0xFF, 1); return 1; }
    switch (port) {
    case 0x3C2: misc = v; return 1;
    case 0x3C4: seq_idx = v & 7; return 1;
    case 0x3C5: seq[seq_idx] = v; return 1;
    case 0x3C6: dac_mask = v; return 1;
    case 0x3C7: dac_rd_idx = v; dac_rd_phase = 0; return 1;
    case 0x3C8: dac_wr_idx = v; dac_wr_phase = 0; return 1;
    case 0x3C9: vga_dac[dac_wr_idx * 3 + dac_wr_phase] = v & 63; if (++dac_wr_phase == 3) { dac_wr_phase = 0; dac_wr_idx++; } return 1;
    case 0x3CE: gc_idx = v & 15; return 1;
    case 0x3CF: gc[gc_idx] = v; return 1;
    case 0x3D4: case 0x3B4: crtc_idx = v & 31; return 1;
    case 0x3D5: case 0x3B5: crtc[crtc_idx] = v; return 1;
    case 0x3C0: if (!attr_flip) attr_idx = v & 31; else attr[attr_idx] = v; attr_flip ^= 1; return 1;
    case 0x3C1: return 1;
    case 0x3CC: return 1;
    }
    return 0;
}
u32 vga_port_in(u32 port, int size, int *h) {
    *h = 1;
    switch (port) {
    case 0x3C4: return seq_idx;
    case 0x3C5: return seq[seq_idx];
    case 0x3C7: return 0;
    case 0x3C8: return dac_wr_idx;
    case 0x3C9: { u8 r = vga_dac[dac_rd_idx * 3 + dac_rd_phase]; if (++dac_rd_phase == 3) { dac_rd_phase = 0; dac_rd_idx++; } return r; }
    case 0x3CE: return gc_idx;
    case 0x3CF: return gc[gc_idx];
    case 0x3D4: return crtc_idx;
    case 0x3D5: return crtc[crtc_idx];
    case 0x3C0: return attr_idx;
    case 0x3C1: return attr[attr_idx];
    case 0x3CC: return misc;
    case 0x3DA: case 0x3BA: attr_flip = 0; return vga_vretrace_state();
    }
    *h = 0; return 0xFF;
}

/* display description for dumps / the Amiga renderer */
void vga_get_display(u32 *start, u32 *pitch, int *chain4) {
    *start = ((crtc[0x0C] << 8) | crtc[0x0D]) * 4;
    *pitch = crtc[0x13] * 8;
    *chain4 = CHAIN4 != 0;
}
u32 vga_pan(void) { return attr[0x13]; }
