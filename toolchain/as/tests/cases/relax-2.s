.text
.globl f
f:
.Lb0:
	movl $1, %eax
	jnl .Lb23
	ret
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	pushq %r12
	pushq %r12
	pushq %r12
	movl $1, %eax
	addq $1000, %rdi
	addq $1000, %rdi
	callq ext
	pushq %r12
	ret
	movl $1, %eax
	ret
	jne .Lb32
	pushq %r12
	ret
	nop
	pushq %r12
	addq $1000, %rdi
	addq $1000, %rdi
	nop
	callq ext
	nop
	movabsq $81985529216486895, %rcx
.Lb1:
	pushq %r12
	addq $1000, %rdi
	nop
	nop
	pushq %r12
	nop
	addq $1000, %rdi
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	pushq %r12
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
.Lb2:
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	jnc .Lb3
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	ret
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	movl $1, %eax
	jnae .Lb23
	movabsq $81985529216486895, %rcx
.p2align 4
.Lb3:
	movl $1, %eax
	jne .Lb1
	leaq glob(%rip), %rax
	jl .Lb10
.p2align 4
.Lb4:
	movl $1, %eax
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	jpe .Lb39
	movq %rax, %rbx
	nop
	js .Lb28
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	movq %rax, %rbx
	addq $1000, %rdi
	movl $1, %eax
	ret
	callq ext
	addq $1000, %rdi
	jnc .Lb21
	leaq glob(%rip), %rax
	callq ext
	pushq %r12
	movl $1, %eax
	ret
	movq %rax, %rbx
.p2align 4
.Lb5:
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	movl $1, %eax
	callq ext
	movq %rax, %rbx
	movq %rax, %rbx
	movq %rax, %rbx
	jb .Lb14
	movabsq $81985529216486895, %rcx
	jna .Lb29
	leaq glob(%rip), %rax
	callq ext
	movabsq $81985529216486895, %rcx
	callq ext
	movl $1, %eax
	movl $1, %eax
	pushq %r12
	movq %rax, %rbx
	nop
	pushq %r12
	movq %rax, %rbx
	leaq glob(%rip), %rax
	addq $1000, %rdi
	callq ext
	jae .Lb19
	movl $1, %eax
	movl $1, %eax
.Lb6:
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	addq $1000, %rdi
	movq %rax, %rbx
	nop
	movl %eax, 1000(%rsp, %r12, 8)
.Lb7:
	addq $1000, %rdi
	movq %rax, %rbx
	movq %rax, %rbx
	jpe .Lb21
	callq ext
.Lb8:
.Lb9:
	movl $1, %eax
	nop
	nop
	jnc .Lb23
	nop
	nop
	callq ext
	leaq glob(%rip), %rax
	ret
	leaq glob(%rip), %rax
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	jnae .Lb22
	pushq %r12
	jng .Lb26
	movq %rax, %rbx
	movl $1, %eax
	jnz .Lb18
	movabsq $81985529216486895, %rcx
.Lb10:
	nop
	leaq glob(%rip), %rax
	jnle .Lb34
	callq ext
	jnb .Lb33
	pushq %r12
	ret
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	movl $1, %eax
.Lb11:
	ret
	movl $1, %eax
	callq ext
	callq ext
	nop
	pushq %r12
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	callq ext
	movl $1, %eax
	pushq %r12
	nop
	movq %rax, %rbx
	jl .Lb13
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	pushq %r12
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	ret
	ja .Lb27
	leaq glob(%rip), %rax
.p2align 4
.Lb12:
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	nop
	movq %rax, %rbx
	nop
	addq $1000, %rdi
	pushq %r12
	movq %rax, %rbx
	addq $1000, %rdi
	callq ext
	ret
	jnz .Lb31
	nop
	ret
	jna .Lb35
	movl $1, %eax
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	ret
	pushq %r12
	leaq glob(%rip), %rax
	je .Lb20
	addq $1000, %rdi
	leaq glob(%rip), %rax
