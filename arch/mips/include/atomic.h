/*
 * Copyright (C) 2005 Ondrej Palkovsky
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __MIPS_ATOMIC_H__
#define __MIPS_ATOMIC_H__

#define atomic_inc(x)	(a_add(x,1))
#define atomic_dec(x)	(a_sub(x,1))

/*
 * Atomic addition
 *
 * This case is harder, and we have to use the special LL and SC operations
 * to achieve atomicity. The instructions are similar to LW (load) and SW
 * (store), except that the LL (load-linked) instruction loads the address
 * of the variable to a special register and if another process writes to
 * the same location, the SC (store-conditional) instruction fails.
 */
static inline int a_add( volatile int *val, int i)
{
	int tmp, tmp2;

	asm volatile (
		"	.set	push\n"
		"	.set	noreorder\n"
		"	nop\n"
		"1:\n"
		"	ll	%0, %1\n"
		"	addu	%0, %0, %2\n"
		"       move    %3, %0\n"
		"	sc	%0, %1\n"
		"	beq	%0, 0x0, 1b\n"
		"	move    %0, %3\n"
		"	.set	pop\n"
		: "=&r" (tmp), "=o" (*val)
		: "r" (i), "r" (tmp2)
		);
	return tmp;
}


/*
 * Atomic subtraction
 *
 * Implemented in the same manner as a_add, except we substract the value.
 */
static inline int a_sub( volatile int *val, int i)

{
	int tmp, tmp2;

	asm volatile (
		"	.set	push\n"
		"	.set	noreorder\n"
		"	nop\n"
		"1:\n"
		"	ll	%0, %1\n"
		"	subu	%0, %0, %2\n"
		"       move    %3, %0\n"
		"	sc	%0, %1\n"
		"	beq	%0, 0x0, 1b\n"
		"       move    %0, %3\n"
		"	.set	pop\n"
		: "=&r" (tmp), "=o" (*val)
		: "r" (i), "r" (tmp2)
		);
	return tmp;
}


#endif
