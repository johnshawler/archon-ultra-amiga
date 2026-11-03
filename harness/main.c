/* Linux/qemu-m68k test harness: loads the relocated LE objects, runs the translated game with the runtime, dumps VGA frames. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <ucontext.h>
#include "../runtime/rt.h"
#include "layout.h"

static u8 win[0x1000000 + 0x2000] __attribute__((aligned(4096)));
static struct ctx ctx;
extern u32 xl_entry;
extern void rt_build_xl_tab(void); extern u32 xl_tab[];
static const char *dir = "."; static FILE *fdump, *faudio; static u32 max_frames = 600; static u32 prev_hash;

void plat_log(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
void plat_exit(int code) { fflush(NULL); if (fdump) fclose(fdump); _exit(code); }
long plat_file_open(const char *name) {
    char p[512], l[128]; int i = 0; for (; name[i] && i < 127; i++) l[i] = tolower((unsigned char)name[i]); l[i] = 0;
    snprintf(p, sizeof p, "%s/%s", dir, l); int fd = open(p, O_RDONLY); plat_log("harness: open \"%s\" -> %d\n", name, fd); if (fd < 0) plat_log("harness: cannot open %s\n", p); return fd;
}
long plat_file_read(long h, void *buf, u32 n) { return read((int)h, buf, n); }
long plat_file_seek(long h, long off, int wh) { return lseek((int)h, off, wh); }
void plat_file_close(long h) { close((int)h); }
u32 plat_time_us(void) { return 0; }
void plat_audio_push(const unsigned char *d, u32 n, u32 rate, int is16, int stereo, int sign) {
    if (!faudio) return; static u32 hdr_done; (void)hdr_done; fwrite(d, 1, n, faudio);
}
static u32 fnv(const u8 *p, u32 n, u32 h) { while (n--) { h ^= *p++; h *= 16777619u; } return h; }
void plat_vsync(u32 frame) {
    u32 start, pitch; int c4; vga_get_display(&start, &pitch, &c4);
    u32 h = fnv(vga_mem, sizeof vga_mem, 2166136261u); h = fnv(vga_dac, 768, h); h ^= start * 31 + pitch;
    if (h != prev_hash && fdump) {
        u32 hdr[6] = { frame, 262144, 320 | (200 << 16), start / 4, (pitch / 8) | (3 << 16), vga_pan() };
        fwrite(hdr, 4, 6, fdump); fwrite(vga_dac, 1, 768, fdump); fwrite(vga_mem, 1, 262144, fdump); prev_hash = h;
    }
    if (frame >= max_frames) { plat_log("harness: reached %u frames\n", frame); plat_exit(0); }
}

static void segv(int sig, siginfo_t *si, void *uc_) {
    ucontext_t *uc = uc_; unsigned long *g = (unsigned long *)uc->uc_mcontext.gregs;
    fprintf(stderr, "SEGV addr=%p pc=%lx a0=%lx a1=%lx a7=%lx d7=%lx\n", si->si_addr, g[16], g[8], g[9], g[15], g[7]);
    fflush(NULL); _exit(9);
}
int main(int argc, char **argv) {
    { struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = segv; sa.sa_flags = SA_SIGINFO; sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0); sigaction(SIGILL, &sa, 0); sigaction(SIGFPE, &sa, 0); }
    if (argc > 1) dir = argv[1];
    if (argc > 2) max_frames = atoi(argv[2]);
    const char *dn = argc > 3 ? argv[3] : "dump.bin"; fdump = fopen(dn, "wb");
    if (argc > 4) faudio = fopen(argv[4], "wb");
    static const char *objs[3] = { "obj1.bin", "obj2.bin", "obj3.bin" }; static const u32 bases[3] = { OBJ1_BASE, OBJ2_BASE, OBJ3_BASE };
    for (int i = 0; i < 3; i++) {
        char p[512]; snprintf(p, sizeof p, "%s/%s", dir, objs[i]); int fd = open(p, O_RDONLY);
        if (fd < 0) { plat_log("harness: missing %s\n", p); return 1; }
        long n = read(fd, win + bases[i], 0x40000); close(fd); (void)n;
    }
    rt_build_xl_tab();
    { extern void rt_isr_return(void); extern void rt_trace_isr_return(void); xl_tab[XL_SENTINEL_OFF] = (u32)rt_isr_return; xl_tab[XL_SENTINEL_TRACE_OFF] = (u32)rt_trace_isr_return; }
    rt_init(win, &ctx);
    rt_enter(win, &ctx, OBJ3_ESP_TOP, (void *)xl_entry);
    plat_log("harness: program returned\n");
    plat_exit(0); return 0;
}
