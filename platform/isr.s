| isr.s - glue between the game's platform code and the running OS.
    .text
| Interrupt server entry (exec server chain): A1 = is_Data, A5 = is_Code, A6 = ExecBase.  The OS acknowledges the interrupt; Z set = keep going down the chain.
    .globl  vbl_server
vbl_server:
    move.l  %sp,-(%sp)
    jsr     plat_isr_vbl_c
    addq.l  #4,%sp
    moveq   #0,%d0
    rts

| input.device handler: A0 = event chain, A1 = is_Data.  Returns the chain unchanged in D0.
    .globl  input_handler
input_handler:
    move.l  %a0,-(%sp)
    jsr     input_handler_c
    addq.l  #4,%sp
    rts

| u32 os_call(void *base, s32 lvo, u32 d0, u32 d1, u32 d2, u32 d3, u32 a0, u32 a1) - call a library function, result in D0.
    .globl  os_call
os_call:
    movem.l %d2-%d3/%a2/%a6,-(%sp)
    move.l  20(%sp),%a6
    move.l  24(%sp),%a2
    move.l  28(%sp),%d0
    move.l  32(%sp),%d1
    move.l  36(%sp),%d2
    move.l  40(%sp),%d3
    move.l  44(%sp),%a0
    move.l  48(%sp),%a1
    jsr     (%a6,%a2.l)
    movem.l (%sp)+,%d2-%d3/%a2/%a6
    rts

    .data
    .globl  dbg_pc
dbg_pc: .long 0
