# expect: 8: .size takes a constant or . - NAME of the same section
.data
y:
	.byte 1
.text
x:
	ret
.size x, .-y
