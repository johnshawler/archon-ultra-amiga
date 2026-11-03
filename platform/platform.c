/* platform.c - Archon Ultra on the Amiga 1200 (AGA, 68030, fast RAM): hardware, display, timing, input, IDE data disk, glue to the runtime.
 * No OS, no libc.  Entered from crt0 in supervisor mode.  See docs/DESIGN.md. */
#include "../runtime/rt.h"

#define R16(o) (*(volatile u16 *)(0xDFF000u + (o)))
#define R32(o) (*(volatile u32 *)(0xDFF000u + (o)))
#define CIAA(o) (*(volatile u8 *)(0xBFE001u + (o) * 0x100))
#define CIAB(o) (*(volatile u8 *)(0xBFD000u + (o) * 0x100))
enum { C_PRA = 0, C_PRB = 1, C_DDRA = 2, C_DDRB = 3, C_TALO = 4, C_TAHI = 5, C_TBLO = 6, C_TBHI = 7, C_TOD0 = 8, C_TOD1 = 9, C_TOD2 = 10,
       C_SDR = 12, C_ICR = 13, C_CRA = 14, C_CRB = 15 };

#define DENISEID 0x07C
#define VHPOSR 0x006
#define DMACON 0x096
#define INTENA 0x09A
#define INTREQ 0x09C
#define INTENAR 0x01C
#define INTREQR 0x01E
#define ADKCON 0x09E
#define POTGOR 0x016
#define POTGO 0x034
#define JOY0DAT 0x00A
#define JOY1DAT 0x00C
#define DIWSTRT 0x08E
#define DIWSTOP 0x090
#define DDFSTRT 0x092
#define DDFSTOP 0x094
#define BPLCON0 0x100
#define BPLCON1 0x102
#define BPLCON2 0x104
#define BPLCON3 0x106
#define BPLCON4 0x10C
#define BPL1MOD 0x108
#define BPL2MOD 0x10A
#define COLOR00 0x180
#define FMODE 0x1FC
#define DIWHIGH 0x1E4
#define COP1LC 0x080
#define COPJMP1 0x088
#define SPR0PT 0x120
#define CLXCON 0x098
#define CLXCON2 0x10E
#define SETBIT 0x8000
#define DMA_ALL 0x7FFF
#define DMAEN 0x0200
#define BPLEN 0x0100
#define COPEN 0x0080
#define BLTEN 0x0040
#define INT_PORTS 0x0008
#define INT_VERTB 0x0020
#define INT_EXTER 0x2000
#define INT_INTEN 0x4000
#define AUDx(n, o) (0x0A0 + (n) * 0x10 + (o))
#define SPRx(n, o) (0x140 + (n) * 8 + (o))

#define BPLCON0_8PL 0x0211
#define BPLCON3_BASE 0x0C20
#define BPLCON3_LOCT 0x0200
#define BPLCON4_BASE 0x0011

extern u32 boot_fast, boot_fast_size, boot_chip, boot_chip_size;
extern u8 _end[];
extern void isr_vbl(void), isr_ciab(void), isr_ports(void), isr_aud(void), isr_default(void);
extern void c2p_row320(const u8 *src, u8 *dst);
extern void c2p_row320_dirty(const u8 *src, u8 *dst, u8 *flags, u32 mask);
extern u8 vga_dirty[16384];

/* ------------------------------------------------------------------------------------------- runtime glue */
void plat_log(const char *fmt, ...) { (void)fmt; }
void plat_exit(int code) { (void)code; for (;;) R16(COLOR00) = (u16)(0x0F00 | (code & 15)); }

/* ------------------------------------------------------------------------------------------- chip block */
#define CHIP_COP 0x0000          /* copper list */
#define CHIP_SPR 0x0800          /* null sprite */
#define CHIP_AUD 0x1000          /* four audio rings */
#define CHIP_BUF0 0x10000        /* two 64000-byte display buffers */
#define CHIP_BUF1 0x22000
#define ROW_BYTES 320            /* 8 planes x 40 bytes per line */
#ifdef DEBUGBAR
#define SCR_H 232
#else
#define SCR_H 200
#endif

static u8 *chip;
static u8 *buf[2];
static volatile u8 flip_pending;
static u8 front;                  /* index of the buffer being displayed */
static u16 *cop;                  /* copper list (word array) */
static volatile u32 vbl_count;
static u32 shown_vbl;
static int is_pal;
static u16 vstart;

/* ------------------------------------------------------------------------------------------- exceptions */
static u32 vectors[256];
u32 dbg_n_ports, dbg_n_ciab, dbg_n_aud, dbg_n_def;
void plat_isr_default_c(void) { dbg_n_def++; R16(INTREQ) = 0x7FFF; }
/* crash screen: eight hex digits per line in a one-plane hires picture (vector, SR, PC, fault address / format word, then 8 stack longs) */
static const u8 hexfont[16][7] = { {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,30,1,1,17,14},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},
                                   {14,17,17,14,17,17,14},{14,17,17,15,1,2,12},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16} };
