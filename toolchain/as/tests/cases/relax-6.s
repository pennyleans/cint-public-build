.text
.globl f
f:
.Lb0:
	movq %rax, %rbx
	leaq glob(%rip), %rax
	jnp .Lb9
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	callq ext
	pushq %r12
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	movq %rax, %rbx
	callq ext
	nop
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	jns .Lb2
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	pushq %r12
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	movq %rax, %rbx
	jae .Lb33
	movq %rax, %rbx
.p2align 4
.Lb1:
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	jnl .Lb13
	pushq %r12
	jmp .Lb10
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	callq ext
	nop
	jp .Lb27
	callq ext
	jnb .Lb28
	movq %rax, %rbx
	nop
	callq ext
	pushq %r12
	leaq glob(%rip), %rax
	callq ext
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	ret
	jb .Lb34
	leaq glob(%rip), %rax
	addq $1000, %rdi
	pushq %r12
	pushq %r12
	callq ext
.Lb2:
	leaq glob(%rip), %rax
	nop
	callq ext
	nop
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	ret
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
	addq $1000, %rdi
.Lb3:
	movl %eax, 1000(%rsp, %r12, 8)
	jc .Lb12
	leaq glob(%rip), %rax
	callq ext
	pushq %r12
	nop
	addq $1000, %rdi
	ret
	movabsq $81985529216486895, %rcx
	ret
	jpo .Lb6
	movq %rax, %rbx
	jna .Lb30
	movq %rax, %rbx
	pushq %r12
	jnae .Lb5
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	nop
	movl $1, %eax
	ret
	movabsq $81985529216486895, %rcx
	nop
	nop
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	jne .Lb18
	leaq glob(%rip), %rax
	movl $1, %eax
	jnc .Lb17
	leaq glob(%rip), %rax
	callq ext
	addq $1000, %rdi
	jnb .Lb5
.Lb4:
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	addq $1000, %rdi
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	ret
	movq %rax, %rbx
	ret
	nop
	ret
	jmp .Lb33
	ret
	ret
	ret
	callq ext
	movabsq $81985529216486895, %rcx
	ret
	ret
	movabsq $81985529216486895, %rcx
	movl $1, %eax
.Lb5:
	addq $1000, %rdi
	pushq %r12
	callq ext
	pushq %r12
	ret
	pushq %r12
.Lb6:
	ret
	ret
	ret
	movl $1, %eax
	jnge .Lb30
	movl $1, %eax
	jb .Lb34
	addq $1000, %rdi
	movl $1, %eax
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	jl .Lb4
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	nop
	jpo .Lb7
	ret
	jnge .Lb1
	callq ext
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	pushq %r12
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	addq $1000, %rdi
.p2align 4
.Lb7:
	movl $1, %eax
	movq %rax, %rbx
	movl $1, %eax
	addq $1000, %rdi
	movl $1, %eax
	movl $1, %eax
	ja .Lb4
	movl $1, %eax
	leaq glob(%rip), %rax
	jp .Lb37
	leaq glob(%rip), %rax
	ret
	ret
	callq ext
	callq ext
	nop
	leaq glob(%rip), %rax
	jae .Lb2
	nop
	jns .Lb11
	movq %rax, %rbx
	movl $1, %eax
	movq %rax, %rbx
.Lb8:
	pushq %r12
	pushq %r12
	nop
	callq ext
	nop
	movl $1, %eax
	leaq glob(%rip), %rax
	leaq glob(%rip), %rax
	addq $1000, %rdi
	leaq glob(%rip), %rax
	callq ext
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	callq ext
	jnbe .Lb9
	movl $1, %eax
.Lb9:
	addq $1000, %rdi
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	movq %rax, %rbx
	movl $1, %eax
	jp .Lb38
	ret
	pushq %r12
	jno .Lb5
	movq %rax, %rbx
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	ret
	ret
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	jns .Lb12
.Lb10:
	addq $1000, %rdi
	movq %rax, %rbx
	movl $1, %eax
	ja .Lb1
	nop
.p2align 4
.Lb11:
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	jne .Lb9
	nop
	movl $1, %eax
	ret
	jne .Lb28
	movl $1, %eax
	ret
	nop
	nop
	jge .Lb12
	pushq %r12
	ret
	leaq glob(%rip), %rax
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	nop
	leaq glob(%rip), %rax
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	leaq glob(%rip), %rax
	addq $1000, %rdi
	ret
