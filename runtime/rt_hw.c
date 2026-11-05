/* PC hardware models: PIT, PIC, CMOS RTC, keyboard controller, DMA, Sound Blaster 16 DSP, VGA retrace timing.
   All timing is driven by hw_advance(now_us): virtual microseconds in the test harness, real time on the Amiga. */
#include "rt.h"

static u32 now;                  /* us */
#ifdef HOSTED
#define HOSTED_START_D p->start_d = now_d;
static double now_d;             /* exact time in us (replay builds) */
void hw_set_exact(double us) { now_d = us; }
static int exact_mode;
void hw_use_exact(void) { exact_mode = 1; }
#else
#define HOSTED_START_D
#endif
/* ---- PIC */
static u8 irr[2], isr[2], imr[2] = {0xB8, 0xFF}, vbase[2] = {8, 0x70};
static u8 icw_state[2];
static void irq_raise(int irq) { irr[irq >> 3] |= 1 << (irq & 7); }
static int pic_next(void) {            /* returns vector or -1 */
    for (int irq = 0; irq < 16; irq++) {
        int c = irq >> 3, b = irq & 7;
        if (c == 1 && (imr[0] & 4)) continue;
        if ((irr[c] & (1 << b)) && !(imr[c] & (1 << b))) {
            /* a higher-priority in-service interrupt blocks */
            int blocked = 0;
            for (int k = 0; k < irq; k++) { int cc = k >> 3, bb = k & 7; if (isr[cc] & (1 << bb)) blocked = 1; }
            if (c == 1 && (isr[1] & ((1 << b) - 1)) ) blocked = 1;
            if (isr[c] & (1 << b)) blocked = 1;
            if (blocked) continue;
            return irq;
        }
    }
    return -1;
}
u32 hw_dbg1(void) { return (irr[0] << 24) | (irr[1] << 16) | (isr[0] << 8) | isr[1]; }
static u32 sb_write_busy;
static u32 rtc_last_us, rtc_period_us;
static u8 cmos_idx, cmos[128];
u32 hw_dbg3(void) { return now - rtc_last_us; }
u32 hw_dbg2(void) { return (imr[0] << 24) | (imr[1] << 16) | (cmos[0x0B] << 8) | cmos[0x0A]; }
int hw_irq_pending(void) { return pic_next() >= 0; }
static u32 irq0_acks;
int hw_irq_ack(void) {
    int irq = pic_next(); if (irq < 0) return -1; if (irq == 0) irq0_acks++;
    int c = irq >> 3, b = irq & 7;
    irr[c] &= ~(1 << b); isr[c] |= 1 << b; if (c == 1) isr[0] |= 4;
    return vbase[c] + b;
}
static void pic_out(int c, u32 port, u8 v) {
    if ((port & 1) == 0) {
        if (v & 0x10) { icw_state[c] = 1; imr[c] = 0; }                 /* ICW1 */
        else if (v & 0x08) { }                                           /* OCW3 */
        else {                                                           /* OCW2: EOI */
            if ((v & 0xE0) == 0x20) {                                    /* non-specific */
                for (int b = 0; b < 8; b++) if (isr[c] & (1 << b)) { isr[c] &= ~(1 << b); break; }
                if (c == 1 && !isr[1]) isr[0] &= ~4;
            } else if ((v & 0xE0) == 0x60) { isr[c] &= ~(1 << (v & 7)); }
        }
    } else {
        if (icw_state[c] == 1) { vbase[c] = v & 0xF8; icw_state[c] = 2; }
        else if (icw_state[c] == 2) { icw_state[c] = 3; }
        else if (icw_state[c] == 3) { icw_state[c] = 0; }
        else imr[c] = v;
    }
}

/* ---- PIT */
#ifdef HOSTED
#define LIVE_US() now
#else
#define LIVE_US() plat_time_us()      /* the game phase-locks its timer to the retrace by reading the counter right after the retrace bit changes: both must come from the same live clock */
#endif
struct pit {
#ifdef HOSTED
 double start_d;
#endif
 u32 reload; u32 start_us; u8 mode, rw, latched, lsb_next, wr_state; u16 latch_val, wr_lo; u8 gate; u32 phase_us; };