void plat_crash_c(u32 vec, u32 *sp)
{
    u8 *scr = (u8 *)boot_chip + CHIP_BUF0; u32 words[16]; unsigned n = 0;
    words[n++] = vec; words[n++] = sp[0] >> 16; words[n++] = (sp[0] << 16) | (sp[1] >> 16); words[n++] = (sp[1] << 16) | (sp[2] >> 16); words[n++] = sp[2];
    for (unsigned i = 3; i < 11; i++) words[n++] = sp[i];
    for (u32 i = 0; i < 8000; i += 4) *(u32 *)(scr + i) = 0;
    for (unsigned l = 0; l < n; l++) for (unsigned d = 0; d < 8; d++) {
        unsigned nib = (words[l] >> (28 - 4 * d)) & 15;
        for (unsigned y = 0; y < 7; y++) { u8 row = hexfont[nib][y]; for (unsigned x = 0; x < 5; x++) if (row & (16 >> x)) { unsigned px = 8 + d * 8 + x, py = 10 + l * 10 + y; scr[py * 40 + (px >> 3)] |= (u8)(0x80 >> (px & 7)); } }
    }
    R16(INTENA) = DMA_ALL; R16(DMACON) = DMA_ALL; R16(FMODE) = 0;
    cop[32 + 1] = (u16)((u32)scr >> 16); cop[32 + 3] = (u16)((u32)scr);
    for (unsigned p = 1; p < 8; p++) { cop[32 + p * 4 + 1] = 0; cop[32 + p * 4 + 3] = 0; }
    R16(BPLCON0) = 0x1200; R16(BPLCON3) = 0x0C00; R16(BPL1MOD) = 0; R16(BPL2MOD) = 0;
    R16(DDFSTRT) = 0x0038; R16(DDFSTOP) = 0x00D0; R16(DIWSTRT) = 0x2C81; R16(DIWSTOP) = 0x2CC1 | 0x1000; R16(DIWSTOP) = 0xF4C1;
    R16(0x180) = 0x000; R16(0x182) = 0xFFF;
    R32(COP1LC) = (u32)chip + CHIP_COP; R16(COPJMP1) = 0;
    R16(DMACON) = SETBIT | DMAEN | BPLEN | COPEN;
    for (;;) ;
}

static volatile u32 time_hi;       /* CIA-B timer A underflows (each 65536 E-clock cycles) */

void plat_isr_ciab_c(void)
{
    dbg_n_ciab++;
    u8 icr = CIAB(C_ICR);
    if (icr & 0x01) time_hi++;
    R16(INTREQ) = INT_EXTER;
}

/* palette: AGA 24-bit colours written in banks of 32 (high nibbles with LOCT = 0, low nibbles with LOCT = 1) */
static u16 pal_hi[256], pal_lo[256];
static volatile u8 pal_pending;
static u32 pal_gen_done = 0xFFFFFFFFu;
static void load_palette(void)
{
    volatile u16 *col = (volatile u16 *)(0xDFF000u + COLOR00);
    for (unsigned bank = 0; bank < 8; bank++) {
        R16(BPLCON3) = (u16)(BPLCON3_BASE | (bank << 13));
        for (unsigned i = 0; i < 32; i++) col[i] = pal_hi[bank * 32 + i];
        R16(BPLCON3) = (u16)(BPLCON3_BASE | (bank << 13) | BPLCON3_LOCT);
        for (unsigned i = 0; i < 32; i++) col[i] = pal_lo[bank * 32 + i];
    }
    R16(BPLCON3) = BPLCON3_BASE;
}
static void apply_bpl_ptrs(const u8 *b)
{
    for (unsigned p = 0; p < 8; p++) {
        u32 a = (u32)b + p * 40;
        cop[32 + p * 4 + 1] = (u16)(a >> 16); cop[32 + p * 4 + 3] = (u16)a;
    }
}

#ifdef DEBUGBAR
static void dbg_bar_draw(void);
#endif
void plat_isr_vbl_c(void)
{
    vbl_count++;
#ifdef DEBUGBAR
    dbg_bar_draw();
#endif
    if (flip_pending) { front ^= 1; apply_bpl_ptrs(buf[front]); flip_pending = 0; }
    if (pal_pending) { load_palette(); pal_pending = 0; }
    R16(INTREQ) = INT_VERTB;
}

