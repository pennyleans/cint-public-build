.data
.balign 8
.globl d0
d0:
	.byte 1
	.byte 255, -1, 128
	.short 7, -2, 65535
	.int 1, -1, 4294967295
	.quad 1, -1, 9223372036854775807, -9223372036854775808
	.quad d1
	.quad d1+8
	.quad d0-8
	.quad ext
	.quad ext+24
	.int d1
	.int ext
	.short d1
	.byte d1
	.ascii "hi\n\000\"x\\ # ; /* tab\t\x41\101\7"
	.ascii "a", "b"
	.fill 3,1,0
	.fill 2,4,0x01020304
	.fill 2,8,7
	.zero 5
	.p2align 4
d1:
	.quad 0x10, 010, 0b101
.bss
.balign 32
b0:
	.fill 100,1,0
.balign 8
b1:
	.zero 16
.section .rodata
r0:
	.int 3
.section .data.rel.ro,"aw"
.balign 8
	.quad r0
	.quad b1+4
.section .init_array,"aw"
	.quad f0
.section .tdata,"awT",@progbits
.balign 8
t0:
	.quad 9
.section .tbss,"awT",@nobits
t1:
	.fill 4,1,0
.comm c0,3
.comm c1,24
.comm c2,100,32
.comm c3,8,4
.text
f0:
	ret
.type f0, @function
.size f0, .-f0
.type d0, @object
.size d0, 8
.section .note.GNU-stack,"",@progbits
