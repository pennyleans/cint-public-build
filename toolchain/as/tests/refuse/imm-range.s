# expect: 3: immediate out of range
.text
	addq $4294967296, %rax
