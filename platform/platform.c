/* platform.c - Archon Ultra on the Amiga 1200 (AGA, 68030, fast RAM): display, sound, timing, input, file access, glue to the runtime.
 * The OS stays alive underneath: the game owns the screen, Paula audio and the joystick/mouse registers; it uses OS interrupt servers for the vertical
 * blank, an input.device handler for the keyboard, and AmigaDOS for its data files (so any kind of drive works).  User mode, no libc. */
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
extern u32 os_call(void *base, s32 lvo, u32 d0, u32 d1, u32 d2, u32 d3, u32 a0, u32 a1);
extern void vbl_server(void), input_handler(void);
static u32 sysbase, dosbase, gfxbase, oldview;           /* from the launcher (boot info at the top of the chip block) */
#define EXEC(lvo, d0, d1, a0, a1) os_call((void *)sysbase, lvo, d0, d1, 0, 0, a0, a1)
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
#define SCR_H 208
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
u32 dbg_conv_total;
extern u32 dbg_aud_pushes, dbg_aud_peak;
u32 dbg_n_ports, dbg_n_ciab, dbg_n_aud, dbg_n_def;
/* crash screen: eight hex digits per line in a one-plane hires picture (vector, SR, PC, fault address / format word, then 8 stack longs) */
static const u8 hexfont[16][7] = { {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,30,1,1,17,14},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},
                                   {14,17,17,14,17,17,14},{14,17,17,15,1,2,12},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16} };
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
}

/* ------------------------------------------------------------------------------------------- time */
u32 plat_time_us(void)
{
    /* frame counter (our vertical-blank server) + raster position; monotonic */
    static u32 last;
    u32 c1, c2, line;
    do { c1 = vbl_count; line = ((u32)(R16(0x004) & 1) << 8) | (R16(VHPOSR) >> 8); c2 = vbl_count; } while (c1 != c2);
    u32 frame_us = is_pal ? 19968u : 16683u, lines = is_pal ? 312u : 262u;
    u32 t = c1 * frame_us + (line * frame_us) / lines;
    if ((s32)(t - last) < 0) t = last;
    return last = t;
}

/* ------------------------------------------------------------------------------------------- input */
static volatile u8 kq[64]; static volatile u8 kq_head, kq_tail;
/* input.device handler: sees every raw key event (class 1, code in ie_Code, bit 7 = key up) before the rest of the system and queues it in the same form
   the old CIA-A handler produced ((code << 1) | up), then lets the event continue. */
struct ievent { struct ievent *next; u8 cls, sub; u16 code, qual; };
void *input_handler_c(struct ievent *ev)
{
    for (struct ievent *e = ev; e; e = e->next)
        if (e->cls == 1) {
            u8 d = (u8)(((e->code & 0x7F) << 1) | ((e->code >> 7) & 1));
            u8 h = kq_head, nh = (u8)((h + 1) & 63);
            if (nh != kq_tail) { kq[h] = d; kq_head = nh; }
        }
    return ev;
}

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
extern u32 rt_dbg_port, rt_dbg_pc;
extern u32 hw_dbg1(void), hw_dbg2(void);
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
static u16 prof[8192], top_tmp[6], top_idx[6];
static void dbg_bar_draw(void)
{
    /* one line, redrawn every 16th frame: stays cheap enough not to change the timing of the game */
    if ((vbl_count & 15) != 0) return;
    dbg_hex(8, vbl_count); dbg_hex(60, polls); dbg_hex(112, (rt_dbg_last << 16) | (rt_dbg_nint & 0xFFFF)); dbg_hex(164, hw_dbg2()); dbg_hex(216, rt_dbg_pc - boot_fast); dbg_hex(268, hw_dbg1());
    for (unsigned y = 0; y < 8; y++) { c2p_row320(dbgrows + y * 320, buf[0] + (200 + y) * ROW_BYTES); c2p_row320(dbgrows + y * 320, buf[1] + (200 + y) * ROW_BYTES); }
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
    dbg_conv_total++;
    conv_valid[bb] = 1; conv_start[bb] = start; conv_pitch[bb] = pitch;
#ifdef DEBUGBAR
    for (unsigned y = 0; y < 8; y++) c2p_row320(dbgrows + y * 320, dst + (200 + y) * ROW_BYTES);
#endif
    flip_pending = 1;
}