/* ------------------------------------------------------------------------------------------- time */
u32 plat_time_us(void)
{
    u32 h1, h2, lo;
    do { h1 = time_hi; lo = 0xFFFF - (((u32)CIAB(C_TAHI) << 8) | CIAB(C_TALO)); h2 = time_hi; } while (h1 != h2);
    /* E clock: PAL 709379 Hz (1.40969 us/tick), NTSC 715909 Hz (1.39679 us/tick): 65536 ticks = 92386 / 91541 us */
    u32 whole = h1 * (is_pal ? 92386u : 91541u);
    u32 frac = (u32)(((u64)lo * (is_pal ? 92386u : 91541u)) >> 16);
    return whole + frac;
}

/* ------------------------------------------------------------------------------------------- input */
static volatile u8 kq[64]; static volatile u8 kq_head, kq_tail;
static void kbd_handshake(void)
{
    unsigned n = 0; u8 last = (u8)(R16(VHPOSR) >> 8);
    CIAA(C_CRA) = (u8)(CIAA(C_CRA) | 0x40);
    while (n < 3) { u8 l = (u8)(R16(VHPOSR) >> 8); if (l != last) { last = l; n++; } }
    CIAA(C_CRA) = (u8)(CIAA(C_CRA) & ~0x40);
}
void plat_isr_ports_c(void)
{
    dbg_n_ports++;
    u8 icr = CIAA(C_ICR); dbg_n_aud = (dbg_n_aud & 0xFF00FFFFu) | ((u32)icr << 16); if (icr & 8) dbg_n_aud++; dbg_n_def = R16(INTREQR) | ((u32)R16(INTENAR) << 16);
    if (icr & 0x08) {
        u8 d = (u8)~CIAA(C_SDR);
        kbd_handshake();
        u8 h = kq_head, nh = (u8)((h + 1) & 63);
        if (nh != kq_tail) { kq[h] = d; kq_head = nh; }
    }
    R16(INTREQ) = INT_PORTS;
}
void plat_isr_audio_c(void) { dbg_n_aud++; R16(INTREQ) = 0x0780; }

/* Amiga raw key -> PC scan code (set 1).  0xFF = unmapped.  bit 7 of the result = extended (E0 prefix). */
static const u8 amiga2pc[0x68] = {
    /* 00 */ 0x29, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x2B, 0xFF, 0x52,
    /* 10 */ 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0xFF, 0x4F, 0x50, 0x51,
    /* 20 */ 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2B, 0xFF, 0x4B, 0x4C, 0x4D,
    /* 30 */ 0x56, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0xFF, 0x53, 0x47, 0x48, 0x49,
    /* 40 */ 0x39, 0x0E, 0x0F, 0x1C, 0x1C, 0x01, 0x53, 0xFF, 0xFF, 0xFF, 0x4A, 0xFF, 0xC8, 0xD0, 0xCD, 0xCB,
    /* 50 */ 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44, 0xFF, 0xFF, 0x35, 0x37, 0x4E, 0x57,
    /* 60 */ 0x2A, 0x36, 0xFF, 0x1D, 0x38, 0x38, 0xFF, 0xFF,
};
static void push_scan(u8 sc, int up)
{
    if (sc == 0xFF) return;
    if (sc & 0x80) hw_key(0xE0);
    hw_key((u8)((sc & 0x7F) | (up ? 0x80 : 0)));
}
/* joystick (port 2) -> the Dark side keys; the second button is button B (pull-down menus) */
static u8 joy_prev;
#define J_UP 1
#define J_DOWN 2
#define J_LEFT 4
#define J_RIGHT 8
#define J_FIRE 16
#define J_FIRE2 32
static u8 joy_read(void)
{
    u16 j = R16(JOY1DAT); u8 b = 0;
    if (j & 0x0200) b |= J_LEFT;
    if (j & 0x0002) b |= J_RIGHT;
    if ((j ^ (j >> 1)) & 0x0001) b |= J_DOWN;
    if ((j ^ (j >> 1)) & 0x0100) b |= J_UP;
    if (!(CIAA(C_PRA) & 0x80)) b |= J_FIRE;
    if (!(R16(POTGOR) & 0x4000)) b |= J_FIRE2;
    return b;
}
/* mouse (port 1): position in the DOS mouse coordinate system */
static u8 mouse_px, mouse_py; static int mouse_x = 320, mouse_y = 100;
static void mouse_update(void)
{
    u16 m = R16(JOY0DAT); u8 x = (u8)m, y = (u8)(m >> 8);
    s8 dx = (s8)(x - mouse_px), dy = (s8)(y - mouse_py);
    mouse_px = x; mouse_py = y;
    mouse_x += dx * 2; mouse_y += dy;
    if (mouse_x < 0) mouse_x = 0; if (mouse_x > 639) mouse_x = 639;
    if (mouse_y < 0) mouse_y = 0; if (mouse_y > 199) mouse_y = 199;
    u32 b = 0;
    if (!(CIAA(C_PRA) & 0x40)) b |= 1;
    if (!(R16(POTGOR) & 0x0400)) b |= 2;
    rt_mouse_set((u32)mouse_x, (u32)mouse_y, b);
}
static void input_poll(void)
{
    while (kq_tail != kq_head) {
        u8 d = kq[kq_tail]; kq_tail = (u8)((kq_tail + 1) & 63);
        u8 code = (d >> 1) & 0x7F, up = d & 1;
        if (code < 0x68) push_scan(amiga2pc[code], up);
    }
    u8 j = joy_read(), ch = (u8)(j ^ joy_prev);
    if (ch) {
        if (ch & J_UP) push_scan(0xC8, !(j & J_UP));
        if (ch & J_DOWN) push_scan(0xD0, !(j & J_DOWN));
        if (ch & J_LEFT) push_scan(0xCB, !(j & J_LEFT));
        if (ch & J_RIGHT) push_scan(0xCD, !(j & J_RIGHT));
        if (ch & J_FIRE) push_scan(0x52, !(j & J_FIRE));          /* button A = numeric keypad 0 */
        if (ch & J_FIRE2) push_scan(0x53, !(j & J_FIRE2));        /* button B = numeric keypad . */
        joy_prev = j;
    }
    mouse_update();
}

