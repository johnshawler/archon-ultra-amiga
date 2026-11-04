| launch.s - AmigaDOS executable stub for the single-HDF boot (Kickstart 3.1 boots hard disks through the filesystem, not through a bootblock).
| Started from S:Startup-Sequence as an ordinary program.  The hunk holds this code followed by the stage-2 payload (as on the ADF).  It does what the
| bootblock does without any disk I/O: refuse 68040/68060, allocate the chip block and the largest fast block, copy the payload to the top of the
| fast block, then Supervisor(): quiesce the machine and jump to stage 2 with d0 = fast base, d1 = fast size, d2 = chip block, d3 = chip size.
    .text
start:
    move.l  4.w,%a6             | ExecBase
    move.w  296(%a6),%d0        | AttnFlags: bit 3 = 68040, bit 7 = 68060
    move.w  #0x00FF,%d1
    btst    #3,%d0
    bne     fail
    btst    #7,%d0
    bne     fail
    move.l  #0x40000,%d0
    move.l  #0x10002,%d1        | MEMF_CHIP|MEMF_CLEAR
    jsr     -198(%a6)           | AllocMem(chip block: display, audio, stacks)
    move.l  %d0,%d5
    beq     fail_mem2
    move.l  #0x20004,%d1        | MEMF_FAST|MEMF_LARGEST
    jsr     -216(%a6)           | AvailMem
    sub.l   #0x8000,%d0
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
    move.l  %a2,%d6             | staging (fast)
    move.l  (%sp)+,%d0          | fast base
    lea     super(%pc),%a5
    jsr     -30(%a6)            | Supervisor(a5) - never returns

fail_nofast:
    move.w  #0x0FF0,%d1
    bra     fail
fail_mem2:
    move.w  #0x0F80,%d1
fail:
    move.w  #0x7FFF,0xDFF096
    move.w  #0x7FFF,0xDFF09A
1:  move.w  %d1,0xDFF180
    bra.s   1b

super:                          | supervisor mode: d0 = fast base, d4 = fast size, d5 = chip block, d6 = staging
    move.w  #0x2700,%sr
    move.w  #0x7FFF,0xDFF09A
    move.w  #0x7FFF,0xDFF09C
    move.b  0xDFF002,%d1
2:  btst    #6,0xDFF002         | wait for the Blitter
    bne.s   2b
    move.w  #0x7FFF,0xDFF096
    move.w  #0x7FFF,0xDFF09C
    move.l  #0x0808,%d1         | CACR: clear and disable both caches
    movec   %d1,%cacr
    move.l  %d4,%d1             | d1 = fast size
    move.l  #0x40000,%d3        | d3 = chip block size
    move.l  %d5,%d2             | d2 = chip block
    move.l  %d6,%a0             | a0 = staging = stage-2 code
    move.l  %d5,%a1
    lea     (%a1,%d3.l),%sp     | temporary stack: top of the chip block
    jmp     (%a0)
    .balign 4
payload_len:
    .long   0                   | patched by mkhunk.py
payload:                        | the payload follows
