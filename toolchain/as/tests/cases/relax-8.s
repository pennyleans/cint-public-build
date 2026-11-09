.text
.globl f
f:
.p2align 4
.Lb0:
	ret
	movl $1, %eax
	jge .Lb15
	pushq %r12
	movl $1, %eax
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	jnge .Lb14
	movl $1, %eax
	pushq %r12
	callq ext
	leaq glob(%rip), %rax
	jnz .Lb4
	movl %eax, 1000(%rsp, %r12, 8)
.p2align 4
.Lb1:
	addq $1000, %rdi
.p2align 4
.Lb2:
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	ja .Lb2
	movabsq $81985529216486895, %rcx
	jna .Lb37
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	callq ext
	jb .Lb38
	pushq %r12
	addq $1000, %rdi
	nop
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	pushq %r12
	pushq %r12
	pushq %r12
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	jnz .Lb16
	leaq glob(%rip), %rax
	callq ext
	callq ext
	jae .Lb1
	ret
	movl $1, %eax
	callq ext
	ret
	movabsq $81985529216486895, %rcx
	jna .Lb36
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	jo .Lb6
.p2align 4
.Lb3:
	pushq %r12
	ret
	pushq %r12
.p2align 4
.Lb4:
	movq %rax, %rbx
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	jne .Lb19
	addq $1000, %rdi
	movl $1, %eax
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	jnae .Lb37
	ret
	callq ext
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	nop
	callq ext
	jl .Lb37
	nop
	nop
	movq %rax, %rbx
	nop
	addq $1000, %rdi
	movl $1, %eax
.Lb5:
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
.Lb6:
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	callq ext
	movabsq $81985529216486895, %rcx
	je .Lb39
	movq %rax, %rbx
	callq ext
	leaq glob(%rip), %rax
	jl .Lb26
	addq $1000, %rdi
	addq $1000, %rdi
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movq %rax, %rbx
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
.Lb7:
	callq ext
	pushq %r12
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	pushq %r12
	movq %rax, %rbx
	movq %rax, %rbx
	leaq glob(%rip), %rax
.Lb8:
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	pushq %r12
	callq ext
	movabsq $81985529216486895, %rcx
	pushq %r12
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	callq ext
	jnge .Lb10
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	ret
	jnp .Lb12
	leaq glob(%rip), %rax
	jna .Lb29
	leaq glob(%rip), %rax
	movq %rax, %rbx
	jle .Lb22
	movq %rax, %rbx
	movl $1, %eax
.p2align 4
.Lb9:
	addq $1000, %rdi
	movq %rax, %rbx
	movl $1, %eax
	addq $1000, %rdi
.Lb10:
	movq %rax, %rbx
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	callq ext
	jb .Lb4
	pushq %r12
	leaq glob(%rip), %rax
	addq $1000, %rdi
	movq %rax, %rbx
	movl $1, %eax
.Lb11:
	ret
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	nop
	jc .Lb5
	ret
	movabsq $81985529216486895, %rcx
	ret
	jo .Lb28
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	addq $1000, %rdi
	nop
	pushq %r12
	leaq glob(%rip), %rax
	nop
	leaq glob(%rip), %rax
	pushq %r12
	pushq %r12
	movabsq $81985529216486895, %rcx
	callq ext
	nop
	movq %rax, %rbx
	movl $1, %eax
	jnbe .Lb23
	leaq glob(%rip), %rax
	movl $1, %eax
	ret
.p2align 4
.Lb12:
	ret
	ret
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	pushq %r12
	jpo .Lb38
	addq $1000, %rdi
	addq $1000, %rdi
	jpe .Lb20
	movq %rax, %rbx
	ret
	nop
	addq $1000, %rdi
	callq ext
	movl $1, %eax
	addq $1000, %rdi
	jnbe .Lb14
	addq $1000, %rdi
	nop
	leaq glob(%rip), %rax
	jge .Lb17
	movl $1, %eax
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	nop
	movq %rax, %rbx
	nop
	movl %eax, 1000(%rsp, %r12, 8)
.p2align 4
.Lb13:
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	callq ext
	pushq %r12
	ret
	pushq %r12
	movq %rax, %rbx
	leaq glob(%rip), %rax
	callq ext
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	jz .Lb13
	addq $1000, %rdi
	jl .Lb0
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	jz .Lb13
	addq $1000, %rdi
	callq ext
	movl $1, %eax
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	callq ext
	callq ext