/* ------------------------------------------------------------------------------------------- data files through AmigaDOS (any drive type) */
#define DOS_OPEN (-30)
#define DOS_CLOSE (-36)
#define DOS_READ (-42)
#define DOS_WRITE (-48)
#define DOS_SEEK (-66)
#define DOS(lvo, d1, d2, d3) os_call((void *)dosbase, lvo, 0, d1, d2, d3, 0, 0)
long plat_file_open(const char *name)
{
    char p[80] = "SYS:"; unsigned n = 4;
    for (unsigned i = 0; name[i] && n < 78; i++) p[n++] = name[i];
    p[n] = 0;
    long fh = (long)DOS(DOS_OPEN, (u32)p, 1005, 0);                 /* MODE_OLDFILE */
    return fh ? fh : -1;
}
void plat_file_close(long h) { DOS(DOS_CLOSE, (u32)h, 0, 0); }
long plat_file_seek(long h, long off, int whence)
{
    DOS(DOS_SEEK, (u32)h, (u32)off, whence == 0 ? (u32)-1 : whence == 1 ? 0u : 1u);        /* OFFSET_BEGINNING / CURRENT / END */
    return (long)DOS(DOS_SEEK, (u32)h, 0, 0);                         /* Seek returns the OLD position: ask again for the new one */
}
long plat_file_read(long h, void *buf_, u32 n) { return (long)DOS(DOS_READ, (u32)h, (u32)buf_, n); }

/* ------------------------------------------------------------------------------------------- audio (set up in snd.c later) */
/* Paula: ring buffers (channel 0 = left, channel 1 = right) that loop forever; the game's Sound Blaster blocks are converted to signed 8-bit and
   written just ahead of the read position, which is derived from the clock (Paula has no readable pointer). */
#define AUD_N 8192u
static u32 aud_wp, aud_started, aud_rate, aud_t0_us;
u32 dbg_aud_pushes, dbg_aud_peak;
void plat_audio_push(const u8 *d, u32 n, u32 rate, int is16, int stereo, int sign)
{
    if (!rate) return;
    s8 *L = (s8 *)(chip + CHIP_AUD), *Rr = (s8 *)(chip + CHIP_AUD + AUD_N);
    u32 now = plat_time_us();
    if (!aud_started || rate != aud_rate) {
        aud_rate = rate; aud_started = 1; aud_t0_us = now; aud_wp = 1536;
        u32 period = ((is_pal ? 3546895u : 3579545u) + rate / 2) / rate; if (period < 124) period = 124;
        for (unsigned c = 0; c < 2; c++) {
            R32(AUDx(c, 0)) = (u32)chip + CHIP_AUD + c * AUD_N; R16(AUDx(c, 4)) = AUD_N / 2; R16(AUDx(c, 6)) = (u16)period; R16(AUDx(c, 8)) = 64;
        }
        for (u32 i = 0; i < AUD_N; i++) { L[i] = 0; Rr[i] = 0; }
        R16(DMACON) = SETBIT | 0x0003;                       /* AUD0EN | AUD1EN */
    }
    u32 us = now - aud_t0_us;
    u32 ridx = (((us / 1000000u) * rate) + ((((us % 1000000u) / 1000u) * rate) / 1000u)) % AUD_N;
    u32 ahead = (aud_wp - ridx) & (AUD_N - 1);
    if (ahead < 256 || ahead > AUD_N - 1024) aud_wp = (ridx + 1536) & (AUD_N - 1);     /* under/overrun: resynchronise */
    dbg_aud_pushes++;
    u32 bps = (is16 ? 2u : 1u) * (stereo ? 2u : 1u), frames = n / bps;
    for (u32 i = 0; i < frames; i++, d += bps) {
        s8 l, r;
        if (is16) { l = (s8)(sign ? d[1] : d[1] ^ 0x80); r = stereo ? (s8)(sign ? d[3] : d[3] ^ 0x80) : l; }
        else { l = (s8)(sign ? d[0] : d[0] ^ 0x80); r = stereo ? (s8)(sign ? d[1] : d[1] ^ 0x80) : l; }
        { u32 a = (u32)(l < 0 ? -l : l); if (a > dbg_aud_peak) dbg_aud_peak = a; }
        L[aud_wp] = l; Rr[aud_wp] = r; aud_wp = (aud_wp + 1) & (AUD_N - 1);
    }
}
void plat_vsync(u32 frame) { (void)frame; }