static struct pit pit[3];
static u8 spk = 0;
#define PIT_HZ 1193182.0
#ifdef HOSTED
static u32 pit_count(int ch) {
    struct pit *p = &pit[ch]; u32 rl = p->reload ? p->reload : 65536;
    double ticks = (exact_mode ? (now_d - (double)p->start_d) : (double)(now - p->start_us)) * (PIT_HZ / 1e6);
    if (p->mode == 0 || p->mode == 4) {              /* DOSBox: counter keeps counting after terminal count, truncating */
        if (ticks > (double)rl) { double t = ticks - (double)rl; t = t - 65536.0 * (double)(long long)(t / 65536.0); return (u32)(unsigned short)(int)(0xffff - t); }
        return (u32)(unsigned short)(int)((double)rl - ticks);
    }
    u32 t = (u32)ticks % rl; u32 v = rl - t;
    return v & 0xFFFF;
}
#else
static u32 pit_count(int ch) {
    struct pit *p = &pit[ch]; u32 rl = p->reload ? p->reload : 65536;
    u64 ticks = ((u64)(u32)(LIVE_US() - p->start_us) * 1193182u + 999999u) / 1000000u;      /* integer version of the same maths */
    if (p->mode == 0 || p->mode == 4) {
        if (ticks > rl) { u32 t = (u32)((ticks - rl) % 65536u); return (0xffffu - t) & 0xFFFF; }
        return (u32)(rl - (u32)ticks) & 0xFFFF;
    }
    return (rl - (u32)(ticks % rl)) & 0xFFFF;
}
#endif
static void pit_out(u32 port, u8 v) {
    if (port == 0x43) {
        int ch = v >> 6;
        if (ch == 3) return;
        if (((v >> 4) & 3) == 0) { pit[ch].latch_val = pit_count(ch); pit[ch].latched = 1; pit[ch].lsb_next = 1; return; }
        pit[ch].rw = (v >> 4) & 3; pit[ch].mode = (v >> 1) & 7; pit[ch].wr_state = 0; return;
    }
    int ch = port - 0x40; struct pit *p = &pit[ch];
    if (p->rw == 1) { p->reload = v; p->start_us = LIVE_US(); HOSTED_START_D }
    else if (p->rw == 2) { p->reload = v << 8; p->start_us = LIVE_US(); HOSTED_START_D }
    else { if (!p->wr_state) { p->wr_lo = v; p->wr_state = 1; } else { p->reload = (v << 8) | p->wr_lo; p->wr_state = 0; p->start_us = LIVE_US(); HOSTED_START_D if (ch == 0) p->phase_us = LIVE_US(); } }
}
static u32 pit_in(u32 port) {
    int ch = port - 0x40; struct pit *p = &pit[ch];
    u16 v = p->latched ? p->latch_val : pit_count(ch);
    if (p->rw == 1) { p->latched = 0; return v & 0xFF; }
    if (p->rw == 2) { p->latched = 0; return v >> 8; }
    if (p->lsb_next || !p->latched) { u32 r = (p->lsb_next ? v & 0xFF : v & 0xFF); p->lsb_next = 0; if (!p->latched) { p->lsb_next = 2; } return r; }
    p->latched = 0; return v >> 8;
}
static void pit_advance(void) {
    struct pit *p = &pit[0]; u32 rl = p->reload ? p->reload : 65536;
    if (!p->start_us && !p->reload) return;
    u32 period = (u32)(((u64)rl * 1000000u) / 1193182u); if (!period) period = 1;
    if (now - p->phase_us >= period) {
        irq_raise(0);
        p->phase_us += period;
        if (now - p->phase_us >= 4 * period) p->phase_us = now;      /* running slow: do not queue a storm */
    }
}

/* ---- CMOS RTC */

static void rtc_advance(void) {
    if (!(cmos[0x0B] & 0x40)) { rtc_last_us = now; return; }
    u32 rate = cmos[0x0A] & 15; if (rate < 3) rate = 3;
    rtc_period_us = (1000000u << 0) / (32768u >> (rate - 1));
    if (now - rtc_last_us >= rtc_period_us) { rtc_last_us += rtc_period_us; cmos[0x0C] |= 0xC0; irq_raise(8); }
}
static u32 bcd(u32 v) { return ((v / 10) << 4) | (v % 10); }

/* ---- keyboard controller */
static u8 kb_queue[32]; static int kb_n;
void hw_key(u8 scancode) { if (kb_n < 32) { kb_queue[kb_n++] = scancode; irq_raise(1); } }
static u8 kb_status(void) { return kb_n ? 1 : 0; }
static u8 kb_data(void) { u8 v = kb_n ? kb_queue[0] : 0; if (kb_n) { for (int i = 1; i < kb_n; i++) kb_queue[i - 1] = kb_queue[i]; kb_n--; if (kb_n) irq_raise(1); } return v; }

