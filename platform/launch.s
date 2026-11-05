| launch.s - AmigaDOS executable stub (user mode, OS stays alive).  Started from S:Startup-Sequence as an ordinary program.
| The hunk holds this code followed by the stage-2 payload.  It
|   1. opens dos.library and graphics.library, 2. allocates the chip block (display, audio) and the largest fast block (leaving the system a margin for
|   its file-system buffers), 3. copies the payload to the top of the fast block, 4. Forbid()s (no pre-emption; Wait() still switches tasks, which the
|   DOS file reads need), blanks the system view (LoadView(NULL)), widens the task's stack bounds (the game runs on its own stacks), and 5. jumps to
|   stage 2 with d0 = fast base, d1 = fast size, d2 = chip block, d3 = chip size.
| Boot info for the platform code is left at chip block + 0x3FF00: SysBase, DOSBase, GfxBase, the previous View.
| Failure colours (COLOR00): 0F80 no chip memory, 0FF0 no fast memory, 00FF missing library, 0FFF 68040/68060.
    .text
start:
    move.l  4.w,%a6
    move.w  296(%a6),%d0        | AttnFlags: bit 3 = 68040, bit 7 = 68060
    move.w  #0x0FFF,%d1
    btst    #3,%d0
    bne     fail
    btst    #7,%d0
    bne     fail
    lea     dosname(%pc),%a1
    moveq   #0,%d0
    jsr     -552(%a6)           | OpenLibrary
    move.l  %d0,%d6             | d6 = DOSBase
    beq     fail_lib
    lea     gfxname(%pc),%a1
    moveq   #0,%d0
    jsr     -552(%a6)
    move.l  %d0,%d7             | d7 = GfxBase
    beq     fail_lib
    move.l  #0x40000,%d0
    move.l  #0x10002,%d1        | MEMF_CHIP|MEMF_CLEAR
    jsr     -198(%a6)           | AllocMem(chip block)
    move.l  %d0,%d5
    beq     fail_mem2
    move.l  %d5,%a0
    lea     0x3FF00(%a0),%a0    | boot info
    move.l  %a6,(%a0)+
    move.l  %d6,(%a0)+
    move.l  %d7,(%a0)+
    move.l  %d7,%a1
    move.l  34(%a1),(%a0)+      | GfxBase->ActiView
    move.l  #0x20004,%d1        | MEMF_FAST|MEMF_LARGEST
    jsr     -216(%a6)           | AvailMem
    sub.l   #0x80000,%d0        | the system keeps 512 KB for its own use (file-system buffers, tasks)
    and.l   #0xFFFFF000,%d0
    move.l  %d0,%d4             | d4 = fast size
    ble     fail_nofast
    move.l  #0x10004,%d1        | MEMF_FAST|MEMF_CLEAR
    jsr     -198(%a6)           | AllocMem(fast)
    move.l  %d0,-(%sp)          | fast base
    beq     fail_nofast
    move.l  payload_len(%pc),%d7
    move.l  %d0,%a2
    add.l   %d4,%a2
    sub.l   %d7,%a2             | a2 = staging = top of the fast block
    move.l  %a2,%a1
    lea     payload(%pc),%a0
    move.l  %d7,%d0
    lsr.l   #2,%d0
2:  move.l  (%a0)+,(%a1)+
    subq.l  #1,%d0
    bne.s   2b
    jsr     -132(%a6)           | Forbid()
    move.l  %d5,%a0
    move.l  0x3FF08(%a0),%a6    | GfxBase
    sub.l   %a1,%a1
    jsr     -222(%a6)           | LoadView(NULL)
    jsr     -270(%a6)           | WaitTOF
    jsr     -270(%a6)
    move.l  4.w,%a6
    sub.l   %a1,%a1
    jsr     -294(%a6)           | FindTask(NULL)
    move.l  %d0,%a0
    clr.l   58(%a0)             | tc_SPLower = 0
    moveq   #-1,%d0
    move.l  %d0,62(%a0)         | tc_SPUpper = 0xFFFFFFFF (the game and the runtime use their own stacks; exec checks the bounds at task switches)
    move.l  %d4,%d1             | d1 = fast size
    move.l  #0x40000,%d3        | d3 = chip block size
    move.l  %d5,%d2             | d2 = chip block
    move.l  %a2,%a0             | a0 = staging = stage-2 code
    move.l  (%sp)+,%d0          | fast base
    jmp     (%a0)

fail_lib:
    move.w  #0x00FF,%d1
    bra     fail
fail_nofast:
    move.w  #0x0FF0,%d1
    bra     fail
fail_mem2:
    move.w  #0x0F80,%d1
fail:
    move.w  #0x7FFF,0xDFF096
1:  move.w  %d1,0xDFF180
    bra.s   1b

dosname: .asciz "dos.library"
gfxname: .asciz "graphics.library"
    .balign 4
payload_len:
    .long   0                   | patched by mkhdfboot.py
payload:                        | the payload follows