/* ------------------------------------------------------------------------------------------- polling hook called by the runtime */
#ifdef TESTKEYS
/* test-only scripted mouse (game time ms, x 0..639, y 0..199, buttons): Light=Computer, Dark=Computer, START */
static const struct { u32 ms; u16 x, y; u8 b; } tm[] = {
    {70000,252,61,0},{70500,252,61,1},{71000,252,61,0},
    {71500,398,61,0},{72000,398,61,1},{72500,398,61,0},
    {73000,248,130,0},{73500,248,130,1},{74000,248,130,0},
};
static void test_keys(void)
{
    u32 ms = plat_time_us() / 1000; int k = -1;
    for (unsigned i = 0; i < sizeof tm / sizeof tm[0]; i++) if (ms >= tm[i].ms) k = (int)i;
    if (k >= 0) rt_mouse_set(tm[k].x, tm[k].y, tm[k].b);
}
#endif
void plat_poll(void)
{
#ifdef DEBUGBAR
    polls++;
#endif
    input_poll();
#ifdef TESTKEYS
    test_keys();
#endif
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
    /* the system keeps running: touch only what the game owns (display, sprites, audio, the pot/button lines) */
    R16(DMACON) = 0x0020;                                  /* sprite DMA off (no system pointer) */
    for (int i = 0; i < 4; i++) { R16(AUDx(i, 8)) = 0; R16(AUDx(i, 4)) = 1; R16(AUDx(i, 6)) = 124; }
    for (int i = 0; i < 8; i++) { R16(SPRx(i, 0)) = 0; R16(SPRx(i, 4)) = 0; R16(SPRx(i, 6)) = 0; R16(SPRx(i, 2)) = 0; }
    R16(FMODE) = 0; R16(BPLCON0) = 0x0200; R16(BPLCON1) = 0; R16(BPLCON2) = 0; R16(BPLCON3) = BPLCON3_BASE; R16(BPLCON4) = BPLCON4_BASE;
    R16(CLXCON) = 0; R16(CLXCON2) = 0; R16(BPL1MOD) = 0; R16(BPL2MOD) = 0; R16(COLOR00) = 0;
    R16(POTGO) = 0xFF00;
}