/* ---- DMA (8-bit channel 1 and 16-bit channel 5 address/count/page) */
struct dmach { u32 addr, count; u8 page, ff; };
static struct dmach dma[8];
static u8 dma_ff[2];

/* ---- Sound Blaster 16 */
static struct {
    u8 resetting, ready_data[16]; int nready; u8 cmd; int argn; u8 args[8]; u8 speaker;
    u32 rate; u8 is16, stereo, sign, autoinit, active; u32 blocklen; u32 pos_bytes; u32 dma_buf; u32 dma_len;
    u32 next_irq_us; u32 irq_period_us; u8 mixer_idx; u8 mixer[256]; u8 irq_pending16; u32 irq_line;
} sb;
#define SB_IRQ 7
extern void plat_audio_push(const u8 *data, u32 bytes, u32 rate, int is16, int stereo, int sign);
static void sb_put(u8 v) { if (sb.nready < 16) sb.ready_data[sb.nready++] = v; }
static u8 sb_get(void) { u8 v = sb.nready ? sb.ready_data[0] : 0xFF; if (sb.nready) { for (int i = 1; i < sb.nready; i++) sb.ready_data[i - 1] = sb.ready_data[i]; sb.nready--; } return v; }
static void sb_start(int is16, int autoinit, u8 mode, u32 len) {
    sb.is16 = is16; sb.autoinit = autoinit; sb.stereo = (mode >> 5) & 1; sb.sign = (mode >> 4) & 1;
    sb.blocklen = len + 1; sb.active = 1; sb.pos_bytes = 0;
    int ch = is16 ? 5 : 1;
    if (is16 && dma[5].count == 0 && dma[1].count != 0) ch = 1;          /* the game programs the 8-bit controller (channel 1) for its 16-bit output, as the reference run shows */
    sb.dma_buf = (dma[ch].page << 16) | dma[ch].addr; sb.dma_len = dma[ch].count + 1;
    if (is16 && ch == 5) { sb.dma_buf = ((dma[5].page & 0xFE) << 16) | (dma[5].addr << 1); sb.dma_len = (dma[5].count + 1) * 2; }
    u32 samples = sb.blocklen;            /* for 16-bit commands length is in samples (words) */
    u32 frames = sb.stereo ? samples / 2 : samples; if (!frames) frames = 1;
    sb.irq_period_us = (u32)((u64)frames * 1000000u / (sb.rate ? sb.rate : 11025));
    sb.next_irq_us = now + sb.irq_period_us;
    plat_log("hw: sb start is16=%d auto=%d stereo=%d rate=%u blocklen=%u dma=%x len=%u period=%u now=%u\n", is16, autoinit, sb.stereo, sb.rate, sb.blocklen, sb.dma_buf, sb.dma_len, sb.irq_period_us, now);
}
static void sb_cmd_done(void) {
    u8 c = sb.cmd; u8 *a = sb.args;
    switch (c) {
    case 0x40: sb.rate = 1000000 / (256 - a[0]); break;
    case 0x41: case 0x42: sb.rate = (a[0] << 8) | a[1]; break;
    case 0x14: sb_start(0, 0, 0, (a[1] << 8) | a[0]); break;
    case 0x10: break;
    case 0xE0: sb_put(~a[0]); break;
    case 0x48: break;
    default:
        if ((c & 0xF0) == 0xB0) sb_start(1, (c & 4) != 0, a[0], (a[2] << 8) | a[1]);
        else if ((c & 0xF0) == 0xC0) sb_start(0, (c & 4) != 0, a[0], (a[2] << 8) | a[1]);
    }
}
static int sb_cmd_args(u8 c) {
    if (c == 0x40 || c == 0xE0 || c == 0x10) return 1;
    if (c == 0x41 || c == 0x42 || c == 0x14 || c == 0x48) return 2;
    if ((c & 0xF0) == 0xB0 || (c & 0xF0) == 0xC0) return 3;
    return 0;
}
static void sb_dsp_write(u8 v) {
    { static int n; if (n++ < 40) plat_log("hw: dsp write %02x\n", v); }
    if (sb.argn < 0) { sb.cmd = v; sb.argn = 0; if (sb_cmd_args(v) == 0) {
        switch (v) {
        case 0xE1: sb_put(4); sb_put(5); break;
        case 0xD0: case 0xD5: sb.active = 0; break;
        case 0xD4: case 0xD6: sb.active = 1; break;
        case 0xD1: sb.speaker = 1; break; case 0xD3: sb.speaker = 0; break;
        case 0xDA: case 0xD9: sb.autoinit = 0; break;
        case 0xF2: case 0xF3: irq_raise(SB_IRQ); break;
        case 0xE4: break;
        }
        sb.argn = -1; } return; }
    sb.args[sb.argn++] = v;
    if (sb.argn >= sb_cmd_args(sb.cmd)) { sb_cmd_done(); sb.argn = -1; }
}
static void sb_advance(void) {
    if (!sb.active) return;
    while (now >= sb.next_irq_us) {
        /* push this block's audio to the platform */
        u32 bytes = sb.blocklen * (sb.is16 ? 2 : 1);
        u32 off = sb.pos_bytes % (sb.dma_len ? sb.dma_len : 1);
        { static int np; if (np++ < 5) { const u8 *q = WIN + sb.dma_buf + off; plat_log("hw: sb push off=%u bytes=%u dma_len=%u now=%u buf=%x data=%02x%02x%02x%02x %02x%02x%02x%02x\n", off, bytes, sb.dma_len, now, sb.dma_buf, q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7]); } }
        if (off + bytes <= sb.dma_len) plat_audio_push(WIN + sb.dma_buf + off, bytes, sb.rate, sb.is16, sb.stereo, sb.sign);
        sb.pos_bytes += bytes;
        irq_raise(SB_IRQ); sb.mixer[0x82] |= sb.is16 ? 2 : 1;
        sb.next_irq_us += sb.irq_period_us;
        if (!sb.autoinit) { sb.active = 0; break; }
    }
}