/* ------------------------------------------------------------------------------------------- display */
static u32 shown_gen = 0xFFFFFFFFu;
#ifdef DEBUGBAR
extern u32 dbg_pc; extern u32 hw_dbg1(void), hw_dbg2(void);
extern u32 rt_dbg_hist[32], rt_dbg_exit_code, rt_dbg_exit_a, rt_dbg_exit_b;
extern u32 rt_dbg_last, rt_dbg_nint, rt_dbg_irq, rt_dbg_a, rt_dbg_b, rt_dbg_n5;
static u8 dbgrows[32 * 320] __attribute__((aligned(4)));
static u32 polls;
static void dbg_hex2(unsigned col, unsigned row, u32 v);
static void dbg_hex(unsigned col, u32 v) { dbg_hex2(col, 0, v); }
static void dbg_hex2(unsigned col, unsigned row, u32 v)
{
    for (unsigned d = 0; d < 8; d++) {
        unsigned nib = (v >> (28 - 4 * d)) & 15;
        for (unsigned y = 0; y < 7; y++) { u8 bits = hexfont[nib][y]; for (unsigned x = 0; x < 5; x++) dbgrows[(y + row) * 320 + col + d * 6 + x] = (bits & (16 >> x)) ? 255 : 0; }
    }
}
#endif
#ifdef DEBUGBAR
static u32 dbg_disk_word(void);
static u16 prof[8192], top_tmp[6], top_idx[6];
static void dbg_bar_draw(void)
{
    { u32 o = (dbg_pc - boot_fast) >> 8; if (o < 8192) prof[o]++; }
    if ((vbl_count & 15) == 0) {
        for (unsigned r = 0; r < 6; r++) {
            unsigned best = 0, bi = 0;
            for (unsigned i = 0; i < 8192; i++) if (prof[i] > best) { best = prof[i]; bi = i; }
            dbg_hex2(8 + r * 52, 16, bi << 8); dbg_hex2(8 + r * 52, 24, best);
            if (best) prof[bi] = 0;     /* temporarily removed so the next-best is found; restored below */
            top_tmp[r] = (u16)best; top_idx[r] = (u16)bi;
        }
        for (unsigned r = 0; r < 6; r++) prof[top_idx[r]] = top_tmp[r];
    }
    dbg_hex(8, dbg_pc - boot_fast); dbg_hex(60, polls); dbg_hex(112, vbl_count); dbg_hex(164, plat_time_us());
    dbg_hex(216, (rt_dbg_last << 16) | (rt_dbg_nint & 0xFFFF)); dbg_hex(268, dbg_disk_word());
    dbg_hex2(8, 8, dbg_n_ports); dbg_hex2(60, 8, dbg_n_ciab); dbg_hex2(112, 8, dbg_n_aud); dbg_hex2(164, 8, rt_dbg_exit_code); dbg_hex2(216, 8, rt_dbg_exit_a); dbg_hex2(268, 8, rt_dbg_exit_b ? rt_dbg_exit_b - boot_fast : 0);
    for (unsigned y = 0; y < 32; y++) { c2p_row320(dbgrows + y * 320, buf[0] + (200 + y) * ROW_BYTES); c2p_row320(dbgrows + y * 320, buf[1] + (200 + y) * ROW_BYTES); }
}
#endif
static u8 conv_valid[2]; static u32 conv_start[2], conv_pitch[2];
static u8 rowtmp[336] __attribute__((aligned(4)));
static void display_update(void)
{
    if (flip_pending) return;
    u32 gen = vga_gen;
    if (vga_dac_gen != pal_gen_done) {
        pal_gen_done = vga_dac_gen;
        for (unsigned i = 0; i < 256; i++) {
            u32 r = vga_dac[i * 3] & 63, g = vga_dac[i * 3 + 1] & 63, b = vga_dac[i * 3 + 2] & 63;
            r = (r << 2) | (r >> 4); g = (g << 2) | (g >> 4); b = (b << 2) | (b >> 4);
            pal_hi[i] = (u16)(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
#ifdef DEBUGBAR
            if (i == 255) { pal_hi[i] = 0x0FFF; pal_lo[i] = 0x0FFF; continue; }
#endif
            pal_lo[i] = (u16)(((r & 15) << 8) | ((g & 15) << 4) | (b & 15));
        }
        pal_pending = 1;
    }
    if (gen == shown_gen) return;
    shown_gen = gen;
    u32 start, pitch; int c4; vga_get_display(&start, &pitch, &c4);
    if (pitch == 0) pitch = 320;
    unsigned bb = front ^ 1; u8 mask = (u8)(1u << bb);
    u8 *dst = buf[bb];
    /* incremental conversion needs 16-byte aligned rows and an unchanged window since this buffer was last converted */
    int incr = !(start & 15) && !(pitch & 15) && conv_valid[bb] && conv_start[bb] == start && conv_pitch[bb] == pitch;
    for (unsigned y = 0; y < 200; y++) {
        u32 a = (start + y * pitch) & 0x3FFFF;
        const u8 *src;
        if (a + 320 <= 0x40000) src = vga_mem + a;
        else { for (unsigned k = 0; k < 320; k++) rowtmp[k] = vga_mem[(a + k) & 0x3FFFF]; src = rowtmp; incr = 0; }
        if (incr) c2p_row320_dirty(src, dst + y * ROW_BYTES, &vga_dirty[a >> 4], mask);
        else c2p_row320(src, dst + y * ROW_BYTES);
    }
    if (!incr) { for (u32 i = 0; i < sizeof vga_dirty; i++) vga_dirty[i] &= (u8)~mask; }
    conv_valid[bb] = 1; conv_start[bb] = start; conv_pitch[bb] = pitch;
#ifdef DEBUGBAR
    for (unsigned y = 0; y < 32; y++) c2p_row320(dbgrows + y * 320, dst + (200 + y) * ROW_BYTES);
#endif
    flip_pending = 1;
}

/* ------------------------------------------------------------------------------------------- IDE data disk (Gayle, PIO, LBA) */
#define IDE_BASE 0xDA0000u
#define IDE_DATA (*(volatile u16 *)(IDE_BASE + 0x0000))
#define IDE_REG(n) (*(volatile u8 *)(IDE_BASE + 0x2000 + (n) * 4))
#define IDE_ERR 1
#define IDE_COUNT 2
#define IDE_SECT 3
#define IDE_CYLLO 4
#define IDE_CYLHI 5
#define IDE_HEAD 6
#define IDE_STATUS 7
#define IDE_ALT (*(volatile u8 *)(IDE_BASE + 0x3018))

#define GAYLE_IRQ (*(volatile u8 *)0xDA9000)
#define GAYLE_INTENA (*(volatile u8 *)0xDAA000)
static void ide_quiet(void)
{
    IDE_ALT = 0x02;                       /* nIEN: the drive does not raise its interrupt line */
    GAYLE_INTENA = 0;                     /* and Gayle does not forward it to INT2 */
    GAYLE_IRQ = 0x7C;                     /* acknowledge what is latched (A1200 Gayle: write 0 bits to clear) */
}
static int ide_wait(u8 mask, u8 want)
{
    for (u32 n = 0; n < 4000000u; n++) { u8 s = IDE_REG(IDE_STATUS); if (!(s & 0x80) && (s & mask) == want) return 0; }
    return -1;
}
static int ide_read_sector(u32 lba, u8 *dst)
{
    IDE_REG(IDE_HEAD) = (u8)(0xE0 | ((lba >> 24) & 15));
    for (volatile int d = 0; d < 200; d++) (void)IDE_REG(IDE_STATUS);
    if (ide_wait(0x40, 0x40)) return -1;
    IDE_REG(IDE_COUNT) = 1; IDE_REG(IDE_SECT) = (u8)lba; IDE_REG(IDE_CYLLO) = (u8)(lba >> 8); IDE_REG(IDE_CYLHI) = (u8)(lba >> 16);
    IDE_REG(IDE_STATUS) = 0x20;
    if (ide_wait(0x08, 0x08)) return -2;
    volatile u16 *p = &IDE_DATA; u16 *d = (u16 *)dst;
    for (unsigned i = 0; i < 256; i++) d[i] = *p;
    (void)IDE_REG(IDE_STATUS);
    ide_quiet();
    return 0;
}

struct hfile { char name[16]; u32 lba, size, pos; } files_tab[8];
static u32 nfiles; static u8 sector[512] __attribute__((aligned(4))); static u32 sector_lba = 0xFFFFFFFFu;
static int disk_ok;
int dbg_disk_rc = 99;
static u32 disk_base;
static void disk_init(void)
{
    /* the table sits at LBA 0 of a raw image, but emulators/adapters may put a (virtual) RDB in front: scan for the signature */
    ide_quiet();
    for (u32 l = 0; l < 8192; l++) {
        dbg_disk_rc = ide_read_sector(l, sector); if (dbg_disk_rc) return;
        if (sector[0] == 'A' && sector[1] == 'R' && sector[2] == 'C' && sector[3] == 'H') { disk_base = l; sector_lba = l; goto found; }
    }
    return;
found:
    nfiles = sector[4]; if (nfiles > 8) nfiles = 8;
    for (u32 i = 0; i < nfiles; i++) {
        const u8 *e = sector + 16 + i * 32;
        for (unsigned k = 0; k < 16; k++) files_tab[i].name[k] = (char)e[k];
        files_tab[i].lba = disk_base + (((u32)e[16] << 24) | ((u32)e[17] << 16) | ((u32)e[18] << 8) | e[19]);
        files_tab[i].size = ((u32)e[20] << 24) | ((u32)e[21] << 16) | ((u32)e[22] << 8) | e[23];
        files_tab[i].pos = 0;
    }
    disk_ok = 1;
}
#ifdef DEBUGBAR
static u32 dbg_disk_word(void) { return ((u32)(dbg_disk_rc & 0xFF) << 24) | (nfiles << 16) | ((u32)IDE_REG(IDE_STATUS) << 8) | sector[0]; }
#endif
static int streq_ci(const char *a, const char *b)
{
    for (;; a++, b++) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return 0; if (!x) return 1; }
}
long plat_file_open(const char *name)
{
    for (u32 i = 0; i < nfiles; i++) if (streq_ci(name, files_tab[i].name)) { files_tab[i].pos = 0; return (long)i; }
    return -1;
}
void plat_file_close(long h) { (void)h; }
long plat_file_seek(long h, long off, int whence)
{
    struct hfile *f = &files_tab[h]; long p = whence == 0 ? off : whence == 1 ? (long)f->pos + off : (long)f->size + off;
    if (p < 0) p = 0; f->pos = (u32)p; return p;
}
long plat_file_read(long h, void *buf_, u32 n)
{
    struct hfile *f = &files_tab[h]; u8 *d = buf_; u32 done = 0;
    if (f->pos >= f->size) return 0;
    if (n > f->size - f->pos) n = f->size - f->pos;
    while (done < n) {
        u32 abs = f->pos + done, lba = f->lba + (abs >> 9), off = abs & 511, take = 512 - off;
        if (take > n - done) take = n - done;
        if (lba != sector_lba) { if (ide_read_sector(lba, sector)) return done ? (long)done : -1; sector_lba = lba; }
        for (u32 k = 0; k < take; k++) d[done + k] = sector[off + k];
        done += take;
    }
    f->pos += done; return (long)done;
}

