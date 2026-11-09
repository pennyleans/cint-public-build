.text
.globl f
f:
.p2align 4
.Lb0:
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	jg .Lb0
	nop
	movabsq $81985529216486895, %rcx
	nop
	pushq %r12
	ret
	ret
	pushq %r12
	movl $1, %eax
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	jnl .Lb1
	leaq glob(%rip), %rax
	callq ext
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
.Lb1:
	addq $1000, %rdi
	ja .Lb8
	movabsq $81985529216486895, %rcx
	callq ext
	leaq glob(%rip), %rax
	callq ext
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	jno .Lb30
	addq $1000, %rdi
	callq ext
	movl $1, %eax
	callq ext
	movl $1, %eax
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	movl $1, %eax
	movq %rax, %rbx
	jo .Lb34
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
.p2align 4
.Lb2:
	addq $1000, %rdi
.Lb3:
	callq ext
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	pushq %r12
	movabsq $81985529216486895, %rcx
	callq ext
	pushq %r12
	addq $1000, %rdi
	jns .Lb26
	addq $1000, %rdi
	jns .Lb39
	ret
	jna .Lb21
	addq $1000, %rdi
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movl $1, %eax
	movl $1, %eax
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	addq $1000, %rdi
	ret
	addq $1000, %rdi
	leaq glob(%rip), %rax
	callq ext
	jns .Lb1
	ret
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	ret
	movq %rax, %rbx
	jnle .Lb20
.Lb4:
	movabsq $81985529216486895, %rcx
	ret
	jng .Lb13
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	movq %rax, %rbx
	addq $1000, %rdi
	callq ext
	leaq glob(%rip), %rax
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	movabsq $81985529216486895, %rcx
	jnl .Lb29
	pushq %r12
	pushq %r12
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	movl $1, %eax
	jg .Lb32
	movabsq $81985529216486895, %rcx
.Lb5:
	movl $1, %eax
	jnae .Lb7
	pushq %r12
	movl $1, %eax
	callq ext
	jb .Lb39
	addq $1000, %rdi
	ja .Lb34
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	jnae .Lb7
	movabsq $81985529216486895, %rcx
	ret
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	pushq %r12
	movl $1, %eax
	movl $1, %eax
	ret
	jno .Lb3
	nop
.Lb6:
	pushq %r12
	addq $1000, %rdi
.p2align 4
.Lb7:
	callq ext
	addq $1000, %rdi
	addq $1000, %rdi
	ret
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	pushq %r12
	movl $1, %eax
	callq ext
	jnge .Lb31
	addq $1000, %rdi
	callq ext
.p2align 4
.Lb8:
	leaq glob(%rip), %rax
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	addq $1000, %rdi
	pushq %r12
	leaq glob(%rip), %rax
	jnle .Lb7
	pushq %r12
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	jo .Lb24
	pushq %r12
	jnp .Lb6
	callq ext
	movl $1, %eax
	movq %rax, %rbx
	jnp .Lb7
	nop
.Lb9:
	leaq glob(%rip), %rax
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	jg .Lb6
	pushq %r12
	jle .Lb20
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	ret
	nop
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	callq ext
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
.p2align 4
.Lb10:
	ret
	nop
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	jc .Lb12
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	pushq %r12
	leaq glob(%rip), %rax
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
.Lb11:
	nop
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	nop
	ret
	nop
	movabsq $81985529216486895, %rcx
	pushq %r12
	jle .Lb4
.Lb12:
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	ret
	movl $1, %eax
	leaq glob(%rip), %rax
	movl $1, %eax
	callq ext
	jb .Lb5
	movq %rax, %rbx
	leaq glob(%rip), %rax
	jns .Lb28
	addq $1000, %rdi
	jz .Lb21
	callq ext
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	pushq %r12
	leaq glob(%rip), %rax
	jb .Lb27
	nop
	leaq glob(%rip), %rax
	jnp .Lb23
	addq $1000, %rdi
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	jnp .Lb21
	movl %eax, 1000(%rsp, %r12, 8)
