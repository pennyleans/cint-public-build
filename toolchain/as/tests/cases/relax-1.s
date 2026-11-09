.text
.globl f
f:
.p2align 4
.Lb0:
	movq %rax, %rbx
	nop
	nop
	movabsq $81985529216486895, %rcx
	jng .Lb1
	callq ext
	movl $1, %eax
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	movl $1, %eax
	jmp .Lb34
	callq ext
	callq ext
	pushq %r12
	nop
	pushq %r12
	movabsq $81985529216486895, %rcx
	nop
	movl $1, %eax
	pushq %r12
	movq %rax, %rbx
	leaq glob(%rip), %rax
	jng .Lb21
	pushq %r12
	callq ext
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	pushq %r12
.Lb1:
	movabsq $81985529216486895, %rcx
	callq ext
	ret
	addq $1000, %rdi
	jb .Lb32
	ret
	callq ext
	movl $1, %eax
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	ret
	movl $1, %eax
	pushq %r12
	pushq %r12
	pushq %r12
.Lb2:
	nop
	pushq %r12
	movl $1, %eax
	pushq %r12
	pushq %r12
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	callq ext
	addq $1000, %rdi
.p2align 4
.Lb3:
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	movl $1, %eax
	ret
	ret
	pushq %r12
	leaq glob(%rip), %rax
	jno .Lb4
	movl $1, %eax
	leaq glob(%rip), %rax
	movq %rax, %rbx
	ret
	movq %rax, %rbx
	leaq glob(%rip), %rax
	ret
	leaq glob(%rip), %rax
	addq $1000, %rdi
	movq %rax, %rbx
	jz .Lb24
.Lb4:
	leaq glob(%rip), %rax
	jae .Lb32
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	callq ext
	jna .Lb10
	pushq %r12
	pushq %r12
.Lb5:
	pushq %r12
	pushq %r12
	callq ext
	addq $1000, %rdi
	callq ext
	jc .Lb19
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	je .Lb4
	leaq glob(%rip), %rax
	callq ext
	ret
	jns .Lb2
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	movq %rax, %rbx
	callq ext
	nop
	je .Lb24
	pushq %r12
	addq $1000, %rdi
	callq ext
.p2align 4
.Lb6:
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	leaq glob(%rip), %rax
	callq ext
	addq $1000, %rdi
.Lb7:
	nop
	pushq %r12
	movl $1, %eax
	jnae .Lb10
	pushq %r12
	addq $1000, %rdi
	leaq glob(%rip), %rax
	addq $1000, %rdi
	jle .Lb15
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	jnge .Lb35
	movq %rax, %rbx
	callq ext
	jnl .Lb9
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	jnb .Lb35
	movl %eax, 1000(%rsp, %r12, 8)
	jne .Lb17
	leaq glob(%rip), %rax
	movq %rax, %rbx
.p2align 4
.Lb8:
	movl $1, %eax
	movl $1, %eax
	movl $1, %eax
	jnl .Lb7
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	callq ext
	pushq %r12
	leaq glob(%rip), %rax
	nop
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	jl .Lb18
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	movq %rax, %rbx
	jp .Lb20
	nop
	jge .Lb13
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	nop
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	jno .Lb28
.Lb9:
	addq $1000, %rdi
	callq ext
	movl $1, %eax
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	movl $1, %eax
	callq ext
	jle .Lb35
	movq %rax, %rbx
	movl $1, %eax
	leaq glob(%rip), %rax
	addq $1000, %rdi
	ret
	jno .Lb20
	pushq %r12
	ret
	ret
	addq $1000, %rdi
.Lb10:
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	jnge .Lb2
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	movabsq $81985529216486895, %rcx
	callq ext
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	nop
	pushq %r12
	nop
	nop
	jnae .Lb21
	leaq glob(%rip), %rax
	callq ext
	movl $1, %eax
	jns .Lb22
	ret
	ret
	leaq glob(%rip), %rax
	callq ext
	movq %rax, %rbx
	movl $1, %eax
	addq $1000, %rdi
	nop
	movabsq $81985529216486895, %rcx
.Lb11:
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	jnge .Lb4
	pushq %r12
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	addq $1000, %rdi
	nop
	movq %rax, %rbx
	pushq %r12
	ret
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
.Lb12:
	callq ext
	addq $1000, %rdi
	ret
	movl $1, %eax
	movq %rax, %rbx
	movq %rax, %rbx
	movq %rax, %rbx
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	callq ext
	callq ext
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
.Lb13:
	callq ext
	callq ext
	leaq glob(%rip), %rax
