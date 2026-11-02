| Assembly stubs between translated code and the C runtime.  GNU as, MIT syntax, 68020/030.
| Calling conventions from translated code:
|   rt_int_NN   : INT NN. d0..d6 = x86 regs; service may change them; carry returned in CCR.C
|   rt_in8/16/32: a0 = port ; result in d7
|   rt_out8/16/32: a0 = port, d7 = value
|   rt_<str>    : string instructions; esi=d5 edi=d6 ecx=d1 eax=d0; direction flag in 24(%a6)
	.text
	.even
	.globl rt_start_game

| rt_enter: call the translated program entry.  C calls: rt_enter(win, ctx, esp, entry)
	.globl rt_enter
rt_enter:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 44+4(%sp),%a5         | win
	move.l 44+8(%sp),%a6         | ctx
	move.l 44+16(%sp),%a0        | entry address
	move.l 44+12(%sp),%d0        | esp (offset)
	move.l %sp,saved_csp
	lea (%a5,%d0.l),%sp
	moveq #0,%d0
	moveq #0,%d1
	moveq #0,%d2
	moveq #0,%d3
	moveq #0,%d4
	moveq #0,%d5
	moveq #0,%d6
	jsr (%a0)
	| translated main returned: leave
	move.l saved_csp,%sp
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
	.globl rt_leave
rt_leave:
	move.l saved_csp,%sp
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
	.bss
saved_csp: .space 4
	.text

	.globl rt_int_10
rt_int_10:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x10,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_1a
rt_int_1a:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x1a,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_21
rt_int_21:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x21,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_31
rt_int_31:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x31,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_33
rt_int_33:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x33,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_08
rt_int_08:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x8,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_09
rt_int_09:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x9,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_15
rt_int_15:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x15,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_16
rt_int_16:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x16,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_2f
rt_int_2f:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x2f,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_67
rt_int_67:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x67,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_03
rt_int_03:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x3,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_00
rt_int_00:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x0,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_1c
rt_int_1c:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x1c,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_23
rt_int_23:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x23,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_24
rt_int_24:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x24,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int_70
rt_int_70:
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0x70,-(%sp)
	jbsr rt_c_int
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	lsr.l #1,%d7                 | C = carry returned by the service
	rts

	.globl rt_int3
rt_int3:
	rts

	.globl rt_in8
rt_in8:
	movem.l %d0-%d1,-(%sp)
	move.l #1,-(%sp)
	move.l %a0,%d0
	and.l #0xffff,%d0
	move.l %d0,-(%sp)
	jbsr rt_c_in
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d1
	rts

	.globl rt_out8
rt_out8:
	movem.l %d0-%d1,-(%sp)
	move.l #1,-(%sp)
	move.l %d7,-(%sp)
	move.l %a0,%d0
	and.l #0xffff,%d0
	move.l %d0,-(%sp)
	jbsr rt_c_out
	lea 12(%sp),%sp
	movem.l (%sp)+,%d0-%d1
	rts

	.globl rt_in16
rt_in16:
	movem.l %d0-%d1,-(%sp)
	move.l #2,-(%sp)
	move.l %a0,%d0
	and.l #0xffff,%d0
	move.l %d0,-(%sp)
	jbsr rt_c_in
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d1
	rts

	.globl rt_out16
rt_out16:
	movem.l %d0-%d1,-(%sp)
	move.l #2,-(%sp)
	move.l %d7,-(%sp)
	move.l %a0,%d0
	and.l #0xffff,%d0
	move.l %d0,-(%sp)
	jbsr rt_c_out
	lea 12(%sp),%sp
	movem.l (%sp)+,%d0-%d1
	rts

	.globl rt_in32
rt_in32:
	movem.l %d0-%d1,-(%sp)
	move.l #4,-(%sp)
	move.l %a0,%d0
	and.l #0xffff,%d0
	move.l %d0,-(%sp)
	jbsr rt_c_in
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d1
	rts

	.globl rt_out32