/* ------------------------------------------------------------------------------------------- audio (set up in snd.c later) */
void plat_audio_push(const u8 *d, u32 n, u32 rate, int is16, int stereo, int sign) { (void)d; (void)n; (void)rate; (void)is16; (void)stereo; (void)sign; }
void plat_vsync(u32 frame) { (void)frame; }

/* ------------------------------------------------------------------------------------------- polling hook called by the runtime */
#ifdef TESTKEYS
/* test-only scripted input (game time in ms since start, PC scancode): D = down, space = fire (Light keyboard map) */
static const struct { u32 ms; u8 sc; } tk[] = {
    {70000,0x20},{70200,0xA0},{70600,0x20},{70800,0xA0},{71200,0x20},{71400,0xA0},{71800,0x20},{72000,0xA0},
    {72400,0x20},{72600,0xA0},{73000,0x20},{73200,0xA0},{73600,0x39},{73800,0xB9},
};
static unsigned tk_i;
static void test_keys(void)
{
    u32 ms = plat_time_us() / 1000;
    while (tk_i < sizeof tk / sizeof tk[0] && ms >= tk[tk_i].ms) { hw_key(tk[tk_i].sc); tk_i++; }
}
#endif
void plat_poll(void)
{
#ifdef TESTKEYS
    test_keys();
#endif
#ifdef DEBUGBAR
    polls++;
#endif
    input_poll();
    if (vbl_count != shown_vbl) { shown_vbl = vbl_count; display_update(); }
    else if (!flip_pending) display_update();
}

