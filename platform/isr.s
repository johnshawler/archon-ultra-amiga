| isr.s - interrupt entry stubs.  Bodies are C functions in platform.c.  The game runs in master mode (SR.M = 1), so these always run on the
| interrupt stack and never touch the x86 stack of the translated program.
    .text
    .macro  ISR name, cfunc
    .globl  \name
\name:
    movem.l %d0-%d1/%a0-%a1,-(%sp)
    jsr     \cfunc
    movem.l (%sp)+,%d0-%d1/%a0-%a1
    rte
    .endm
    .globl isr_vbl, dbg_pc
isr_vbl:
    movem.l %d0-%d1/%a0-%a1,-(%sp)
    move.l  18(%sp),dbg_pc
    jsr     plat_isr_vbl_c
    movem.l (%sp)+,%d0-%d1/%a0-%a1
    rte
    .data
dbg_pc: .long 0
    .text
    ISR isr_ciab, plat_isr_ciab_c       | level 6  CIA-B (time base)
    ISR isr_ports, plat_isr_ports_c     | level 2  CIA-A (keyboard)
    ISR isr_aud,  plat_isr_audio_c      | level 4  Paula audio
    ISR isr_default, plat_isr_default_c

    .globl exc_common
exc_common:                         | d0 = vector number; the exception frame is at (sp)
    move.w  #0x2700,%sr
    move.l  %sp,-(%sp)
    move.l  %d0,-(%sp)
    jsr     plat_crash_c
1:  bra.s   1b
    .macro EXC n
    .globl exc_\n
exc_\n:
    moveq   #\n,%d0
    bra     exc_common
    .endm
    EXC 2
    EXC 3
    EXC 4
    EXC 5
    EXC 6
    EXC 7
    EXC 8
    EXC 9
    EXC 10
    EXC 11
    EXC 12
    EXC 13
    EXC 14
    EXC 15
    EXC 24
    EXC 25
    EXC 26
    EXC 27
    EXC 28
    EXC 29
    EXC 30
    EXC 31
    EXC 32
    EXC 33
    EXC 34
    EXC 35
    EXC 36
    EXC 37
    EXC 38
    EXC 39
    EXC 40
    EXC 41
    EXC 42
    EXC 43
    EXC 44
    EXC 45
    EXC 46
    EXC 47
    .data
    .globl exc_table
exc_table:
    .long isr_default
    .long isr_default
    .long exc_2
    .long exc_3
    .long exc_4
    .long exc_5
    .long exc_6
    .long exc_7
    .long exc_8
    .long exc_9
    .long exc_10
    .long exc_11
    .long exc_12
    .long exc_13
    .long exc_14
    .long exc_15
    .long isr_default
    .long isr_default
    .long isr_default
    .long isr_default
    .long isr_default
    .long isr_default
    .long isr_default
    .long isr_default
    .long exc_24
    .long exc_25
    .long exc_26
    .long exc_27
    .long exc_28
    .long exc_29
    .long exc_30
    .long exc_31
    .long exc_32
    .long exc_33
    .long exc_34
    .long exc_35
    .long exc_36
    .long exc_37
    .long exc_38
    .long exc_39
    .long exc_40
    .long exc_41
    .long exc_42
    .long exc_43
    .long exc_44
    .long exc_45
    .long exc_46
    .long exc_47