.p2align 4
.Lb13:
	pushq %r12
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	nop
	jc .Lb35
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	nop
	ret
	movq %rax, %rbx
	pushq %r12
	callq ext
	leaq glob(%rip), %rax
	movl $1, %eax
.p2align 4
.Lb14:
	leaq glob(%rip), %rax
	pushq %r12
	addq $1000, %rdi
	nop
	movq %rax, %rbx
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	jns .Lb7
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	callq ext
	ret
	ret
	movabsq $81985529216486895, %rcx
	ret
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
.p2align 4
.Lb15:
	nop
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	leaq glob(%rip), %rax
	nop
	jl .Lb12
	movl $1, %eax
	ja .Lb14
	ret
	nop
	pushq %r12
	pushq %r12
	nop
	jnle .Lb18
	ret
	jp .Lb5
	movl $1, %eax
	jnb .Lb39
	pushq %r12
	jmp .Lb34
	addq $1000, %rdi
	addq $1000, %rdi
	ret
	jnge .Lb38
	movl $1, %eax
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	callq ext
	jpo .Lb12
.p2align 4
.Lb16:
	nop
	pushq %r12
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	jnge .Lb29
	callq ext
	nop
	leaq glob(%rip), %rax
	addq $1000, %rdi
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	js .Lb29
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	callq ext
.Lb17:
	movq %rax, %rbx
	nop
	movl $1, %eax
	callq ext
.Lb18:
	movl $1, %eax
	movl $1, %eax
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	callq ext
	ret
	movq %rax, %rbx
	movq %rax, %rbx
	leaq glob(%rip), %rax
	nop
	leaq glob(%rip), %rax
	pushq %r12
	addq $1000, %rdi
	addq $1000, %rdi
	movl $1, %eax
	movq %rax, %rbx
	movl $1, %eax
	jmp .Lb5
	ret
	nop
	jng .Lb32
	addq $1000, %rdi
	nop
	nop
.Lb19:
	callq ext
.Lb20:
	movq %rax, %rbx
	ret
	movq %rax, %rbx
	pushq %r12
	callq ext
	callq ext
	pushq %r12
	ret
	callq ext
	nop
	nop
	ret
.Lb21:
	callq ext
	movl $1, %eax
	movl $1, %eax
	movq %rax, %rbx
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	movl $1, %eax
	addq $1000, %rdi
	movl $1, %eax
	jge .Lb25
	movq %rax, %rbx
	leaq glob(%rip), %rax
	jnl .Lb5
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	ret
	movl $1, %eax
	jnle .Lb18
	movabsq $81985529216486895, %rcx
	pushq %r12
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	nop
	movq %rax, %rbx
	jp .Lb7
	movl $1, %eax
	movq %rax, %rbx
	leaq glob(%rip), %rax
	jnz .Lb29
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	jnge .Lb31
	addq $1000, %rdi
.Lb22:
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	pushq %r12
	jl .Lb19
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	addq $1000, %rdi
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	callq ext
	leaq glob(%rip), %rax
	ret
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	movl $1, %eax
.p2align 4
.Lb23:
	ret
	pushq %r12
	jo .Lb30
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	je .Lb28
	callq ext
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	addq $1000, %rdi
	callq ext
	nop
	pushq %r12
	leaq glob(%rip), %rax
	nop
	addq $1000, %rdi
	leaq glob(%rip), %rax
	pushq %r12
	jpe .Lb8
	pushq %r12
	jnae .Lb1
	movl $1, %eax
	jne .Lb29
	addq $1000, %rdi
	movl $1, %eax
.p2align 4
.Lb24:
	callq ext
	movl $1, %eax
	movq %rax, %rbx
	leaq glob(%rip), %rax
	nop
	leaq glob(%rip), %rax
	movl $1, %eax
	ret
.Lb25:
	ret
	callq ext
	addq $1000, %rdi
	leaq glob(%rip), %rax
	jmp .Lb2
	nop
	nop
	movl %eax, 1000(%rsp, %r12, 8)
