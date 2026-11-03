/* Runtime for the translated Archon Ultra (DOS/4GW) program: context, services, hardware models. */
#ifndef RT_H
#define RT_H
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef signed int s32; typedef signed short s16; typedef signed char s8;
typedef unsigned long long u64;

/* Context pointed to by a6 in translated code (offsets are used by the translator: keep in step with xl.py CTX). */
struct ctx {
    s32 poll;            /* 0  */
    u8  iflag;           /* 4  */
    u8  pad4[3];
    u32 es_base;         /* 8  */
    u32 fs_base;         /* 12 */
    u32 gs_base;         /* 16 */
    u32 ds_base;         /* 20 */
    u8  df;              /* 24 */
    u8  pad24[3];
    u16 ccr_save;        /* 28 */
    u16 pad28;
    u32 tmp[4];          /* 32..47 */
    u32 saved_regs[8];   /* 48.. */
    u16 sel[6];          /* 80: es cs ss ds fs gs selector values */
    u32 pad92;
    u32 regs[8];         /* 96: register save area used by the asm stubs */
    u32 pad128[2];
    u16 ccr_stub;        /* 136 */
};

/* Registers as passed to C by the asm stubs (d0..d6 = eax ecx edx ebx ebp esi edi). */
struct regs { u32 eax, ecx, edx, ebx, ebp, esi, edi; };

extern u8 *WIN;                 /* a5: x86 linear address L is at WIN+L */
extern struct ctx *CTX;

/* 68k CCR bits */
#define CCR_C 1
#define CCR_V 2
#define CCR_Z 4
#define CCR_N 8

static inline u32 ld32(u32 a) { u8 *p = WIN + a; return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
static inline u16 ld16(u32 a) { u8 *p = WIN + a; return p[0] | (p[1] << 8); }
static inline u8 ld8(u32 a) { return WIN[a]; }
static inline void st32(u32 a, u32 v) { u8 *p = WIN + a; p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static inline void st16(u32 a, u16 v) { u8 *p = WIN + a; p[0] = v; p[1] = v >> 8; }
static inline void st8(u32 a, u8 v) { WIN[a] = v; }

/* platform interface (plat_*.c) */
void plat_log(const char *fmt, ...);
void plat_exit(int code);
long plat_file_open(const char *name);        /* returns handle >=0 or -1 */
long plat_file_read(long h, void *buf, u32 n);
long plat_file_seek(long h, long off, int whence);
void plat_file_close(long h);
u32  plat_time_us(void);                      /* monotonic microseconds (virtual in the test harness) */

/* services */
int  rt_c_int(u32 vec, struct regs *r);       /* returns carry */
u32  rt_c_in(u32 port, int size);
void rt_c_out(u32 port, u32 val, int size);
u32  rt_c_str(int op, struct regs *r);        /* returns 68k ccr bits (scas/cmps) */
u32  rt_c_poll(struct regs *r);
void rt_c_badtarget(u32 off, u32 ret);
void plat_audio_push(const unsigned char *d, u32 n, u32 rate, int is16, int stereo, int sign);
void plat_vsync(u32 frame);
void rt_init(u8 *win, struct ctx *ctx);
void rt_enter(u8 *win, struct ctx *ctx, u32 esp, void *entry);
void hw_key(u8 sc);
void rt_mouse_set(u32 x, u32 y, u32 b);
void vga_get_display(u32 *start, u32 *pitch, int *chain4);
u32 vga_pan(void);
u32 hw_now(void);
void rt_c_unimpl(u32 where);
u32  rt_c_trace(u32 addr, struct regs *r);
void hw_advance(u32 now_us);
u32  rt_mk_eflags(u32 ccr);
u32  rt_set_eflags(u32 v);                    /* returns ccr */
void rt_c_setseg(int which, u32 sel);

/* vga */
void vga_init(void);
u32  vga_read(u32 addr, int size);            /* addr = linear 0xA0000.. */
void vga_write(u32 addr, u32 val, int size);
int  vga_in_window(u32 a);
u32  vga_port_in(u32 port, int size, int *handled);
int  vga_port_out(u32 port, u32 val, int size);
extern u8 vga_mem[262144];
extern u32 vga_gen, vga_dac_gen;
extern u8 vga_dac[768];
void vga_vsync(void);
/* hw */
void hw_init(void);
void hw_advance(u32 now_us);
u32  hw_port_in(u32 port, int size, int *handled);
int  hw_port_out(u32 port, u32 val, int size);
int  hw_irq_pending(void);
int  hw_irq_ack(void);
extern void (*hw_deliver)(int vec);
void rt_deliver_irq(int vec);
extern const unsigned char XL_ENTRY_DUMMY;
#endif
