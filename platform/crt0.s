| crt0.s - image entry, reached from stage2 in USER mode (the OS is alive), relocations applied, .bss cleared.
| d0 = fast base, d1 = fast size, d2 = chip block, d3 = chip block size.  Never returns.
    .section .text.start,"ax"
    .globl  _start
_start:
    move.w  #0x0FF0,0xDFF180        | debug: yellow = image running
    move.l  %d0,boot_fast
    move.l  %d1,boot_fast_size
    move.l  %d2,boot_chip
    move.l  %d3,boot_chip_size
    move.l  %d2,%a0
    lea     0x3FF00(%a0),%sp        | start stack: just below the boot info at the top of the chip block
    jsr     amiga_main
1:  bra.s   1b
    .bss
    .globl boot_fast, boot_fast_size, boot_chip, boot_chip_size
boot_fast: .space 4
boot_fast_size: .space 4
boot_chip: .space 4
boot_chip_size: .space 4
