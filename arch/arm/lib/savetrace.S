/*
 *  linux/arch/arm/lib/backtrace.S
 *
 *  Copyright (C) 1995, 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 27/03/03 Ian Molton Clean up CONFIG_CPU
 *
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
		.text

@ fp is 0 or stack frame

#define frame	r7
#define sv_fp	r8
#define sv_pc	r9
#define mask	r10
#define offset	r11

ENTRY(__savetrace)
		mov	r1, #0x10
		mov	r0, fp

ENTRY(c_savetrace)

#if !defined(CONFIG_FRAME_POINTER) || !defined(CONFIG_PRINTK)
		mov	pc, lr
ENDPROC(__savetrace)
ENDPROC(c_savetrace)
#else
		stmfd	sp!, {r2 - r11, lr}	@ Save an extra register so we have a location...
		stmfd	sp!, {r1}
		movs	frame, r2		@ if frame pointer is zero
		beq	no_frame		@ we have no stack frames

		tst	r3, #0x10		@ 26 or 32-bit mode?
 ARM(		moveq	mask, #0xfc000003	)
 THUMB(		moveq	mask, #0xfc000000	)
 THUMB(		orreq	mask, #0x03		)
		movne	mask, #0		@ mask for 32-bit

1:		stmfd	sp!, {pc}		@ calculate offset of PC stored
		ldr	r2, [sp], #4		@ by stmfd for this CPU
		adr	r3, 1b
		sub	offset, r2, r3


for_each_frame:	tst	frame, mask		@ Check for address exceptions
		bne	no_frame

1001:		ldr	sv_pc, [frame, #0]	@ get saved pc
1002:		ldr	sv_fp, [frame, #-12]	@ get saved fp

		sub	sv_pc, sv_pc, offset	@ Correct PC for prefetching
		bic	sv_pc, sv_pc, mask	@ mask PC/LR for the mode

1003:		ldr	r4, [sv_pc, #-4]	@ if stmfd sp!, {args} exists,
		ldr	r5, .Ldsi+4		@ adjust saved 'pc' back one
		teq	r5, r4, lsr #10		@ instruction
		subne	r2, sv_pc, #4		@ allow for mov
		subeq	r2, sv_pc, #8		@ allow for mov + stmia

		ldr	r3, [frame, #-4]	@ get saved lr
		mov	r4, frame
		bic	r3, r3, mask		@ mask PC/LR for the mode
		stmfd	sp!, {r0, r1}
		bl	save_backtrace_entry
		mov	r6, r0
		ldmfd	sp!, {r0, r1}
		add	r0, r0, r6
		sub	r1, r1, r6
		teq	r1, #0			@ zero saved r1 means
		beq	no_frame		@ no further buffers
	
		teq	sv_fp, #0		@ zero saved fp means
		beq	no_frame		@ no further frames

		cmp	sv_fp, frame		@ next frame must be
		mov	frame, sv_fp		@ above the current frame
		bhi	for_each_frame

1006:		adr	r2, .Lbad
		mov	r3, frame
		stmfd	sp!, {r0, r1}
		bl	snprintf
		mov	r6, r0
		ldmfd	sp!, {r0, r1}
		sub	r0, r0, r6
		sub	r1, r1, r6
no_frame:	ldmfd	sp!, {r0}
		sub	r0, r0, r1	
		ldmfd	sp!, {r2 - r11, pc}
ENDPROC(__savetrace)
ENDPROC(c_savetrace)
		
		.section __ex_table,"a"
		.align	3
		.long	1001b, 1006b
		.long	1002b, 1006b
		.long	1003b, 1006b
		.previous

.Lbad:		.asciz	"Backtrace aborted due to bad frame pointer <%p>\n"
		.align
.Ldsi:		.word	0xe92dd800 >> 10	@ stmfd sp!, {... fp, ip, lr, pc}
		.word	0xe92d0000 >> 10	@ stmfd sp!, {}

#endif
