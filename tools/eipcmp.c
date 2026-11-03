/* eipcmp dosbox.bin ours.bin [max]: collapse short repeating periods (spin loops, whose iteration counts are timing dependent) and compare
   the EIP streams.  dosbox.bin is little endian u32 per instruction, ours.bin is big endian. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

static uint32_t *load(const char *p, size_t *n, int be) {
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint32_t *a = malloc(sz); if (fread(a, 1, sz, f) != (size_t)sz) exit(1); fclose(f); *n = sz / 4;
    if (be) for (size_t i = 0; i < *n; i++) a[i] = __builtin_bswap32(a[i]);
    return a;
}
static size_t collapse(uint32_t *a, size_t n, size_t *raw) {
    size_t m = 0;
    for (size_t i = 0; i < n; i++) {
        raw[m] = i; a[m++] = a[i];
        for (int p = 1; p <= 6; p++)
            if (m >= (size_t)(2 * p) && !memcmp(a + m - p, a + m - 2 * p, p * 4)) { m -= p; break; }
    }
    return m;
}
int main(int argc, char **argv) {
    size_t nd, no;
    uint32_t *d = load(argv[1], &nd, 0), *o = load(argv[2], &no, 1);
    if (argc > 3) { size_t m = atol(argv[3]); if (m < nd) nd = m; if (m < no) no = m; }
    size_t *rd = malloc(nd * sizeof(size_t)), *ro = malloc(no * sizeof(size_t));
    size_t cd = collapse(d, nd, rd), co = collapse(o, no, ro), n = cd < co ? cd : co;
    printf("collapsed lengths %zu %zu\n", cd, co);
    /* DOSBox does not trace an interrupt handler that it runs inside its nested INT 21h processing; we do. Skip our handler run (entry 'hv') when the
       DOSBox stream has none and resumes at the instruction we reach after it. */
    uint32_t hv = argc > 4 ? strtoul(argv[4], 0, 16) : 0x208d6; size_t skips = 0, kd = 0, ko = 0;
    while (kd < cd && ko < co) {
        if (d[kd] == o[ko]) { kd++; ko++; continue; }
        if (o[ko] == hv) {
            size_t j = ko; while (j < co && j < ko + 20000 && o[j] != d[kd]) j++;
            if (j < co && o[j] == d[kd]) { skips++; printf("skipped our handler run at raw %zu (%zu steps)\n", ro[ko], j - ko); ko = j; continue; }
        }
        printf("EIP diverges at collapsed steps dosbox %zu ours %zu: dosbox %x ours %x (obj1 offsets); raw index dosbox %zu ours %zu\ncontext dosbox:", kd, ko, d[kd], o[ko], rd[kd], ro[ko]);
        for (size_t t = kd > 12 ? kd - 12 : 0; t < kd + 4 && t < cd; t++) printf(" %x", d[t]);
        printf("\ncontext ours  :");
        for (size_t t = ko > 12 ? ko - 12 : 0; t < ko + 4 && t < co; t++) printf(" %x", o[t]);
        printf("\n");
        return 1;
    }
    printf("no divergence: %zu / %zu collapsed steps compared, %zu untraced-handler skips\n", kd, ko, skips);
    return 0;
}