/* ------------------------------------------------------------------------------------------- hardware bring-up */
static u16 beam_line(void) { u16 v = R16(VHPOSR); u16 hi = (u16)(R16(0x004) & 1); return (u16)((hi << 8) | (v >> 8)); }   /* VPOSR bit 0 = V8 */
static u16 measure_lines(void)
{
    u16 prev = beam_line(), cur, max = 0; unsigned wraps = 0, guard = 0;
    while (wraps < 2 && ++guard < 8000000u) {
        cur = beam_line();
        if (cur + 100u < prev) { wraps++; if (wraps == 1) max = 0; } else if (wraps == 1 && cur > max) max = cur;
        prev = cur;
    }
    return (u16)(max + 1);
}

static void hw_reset(void)
{
    R16(INTENA) = DMA_ALL; R16(INTREQ) = DMA_ALL; R16(DMACON) = DMA_ALL; R16(ADKCON) = DMA_ALL; R16(INTREQ) = DMA_ALL;
    for (int i = 0; i < 4; i++) { R16(AUDx(i, 8)) = 0; R16(AUDx(i, 4)) = 1; R16(AUDx(i, 6)) = 124; }
    for (int i = 0; i < 8; i++) { R16(SPRx(i, 0)) = 0; R16(SPRx(i, 4)) = 0; R16(SPRx(i, 6)) = 0; R16(SPRx(i, 2)) = 0; }
    R16(FMODE) = 0; R16(BPLCON0) = 0x0200; R16(BPLCON1) = 0; R16(BPLCON2) = 0; R16(BPLCON3) = BPLCON3_BASE; R16(BPLCON4) = BPLCON4_BASE;
    R16(CLXCON) = 0; R16(CLXCON2) = 0; R16(BPL1MOD) = 0; R16(BPL2MOD) = 0; R16(COLOR00) = 0;
    R16(POTGO) = 0xFF00;
    CIAA(C_ICR) = 0x7F; CIAA(C_CRA) = 0; CIAA(C_CRB) = 0; (void)CIAA(C_ICR);
    CIAA(C_DDRA) = 0x03; CIAA(C_PRA) = (u8)(CIAA(C_PRA) & ~0x01u);
    CIAB(C_ICR) = 0x7F; CIAB(C_CRA) = 0; CIAB(C_CRB) = 0; (void)CIAB(C_ICR);
    CIAB(C_DDRB) = 0xFF; CIAB(C_PRB) = 0xFF; CIAB(C_PRB) = 0xF7; CIAB(C_PRB) = 0xFF;
    R16(INTREQ) = DMA_ALL;
}