.Lb14:
	movl $1, %eax
	pushq %r12
	movl $1, %eax
	jnb .Lb38
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	jnc .Lb12
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	nop
	ret
	nop
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	movl $1, %eax
	js .Lb0
	leaq glob(%rip), %rax
	ret
	jns .Lb23
.Lb15:
	pushq %r12
	pushq %r12
	movq %rax, %rbx
	nop
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	pushq %r12
	jpe .Lb17
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	nop
	pushq %r12
	leaq glob(%rip), %rax
.Lb16:
	pushq %r12
	pushq %r12
	jns .Lb24
	callq ext
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	callq ext
	ret
	callq ext
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	jge .Lb16
	callq ext
	pushq %r12
	nop
	nop
	pushq %r12
	jb .Lb32
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	jmp .Lb28
	ret
.Lb17:
	movq %rax, %rbx
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	jnp .Lb33
	addq $1000, %rdi
	pushq %r12
	ret
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	ret
	leaq glob(%rip), %rax
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	jle .Lb10
	movl %eax, 1000(%rsp, %r12, 8)
.Lb18:
	ret
	leaq glob(%rip), %rax
	ret
	jng .Lb8
	nop
	callq ext
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	jnz .Lb14
	addq $1000, %rdi
	movq %rax, %rbx
	movl $1, %eax
	jmp .Lb38
	movabsq $81985529216486895, %rcx
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	jb .Lb11
	movabsq $81985529216486895, %rcx
	nop
	ret
	movabsq $81985529216486895, %rcx
	nop
	callq ext
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	jno .Lb13
	movl $1, %eax
	jle .Lb0
.Lb19:
	callq ext
	leaq glob(%rip), %rax
	callq ext
	ret
	movl $1, %eax
	jng .Lb9
	pushq %r12
	jnc .Lb24
	ret
	jng .Lb19
	movl $1, %eax
	jnbe .Lb3
	ret
	jnge .Lb17
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	jl .Lb8
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	callq ext
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	pushq %r12
	movl $1, %eax
	pushq %r12
	movabsq $81985529216486895, %rcx
	pushq %r12
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	movq %rax, %rbx
.Lb20:
	callq ext
	movl $1, %eax
	pushq %r12
	addq $1000, %rdi
	jo .Lb20
	ret
	nop
	callq ext
	nop
	jnc .Lb33
	movq %rax, %rbx
	addq $1000, %rdi
	jle .Lb2
	callq ext
	jz .Lb16
	ret
	callq ext
	leaq glob(%rip), %rax
	jnbe .Lb15
	pushq %r12
	addq $1000, %rdi
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	nop
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	jl .Lb18
	ret
	callq ext
	movq %rax, %rbx
	ret
.p2align 4
.Lb21:
	pushq %r12
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	pushq %r12
	pushq %r12
	jg .Lb20
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	ja .Lb17
	nop
	callq ext
	movq %rax, %rbx
	movl $1, %eax
	ja .Lb33
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	addq $1000, %rdi
	nop
	addq $1000, %rdi
	ret
	jnp .Lb16
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	js .Lb6
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
.p2align 4
.Lb22:
	movabsq $81985529216486895, %rcx
	ret
.Lb23:
	addq $1000, %rdi
	nop
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	leaq glob(%rip), %rax
	movl $1, %eax
	callq ext
	nop
	jng .Lb39
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	nop
	jpo .Lb19
	movl $1, %eax
	leaq glob(%rip), %rax
.Lb24:
	pushq %r12
	pushq %r12
	callq ext
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	jns .Lb28
	ret
	ret
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	jnz .Lb26
.p2align 4
.Lb25:
	movl $1, %eax
	movq %rax, %rbx
	callq ext
	leaq glob(%rip), %rax
	addq $1000, %rdi
	nop
	callq ext
	movq %rax, %rbx
	ret
	movl $1, %eax
	leaq glob(%rip), %rax
	ret
	leaq glob(%rip), %rax
	callq ext
	pushq %r12
	callq ext
	callq ext
	nop
	nop
	callq ext
	movq %rax, %rbx
	jnle .Lb13
.p2align 4
.Lb26:
	movl $1, %eax
	ja .Lb9
	movq %rax, %rbx
	ret
	movq %rax, %rbx
	movl $1, %eax
	pushq %r12
	movl $1, %eax
	movq %rax, %rbx
	callq ext
	movq %rax, %rbx
	leaq glob(%rip), %rax
	movl $1, %eax
	movq %rax, %rbx
	movq %rax, %rbx
	leaq glob(%rip), %rax
	nop
	movq %rax, %rbx
	nop
	jp .Lb24
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	pushq %r12
	nop
	pushq %r12
