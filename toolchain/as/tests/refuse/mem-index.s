# expect: 3: memory operand not accepted
.text
	movq (%rax,%rsp), %rbx
