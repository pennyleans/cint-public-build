.text
f:
	testl %eax, %ecx
	testq $5, %rax
	testl $5, %ecx
	xchgq %rax, %rbx
	xchgq %rbx, %rax
	xchgl %ecx, %edx
	movq %fs:0, %rax
	movl %fs:0, %eax
	movq %fs:tv@tpoff, %rax
	leaq tv@tpoff(%rax), %rax
	addq tie@gottpoff(%rip), %rax
	movq ext@gotpcrel(%rip), %rcx
	movl ext@gotpcrel(%rip), %ecx
	addl $4294967295, %eax
	andl $4294967295, %ecx
	movl $-4294967296, -8(%rbp)
	movl $81985529216486895>>32, 4+-8(%rbp)
	leaq f(%rip), %rax
	leaq g(%rip), %rax
	leaq .Lx(%rip), %rax
	movq $ext, %rax
	movl $ext, %eax
	movq ext+8(%rax), %rcx
	mov %eax, %ecx
	cmovz %ecx, %eax
	cmovnz -8(%rbp), %rax
	imull $3, %eax
	imulq $300, %rcx, %rdx
	shlq $1, %rax
	shll %cl, %eax
	sarq $3, -8(%rbp)
	movb %sil, (%rax)
	setz %dil
	setb %r9b
	movzbl %dil, %edi
	movl %eax, (%rsp)
	movl %eax, (%r12)
	movl %eax, (%rbp)
	movl %eax, (%r13)
	movl %eax, 8(%rax, %rcx, 4)
	movl %eax, (, %rcx, 4)
	movl %eax, 300(%rax)
	callq *%rax
	callq *%r11
	cltd
	cqto
	idivl %ecx
	divq %r8
	negq %rax
	.p2align 4
.Lx:
	ret
	.globl g
g:
	ret
.section .tbss,"awT"
.balign 8
tv:
	.fill 8,1,0
.section .tdata,"awT"
tw:
	.quad 5
.section .rodata
	.quad 1
.comm cm,16,8
.globl undef1
.type ext2, @function