.p2align 4
.Lb14:
	addq $1000, %rdi
	callq ext
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	jz .Lb31
	ret
	callq ext
	leaq glob(%rip), %rax
	pushq %r12
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	nop
	addq $1000, %rdi
	movq %rax, %rbx
	pushq %r12
	pushq %r12
	nop
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
.Lb15:
	ret
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	jl .Lb15
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	callq ext
	leaq glob(%rip), %rax
	jnp .Lb24
	nop
	movl $1, %eax
	pushq %r12
	movabsq $81985529216486895, %rcx
	jnle .Lb0
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	nop
	movq %rax, %rbx
.p2align 4
.Lb16:
	leaq glob(%rip), %rax
	jae .Lb1
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	jnz .Lb22
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	leaq glob(%rip), %rax
	movl $1, %eax
	nop
	movabsq $81985529216486895, %rcx
	pushq %r12
	movl $1, %eax
	pushq %r12
	nop
.Lb17:
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	ret
	leaq glob(%rip), %rax
	callq ext
	nop
	leaq glob(%rip), %rax
	addq $1000, %rdi
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	pushq %r12
	nop
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	leaq glob(%rip), %rax
	jb .Lb25
	pushq %r12
	jpo .Lb11
	nop
	movabsq $81985529216486895, %rcx
	nop
.Lb18:
	addq $1000, %rdi
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	jna .Lb6
	addq $1000, %rdi
	movq %rax, %rbx
	jbe .Lb31
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	jnz .Lb8
	nop
	pushq %r12
	callq ext
	callq ext
	callq ext
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	ret
.Lb19:
	addq $1000, %rdi
	addq $1000, %rdi
	jnbe .Lb10
	addq $1000, %rdi
	callq ext
	leaq glob(%rip), %rax
	jg .Lb24
	movl $1, %eax
	ret
	ret
	nop
	pushq %r12
	nop
	movq %rax, %rbx
	nop
	nop
	leaq glob(%rip), %rax
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	jnle .Lb16
	addq $1000, %rdi
	jp .Lb11
	movq %rax, %rbx
	movl $1, %eax
	movl $1, %eax
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
.Lb20:
	addq $1000, %rdi
	pushq %r12
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	jmp .Lb26
	movabsq $81985529216486895, %rcx
	jz .Lb26
	ret
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	pushq %r12
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	pushq %r12
	ret
	nop
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	ret
	nop
	movabsq $81985529216486895, %rcx
.Lb21:
	pushq %r12
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	addq $1000, %rdi
	movq %rax, %rbx
	movl $1, %eax
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	ret
	pushq %r12
	jnl .Lb21
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	pushq %r12
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	pushq %r12
	callq ext
	movq %rax, %rbx
.Lb22:
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	movabsq $81985529216486895, %rcx
	pushq %r12
	callq ext
	leaq glob(%rip), %rax
	pushq %r12
	jnb .Lb19
	callq ext
	leaq glob(%rip), %rax
	addq $1000, %rdi
	jnb .Lb39
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
.Lb23:
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	callq ext
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	je .Lb16
.p2align 4
.Lb24:
	nop
	jp .Lb6
	pushq %r12
	movq %rax, %rbx
	ret
	movl $1, %eax
	addq $1000, %rdi
	leaq glob(%rip), %rax
	jo .Lb36
	ret
	movl $1, %eax
	jna .Lb28
	addq $1000, %rdi
	movl $1, %eax
	nop
	pushq %r12
	leaq glob(%rip), %rax
	movq %rax, %rbx
	jae .Lb21
	pushq %r12
	nop
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	movl $1, %eax
	jpe .Lb2
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
.Lb25:
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	nop
	movq %rax, %rbx
	pushq %r12
	movl $1, %eax
	addq $1000, %rdi
	jnbe .Lb23
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	leaq glob(%rip), %rax
	jbe .Lb7
	pushq %r12
	callq ext
	movq %rax, %rbx
	jle .Lb20
	callq ext
.Lb26:
	nop
	nop
	callq ext
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movl $1, %eax
	movq %rax, %rbx
	pushq %r12
	movl $1, %eax
	movl $1, %eax
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	nop
	movq %rax, %rbx
	addq $1000, %rdi
	ret
	nop
	leaq glob(%rip), %rax
	ret
	leaq glob(%rip), %rax
	jmp .Lb7
	movabsq $81985529216486895, %rcx
	pushq %r12
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	jna .Lb16
	nop
