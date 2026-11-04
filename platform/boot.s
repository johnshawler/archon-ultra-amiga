| boot.s - Archon Ultra A1200 bootblock (KS 3.x, 68020+).  Must stay <= 1024 bytes.
|
| Entered by the OS strap with A6 = ExecBase, A1 = trackdisk IOStdReq (open, DF0:).
| 1. refuse 68040/68060.  2. AllocMem a chip staging buffer, a chip block for the display/audio buffers, and the largest fast block.
| 3. trackdisk CMD_READ: ADF bytes 1024.. -> staging buffer.  4. Supervisor(): quiesce the machine and jump to the stage-2 code at the
| start of the staging buffer (position independent): d0 = fast base, d1 = fast size, d2 = chip block, d3 = chip block size.
| The OS is never used again.  Failure colours: magenta = no memory, red = disk error, cyan = 68040/68060, yellow = no fast RAM.
    .text
    .long   0x444F5300          | 'DOS\0'
    .long   0                   | checksum (filled in by mkadf.py)
    .long   880                 | root block
entry:                          | offset 12
    bra.s   start
    .word   0
    .long   0x41524348          | offset 16: marker 'ARCH'
payload_len:
    .long   0                   | offset 20: patched
payload_off:
    .long   1024                | offset 24: absolute byte offset of the payload on the boot device (1024 on the ADF; patched for the HDF)

start:
    move.l  %a1,%a4             | a4 = IO request
    move.w  296(%a6),%d0        | ExecBase->AttnFlags: bit 3 = 68040, bit 7 = 68060
    move.w  #0x00FF,%d1
    btst    #3,%d0
    bne     fail
    btst    #7,%d0
    bne     fail
    move.l  #0x10000,%d0
    moveq   #2,%d1              | MEMF_CHIP
    jsr     -198(%a6)           | AllocMem(64 KB chip bounce buffer: trackdisk can only DMA to chip RAM)
    move.l  %d0,%d6
    beq     fail_mem
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
    move.l  %a2,-(%sp)
    moveq   #0,%d3              | d3 = bytes done
rd: move.l  %d7,%d0
    sub.l   %d3,%d0
    beq.s   rdone
    cmp.l   #0x10000,%d0
    bls.s   1f
    move.l  #0x10000,%d0
1:  move.l  %d0,%d2             | d2 = chunk
    move.w  #2,28(%a4)          | io_Command = CMD_READ
    move.l  %d2,36(%a4)         | io_Length
    move.l  %d6,40(%a4)         | io_Data
    move.l  payload_off(%pc),%a0
    lea     (%a0,%d3.l),%a0
    move.l  %a0,44(%a4)         | io_Offset
    move.l  %a4,%a1
    jsr     -456(%a6)           | DoIO
    tst.b   31(%a4)
    bne     fail_read
    move.l  %d6,%a0
    move.l  (%sp),%a1
    lea     (%a1,%d3.l),%a1
    move.l  %d2,%d0
    lsr.l   #2,%d0
2:  move.l  (%a0)+,(%a1)+
    subq.l  #1,%d0
    bne.s   2b
    add.l   %d2,%d3
    bra.s   rd
rdone:
    move.w  #9,28(%a4)          | TD_MOTOR off
    clr.l   36(%a4)
    move.l  %a4,%a1
    jsr     -456(%a6)
    move.l  (%sp)+,%d6          | staging (fast)
    move.l  (%sp)+,%d0          | fast base
    lea     super(%pc),%a5
    jsr     -30(%a6)            | Supervisor(a5) - never returns

fail_nofast:
    move.w  #0x0FF0,%d1
    bra     fail
fail_mem2:
    move.w  #0x0F80,%d1
    bra     fail
fail_mem:
    move.w  #0x0F0F,%d1
    bra     fail
fail_read:
    move.w  #0x0F00,%d1
fail:
    move.w  #0x7FFF,0xDFF096
    move.w  #0x7FFF,0xDFF09A
1:  move.w  %d1,0xDFF180
    bra.s   1b

super:                          | supervisor mode: d0 = fast base, d4 = fast size, d5 = chip block, d6 = staging, d7 = length
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
