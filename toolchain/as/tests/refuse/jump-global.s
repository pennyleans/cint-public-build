# expect: 0: jump target is not a local label of this section
.text
	jmp .Lnowhere
