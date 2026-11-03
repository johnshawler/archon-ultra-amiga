| stage2.s - first bytes of the payload (ADF byte 1024).  Position independent; runs in chip RAM from the staging buffer, in supervisor mode,
| caches off, all DMA/interrupts off.  d0 = fast base, d1 = fast size, d2 = chip block, d3 = chip block size, a0 = this code.
| Header (offsets from the payload start):  4 comp_off  8 comp_len  12 raw_len  16 bss_len  20 entry_off  24 reloc_off  28 nreloc
|   1. LZ4-expand the image to the fast block   2. apply the relocations (every listed long += fast base)   3. clear .bss   4. jump to the entry.
    .text
    .globl stage2_start
stage2_start:
    bra.w   stage2
    .long   0, 0, 0, 0, 0, 0, 0                | header, patched by mkimage.py (comp_off comp_len raw_len bss_len entry reloc_off nreloc)
stage2:
    move.w  #0x0F00,0xDFF180                   | debug: red = stage 2 started
    move.l  %a0,%a3                            | a3 = header base
    move.l  %d0,%a1                            | dst
    move.l  4(%a3),%d4
    lea     (%a3,%d4.l),%a0                    | src
    move.l  8(%a3),%d4
    lea     (%a0,%d4.l),%a2                    | src end
1:  moveq   #0,%d4
    move.b  (%a0)+,%d4                         | token
    move.l  %d4,%d5
    lsr.l   #4,%d5                             | literal length
    cmp.l   #15,%d5
    bne.s   3f
2:  moveq   #0,%d6
    move.b  (%a0)+,%d6
    add.l   %d6,%d5
    cmp.b   #255,%d6
    beq.s   2b
3:  tst.l   %d5
    beq.s   5f
4:  move.b  (%a0)+,(%a1)+
    subq.l  #1,%d5
    bne.s   4b
5:  cmp.l   %a0,%a2
    bls.s   9f                                 | all input consumed
    moveq   #0,%d6
    move.b  (%a0)+,%d6                         | offset low
    moveq   #0,%d5
    move.b  (%a0)+,%d5
    lsl.w   #8,%d5
    or.w    %d5,%d6                            | offset
    move.l  %d4,%d5
    and.l   #15,%d5                            | match length
    cmp.l   #15,%d5
    bne.s   7f
6:  moveq   #0,%d7
    move.b  (%a0)+,%d7
    add.l   %d7,%d5
    cmp.b   #255,%d7
    beq.s   6b
7:  addq.l  #4,%d5
    move.l  %a1,%a4
    sub.l   %d6,%a4
8:  move.b  (%a4)+,(%a1)+
    subq.l  #1,%d5
    bne.s   8b
    bra.s   1b
9:  move.w  #0x00F0,0xDFF180                   | debug: green = expanded
                                              | relocations
    move.l  %d0,%a4                            | image base
    move.l  24(%a3),%d4
    lea     (%a4,%d4.l),%a5                    | table
    move.l  28(%a3),%d5
    beq.s   11f
10: move.l  (%a5)+,%d6
    add.l   %d0,(%a4,%d6.l)
    subq.l  #1,%d5
    bne.s   10b
11: move.l  12(%a3),%d4                        | clear bss after the raw image
    lea     (%a4,%d4.l),%a5
    move.l  16(%a3),%d5
    lsr.l   #2,%d5
    beq.s   13f
12: clr.l   (%a5)+
    subq.l  #1,%d5
    bne.s   12b
13: move.w  #0x000F,0xDFF180                   | debug: blue = relocated
    move.l  20(%a3),%d4
    lea     (%a4,%d4.l),%a0
    jmp     (%a0)