.Lb27:
	addq $1000, %rdi
	movq %rax, %rbx
	jg .Lb22
	leaq glob(%rip), %rax
	je .Lb28
	movq %rax, %rbx
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	ret
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	nop
	leaq glob(%rip), %rax
	nop
	nop
	ret
	ret
	movl %eax, 1000(%rsp, %r12, 8)
.Lb28:
	pushq %r12
.Lb29:
	movabsq $81985529216486895, %rcx
	nop
	movq %rax, %rbx
	jpo .Lb8
	leaq glob(%rip), %rax
	pushq %r12
	movl $1, %eax
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	movl $1, %eax
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	jnc .Lb14
	addq $1000, %rdi
	pushq %r12
	movq %rax, %rbx
	ret
	jnp .Lb9
	movl %eax, 1000(%rsp, %r12, 8)
	jno .Lb4
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	movl $1, %eax
.p2align 4
.Lb30:
	callq ext
	movabsq $81985529216486895, %rcx
	jnc .Lb21
	movl $1, %eax
	addq $1000, %rdi
	movq %rax, %rbx
	ret
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	callq ext
	callq ext
	movabsq $81985529216486895, %rcx
	pushq %r12
	jnbe .Lb38
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	pushq %r12
	ret
	movq %rax, %rbx
	movq %rax, %rbx
	pushq %r12
	pushq %r12
	pushq %r12
	movl $1, %eax
	nop
	ret
	jc .Lb37
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
.p2align 4
.Lb31:
	callq ext
	callq ext
	jc .Lb38
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
.Lb32:
.Lb33:
	ret
	pushq %r12
	movq %rax, %rbx
	callq ext
	callq ext
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	movl $1, %eax
	movl $1, %eax
	movq %rax, %rbx
	pushq %r12
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	ret
	jg .Lb2
	addq $1000, %rdi
.Lb34:
	movl $1, %eax
	callq ext
	leaq glob(%rip), %rax
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	pushq %r12
	jnp .Lb21
	movq %rax, %rbx
	ret
.Lb35:
	addq $1000, %rdi
	movq %rax, %rbx
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	ret
	movabsq $81985529216486895, %rcx
	jz .Lb21
	movabsq $81985529216486895, %rcx
	nop
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	ret
	jb .Lb26
	movl $1, %eax
	ret
	callq ext
	leaq glob(%rip), %rax
	ret
	movq %rax, %rbx
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	callq ext
	pushq %r12
	callq ext
	ret
	movabsq $81985529216486895, %rcx
	jg .Lb34
	pushq %r12
.p2align 4
.Lb36:
	movl $1, %eax
	callq ext
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	jnge .Lb25
	nop
	jnz .Lb3
	movq %rax, %rbx
	nop
	jnb .Lb33
	movl $1, %eax
	jna .Lb19
	leaq glob(%rip), %rax
	ret
	pushq %r12
	addq $1000, %rdi
	nop
	callq ext
	callq ext
	movabsq $81985529216486895, %rcx
.p2align 4
.Lb37:
	ret
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	jz .Lb23
	ret
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	movl $1, %eax
	jnle .Lb8
	leaq glob(%rip), %rax
	movq %rax, %rbx
	pushq %r12
	pushq %r12
	movabsq $81985529216486895, %rcx
	pushq %r12
	movabsq $81985529216486895, %rcx
	jc .Lb4
	movl $1, %eax
	jnz .Lb25
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	movabsq $81985529216486895, %rcx
	ret
	callq ext
	ret
	ret
	pushq %r12
	pushq %r12
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
.Lb38:
	movl $1, %eax
	jnae .Lb20
	ret
	movq %rax, %rbx
	jpe .Lb27
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	callq ext
	jnl .Lb13
	callq ext
	pushq %r12
	jnc .Lb37
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	movabsq $81985529216486895, %rcx
	ret
	pushq %r12
	callq ext
	callq ext
	nop
	jnae .Lb8
	pushq %r12
	ja .Lb2
.p2align 4
.Lb39:
	pushq %r12
	addq $1000, %rdi
	movq %rax, %rbx
	jle .Lb2
	callq ext
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	addq $1000, %rdi
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	addq $1000, %rdi
	movl $1, %eax
	nop
	jns .Lb19
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	jge .Lb38
	ret
.type f, @function
.size f, .-f
