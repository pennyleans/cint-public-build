.data
.balign 8
.globl gdat
gdat:
	.quad 1
	.quad ldat+8
	.quad foo
ldat:
	.int 7
	.byte 1
	.ascii "hi\n\000\"x"
.bss
.balign 16
zz:
	.fill 24,1,0
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	pushq %rbx
	movl $1, %eax
	movq $1, %rax
	movq $4294967296, %rax
	movq $-1, %rcx
	leaq ldat(%rip), %rax
	leaq gdat(%rip), %rax
	leaq zz+4(%rip), %rdx
	callq foo
	callq puts@plt
	callq loc
	callq main
.Lbb2:
	cmpl $5, %eax
	jz .Lbb2
	jmp .Lbb3
.p2align 4
.Lbb3:
	ud2
	popq %rbx
	leave
	ret
.type main, @function
.size main, .-main
.text
loc:
	endbr64
	ret
.type loc, @function
.size loc, .-loc
.section .note.GNU-stack,"",@progbits