rt_out32:
	movem.l %d0-%d1,-(%sp)
	move.l #4,-(%sp)
	move.l %d7,-(%sp)
	move.l %a0,%d0
	and.l #0xffff,%d0
	move.l %d0,-(%sp)
	jbsr rt_c_out
	lea 12(%sp),%sp
	movem.l (%sp)+,%d0-%d1
	rts

	.globl rt_movsb
rt_movsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #0,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_movsw
rt_movsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #1,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_movsd
rt_movsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #2,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_stosb
rt_stosb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #3,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_stosw
rt_stosw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #4,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_stosd
rt_stosd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #5,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_lodsb
rt_lodsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #6,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_lodsw
rt_lodsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #7,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_lodsd
rt_lodsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #8,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_scasb
rt_scasb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #9,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_scasw
rt_scasw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #10,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_scasd
rt_scasd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #11,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_cmpsb
rt_cmpsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #12,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_cmpsw
rt_cmpsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #13,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_cmpsd
rt_cmpsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #14,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_outsb
rt_outsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #15,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_outsw
rt_outsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #16,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_outsd
rt_outsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #17,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_movsb
rt_rep_movsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #18,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_movsw
rt_rep_movsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #19,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_movsd
rt_rep_movsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #20,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_stosb
rt_rep_stosb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #21,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_stosw
rt_rep_stosw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #22,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_stosd
rt_rep_stosd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #23,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_lodsb
rt_rep_lodsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #24,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_lodsw
rt_rep_lodsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #25,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_lodsd
rt_rep_lodsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #26,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_scasb
rt_rep_scasb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #27,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_scasw
rt_rep_scasw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #28,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_scasd
rt_rep_scasd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #29,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_cmpsb
rt_rep_cmpsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #30,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_cmpsw
rt_rep_cmpsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #31,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_cmpsd
rt_rep_cmpsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #32,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_outsb
rt_rep_outsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #33,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_outsw
rt_rep_outsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #34,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_rep_outsd
rt_rep_outsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #35,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_movsb
rt_repe_movsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #36,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_movsw
rt_repe_movsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #37,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_movsd
rt_repe_movsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #38,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_stosb
rt_repe_stosb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #39,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_stosw
rt_repe_stosw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #40,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_stosd
rt_repe_stosd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #41,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_lodsb
rt_repe_lodsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #42,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_lodsw
rt_repe_lodsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #43,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_lodsd
rt_repe_lodsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #44,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_scasb
rt_repe_scasb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #45,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_scasw
rt_repe_scasw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #46,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_scasd
rt_repe_scasd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #47,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_cmpsb
rt_repe_cmpsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #48,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_cmpsw
rt_repe_cmpsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #49,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_cmpsd
rt_repe_cmpsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #50,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_outsb
rt_repe_outsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #51,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_outsw
rt_repe_outsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #52,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repe_outsd
rt_repe_outsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #53,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_movsb
rt_repne_movsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #54,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_movsw
rt_repne_movsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #55,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_movsd
rt_repne_movsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #56,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_stosb
rt_repne_stosb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #57,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_stosw
rt_repne_stosw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #58,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_stosd
rt_repne_stosd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #59,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_lodsb
rt_repne_lodsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #60,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_lodsw
rt_repne_lodsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #61,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_lodsd
rt_repne_lodsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #62,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_scasb
rt_repne_scasb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #63,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_scasw
rt_repne_scasw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #64,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_scasd
rt_repne_scasd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #65,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_cmpsb
rt_repne_cmpsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #66,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_cmpsw
rt_repne_cmpsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #67,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_cmpsd
rt_repne_cmpsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #68,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 1
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_outsb
rt_repne_outsb:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #69,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_outsw
rt_repne_outsw:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #70,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_repne_outsd
rt_repne_outsd:
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	move.l #71,-(%sp)
	jbsr rt_c_str
	addq.l #8,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d6
	| scas/cmps/repe/repne variants define flags (d7 = ccr value); others restore the caller's
	.if 0
	addq.l #2,%sp
	move.w %d7,%ccr
	.else
	move.w (%sp)+,%ccr
	.endif
	rts

	.globl rt_poll
