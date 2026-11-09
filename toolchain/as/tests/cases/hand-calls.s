.text
f:
	leaq zb(%rip), %rax
	leaq zd(%rip), %rax
	callq h1
	callq h0
	jmp .La
.La:
	ret
.section .text.x,"ax"
h0:
	ret
h1:
	ret
.data
zd: .quad 0
.bss
zb: .fill 8,1,0
