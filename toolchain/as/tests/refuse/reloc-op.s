# expect: 3: relocation operator not accepted here
.text
	addq $x@plt, %rax