rt_poll:				| translated code reached a loop head with the poll counter below zero
	move.w %ccr,-(%sp)
	movem.l %d0-%d6,-(%sp)
	move.l %sp,%a0
	move.l %a0,-(%sp)
	jbsr rt_c_poll
	addq.l #4,%sp
	movem.l (%sp)+,%d0-%d6
	move.w (%sp)+,%ccr
	rts

	.globl rt_unimpl
rt_unimpl:
	move.l (%sp),%a0
	move.l (%a0),-(%sp)
	jbsr rt_c_unimpl
	rts
	.globl rt_badjump
rt_badjump:
	move.l (%sp),%a0
	move.l (%a0),-(%sp)
	jbsr rt_c_unimpl
	rts

	.globl rt_pushfd
rt_pushfd:
	move.w %ccr,%d7
	movem.l %d0-%d1,-(%sp)
	and.l #0xff,%d7
	move.l %d7,-(%sp)
	jbsr rt_mk_eflags
	addq.l #4,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d1
	ror.w #8,%d7
	swap %d7
	ror.w #8,%d7
	move.l 0(%sp),%a0            | return address
	addq.l #4,%sp
	move.l %d7,-(%sp)
	jmp (%a0)
	.globl rt_popfd
rt_popfd:
	move.l (%sp)+,%a0            | return address
	move.l (%sp)+,%d7
	ror.w #8,%d7
	swap %d7
	ror.w #8,%d7
	movem.l %d0-%d1,-(%sp)
	move.l %d7,-(%sp)
	jbsr rt_set_eflags
	addq.l #4,%sp
	move.l %d0,%d7
	movem.l (%sp)+,%d0-%d1
	move.w %d7,%ccr
	jmp (%a0)

	.globl rt_setseg_es
rt_setseg_es:
	movem.l %d0-%d1,-(%sp)
	and.l #0xffff,%d7
	move.l %d7,-(%sp)
	move.l #0,-(%sp)
	jbsr rt_c_setseg
	addq.l #8,%sp
	movem.l (%sp)+,%d0-%d1
	rts

	.globl rt_setseg_fs
rt_setseg_fs:
	movem.l %d0-%d1,-(%sp)
	and.l #0xffff,%d7
	move.l %d7,-(%sp)
	move.l #1,-(%sp)
	jbsr rt_c_setseg
	addq.l #8,%sp
	movem.l (%sp)+,%d0-%d1
	rts

	.globl rt_setseg_gs
rt_setseg_gs:
	movem.l %d0-%d1,-(%sp)
	and.l #0xffff,%d7
	move.l %d7,-(%sp)
	move.l #2,-(%sp)
	jbsr rt_c_setseg
	addq.l #8,%sp
	movem.l (%sp)+,%d0-%d1
	rts

	.globl rt_setseg_ds
rt_setseg_ds:
	movem.l %d0-%d1,-(%sp)
	and.l #0xffff,%d7
	move.l %d7,-(%sp)
	move.l #3,-(%sp)
	jbsr rt_c_setseg
	addq.l #8,%sp
	movem.l (%sp)+,%d0-%d1
	rts

	.globl rt_setseg_ss
rt_setseg_ss:
	movem.l %d0-%d1,-(%sp)
	and.l #0xffff,%d7
	move.l %d7,-(%sp)
	move.l #4,-(%sp)
	jbsr rt_c_setseg
	addq.l #8,%sp
	movem.l (%sp)+,%d0-%d1
	rts

	.globl rt_setseg_cs
rt_setseg_cs:
	movem.l %d0-%d1,-(%sp)
	and.l #0xffff,%d7
	move.l %d7,-(%sp)
	move.l #5,-(%sp)
	jbsr rt_c_setseg
	addq.l #8,%sp
	movem.l (%sp)+,%d0-%d1
	rts