.p2align 4
.Lb12:
	addq $1000, %rdi
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	pushq %r12
	movq %rax, %rbx
	movq %rax, %rbx
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	movl $1, %eax
	pushq %r12
	leaq glob(%rip), %rax
	addq $1000, %rdi
	callq ext
	movq %rax, %rbx
	pushq %r12
	ret
	movl $1, %eax
	addq $1000, %rdi
	pushq %r12
	jo .Lb7
	nop
	movq %rax, %rbx
	leaq glob(%rip), %rax
	movq %rax, %rbx
	leaq glob(%rip), %rax
	callq ext
	movq %rax, %rbx
	leaq glob(%rip), %rax
.Lb13:
	leaq glob(%rip), %rax
	movl $1, %eax
	pushq %r12
	callq ext
	leaq glob(%rip), %rax
	movl $1, %eax
	js .Lb8
	movq %rax, %rbx
	ja .Lb16
	movabsq $81985529216486895, %rcx
	jnge .Lb17
.p2align 4
.Lb14:
	ret
	jge .Lb1
	movabsq $81985529216486895, %rcx
	callq ext
	nop
.Lb15:
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	movabsq $81985529216486895, %rcx
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
.Lb16:
	movq %rax, %rbx
	movl $1, %eax
	addq $1000, %rdi
	leaq glob(%rip), %rax
	callq ext
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	nop
	nop
	ret
	callq ext
	nop
	movq %rax, %rbx
	nop
	jp .Lb6
	movq %rax, %rbx
.Lb17:
	movl $1, %eax
	pushq %r12
	addq $1000, %rdi
	ret
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	jnl .Lb22
	movabsq $81985529216486895, %rcx
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	callq ext
	movl $1, %eax
	callq ext
	nop
	addq $1000, %rdi
	jge .Lb34
	movabsq $81985529216486895, %rcx
	jnl .Lb2
	movl $1, %eax
	movq %rax, %rbx
	nop
	nop
.Lb18:
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	addq $1000, %rdi
.p2align 4
.Lb19:
	leaq glob(%rip), %rax
	pushq %r12
	jae .Lb12
	callq ext
	pushq %r12
	movl $1, %eax
	movl $1, %eax
	movq %rax, %rbx
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
	ret
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	movl $1, %eax
	jnz .Lb6
	pushq %r12
	movabsq $81985529216486895, %rcx
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	movq %rax, %rbx
.Lb20:
	pushq %r12
	ret
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	callq ext
	nop
	leaq glob(%rip), %rax
	callq ext
	movl $1, %eax
	addq $1000, %rdi
	addq $1000, %rdi
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	pushq %r12
	pushq %r12
	jnb .Lb4
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	js .Lb13
	leaq glob(%rip), %rax
	jo .Lb35
	nop
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movl $1, %eax
	leaq glob(%rip), %rax
	ret
.Lb21:
	pushq %r12
	movq %rax, %rbx
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	jp .Lb1
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	callq ext
	movl $1, %eax
	jle .Lb35
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	ret
	jge .Lb36
	pushq %r12
	movq %rax, %rbx
	jo .Lb35
	addq $1000, %rdi
	callq ext
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	addq $1000, %rdi
.Lb22:
	pushq %r12
	movabsq $81985529216486895, %rcx
	jle .Lb5
	callq ext
	callq ext
	jna .Lb32
	addq $1000, %rdi
	ret
	jb .Lb4
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	ret
	movl $1, %eax
	leaq glob(%rip), %rax
	movl %eax, 1000(%rsp, %r12, 8)
	jnae .Lb30
	movl $1, %eax
	jnb .Lb22
	ret
	callq ext
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	callq ext
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
.Lb23:
	addq $1000, %rdi
	leaq glob(%rip), %rax
	addq $1000, %rdi
	addq $1000, %rdi
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	jnbe .Lb22
	movl $1, %eax
	jnae .Lb7
	movl $1, %eax
	addq $1000, %rdi
	jc .Lb2
	movabsq $81985529216486895, %rcx
	jnc .Lb7
	ret
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	movl $1, %eax
	addq $1000, %rdi
	callq ext
	jl .Lb1
	addq $1000, %rdi
	ret
	nop
.Lb24:
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	leaq glob(%rip), %rax
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	leaq glob(%rip), %rax
	movl $1, %eax
	movq %rax, %rbx
	jns .Lb0
	addq $1000, %rdi
	pushq %r12
	addq $1000, %rdi
	addq $1000, %rdi
	movq %rax, %rbx
	movq %rax, %rbx
	callq ext
	jg .Lb23
	movabsq $81985529216486895, %rcx
	jpo .Lb16
	movq %rax, %rbx
.p2align 4
.Lb25:
.Lb26:
	movq %rax, %rbx
	movl $1, %eax
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movl $1, %eax
	movq %rax, %rbx
	movq %rax, %rbx
	callq ext
