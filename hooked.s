/*
** Copyright (C) 2011 EADS France
** stephane duverger <stephane.duverger@eads.net>
** nicolas bareil <nicolas.bareil@eads.net>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License along
** with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
.text
.globl shatner, payload
.type  shatner,"function"
.type  payload,"function"

shatner:
	jmp 1f
__old_ld:
	.long 0  /* ld original entry point (from 0) */
__old_user:
	.long 0  /* location to store original user entry point (from 0) */
__inject_entry:  /* entry point of injected code (from 0) */
	.long 0
1:
	pushl $0
	push %eax
	lea 8(%esp), %eax
	push %edx
	push %ecx
	call __get_pc
__get_pc:
	pop %edx
	mov %edx, %ecx
	add $(payload_caller - __get_pc), %edx
	sub $(__get_pc - __old_user), %ecx
	push %ebx
	push %edi
	xor  %edi, %edi
fix_auxv: /* walk stack to find 2 NULL words (one ending argv, one ending envp) */
	mov (%eax), %ebx
	test %ebx, %ebx
	jnz next_word
	inc %edi
	cmp $2, %edi
	je  auxv_found
next_word:
	add $4, %eax
	jmp fix_auxv

auxv_found: /* found second NULL word, start lokking for AUX_TYPE==ENTRY (9) */
	add $4, %eax
next_tag:
	cmp $9, (%eax)
	je  auxv_entry_tag_found
	add $8, %eax
	jmp next_tag

auxv_entry_tag_found:
	add $4, %eax
	mov (%eax), %edi
	mov (%ecx), %ecx
	push %esi
	call __get_pc2
__get_pc2:
	pop %esi
	sub $((__get_pc2 - shatner)+52), %esi /* base address of ld in memory */
	add %esi, %ecx
	mov %edi, (%ecx) /* save user entry */
	mov %edx, (%eax) /* install new one */

resume_ld:
	sub $(payload_caller - __old_ld), %edx
	mov (%edx), %edx
	add %esi, %edx  /* base + ld original entry */
	pop %esi
	pop %edi
	pop %ebx
	pop %ecx
	mov %edx, 8(%esp)
	pop %edx
	pop %eax
	ret

payload_caller:
	call real_payload_caller
	push $0
	push %eax
	call __get_pc3
__get_pc3:
	pop %eax
	push %edx
	mov %eax, %edx
	sub $((__get_pc3 - shatner)+52), %eax /* base address of ld in memory */
	sub $(__get_pc3 - __old_user), %edx /* user offset */
	mov (%edx), %edx
	add %eax, %edx
	mov (%edx), %eax  /* original user entry */
	movl $0, (%edx)   /* clear place */
	mov %eax, 8(%esp)
	pop %edx
	pop %eax
	ret
real_payload_caller:
	push %eax
	call __get_pc4
__get_pc4:
	pop %eax
	push %edx
	mov %eax, %edx
	add $(payload - __get_pc4), %eax
	sub $(__get_pc4 - __inject_entry), %edx /* injected entry offset */
	mov (%edx), %edx
	add %edx, %eax
	call *%eax
	pop %edx
	pop %eax
	ret

	.align 4
payload:
	nop
