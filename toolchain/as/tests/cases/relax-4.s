.text
.globl f
f:
.p2align 4
.Lb0:
	callq ext
	movq %rax, %rbx
	js .Lb25
	leaq glob(%rip), %rax
.Lb1:
	pushq %r12
	leaq glob(%rip), %rax
	movq %rax, %rbx
	movl $1, %eax
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
.Lb2:
	addq $1000, %rdi
	jz .Lb38
	callq ext
	ret
	leaq glob(%rip), %rax
	jnl .Lb35
	leaq glob(%rip), %rax
	jns .Lb18
	leaq glob(%rip), %rax
	pushq %r12
	callq ext
	callq ext
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	jpe .Lb29
	leaq glob(%rip), %rax
	nop
	ret
	movabsq $81985529216486895, %rcx
	jpe .Lb12
	nop
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	addq $1000, %rdi
	jpo .Lb3
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	je .Lb11
	nop
	jpo .Lb22
	movq %rax, %rbx
	leaq glob(%rip), %rax
	addq $1000, %rdi
	jz .Lb18
.Lb3:
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	jp .Lb24
	ret
	movl $1, %eax
	nop
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	jbe .Lb13
	movabsq $81985529216486895, %rcx
	jo .Lb3
	ret
	ret
	pushq %r12
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	je .Lb33
	callq ext
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	nop
	jna .Lb26
	movabsq $81985529216486895, %rcx
	callq ext
	nop
	movl $1, %eax
.p2align 4
.Lb4:
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	jng .Lb3
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	movq %rax, %rbx
.Lb5:
	ret
	nop
	addq $1000, %rdi
	pushq %r12
	leaq glob(%rip), %rax
	callq ext
	movq %rax, %rbx
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	jc .Lb39
	leaq glob(%rip), %rax
	movl $1, %eax
	nop
	callq ext
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	leaq glob(%rip), %rax
	jng .Lb24
	movabsq $81985529216486895, %rcx
.Lb6:
	ret
	addq $1000, %rdi
	pushq %r12
	nop
	nop
	jpo .Lb2
	movl %eax, 1000(%rsp, %r12, 8)
	jle .Lb35
.p2align 4
.Lb7:
	ret
	jnge .Lb5
	pushq %r12
	leaq glob(%rip), %rax
	movq %rax, %rbx
	jna .Lb34
	callq ext
	leaq glob(%rip), %rax
	nop
	callq ext
	jg .Lb7
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	callq ext
	nop
	jpe .Lb12
	leaq glob(%rip), %rax
	nop
	pushq %r12
	ret
	ret
	addq $1000, %rdi
	movl $1, %eax
	movl $1, %eax
	movq %rax, %rbx
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
.Lb8:
	addq $1000, %rdi
	ret
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
.Lb9:
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	jp .Lb5
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	addq $1000, %rdi
	movq %rax, %rbx
	jz .Lb11
	addq $1000, %rdi
.Lb10:
.Lb11:
	movq %rax, %rbx
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	movq %rax, %rbx
	nop
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	callq ext
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	pushq %r12
	nop
	callq ext
.Lb12:
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	jng .Lb11
	pushq %r12
	pushq %r12
	addq $1000, %rdi
	callq ext
	ret
	nop
	addq $1000, %rdi
	leaq glob(%rip), %rax
.Lb13:
	movabsq $81985529216486895, %rcx
	nop
	movq %rax, %rbx
	jno .Lb15
	leaq glob(%rip), %rax
	jnae .Lb3
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	pushq %r12
	movq %rax, %rbx
	addq $1000, %rdi
	jo .Lb17
	leaq glob(%rip), %rax
	ret
	callq ext
	movq %rax, %rbx
	movl $1, %eax
.Lb14:
	callq ext
	leaq glob(%rip), %rax
	movq %rax, %rbx
	ret
	leaq glob(%rip), %rax
	pushq %r12
	callq ext
	callq ext
	jnc .Lb1
	movl $1, %eax
	ret
	jle .Lb7
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	pushq %r12
	nop
	callq ext