.Lb26:
	ret
	movq %rax, %rbx
	pushq %r12
	jna .Lb22
	nop
	leaq glob(%rip), %rax
	pushq %r12
	addq $1000, %rdi
	jbe .Lb2
	nop
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	pushq %r12
	jnc .Lb8
	leaq glob(%rip), %rax
	jne .Lb4
	movl $1, %eax
	nop
	addq $1000, %rdi
	callq ext
	callq ext
	leaq glob(%rip), %rax
	movl $1, %eax
.Lb27:
	pushq %r12
	ret
	callq ext
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	pushq %r12
	jpe .Lb7
	movl $1, %eax
	movq %rax, %rbx
	nop
	leaq glob(%rip), %rax
	movl $1, %eax
	nop
.p2align 4
.Lb28:
	ret
	nop
	movl $1, %eax
	jnae .Lb3
	ret
	ret
	movl $1, %eax
	ret
	callq ext
	callq ext
	movl $1, %eax
	ret
	jc .Lb18
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	jmp .Lb12
	addq $1000, %rdi
	nop
	addq $1000, %rdi
	nop
	ja .Lb20
	movl $1, %eax
	pushq %r12
	nop
	nop
	leaq glob(%rip), %rax
	pushq %r12
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	ret
	movabsq $81985529216486895, %rcx
	jo .Lb38
.Lb29:
	ret
	jnae .Lb0
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	leaq glob(%rip), %rax
	jns .Lb34
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	nop
	ret
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	jbe .Lb26
	pushq %r12
	leaq glob(%rip), %rax
	jnz .Lb24
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
.p2align 4
.Lb30:
	movq %rax, %rbx
	callq ext
	addq $1000, %rdi
	jnbe .Lb23
	addq $1000, %rdi
	addq $1000, %rdi
	movq %rax, %rbx
	ret
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	jae .Lb5
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	jl .Lb34
	addq $1000, %rdi
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	jnb .Lb24
.Lb31:
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	jp .Lb11
	movq %rax, %rbx
	addq $1000, %rdi
	nop
	ret
	ret
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	movq %rax, %rbx
	movq %rax, %rbx
	jno .Lb20
	movl $1, %eax
	callq ext
	movq %rax, %rbx
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	callq ext
	movl $1, %eax
	movl $1, %eax
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	nop
	pushq %r12
	addq $1000, %rdi
	pushq %r12
.Lb32:
	movl %eax, 1000(%rsp, %r12, 8)
	jne .Lb34
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	movl $1, %eax
	leaq glob(%rip), %rax
	nop
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
.p2align 4
.Lb33:
	pushq %r12
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	addq $1000, %rdi
	ret
	addq $1000, %rdi
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	nop
	ret
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	ret
	pushq %r12
	pushq %r12
	ret
	nop
	movq %rax, %rbx
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	nop
	pushq %r12
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	jz .Lb26
	ret
.p2align 4
.Lb34:
	ret
	nop
	ret
	nop
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	callq ext
	leaq glob(%rip), %rax
	callq ext
	addq $1000, %rdi
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
.Lb35:
	addq $1000, %rdi
	addq $1000, %rdi
	nop
.Lb36:
	callq ext
	addq $1000, %rdi
	jp .Lb3
	nop
	ret
	ret
	js .Lb35
	movq %rax, %rbx
.Lb37:
	addq $1000, %rdi
	movq %rax, %rbx
	addq $1000, %rdi
	leaq glob(%rip), %rax
	ja .Lb7
	addq $1000, %rdi
	leaq glob(%rip), %rax
	ret
	pushq %r12
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	addq $1000, %rdi
	movq %rax, %rbx
	ret
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	addq $1000, %rdi
.p2align 4
.Lb38:
	nop
.Lb39:
	nop
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	pushq %r12
	callq ext
	movl $1, %eax
	movl $1, %eax
	ret
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	ret
.type f, @function
.size f, .-f
