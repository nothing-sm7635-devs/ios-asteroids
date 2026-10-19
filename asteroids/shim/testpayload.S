/*
 * Stand-in for xnu: the shim jumps here exactly as it would to xnu's _start
 * (MMU off, x0 = boot_args physical). Paint the boot_args framebuffer
 * green if boot_args looks right, red if not, then spin.
 * Checks: physBase == 0x8c0000000, Video.v_depth == 32, rowBytes >= 4 * width,
 * and that we run inside [physBase, physBase + memSize).
 */
	.text
	.globl _start
_start:
	ldr	x9, [x0, #16]		/* physBase */
	ldr	x10, [x0, #24]		/* memSize */
	ldr	x1, [x0, #40]		/* Video.v_baseAddr */
	ldr	x2, [x0, #56]		/* v_rowBytes */
	ldr	x5, [x0, #64]		/* v_width */
	ldr	x3, [x0, #72]		/* v_height */
	ldr	x6, [x0, #80]		/* v_depth */
	movz	w4, #0x00ff, lsl #16	/* red: 0x00ff0000 */
	movz	x7, #0xc000, lsl #16	/* 0x8c0000000 */
	movk	x7, #0x8, lsl #32
	cmp	x9, x7
	b.ne	paint
	cmp	x6, #32
	b.ne	paint
	lsl	x8, x5, #2
	cmp	x2, x8
	b.lo	paint
	adr	x8, _start
	cmp	x8, x9
	b.lo	paint
	add	x7, x9, x10
	cmp	x8, x7
	b.hs	paint
	movz	w4, #0xff00		/* green: 0x0000ff00 */
paint:
	mov	x11, x3
row:
	mov	x12, x1
	mov	x13, x5
px:
	str	w4, [x12], #4
	subs	x13, x13, #1
	b.ne	px
	add	x1, x1, x2
	subs	x11, x11, #1
	b.ne	row
	dsb	sy
1:	wfe
	b	1b