.p2align 4
.Lb15:
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	addq $1000, %rdi
	callq ext
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	addq $1000, %rdi
	movq %rax, %rbx
	addq $1000, %rdi
	movl $1, %eax
	pushq %r12
	addq $1000, %rdi
	ret
	nop
.Lb16:
	movabsq $81985529216486895, %rcx
	jz .Lb4
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	jnl .Lb32
	movl $1, %eax
	jp .Lb14
	leaq glob(%rip), %rax
	movl $1, %eax
	movl $1, %eax
	jae .Lb9
	addq $1000, %rdi
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	movq %rax, %rbx
	pushq %r12
	leaq glob(%rip), %rax
	callq ext
	movabsq $81985529216486895, %rcx
	jg .Lb27
	leaq glob(%rip), %rax
	callq ext
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
.p2align 4
.Lb17:
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	addq $1000, %rdi
	jp .Lb10
	movq %rax, %rbx
	leaq glob(%rip), %rax
	nop
	addq $1000, %rdi
	jae .Lb36
	ret
	jnl .Lb33
.p2align 4
.Lb18:
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	movq %rax, %rbx
	ret
	movl $1, %eax
	ret
	leaq glob(%rip), %rax
	jpe .Lb20
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	movabsq $81985529216486895, %rcx
	jnle .Lb3
	ret
	movl $1, %eax
	leaq glob(%rip), %rax
	nop
	jge .Lb12
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	callq ext
	movl $1, %eax
	movl $1, %eax
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
.Lb19:
	addq $1000, %rdi
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	ret
	movabsq $81985529216486895, %rcx
	jae .Lb7
	leaq glob(%rip), %rax
	addq $1000, %rdi
	nop
	leaq glob(%rip), %rax
	callq ext
	callq ext
	addq $1000, %rdi
.Lb20:
	nop
	jae .Lb26
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
.Lb21:
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	ret
	ret
	leaq glob(%rip), %rax
	addq $1000, %rdi
	addq $1000, %rdi
	addq $1000, %rdi
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	pushq %r12
	jne .Lb9
	movl %eax, 1000(%rsp, %r12, 8)
.p2align 4
.Lb22:
	movq %rax, %rbx
	jae .Lb38
	ret
	jp .Lb8
	movl $1, %eax
	addq $1000, %rdi
	movq %rax, %rbx
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	jnc .Lb11
	movl %eax, 1000(%rsp, %r12, 8)
	jng .Lb30
	leaq glob(%rip), %rax
	nop
	pushq %r12
	movl $1, %eax
	leaq glob(%rip), %rax
	movl $1, %eax
	jp .Lb33
	addq $1000, %rdi
	jnp .Lb27
.p2align 4
.Lb23:
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	movl $1, %eax
	leaq glob(%rip), %rax
	pushq %r12
	movq %rax, %rbx
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	pushq %r12
	jno .Lb3
	ret
	ret
.Lb24:
	addq $1000, %rdi
	leaq glob(%rip), %rax
	jb .Lb37
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	movl $1, %eax
	leaq glob(%rip), %rax
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
.Lb25:
	nop
	ret
	addq $1000, %rdi
	addq $1000, %rdi
	pushq %r12
	leaq glob(%rip), %rax
	ret
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	leaq glob(%rip), %rax
	jo .Lb0
	movl $1, %eax
	pushq %r12
	movabsq $81985529216486895, %rcx
	callq ext
	nop
	callq ext
	movq %rax, %rbx
	movl $1, %eax
	jbe .Lb3
	movl $1, %eax
	ret
.Lb26:
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	ret
	jb .Lb23
	callq ext
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	movq %rax, %rbx
	pushq %r12
	movl $1, %eax
	jc .Lb6
	ret
	addq $1000, %rdi
	callq ext
	leaq glob(%rip), %rax
	callq ext
	movq %rax, %rbx
	addq $1000, %rdi
.Lb27:
	movq %rax, %rbx
	callq ext
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	leaq glob(%rip), %rax
	jo .Lb34
	pushq %r12
	movabsq $81985529216486895, %rcx
	nop
	pushq %r12
	callq ext
	pushq %r12
	movq %rax, %rbx
	movl $1, %eax
	callq ext
	nop
	leaq glob(%rip), %rax
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	ret
	callq ext
	addq $1000, %rdi
	movl $1, %eax
	callq ext