.Lb13:
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	pushq %r12
	ret
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	pushq %r12
	leaq glob(%rip), %rax
	jb .Lb7
	callq ext
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	addq $1000, %rdi
	nop
	nop
	nop
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	ret
	movabsq $81985529216486895, %rcx
	pushq %r12
	callq ext
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
.Lb14:
	leaq glob(%rip), %rax
	movq %rax, %rbx
	callq ext
	leaq glob(%rip), %rax
	addq $1000, %rdi
	movq %rax, %rbx
	ret
	callq ext
	jb .Lb1
	addq $1000, %rdi
	movq %rax, %rbx
	callq ext
	jnb .Lb20
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	ret
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	ret
	leaq glob(%rip), %rax
	jnb .Lb12
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	jnae .Lb9
	movq %rax, %rbx
	leaq glob(%rip), %rax
	jns .Lb33
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
.p2align 4
.Lb15:
	callq ext
.Lb16:
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	nop
	callq ext
	movl $1, %eax
	nop
	ret
	jg .Lb32
	ret
	jna .Lb19
	movl $1, %eax
	addq $1000, %rdi
	movl $1, %eax
	jz .Lb5
	nop
	jnb .Lb30
	movq %rax, %rbx
	pushq %r12
	js .Lb32
	movl $1, %eax
	jg .Lb34
	movl $1, %eax
	pushq %r12
	pushq %r12
	ret
	movl $1, %eax
	jnb .Lb7
	movq %rax, %rbx
	movl $1, %eax
	callq ext
	callq ext
	pushq %r12
.Lb17:
	pushq %r12
	jpe .Lb9
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	callq ext
	jnz .Lb8
	addq $1000, %rdi
.Lb18:
	movq %rax, %rbx
	leaq glob(%rip), %rax
	nop
	movabsq $81985529216486895, %rcx
	ret
	movq %rax, %rbx
	jnge .Lb35
	movabsq $81985529216486895, %rcx
	callq ext
	ret
	leaq glob(%rip), %rax
	nop
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	ret
	pushq %r12
	movq %rax, %rbx
.Lb19:
	leaq glob(%rip), %rax
	ret
	leaq glob(%rip), %rax
	pushq %r12
	callq ext
	movl $1, %eax
	movq %rax, %rbx
	leaq glob(%rip), %rax
	movl $1, %eax
	addq $1000, %rdi
	callq ext
	movabsq $81985529216486895, %rcx
	ret
	nop
.Lb20:
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	nop
	addq $1000, %rdi
	ret
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	pushq %r12
	leaq glob(%rip), %rax
	jae .Lb1
.Lb21:
	addq $1000, %rdi
.Lb22:
	addq $1000, %rdi
	movq %rax, %rbx
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	jnae .Lb9
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	nop
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	callq ext
	movabsq $81985529216486895, %rcx
	pushq %r12
	jc .Lb15
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	jnz .Lb29
	callq ext
	callq ext
	movabsq $81985529216486895, %rcx
	pushq %r12
.Lb23:
	pushq %r12
	movl $1, %eax
	callq ext
	pushq %r12
	addq $1000, %rdi
	pushq %r12
	nop
	nop
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	ret
	movabsq $81985529216486895, %rcx
	pushq %r12
	jpe .Lb9
	leaq glob(%rip), %rax
	movq %rax, %rbx
	pushq %r12
	jbe .Lb8
	ret
	jnp .Lb32
	leaq glob(%rip), %rax
	pushq %r12
.Lb24:
	movq %rax, %rbx
	movq %rax, %rbx
	addq $1000, %rdi
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	ret
	jbe .Lb21
	movl $1, %eax
	pushq %r12
	movq %rax, %rbx
	pushq %r12
	callq ext
	movabsq $81985529216486895, %rcx
	ret
	movl $1, %eax
	addq $1000, %rdi
	leaq glob(%rip), %rax
	jnle .Lb2
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	jg .Lb38
	movq %rax, %rbx
	pushq %r12
	movl $1, %eax
	movq %rax, %rbx
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	jnbe .Lb33
	callq ext
	ret
	ret
	addq $1000, %rdi
	jnae .Lb36