.Lb27:
	addq $1000, %rdi
	jc .Lb4
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	callq ext
	addq $1000, %rdi
	nop
	movq %rax, %rbx
	ret
	movabsq $81985529216486895, %rcx
	ret
	movl %eax, 1000(%rsp, %r12, 8)
.Lb28:
.Lb29:
	pushq %r12
	movl $1, %eax
	movq %rax, %rbx
	addq $1000, %rdi
	jle .Lb20
	pushq %r12
	nop
	movq %rax, %rbx
	callq ext
	jmp .Lb9
	nop
	jnl .Lb7
	movq %rax, %rbx
	jae .Lb13
	pushq %r12
	movabsq $81985529216486895, %rcx
	jp .Lb6
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
.Lb30:
	addq $1000, %rdi
	jp .Lb6
	nop
	jpe .Lb21
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	nop
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	movq %rax, %rbx
	movq %rax, %rbx
	jb .Lb7
	callq ext
	movq %rax, %rbx
	ret
	nop
	ret
	jnae .Lb0
	movq %rax, %rbx
	jpe .Lb19
	callq ext
	callq ext
	jnle .Lb22
	movq %rax, %rbx
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movq %rax, %rbx
	movl %eax, 1000(%rsp, %r12, 8)
.p2align 4
.Lb31:
	leaq glob(%rip), %rax
	ret
	leaq glob(%rip), %rax
	jpo .Lb18
	movabsq $81985529216486895, %rcx
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	callq ext
	movabsq $81985529216486895, %rcx
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	pushq %r12
	jnbe .Lb9
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	ret
	addq $1000, %rdi
	movabsq $81985529216486895, %rcx
	nop
	movq %rax, %rbx
	movl $1, %eax
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	leaq glob(%rip), %rax
	movq %rax, %rbx
	nop
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	movq %rax, %rbx
.Lb32:
	callq ext
.p2align 4
.Lb33:
	leaq glob(%rip), %rax
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	pushq %r12
	pushq %r12
	jng .Lb38
	pushq %r12
	movabsq $81985529216486895, %rcx
	movabsq $81985529216486895, %rcx
	ret
	leaq glob(%rip), %rax
	pushq %r12
	nop
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	addq $1000, %rdi
	ret
	leaq glob(%rip), %rax
	pushq %r12
	jnp .Lb4
	movq %rax, %rbx
	callq ext
	nop
	ret
.p2align 4
.Lb34:
	movabsq $81985529216486895, %rcx
.Lb35:
	movl $1, %eax
	movl $1, %eax
	nop
	pushq %r12
	addq $1000, %rdi
	jpo .Lb17
	callq ext
	pushq %r12
	movl %eax, 1000(%rsp, %r12, 8)
	movl $1, %eax
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	ret
	ret
	movl %eax, 1000(%rsp, %r12, 8)
	nop
	movl $1, %eax
	movabsq $81985529216486895, %rcx
	callq ext
	movq %rax, %rbx
	jnz .Lb12
	ret
	pushq %r12
.Lb36:
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	pushq %r12
	leaq glob(%rip), %rax
	pushq %r12
	addq $1000, %rdi
	leaq glob(%rip), %rax
	movabsq $81985529216486895, %rcx
	movq %rax, %rbx
	leaq glob(%rip), %rax
	movl $1, %eax
	ret
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	callq ext
	nop
	addq $1000, %rdi
	ja .Lb33
	movl %eax, 1000(%rsp, %r12, 8)
.Lb37:
	movq %rax, %rbx
	nop
	addq $1000, %rdi
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
.Lb38:
	movabsq $81985529216486895, %rcx
	ret
	addq $1000, %rdi
	callq ext
	movl %eax, 1000(%rsp, %r12, 8)
	movabsq $81985529216486895, %rcx
	ja .Lb8
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	leaq glob(%rip), %rax
	addq $1000, %rdi
	jnge .Lb24
	movl %eax, 1000(%rsp, %r12, 8)
	jnle .Lb22
	addq $1000, %rdi
	movq %rax, %rbx
.p2align 4
.Lb39:
	movq %rax, %rbx
	jnle .Lb13
	callq ext
	movabsq $81985529216486895, %rcx
	movl $1, %eax
	jae .Lb34
	leaq glob(%rip), %rax
	movl $1, %eax
	movq %rax, %rbx
	pushq %r12
	movl $1, %eax
	movl %eax, 1000(%rsp, %r12, 8)
	pushq %r12
	callq ext
	movabsq $81985529216486895, %rcx
	jnae .Lb27
	movq %rax, %rbx
	addq $1000, %rdi
	ret
.type f, @function
.size f, .-f