.p2align 4
.Lb27:
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	callq ext
	ret
	nop
	jne .Lb8
	nop
	jb .Lb17
	movl $1, %eax
	jnge .Lb11
	callq ext
	callq ext
	pushq %r12
	jc .Lb36
	nop
	ret
	nop
	jnbe .Lb30
	callq ext
	addq $1000, %rdi
	addq $1000, %rdi
	jnz .Lb10
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
.p2align 4
.Lb28:
	movl %eax, 1000(%rsp, %r12, 8)
	jna .Lb39
	callq ext
	ret
	pushq %r12
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
.Lb29:
	ret
	movq %rax, %rbx
	movq %rax, %rbx
	jnc .Lb8
	pushq %r12
	movabsq $81985529216486895, %rcx
	callq ext
	nop
	addq $1000, %rdi
	callq ext
	callq ext
	pushq %r12
	movl $1, %eax
	addq $1000, %rdi
	movq %rax, %rbx
	movq %rax, %rbx
	jnge .Lb29
	pushq %r12
	movabsq $81985529216486895, %rcx
	ret
	jpo .Lb16
	ret
	jz .Lb33
	nop
	jl .Lb39
	movl $1, %eax
	movl $1, %eax
	ret
	callq ext
	pushq %r12
.p2align 4
.Lb30:
	movl $1, %eax
	pushq %r12
	movabsq $81985529216486895, %rcx
	nop
	ret
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	pushq %r12
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
.Lb31:
	ret
	jle .Lb32
	pushq %r12
	jpo .Lb27
	movq %rax, %rbx
	ret
	je .Lb11
	movq %rax, %rbx
	movl $1, %eax
	pushq %r12
	jg .Lb31
	ret
	movl $1, %eax
	movq %rax, %rbx
	ret
.Lb32:
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	nop
	callq ext
	ret
	jge .Lb4
	nop
	movabsq $81985529216486895, %rcx
.Lb33:
	movl $1, %eax
	jb .Lb34
	leaq glob(%rip), %rax
	jno .Lb23
	pushq %r12
	pushq %r12
	addq $1000, %rdi
	jnge .Lb34
.Lb34:
	movabsq $81985529216486895, %rcx
	pushq %r12
	leaq glob(%rip), %rax
	ret
	addq $1000, %rdi
	addq $1000, %rdi
	jae .Lb25
	movl $1, %eax
.Lb35:
	leaq glob(%rip), %rax
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	movq %rax, %rbx
	leaq glob(%rip), %rax
	callq ext
.Lb36:
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	jpo .Lb16
	nop
	pushq %r12
	callq ext
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	js .Lb4
	movl $1, %eax
	nop
	callq ext
	addq $1000, %rdi
	callq ext
	nop
	movl $1, %eax
	ret
	leaq glob(%rip), %rax
	pushq %r12
.Lb37:
	leaq glob(%rip), %rax
	pushq %r12
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	jp .Lb11
	nop
	ret
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	jbe .Lb33
	ret
	jnc .Lb12
	movl $1, %eax
	movl $1, %eax
	nop
	leaq glob(%rip), %rax
	ret
	je .Lb35
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
.Lb38:
	ret
	movl $1, %eax
	movq %rax, %rbx
	ret
	movabsq $81985529216486895, %rcx
	jz .Lb29
	movq %rax, %rbx
	nop
	movq %rax, %rbx
	movl $1, %eax
	movq %rax, %rbx
	movl $1, %eax
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	jnb .Lb35
	leaq glob(%rip), %rax
	movq %rax, %rbx
	pushq %r12
	callq ext
	callq ext
	movq %rax, %rbx
	jng .Lb9
	pushq %r12
	movabsq $81985529216486895, %rcx
.p2align 4
.Lb39:
	pushq %r12
	movl $1, %eax
	pushq %r12
	pushq %r12
	addq $1000, %rdi
	pushq %r12
	addq $1000, %rdi
	movq %rax, %rbx
	movl $1, %eax
	movq %rax, %rbx
	movl $1, %eax
	pushq %r12
	jnb .Lb8
	pushq %r12
	callq ext
	pushq %r12
	addq $1000, %rdi
	movl $1, %eax
	pushq %r12
	ret
.type f, @function
.size f, .-f
