.text
	movss %xmm1, %xmm0
	movsd %xmm9, %xmm0
	movsd -8(%rbp), %xmm15
	movsd %xmm15, -8(%rbp)
	movq %xmm0, %rax
	movq %rax, %xmm1
	movq -8(%rbp), %xmm0
	movq %xmm0, -8(%rbp)
	movq %xmm1, %xmm2
	movaps %xmm7, -48(%rbp)
	cvtsi2sd %eax, %xmm0
	cvtsi2sd %rax, %xmm0
	cvtsi2ss -8(%rbp), %xmm0
	cvttsd2siq %xmm0, %rax
	cvttss2sil %xmm0, %eax
	xorps .Lfp0(%rip), %xmm0
	ucomisd %xmm1, %xmm0
	movabsq $81985529216486895, %rax
	movq $81985529216486895, %rax
	movq $-2147483648, %rax
	movq $2147483648, %rax
	movl $4294967295, %eax
	movw %ax, (%rcx)
	movw $5, (%rcx)
	movb $5, (%rcx)
	movb $-1, %al
	movslq %eax, %rcx
	movsbq (%rax), %rcx
	movzwl %ax, %ecx
	movl 1234, %eax
	pushq %r12
	popq %r12
	cmpq $-129, %rax
	cmpq $128, %rbx
	subq %rax, %rsp
	imulq %rcx, %rax
	imulq -8(%rbp), %rax
	movq %rax, %r9
	movl (%rax,%rcx), %edx
.section .foo
	.byte 1
.section .rodata
.p2align 3
.Lfp0:
	.int -2147483648 /* -0.000000 */
	.quad -1