.Lb25:
	callq ext
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	jpo .Lb16
	ret
	callq ext
	leaq glob(%rip), %rax
	nop
	callq ext
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	movl $1, %eax
.Lb26:
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	pushq %r12
	leaq glob(%rip), %rax
	addq $1000, %rdi
	pushq %r12
	movabsq $81985529216486895, %rcx
	callq ext
	jna .Lb38
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	callq ext
	addq $1000, %rdi
	pushq %r12
	ret
	ret
.Lb27:
	ret
	addq $1000, %rdi
	callq ext
	leaq glob(%rip), %rax
	movl $1, %eax
	movl $1, %eax
	movq %rax, %rbx
	jz .Lb23
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	addq $1000, %rdi
	pushq %r12
	callq ext
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	addq $1000, %rdi
	jc .Lb31
	movl $1, %eax
	ja .Lb25
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	callq ext
	nop
.p2align 4
.Lb28:
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	ret
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	jl .Lb30
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	ret
	movabsq $81985529216486895, %rcx
	jna .Lb5
.Lb29:
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	jnae .Lb29
.p2align 4
.Lb30:
	nop
	movq %rax, %rbx
	nop
	pushq %r12
	leaq glob(%rip), %rax
	movl $1, %eax
	pushq %r12
	callq ext
	ret
	nop
	movl $1, %eax
	nop
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	movq %rax, %rbx
	ret
	movabsq $81985529216486895, %rcx
	jnc .Lb7
	movq %rax, %rbx
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	jnle .Lb8
	callq ext
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	addq $1000, %rdi
	movq %rax, %rbx
	movl $1, %eax
	jbe .Lb31
	addq $1000, %rdi
.p2align 4
.Lb31:
	ret
	movq %rax, %rbx
	jng .Lb5
.p2align 4
.Lb32:
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	jns .Lb14
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	callq ext
	movl $1, %eax
	addq $1000, %rdi
	nop
	addq $1000, %rdi
	pushq %r12
	movl $1, %eax
	jc .Lb24
	callq ext
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	jl .Lb36
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	addq $1000, %rdi
	jnb .Lb15
	nop
	nop
	jo .Lb14
	addq $1000, %rdi
	jns .Lb1
	movq %rax, %rbx
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	jnle .Lb10
	ret
.Lb33:
	callq ext
	movq %rax, %rbx
	callq ext
	movq %rax, %rbx
	movq %rax, %rbx
.p2align 4
.Lb34:
.p2align 4
.Lb35:
	callq ext
	ret
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	leaq glob(%rip), %rax
	callq ext
	callq ext
	callq ext
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	jng .Lb26
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	pushq %r12
	pushq %r12
	ret
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	callq ext
	jng .Lb32
	movabsq $81985529216486895, %rcx
	ret
	leaq glob(%rip), %rax
	ret
	movq %rax, %rbx
.Lb36:
	pushq %r12
	jo .Lb35
	callq ext
	leaq glob(%rip), %rax
	ret
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
.p2align 4
.Lb37:
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	callq ext
	movl $1, %eax
	jae .Lb31
	pushq %r12
	leaq glob(%rip), %rax
	movl $1, %eax
	ret
.p2align 4
.Lb38:
	nop
	pushq %r12
	movl $1, %eax
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	nop
	jno .Lb18
	ret
	movl $1, %eax
.Lb39:
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	jno .Lb0
	movabsq $81985529216486895, %rcx
	jle .Lb33
	pushq %r12
	je .Lb1
	leaq glob(%rip), %rax
	jmp .Lb37
	addq $1000, %rdi
	movq %rax, %rbx
	ret
	movabsq $81985529216486895, %rcx
	ret
	callq ext
	movl $1, %eax
	movq %rax, %rbx
	ret
.type f, @function
.size f, .-f