/* ------------------------------------------------------------------------------------------- fatal-error screen (5x7 font, upper case) */
static const u8 font5x7[][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x07,0x02,0x02,0x02,0x02,0x12,0x0C},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},{0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},{0x11,0x11,0x11,0x15,0x15,0x1B,0x11},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},{0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    {0,0,0,0,0,0,0},{0,0,0,0,0,0x06,0x06},{0,0,0,0x1F,0,0,0},{0,0x06,0x06,0,0x06,0x06,0},{0x01,0x02,0x02,0x04,0x08,0x08,0x10},
};
static const u8 *glyph(char c)
{
    if (c >= 'A' && c <= 'Z') return font5x7[c - 'A'];
    if (c >= '0' && c <= '9') return font5x7[26 + c - '0'];
    if (c == '.') return font5x7[37]; if (c == '-') return font5x7[38]; if (c == ':') return font5x7[39]; if (c == '/') return font5x7[40];
    return font5x7[36];
}
extern u32 rt_dbg_last;
static u8 msgrows[200 * 320] __attribute__((aligned(4)));
static void msg_line(unsigned y, const char *t)
{
    for (unsigned n = 0; t[n] && n < 26; n++) {
        const u8 *g = glyph(t[n]);
        for (unsigned r = 0; r < 7; r++) for (unsigned c = 0; c < 5; c++) if (g[r] & (16 >> c))
            for (unsigned dy = 0; dy < 2; dy++) for (unsigned dx = 0; dx < 2; dx++) msgrows[(y + r * 2 + dy) * 320 + 8 + n * 12 + c * 2 + dx] = 1;
    }
}
static void hex8(char *o, u32 v) { for (int i = 0; i < 8; i++) o[i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15]; o[8] = 0; }
/* shows up to six lines of text and stops; the VBL interrupt keeps running */
static void fatal(const char *a, const char *b, const char *c, const char *d, const char *e, u32 code)
{
    for (u32 i = 0; i < sizeof msgrows; i++) msgrows[i] = 0;
    msg_line(20, a); msg_line(50, b); msg_line(70, c); msg_line(90, d); msg_line(110, e);
    char h[16]; hex8(h, code); { char l[24] = "CODE "; for (int i = 0; i < 8; i++) l[5 + i] = h[i]; l[13] = 0; msg_line(150, l); }
    for (unsigned y = 0; y < 200; y++) { c2p_row320(msgrows + y * 320, buf[0] + y * ROW_BYTES); c2p_row320(msgrows + y * 320, buf[1] + y * ROW_BYTES); }
    for (unsigned i = 0; i < 256; i++) { pal_hi[i] = 0x0FFF; pal_lo[i] = 0x0FFF; }
    pal_hi[0] = 0x0002; pal_lo[0] = 0x0002;
    load_palette(); front = 0; flip_pending = 0; apply_bpl_ptrs(buf[0]);
    for (;;) { }
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

static inline void dbg(u16 c) { R16(COLOR00) = c; }
#ifndef DBG_STOP
#define DBG_STOP 0
#endif
#define STOP(n, col) do { if (DBG_STOP == (n)) { R16(DMACON) = DMA_ALL; R16(BPLCON0) = 0x0200; R16(BPLCON3) = BPLCON3_BASE; for (;;) R16(COLOR00) = (col); } } while (0)
void amiga_main(void)
{
    { const u32 *bi = (const u32 *)(boot_chip + 0x3FF00); sysbase = bi[0]; dosbase = bi[1]; gfxbase = bi[2]; oldview = bi[3]; }
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

    /* system hooks: vertical-blank server (frame flips, palette, clock) and an input.device handler (keyboard) */
    {
        static struct __attribute__((packed, aligned(2))) intr { u32 succ, pred; u8 type, pri; u32 name, data, code; } vbl_is, inp_is;     /* struct Interrupt */
        static const char vname[] = "ArchonUltra VBL", iname[] = "ArchonUltra keys";
        vbl_is.type = 2; vbl_is.pri = 50; vbl_is.name = (u32)vname; vbl_is.data = 0; vbl_is.code = (u32)vbl_server;
        EXEC(-168, 5, 0, 0, (u32)&vbl_is);                     /* AddIntServer(INTB_VERTB, &is) */
        u32 port = EXEC(-666, 0, 0, 0, 0);                    /* CreateMsgPort */
        u32 io = port ? EXEC(-654, 48, 0, port, 0) : 0;       /* CreateIORequest(port, sizeof IOStdReq) */
        static const char indev[] = "input.device";
        if (io && !EXEC(-444, 0, 0, (u32)indev, io)) {        /* OpenDevice("input.device", 0, io, 0) */
            inp_is.type = 2; inp_is.pri = 100; inp_is.name = (u32)iname; inp_is.data = 0; inp_is.code = (u32)input_handler;
            *(volatile u16 *)(io + 28) = 9;                   /* IND_ADDHANDLER */
            *(volatile u32 *)(io + 40) = (u32)&inp_is;         /* io_Data */
            EXEC(-456, 0, 0, 0, io);                          /* DoIO */
        }
    }
    mouse_px = (u8)R16(JOY0DAT); mouse_py = (u8)(R16(JOY0DAT) >> 8);

    STOP(4, 0x0FF0);
    pal_pending = 0;
    R16(DMACON) = SETBIT | DMAEN | BPLEN | COPEN | BLTEN;
    R16(INTENA) = SETBIT | INT_INTEN | INT_VERTB;

    STOP(5, 0x0F0F);
    /* ---------------------------------------------------------------- the PC world */
    u8 *image_end = (u8 *)((u32)_end);
    u8 *w = (u8 *)(((u32)image_end + 0xFFFu) & ~0xFFFu);
    u32 avail = boot_fast + boot_fast_size - (u32)w;
    rt_heap_end = avail - 0x10000; if (rt_heap_end > 0xFFF000u) rt_heap_end = 0xFFF000u;
    rt_heap_reserve = 0x40000;
    { u32 *z = (u32 *)w; u32 words = (rt_heap_end + 0x1000u) / 4; for (u32 i = 0; i < words; i++) z[i] = 0; }      /* fast RAM is not cleared by the system */
    for (u32 i = 0; i < obj1_len; i++) w[OBJ1_BASE + i] = obj1_data[i];
    for (u32 i = 0; i < obj2_len; i++) w[OBJ2_BASE + i] = obj2_data[i];
    for (u32 i = 0; i < obj3_len; i++) w[OBJ3_BASE + i] = obj3_data[i];
    dbg(0x0F80);
    dbg(0x0F00);
    rt_init(w, &ctx);
    rt_build_xl_tab();
    xl_tab[XL_SENTINEL_OFF] = (u32)rt_isr_return;
    dbg(0x00F0); STOP(6, 0x0FFF);

    rt_enter(w, &ctx, OBJ3_ESP_TOP, (void *)xl_entry);
    fatal("THE GAME HAS EXITED", "", "", "", "", rt_dbg_last);
    for (;;) R16(COLOR00) = 0x00F0;
}