extern void rt_enter(u8 *win, struct ctx *ctx, u32 esp, void *entry);
extern void rt_build_xl_tab(void);
extern u32 xl_entry, rt_heap_end, rt_heap_reserve;
extern u32 xl_tab[];
extern const u8 obj1_data[], obj2_data[], obj3_data[];
extern const u32 obj1_len, obj2_len, obj3_len;
extern void rt_isr_return(void);
#include "layout.h"

static struct ctx ctx;
static void set_vec(unsigned n, void (*f)(void)) { vectors[n] = (u32)f; }

static inline void dbg(u16 c) { R16(COLOR00) = c; }
#ifndef DBG_STOP
#define DBG_STOP 0
#endif
#define STOP(n, col) do { if (DBG_STOP == (n)) { R16(DMACON) = DMA_ALL; R16(BPLCON0) = 0x0200; R16(BPLCON3) = BPLCON3_BASE; for (;;) R16(COLOR00) = (col); } } while (0)
void amiga_main(void)
{
    hw_reset();
    dbg(0x0A0A); STOP(1, 0x0F00);
    chip = (u8 *)boot_chip;
    buf[0] = chip + CHIP_BUF0; buf[1] = chip + CHIP_BUF1;
    cop = (u16 *)(chip + CHIP_COP);

    if ((R16(DENISEID) & 0xFF) != 0xF8) for (;;) R16(COLOR00) = 0x0F00;          /* AGA required */
    dbg(0x0FFF);
    is_pal = measure_lines() >= 286;
    dbg(0x0888); STOP(2, 0x00F0);
    vstart = is_pal ? 0x4A : 0x42;

    *(u32 *)(chip + CHIP_SPR) = 0;                                                  /* null sprite */
    for (unsigned i = 0; i < 8; i++) {
        cop[i * 4 + 0] = (u16)(SPR0PT + i * 4);     cop[i * 4 + 1] = (u16)(((u32)chip + CHIP_SPR) >> 16);
        cop[i * 4 + 2] = (u16)(SPR0PT + i * 4 + 2); cop[i * 4 + 3] = (u16)(((u32)chip + CHIP_SPR));
    }
    for (unsigned p = 0; p < 8; p++) { cop[32 + p * 4 + 0] = (u16)(0x0E0 + p * 4); cop[32 + p * 4 + 2] = (u16)(0x0E2 + p * 4); }
    cop[64] = 0xFFFF; cop[65] = 0xFFFE;
    front = 0; apply_bpl_ptrs(buf[0]);

    /* display window 320x200, lores, 8 planes, interleaved (plane p of a line at +40*p; BPLxMOD skips the other 7) */
    {
        unsigned vs = vstart, ve = vstart + SCR_H;
        R16(DIWSTRT) = (u16)(((vs & 0xFF) << 8) | 0x81);
        R16(DIWSTOP) = (u16)(((ve & 0xFF) << 8) | 0xC1);
        R16(DIWHIGH) = (u16)(((ve >> 8) & 1) << 13);
        R16(DDFSTRT) = 0x0038; R16(DDFSTOP) = 0x00C8;
        R16(FMODE) = 0x0001;
        R16(BPLCON0) = BPLCON0_8PL; R16(BPLCON1) = 0; R16(BPLCON2) = 0; R16(BPLCON3) = BPLCON3_BASE; R16(BPLCON4) = BPLCON4_BASE;
        R16(BPL1MOD) = 280; R16(BPL2MOD) = 280;
    }
    R32(COP1LC) = (u32)chip + CHIP_COP; R16(COPJMP1) = 0;
    STOP(3, 0x000F);

    /* exception vectors in chip RAM at 0 */
    for (unsigned i = 0; i < 256; i++) vectors[i] = (u32)isr_default;
    { extern u32 exc_table[]; for (unsigned i = 0; i < 48; i++) vectors[i] = exc_table[i]; }
    set_vec(26, isr_ports);       /* level 2 */
    set_vec(27, isr_vbl);         /* level 3 */
    set_vec(28, isr_aud);         /* level 4 */
    set_vec(30, isr_ciab);        /* level 6 */
    { u32 *v0 = (u32 *)0; for (unsigned i = 0; i < 64; i++) v0[i] = vectors[i]; }
    vectors[0] = boot_chip + boot_chip_size;                                        /* (unused) */

    /* time base: CIA-B timer A free-running at the E clock, underflow interrupt counts the high part */
    CIAB(C_CRB) = 0x00; CIAB(C_TALO) = 0xFF; CIAB(C_TAHI) = 0xFF; CIAB(C_CRA) = 0x11; (void)CIAB(C_ICR); CIAB(C_ICR) = 0x81;
    kbd_handshake(); (void)CIAA(C_ICR); CIAA(C_ICR) = 0x88;
    mouse_px = (u8)R16(JOY0DAT); mouse_py = (u8)(R16(JOY0DAT) >> 8);

    STOP(4, 0x0FF0);
    pal_pending = 0;
    R16(INTREQ) = DMA_ALL;
    R16(DMACON) = SETBIT | DMAEN | BPLEN | COPEN | BLTEN;
    R16(INTENA) = SETBIT | INT_INTEN | INT_EXTER | INT_VERTB | INT_PORTS;

    STOP(5, 0x0F0F);
    /* ---------------------------------------------------------------- the PC world */
    u8 *image_end = (u8 *)((u32)_end);
    u8 *w = (u8 *)(((u32)image_end + 0xFFFu) & ~0xFFFu);
    u32 avail = boot_fast + boot_fast_size - (u32)w;
    rt_heap_end = avail - 0x10000; if (rt_heap_end > 0xFFF000u) rt_heap_end = 0xFFF000u;
    rt_heap_reserve = 0x40000;
    for (u32 i = 0; i < obj1_len; i++) w[OBJ1_BASE + i] = obj1_data[i];
    for (u32 i = 0; i < obj2_len; i++) w[OBJ2_BASE + i] = obj2_data[i];
    for (u32 i = 0; i < obj3_len; i++) w[OBJ3_BASE + i] = obj3_data[i];
    dbg(0x0F80);
    disk_init();
    dbg(0x0F00);
    rt_init(w, &ctx);
    rt_build_xl_tab();
    xl_tab[XL_SENTINEL_OFF] = (u32)rt_isr_return;
    dbg(0x00F0); STOP(6, 0x0FFF);

    /* master mode: interrupts then use the interrupt stack and never touch the x86 stack */
    __asm__ volatile("move.l %0,%%d0\n\tmovec %%d0,%%msp\n\tmove.w #0x3000,%%sr" : : "r"(boot_chip + 0x3F000) : "d0", "memory");
    rt_enter(w, &ctx, OBJ3_ESP_TOP, (void *)xl_entry);
    for (;;) R16(COLOR00) = 0x00F0;
}