.Lb28:
	pushq %r12
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	jnge .Lb14
	addq $1000, %rdi
.Lb29:
	nop
	leaq glob(%rip), %rax
	movq %rax, %rbx
	jl .Lb7
	movabsq $81985529216486895, %rcx
	pushq %r12
	jnae .Lb39
	ret
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	addq $1000, %rdi
	movq %rax, %rbx
	leaq glob(%rip), %rax
	jng .Lb21
	movl $1, %eax
	callq ext
	jo .Lb15
	nop
	addq $1000, %rdi
	jle .Lb5
	ret
	nop
	callq ext
	callq ext
	movq %rax, %rbx
	movl $1, %eax
	nop
.Lb30:
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	ret
	callq ext
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	pushq %r12
	leaq glob(%rip), %rax
	jna .Lb32
.Lb31:
	pushq %r12
	jnl .Lb16
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	jns .Lb16
	callq ext
	leaq glob(%rip), %rax
	jne .Lb25
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	nop
	ret
	ret
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	nop
	addq $1000, %rdi
	jnl .Lb10
	addq $1000, %rdi
	callq ext
	nop
	movq %rax, %rbx
.Lb32:
	ret
	jl .Lb16
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	nop
	jl .Lb23
	pushq %r12
	jp .Lb19
	movl $1, %eax
	pushq %r12
.Lb33:
	ret
	jpe .Lb14
	movabsq $81985529216486895, %rcx
	ret
	nop
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	jae .Lb35
	movl $1, %eax
	addq $1000, %rdi
	movl $1, %eax
	jpo .Lb3
	callq ext
	addq $1000, %rdi
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	jc .Lb8
.Lb34:
	ret
	nop
	addq $1000, %rdi
	movq %rax, %rbx
	ret
	jg .Lb39
	pushq %r12
	ret
	jo .Lb35
	leaq glob(%rip), %rax
.p2align 4
.Lb35:
	movl $1, %eax
	leaq glob(%rip), %rax
	movl $1, %eax
	jge .Lb26
	leaq glob(%rip), %rax
	movq %rax, %rbx
	leaq glob(%rip), %rax
	nop
	movl $1, %eax
	leaq glob(%rip), %rax
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	jb .Lb36
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	callq ext
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	nop
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	movq %rax, %rbx
.Lb36:
	pushq %r12
	leaq glob(%rip), %rax
	pushq %r12
	callq ext
	nop
	addq $1000, %rdi
	jna .Lb39
	addq $1000, %rdi
	addq $1000, %rdi
	jp .Lb14
	addq $1000, %rdi
	leaq glob(%rip), %rax
	callq ext
	leaq glob(%rip), %rax
	pushq %r12
	callq ext
	callq ext
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
.p2align 4
.Lb37:
	movl $1, %eax
	jz .Lb25
	addq $1000, %rdi
	ret
	jo .Lb32
	nop
	addq $1000, %rdi
	movq %rax, %rbx
	pushq %r12
	jnp .Lb29
	nop
	nop
	addq $1000, %rdi
	ret
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	nop
	callq ext
	je .Lb1
	ret
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
.Lb38:
	addq $1000, %rdi
	movq %rax, %rbx
	je .Lb32
	ret
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	jno .Lb20
	movl $1, %eax
	jp .Lb8
	movq %rax, %rbx
	leaq glob(%rip), %rax
	ret
	jz .Lb21
	leaq glob(%rip), %rax
	ret
	movq %rax, %rbx
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	js .Lb0
	nop
	movl $1, %eax
	addq $1000, %rdi
	jpe .Lb16
	movl %eax, 1000(%rsp, %r12, 8)
	jnge .Lb34
	pushq %r12
	movq %rax, %rbx
	nop
	pushq %r12
	nop
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
.Lb39:
	ret
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	jl .Lb13
	callq ext
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	movq %rax, %rbx
	ret
.type f, @function
.size f, .-f