/* ---- VGA retrace */
u32 vga_vretrace_state(void) {
    /* DOSBox's model for the 320x200 unchained mode (449 lines in 14268 us): vsync = lines 412..414 (bit 3), vertical blank = from line 400 (bit 0),
       horizontal blank = the last part of every line (bit 0).  The game phase-locks its timer to these edges, so the live clock is used. */
    const u32 frame_us = 14268, lines = 449;
#ifndef HOSTED
    u32 t = plat_time_us() % frame_us;
#else
    u32 t = now % frame_us;
#endif
    u32 lt = t * lines;                                    /* position in units of 1/449 us-lines: line = lt / frame_us */
    u32 r = 0;
    if (lt >= 412 * frame_us && lt <= 414 * frame_us) r |= 8;
    if (lt >= 400 * frame_us) r |= 1;
    else if ((lt % frame_us) >= (frame_us * 13) / 16) r |= 1;
    return r;
}

/* ---- dispatch of ports */
int hw_port_out(u32 port, u32 v, int size) {
    if (size > 1) { hw_port_out(port, v & 0xFF, 1); hw_port_out(port + 1, (v >> 8) & 0xFF, 1); return 1; }
    if (port == 0x20 || port == 0x21) { pic_out(0, port, v); if (port == 0x21) plat_log("hw: imr0=%02x\n", v); return 1; }
    if (port == 0xA0 || port == 0xA1) { if (port == 0xA1) plat_log("hw: imr1=%02x\n", v); pic_out(1, port, v); return 1; }
    if ((port >= 0x40 && port <= 0x42) || port == 0x43) { static int n; if (n++ < 12) plat_log("hw: pit out %x,%02x\n", port, v); pit_out(port, v); return 1; }
    if (port == 0x61) { spk = v; return 1; }
    if (port == 0x70) { cmos_idx = v & 0x7F; return 1; }
    if (port == 0x71) { if (cmos_idx != 0x0F) plat_log("hw: cmos[%02x]=%02x\n", cmos_idx, v); cmos[cmos_idx] = v; if (cmos_idx == 0x0B || cmos_idx == 0x0A) rtc_last_us = now; return 1; }
    if (port == 0x60 || port == 0x64) return 1;
    /* DMA */
    if (port >= 0x00 && port <= 0x0F) {
        int ch = (port >> 1) & 3;
        if (port == 0x0C) { dma_ff[0] = 0; return 1; }
        if (port <= 7) { struct dmach *d = &dma[ch]; if ((port & 1) == 0) { if (!dma_ff[0]) d->addr = (d->addr & 0xFF00) | v; else d->addr = (d->addr & 0xFF) | (v << 8); }
            else { if (!dma_ff[0]) d->count = (d->count & 0xFF00) | v; else d->count = (d->count & 0xFF) | (v << 8); } dma_ff[0] ^= 1; return 1; }
        return 1;
    }
    if (port == 0x83) { dma[1].page = v; return 1; }
    if (port == 0x8B) { dma[5].page = v; return 1; }
    if (port >= 0xC0 && port <= 0xDF) {
        if (port == 0xD8) { dma_ff[1] = 0; return 1; }
        int ch = 4 + (((port - 0xC0) >> 2) & 3);
        if (((port - 0xC0) & 2) == 0 || 1) { struct dmach *d = &dma[ch]; int regsel = ((port - 0xC0) >> 1) & 1; if (port >= 0xC0 && port <= 0xCF) {
            if (!regsel) { if (!dma_ff[1]) d->addr = (d->addr & 0xFF00) | v; else d->addr = (d->addr & 0xFF) | (v << 8); } else { if (!dma_ff[1]) d->count = (d->count & 0xFF00) | v; else d->count = (d->count & 0xFF) | (v << 8); }
            dma_ff[1] ^= 1; return 1; } }
        return 1;
    }
    /* Sound Blaster 0x220 */
    if (port >= 0x220 && port <= 0x22F) {
        switch (port & 0xF) {
        case 0x6: if (v & 1) { if (!sb.resetting) sb_write_busy = 0; } if (v == 0) { if (sb.resetting) { sb.nready = 0; sb_put(0xAA); sb.active = 0; sb.argn = -1; } sb.resetting = 0; } else sb.resetting = 1; return 1;
        case 0xC: sb_dsp_write(v); return 1;
        case 0x4: sb.mixer_idx = v; return 1;
        case 0x5: sb.mixer[sb.mixer_idx] = v; return 1;
        }
        return 1;
    }
    if (port >= 0x388 && port <= 0x38B) return 1;
    return 0;
}
u32 hw_port_in(u32 port, int size, int *h) {
    *h = 1;
    if (size > 1) { u32 lo = hw_port_in(port, 1, h); u32 hi = hw_port_in(port + 1, 1, h); return lo | (hi << 8); }
    if (port == 0x20) return isr[0]; if (port == 0xA0) return isr[1];
    if (port == 0x21) return imr[0]; if (port == 0xA1) return imr[1];
    if (port >= 0x40 && port <= 0x42) return pit_in(port);
    if (port == 0x61) return spk | ((now / 15) & 1) << 4;
    if (port == 0x60) return kb_data();
    if (port == 0x64) return kb_status();
    if (port == 0x71) {
        u32 t = now / 1000000;
        switch (cmos_idx) {
        case 0: return bcd(t % 60); case 2: return bcd((t / 60) % 60); case 4: return bcd((t / 3600) % 24);
        case 0x0C: { u8 v = cmos[0x0C]; cmos[0x0C] = 0; return v; }
        case 0x0A: return cmos[0x0A] & 0x7F;
        case 0x0D: return 0x80;
        default: return cmos[cmos_idx];
        }
    }
    if (port >= 0x220 && port <= 0x22F) {
        switch (port & 0xF) {
        case 0xA: return sb_get();
        case 0xE: return sb.nready ? 0x80 : 0x00;
        case 0xC: if (sb.resetting) return 0xFF; sb_write_busy++; return (sb_write_busy & 8) ? 0xFF : 0x7F;       /* DOSBox: alternating 'write buffer busy' (the game polls bit 7 before every DSP write) */
        case 0x5: return sb.mixer[sb.mixer_idx];
        case 0x4: return sb.mixer_idx;
        case 0xF: sb.mixer[0x82] &= ~2; return 0;
        }
        return 0xFF;
    }
    if (port >= 0x388 && port <= 0x38B) return 0;
    if (port == 0x201) return 0xFF;
    *h = 0; return 0xFF;
}
void hw_init(void) {
    pit[0].rw = 3; pit[1].rw = 3; pit[2].rw = 3; sb.argn = -1; sb.rate = 11025;
    cmos[0x0A] = 0x26; cmos[0x0B] = 0x02;
}
static u32 bios_tick_us;
void hw_advance(u32 t) {
    now = t;
    while (now - bios_tick_us >= 54925) { bios_tick_us += 54925; st32(0x46C, ld32(0x46C) + 1); } pit_advance(); rtc_advance(); sb_advance();
}
u32 hw_now(void) { return now; }

void hw_rtc_flag(void) { cmos[0x0C] |= 0xC0; }
u32 hw_dbg4(void) { return (irq0_acks << 16) | (pit[0].reload & 0xFFFF); }
u32 hw_dbg5(void) { return ((u32)pit[0].mode << 24) | ((now - pit[0].phase_us) & 0xFFFFFF); }
