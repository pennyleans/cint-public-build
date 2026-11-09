.text
.globl main
main:
	callq domain
	callq in
	callq ain
	callq xmain
	ret
.data
.balign 8
.globl text
text:
	.byte 1
	.p2align 3
	.quad 2
.section .data.rel,"aw"
.byte 1
.section .text.hot,"ax",@progbits
	ret
.section .rodata.str,"a"
	.ascii "x\000"
