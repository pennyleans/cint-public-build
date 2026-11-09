.text
	imulw %ax, %bp
	imulw %r11w, %ax
	imulw %cx, %bp
	imulw %r11w, %cx
	imulw %dx, %bp
	imulw %r11w, %dx
	imulw %bx, %bp
	imulw %r11w, %bx
	imulw %sp, %bp
	imulw %r11w, %sp
	imulw %bp, %bp
	imulw %r11w, %bp
	imulw %si, %bp
	imulw %r11w, %si
	imulw %di, %bp
	imulw %r11w, %di
	imulw %r8w, %bp
	imulw %r11w, %r8w
	imulw %r9w, %bp
	imulw %r11w, %r9w
	imulw %r10w, %bp
	imulw %r11w, %r10w
	imulw %r11w, %bp
	imulw %r11w, %r11w
	imulw %r12w, %bp
	imulw %r11w, %r12w
	imulw %r13w, %bp
	imulw %r11w, %r13w
	imulw %r14w, %bp
	imulw %r11w, %r14w
	imulw %r15w, %bp
	imulw %r11w, %r15w
	imulw (%rax), %ax
	imulw $3, (%rax), %r13w
	imulw $300, (%rax), %dx
	imulw (%rsp), %ax
	imulw $3, (%rsp), %r13w
	imulw $300, (%rsp), %dx
	imulw (%rbp), %ax
	imulw $3, (%rbp), %r13w
	imulw $300, (%rbp), %dx
	imulw (%r12), %ax
	imulw $3, (%r12), %r13w
	imulw $300, (%r12), %dx
	imulw (%r13), %ax
	imulw $3, (%r13), %r13w
	imulw $300, (%r13), %dx
	imulw (%r15), %ax
	imulw $3, (%r15), %r13w
	imulw $300, (%r15), %dx
	imulw 8(%rbx), %ax
	imulw $3, 8(%rbx), %r13w
	imulw $300, 8(%rbx), %dx
	imulw -8(%rbp), %ax
	imulw $3, -8(%rbp), %r13w
	imulw $300, -8(%rbp), %dx
	imulw -128(%rcx), %ax
	imulw $3, -128(%rcx), %r13w
	imulw $300, -128(%rcx), %dx
	imulw -129(%rdx), %ax
	imulw $3, -129(%rdx), %r13w
	imulw $300, -129(%rdx), %dx
	imulw 127(%rsi), %ax
	imulw $3, 127(%rsi), %r13w
	imulw $300, 127(%rsi), %dx
	imulw 128(%rdi), %ax
	imulw $3, 128(%rdi), %r13w
	imulw $300, 128(%rdi), %dx
	imulw 4096(%r8), %ax
	imulw $3, 4096(%r8), %r13w
	imulw $300, 4096(%r8), %dx
	imulw (%rax, %rcx), %ax
	imulw $3, (%rax, %rcx), %r13w
	imulw $300, (%rax, %rcx), %dx
	imulw (%rax,%rcx,2), %ax
	imulw $3, (%rax,%rcx,2), %r13w
	imulw $300, (%rax,%rcx,2), %dx
	imulw 8(%rsp, %rbp, 4), %ax
	imulw $3, 8(%rsp, %rbp, 4), %r13w
	imulw $300, 8(%rsp, %rbp, 4), %dx
	imulw -16(%r13, %r14, 8), %ax
	imulw $3, -16(%r13, %r14, 8), %r13w
	imulw $300, -16(%r13, %r14, 8), %dx
	imulw (, %rcx, 8), %ax
	imulw $3, (, %rcx, 8), %r13w
	imulw $300, (, %rcx, 8), %dx
	imulw 16(, %r9, 4), %ax
	imulw $3, 16(, %r9, 4), %r13w
	imulw $300, 16(, %r9, 4), %dx
	imulw 1234, %ax
	imulw $3, 1234, %r13w
	imulw $300, 1234, %dx
	imulw glob(%rip), %ax
	imulw $3, glob(%rip), %r13w
	imulw $300, glob(%rip), %dx
	imulw loc+8(%rip), %ax
	imulw $3, loc+8(%rip), %r13w
	imulw $300, loc+8(%rip), %dx
	imulw ext-4(%rip), %ax
	imulw $3, ext-4(%rip), %r13w
	imulw $300, ext-4(%rip), %dx
	imulw glob(%rax), %ax
	imulw $3, glob(%rax), %r13w
	imulw $300, glob(%rax), %dx
	imulw ext+16(%rbx, %rcx, 2), %ax
	imulw $3, ext+16(%rbx, %rcx, 2), %r13w
	imulw $300, ext+16(%rbx, %rcx, 2), %dx
	imulw %fs:0, %ax
	imulw $3, %fs:0, %r13w
	imulw $300, %fs:0, %dx
	imulw %fs:8(%rax), %ax
	imulw $3, %fs:8(%rax), %r13w
	imulw $300, %fs:8(%rax), %dx
	imulw $1, %ax, %di
	imulw $1, %ax
	imulw $1, %cx, %di
	imulw $1, %cx
	imulw $1, %dx, %di
	imulw $1, %dx
	imulw $1, %bx, %di
	imulw $1, %bx
	imulw $1, %sp, %di
	imulw $1, %sp
	imulw $1, %bp, %di
	imulw $1, %bp
	imulw $1, %si, %di
	imulw $1, %si
	imulw $1, %di, %di
	imulw $1, %di
	imulw $1, %r8w, %di
	imulw $1, %r8w
	imulw $1, %r9w, %di
	imulw $1, %r9w
	imulw $1, %r10w, %di
	imulw $1, %r10w
	imulw $1, %r11w, %di
	imulw $1, %r11w
	imulw $1, %r12w, %di
	imulw $1, %r12w
	imulw $1, %r13w, %di
	imulw $1, %r13w
	imulw $1, %r14w, %di
	imulw $1, %r14w
	imulw $1, %r15w, %di
	imulw $1, %r15w
	imulw $-1, %ax, %di
	imulw $-1, %ax
	imulw $-1, %cx, %di
	imulw $-1, %cx
	imulw $-1, %dx, %di
	imulw $-1, %dx
	imulw $-1, %bx, %di
	imulw $-1, %bx
	imulw $-1, %sp, %di
	imulw $-1, %sp
	imulw $-1, %bp, %di
	imulw $-1, %bp
	imulw $-1, %si, %di
	imulw $-1, %si
	imulw $-1, %di, %di
	imulw $-1, %di
	imulw $-1, %r8w, %di
	imulw $-1, %r8w
	imulw $-1, %r9w, %di
	imulw $-1, %r9w
	imulw $-1, %r10w, %di
	imulw $-1, %r10w
	imulw $-1, %r11w, %di
	imulw $-1, %r11w
	imulw $-1, %r12w, %di
	imulw $-1, %r12w
	imulw $-1, %r13w, %di
	imulw $-1, %r13w
	imulw $-1, %r14w, %di
	imulw $-1, %r14w
	imulw $-1, %r15w, %di
	imulw $-1, %r15w
	imulw $127, %ax, %di
	imulw $127, %ax
	imulw $127, %cx, %di
	imulw $127, %cx
	imulw $127, %dx, %di
	imulw $127, %dx
	imulw $127, %bx, %di
	imulw $127, %bx
	imulw $127, %sp, %di
	imulw $127, %sp
	imulw $127, %bp, %di
	imulw $127, %bp
	imulw $127, %si, %di
	imulw $127, %si
	imulw $127, %di, %di
	imulw $127, %di
	imulw $127, %r8w, %di
	imulw $127, %r8w
	imulw $127, %r9w, %di
	imulw $127, %r9w
	imulw $127, %r10w, %di
	imulw $127, %r10w
	imulw $127, %r11w, %di
	imulw $127, %r11w
	imulw $127, %r12w, %di
	imulw $127, %r12w
	imulw $127, %r13w, %di
	imulw $127, %r13w
	imulw $127, %r14w, %di
	imulw $127, %r14w
	imulw $127, %r15w, %di
	imulw $127, %r15w
	imulw $128, %ax, %di
	imulw $128, %ax
	imulw $128, %cx, %di
	imulw $128, %cx
	imulw $128, %dx, %di
	imulw $128, %dx
	imulw $128, %bx, %di
	imulw $128, %bx
	imulw $128, %sp, %di
	imulw $128, %sp
	imulw $128, %bp, %di
	imulw $128, %bp
	imulw $128, %si, %di
	imulw $128, %si
	imulw $128, %di, %di
	imulw $128, %di
	imulw $128, %r8w, %di
	imulw $128, %r8w
	imulw $128, %r9w, %di
	imulw $128, %r9w
	imulw $128, %r10w, %di
	imulw $128, %r10w
	imulw $128, %r11w, %di
	imulw $128, %r11w
	imulw $128, %r12w, %di
	imulw $128, %r12w
	imulw $128, %r13w, %di
	imulw $128, %r13w
	imulw $128, %r14w, %di
	imulw $128, %r14w
	imulw $128, %r15w, %di
	imulw $128, %r15w
	imulw $-129, %ax, %di
	imulw $-129, %ax
	imulw $-129, %cx, %di
	imulw $-129, %cx
	imulw $-129, %dx, %di
	imulw $-129, %dx
	imulw $-129, %bx, %di
	imulw $-129, %bx
	imulw $-129, %sp, %di
	imulw $-129, %sp
	imulw $-129, %bp, %di
	imulw $-129, %bp
	imulw $-129, %si, %di
	imulw $-129, %si
	imulw $-129, %di, %di
	imulw $-129, %di
	imulw $-129, %r8w, %di
	imulw $-129, %r8w
	imulw $-129, %r9w, %di
	imulw $-129, %r9w
	imulw $-129, %r10w, %di
	imulw $-129, %r10w
	imulw $-129, %r11w, %di
	imulw $-129, %r11w
	imulw $-129, %r12w, %di
	imulw $-129, %r12w
	imulw $-129, %r13w, %di
	imulw $-129, %r13w
	imulw $-129, %r14w, %di
	imulw $-129, %r14w
	imulw $-129, %r15w, %di
	imulw $-129, %r15w
	imulw $4096, %ax, %di
	imulw $4096, %ax
	imulw $4096, %cx, %di
	imulw $4096, %cx
	imulw $4096, %dx, %di
	imulw $4096, %dx
	imulw $4096, %bx, %di
	imulw $4096, %bx
	imulw $4096, %sp, %di
	imulw $4096, %sp
	imulw $4096, %bp, %di
	imulw $4096, %bp
	imulw $4096, %si, %di
	imulw $4096, %si
	imulw $4096, %di, %di
	imulw $4096, %di
	imulw $4096, %r8w, %di
	imulw $4096, %r8w
	imulw $4096, %r9w, %di
	imulw $4096, %r9w
	imulw $4096, %r10w, %di
	imulw $4096, %r10w
	imulw $4096, %r11w, %di
	imulw $4096, %r11w
	imulw $4096, %r12w, %di
	imulw $4096, %r12w
	imulw $4096, %r13w, %di
	imulw $4096, %r13w
	imulw $4096, %r14w, %di
	imulw $4096, %r14w
	imulw $4096, %r15w, %di
	imulw $4096, %r15w
	imull %eax, %ebp
	imull %r11d, %eax
	imull %ecx, %ebp
	imull %r11d, %ecx
	imull %edx, %ebp
	imull %r11d, %edx
	imull %ebx, %ebp
	imull %r11d, %ebx
	imull %esp, %ebp
	imull %r11d, %esp
	imull %ebp, %ebp
	imull %r11d, %ebp
	imull %esi, %ebp
	imull %r11d, %esi
	imull %edi, %ebp
	imull %r11d, %edi
	imull %r8d, %ebp
	imull %r11d, %r8d
	imull %r9d, %ebp
	imull %r11d, %r9d
	imull %r10d, %ebp
	imull %r11d, %r10d
	imull %r11d, %ebp
	imull %r11d, %r11d
	imull %r12d, %ebp
	imull %r11d, %r12d
	imull %r13d, %ebp
	imull %r11d, %r13d
	imull %r14d, %ebp
	imull %r11d, %r14d
	imull %r15d, %ebp
	imull %r11d, %r15d
	imull (%rax), %eax
	imull $3, (%rax), %r13d
	imull $300, (%rax), %edx
	imull (%rsp), %eax
	imull $3, (%rsp), %r13d
	imull $300, (%rsp), %edx
	imull (%rbp), %eax
	imull $3, (%rbp), %r13d
	imull $300, (%rbp), %edx
	imull (%r12), %eax
	imull $3, (%r12), %r13d
	imull $300, (%r12), %edx
	imull (%r13), %eax
	imull $3, (%r13), %r13d
	imull $300, (%r13), %edx
	imull (%r15), %eax
	imull $3, (%r15), %r13d
	imull $300, (%r15), %edx
	imull 8(%rbx), %eax
	imull $3, 8(%rbx), %r13d
	imull $300, 8(%rbx), %edx
	imull -8(%rbp), %eax
	imull $3, -8(%rbp), %r13d
	imull $300, -8(%rbp), %edx
	imull -128(%rcx), %eax
	imull $3, -128(%rcx), %r13d
	imull $300, -128(%rcx), %edx
	imull -129(%rdx), %eax
	imull $3, -129(%rdx), %r13d
	imull $300, -129(%rdx), %edx
	imull 127(%rsi), %eax
	imull $3, 127(%rsi), %r13d
	imull $300, 127(%rsi), %edx
	imull 128(%rdi), %eax
	imull $3, 128(%rdi), %r13d
	imull $300, 128(%rdi), %edx
	imull 4096(%r8), %eax
	imull $3, 4096(%r8), %r13d
	imull $300, 4096(%r8), %edx
	imull (%rax, %rcx), %eax
	imull $3, (%rax, %rcx), %r13d
	imull $300, (%rax, %rcx), %edx
	imull (%rax,%rcx,2), %eax
	imull $3, (%rax,%rcx,2), %r13d
	imull $300, (%rax,%rcx,2), %edx
	imull 8(%rsp, %rbp, 4), %eax
	imull $3, 8(%rsp, %rbp, 4), %r13d
	imull $300, 8(%rsp, %rbp, 4), %edx
	imull -16(%r13, %r14, 8), %eax
	imull $3, -16(%r13, %r14, 8), %r13d
	imull $300, -16(%r13, %r14, 8), %edx
	imull (, %rcx, 8), %eax
	imull $3, (, %rcx, 8), %r13d
	imull $300, (, %rcx, 8), %edx
	imull 16(, %r9, 4), %eax
	imull $3, 16(, %r9, 4), %r13d
	imull $300, 16(, %r9, 4), %edx
	imull 1234, %eax
	imull $3, 1234, %r13d
	imull $300, 1234, %edx
	imull glob(%rip), %eax
	imull $3, glob(%rip), %r13d
	imull $300, glob(%rip), %edx
	imull loc+8(%rip), %eax
	imull $3, loc+8(%rip), %r13d
	imull $300, loc+8(%rip), %edx
	imull ext-4(%rip), %eax
	imull $3, ext-4(%rip), %r13d
	imull $300, ext-4(%rip), %edx
	imull glob(%rax), %eax
	imull $3, glob(%rax), %r13d
	imull $300, glob(%rax), %edx
	imull ext+16(%rbx, %rcx, 2), %eax
	imull $3, ext+16(%rbx, %rcx, 2), %r13d
	imull $300, ext+16(%rbx, %rcx, 2), %edx
	imull %fs:0, %eax
	imull $3, %fs:0, %r13d
	imull $300, %fs:0, %edx
	imull %fs:8(%rax), %eax
	imull $3, %fs:8(%rax), %r13d
	imull $300, %fs:8(%rax), %edx
	imull $1, %eax, %edi
	imull $1, %eax
	imull $1, %ecx, %edi
	imull $1, %ecx
	imull $1, %edx, %edi
	imull $1, %edx
	imull $1, %ebx, %edi
	imull $1, %ebx
	imull $1, %esp, %edi
	imull $1, %esp
	imull $1, %ebp, %edi
	imull $1, %ebp
	imull $1, %esi, %edi
	imull $1, %esi
	imull $1, %edi, %edi
	imull $1, %edi
	imull $1, %r8d, %edi
	imull $1, %r8d
	imull $1, %r9d, %edi
	imull $1, %r9d
	imull $1, %r10d, %edi
	imull $1, %r10d
	imull $1, %r11d, %edi
	imull $1, %r11d
	imull $1, %r12d, %edi
	imull $1, %r12d
	imull $1, %r13d, %edi
	imull $1, %r13d
	imull $1, %r14d, %edi
	imull $1, %r14d
	imull $1, %r15d, %edi
	imull $1, %r15d
	imull $-1, %eax, %edi
	imull $-1, %eax
	imull $-1, %ecx, %edi
	imull $-1, %ecx
	imull $-1, %edx, %edi
	imull $-1, %edx
	imull $-1, %ebx, %edi
	imull $-1, %ebx
	imull $-1, %esp, %edi
	imull $-1, %esp
	imull $-1, %ebp, %edi
	imull $-1, %ebp
	imull $-1, %esi, %edi
	imull $-1, %esi
	imull $-1, %edi, %edi
	imull $-1, %edi
	imull $-1, %r8d, %edi
	imull $-1, %r8d
	imull $-1, %r9d, %edi
	imull $-1, %r9d
	imull $-1, %r10d, %edi
	imull $-1, %r10d
	imull $-1, %r11d, %edi
	imull $-1, %r11d
	imull $-1, %r12d, %edi
	imull $-1, %r12d
	imull $-1, %r13d, %edi
	imull $-1, %r13d
	imull $-1, %r14d, %edi
	imull $-1, %r14d
	imull $-1, %r15d, %edi
	imull $-1, %r15d
	imull $127, %eax, %edi
	imull $127, %eax
	imull $127, %ecx, %edi
	imull $127, %ecx
	imull $127, %edx, %edi
	imull $127, %edx
	imull $127, %ebx, %edi
	imull $127, %ebx
	imull $127, %esp, %edi
	imull $127, %esp
	imull $127, %ebp, %edi
	imull $127, %ebp
	imull $127, %esi, %edi
	imull $127, %esi
	imull $127, %edi, %edi
	imull $127, %edi
	imull $127, %r8d, %edi
	imull $127, %r8d
	imull $127, %r9d, %edi
	imull $127, %r9d
	imull $127, %r10d, %edi
	imull $127, %r10d
	imull $127, %r11d, %edi
	imull $127, %r11d
	imull $127, %r12d, %edi
	imull $127, %r12d
	imull $127, %r13d, %edi
	imull $127, %r13d
	imull $127, %r14d, %edi
	imull $127, %r14d
	imull $127, %r15d, %edi
	imull $127, %r15d
	imull $128, %eax, %edi
	imull $128, %eax
	imull $128, %ecx, %edi
	imull $128, %ecx
	imull $128, %edx, %edi
	imull $128, %edx
	imull $128, %ebx, %edi
	imull $128, %ebx
	imull $128, %esp, %edi
	imull $128, %esp
	imull $128, %ebp, %edi
	imull $128, %ebp
	imull $128, %esi, %edi
	imull $128, %esi
	imull $128, %edi, %edi
	imull $128, %edi
	imull $128, %r8d, %edi
	imull $128, %r8d
	imull $128, %r9d, %edi
	imull $128, %r9d
	imull $128, %r10d, %edi
	imull $128, %r10d
	imull $128, %r11d, %edi
	imull $128, %r11d
	imull $128, %r12d, %edi
	imull $128, %r12d
	imull $128, %r13d, %edi
	imull $128, %r13d
	imull $128, %r14d, %edi
	imull $128, %r14d
	imull $128, %r15d, %edi
	imull $128, %r15d
	imull $-129, %eax, %edi
	imull $-129, %eax
	imull $-129, %ecx, %edi
	imull $-129, %ecx
	imull $-129, %edx, %edi
	imull $-129, %edx
	imull $-129, %ebx, %edi
	imull $-129, %ebx
	imull $-129, %esp, %edi
	imull $-129, %esp
	imull $-129, %ebp, %edi
	imull $-129, %ebp
	imull $-129, %esi, %edi
	imull $-129, %esi
	imull $-129, %edi, %edi
	imull $-129, %edi
	imull $-129, %r8d, %edi
	imull $-129, %r8d
	imull $-129, %r9d, %edi
	imull $-129, %r9d
	imull $-129, %r10d, %edi
	imull $-129, %r10d
	imull $-129, %r11d, %edi
	imull $-129, %r11d
	imull $-129, %r12d, %edi
	imull $-129, %r12d
	imull $-129, %r13d, %edi
	imull $-129, %r13d
	imull $-129, %r14d, %edi
	imull $-129, %r14d
	imull $-129, %r15d, %edi
	imull $-129, %r15d
	imull $4096, %eax, %edi
	imull $4096, %eax
	imull $4096, %ecx, %edi
	imull $4096, %ecx
	imull $4096, %edx, %edi
	imull $4096, %edx
	imull $4096, %ebx, %edi
	imull $4096, %ebx
	imull $4096, %esp, %edi
	imull $4096, %esp
	imull $4096, %ebp, %edi
	imull $4096, %ebp
	imull $4096, %esi, %edi
	imull $4096, %esi
	imull $4096, %edi, %edi
	imull $4096, %edi
	imull $4096, %r8d, %edi
	imull $4096, %r8d
	imull $4096, %r9d, %edi
	imull $4096, %r9d
	imull $4096, %r10d, %edi
	imull $4096, %r10d
	imull $4096, %r11d, %edi
	imull $4096, %r11d
	imull $4096, %r12d, %edi
	imull $4096, %r12d
	imull $4096, %r13d, %edi
	imull $4096, %r13d
	imull $4096, %r14d, %edi
	imull $4096, %r14d
	imull $4096, %r15d, %edi
	imull $4096, %r15d
	imulq %rax, %rbp
	imulq %r11, %rax
	imulq %rcx, %rbp
	imulq %r11, %rcx
	imulq %rdx, %rbp
	imulq %r11, %rdx
	imulq %rbx, %rbp
	imulq %r11, %rbx
	imulq %rsp, %rbp
	imulq %r11, %rsp
	imulq %rbp, %rbp
	imulq %r11, %rbp
	imulq %rsi, %rbp
	imulq %r11, %rsi
	imulq %rdi, %rbp
	imulq %r11, %rdi
	imulq %r8, %rbp
	imulq %r11, %r8
	imulq %r9, %rbp
	imulq %r11, %r9
	imulq %r10, %rbp
	imulq %r11, %r10
	imulq %r11, %rbp
	imulq %r11, %r11
	imulq %r12, %rbp
	imulq %r11, %r12
	imulq %r13, %rbp
	imulq %r11, %r13
	imulq %r14, %rbp
	imulq %r11, %r14
	imulq %r15, %rbp
	imulq %r11, %r15
	imulq (%rax), %rax
	imulq $3, (%rax), %r13
	imulq $300, (%rax), %rdx
	imulq (%rsp), %rax
	imulq $3, (%rsp), %r13
	imulq $300, (%rsp), %rdx
	imulq (%rbp), %rax
	imulq $3, (%rbp), %r13
	imulq $300, (%rbp), %rdx
	imulq (%r12), %rax
	imulq $3, (%r12), %r13
	imulq $300, (%r12), %rdx
	imulq (%r13), %rax
	imulq $3, (%r13), %r13
	imulq $300, (%r13), %rdx
	imulq (%r15), %rax
	imulq $3, (%r15), %r13
	imulq $300, (%r15), %rdx
	imulq 8(%rbx), %rax
	imulq $3, 8(%rbx), %r13
	imulq $300, 8(%rbx), %rdx
	imulq -8(%rbp), %rax
	imulq $3, -8(%rbp), %r13
	imulq $300, -8(%rbp), %rdx
	imulq -128(%rcx), %rax
	imulq $3, -128(%rcx), %r13
	imulq $300, -128(%rcx), %rdx
	imulq -129(%rdx), %rax
	imulq $3, -129(%rdx), %r13
	imulq $300, -129(%rdx), %rdx
	imulq 127(%rsi), %rax
	imulq $3, 127(%rsi), %r13
	imulq $300, 127(%rsi), %rdx
	imulq 128(%rdi), %rax
	imulq $3, 128(%rdi), %r13
	imulq $300, 128(%rdi), %rdx
	imulq 4096(%r8), %rax
	imulq $3, 4096(%r8), %r13
	imulq $300, 4096(%r8), %rdx
	imulq (%rax, %rcx), %rax
	imulq $3, (%rax, %rcx), %r13
	imulq $300, (%rax, %rcx), %rdx
	imulq (%rax,%rcx,2), %rax
	imulq $3, (%rax,%rcx,2), %r13
	imulq $300, (%rax,%rcx,2), %rdx
	imulq 8(%rsp, %rbp, 4), %rax
	imulq $3, 8(%rsp, %rbp, 4), %r13
	imulq $300, 8(%rsp, %rbp, 4), %rdx
	imulq -16(%r13, %r14, 8), %rax
	imulq $3, -16(%r13, %r14, 8), %r13
	imulq $300, -16(%r13, %r14, 8), %rdx
	imulq (, %rcx, 8), %rax
	imulq $3, (, %rcx, 8), %r13
	imulq $300, (, %rcx, 8), %rdx
	imulq 16(, %r9, 4), %rax
	imulq $3, 16(, %r9, 4), %r13
	imulq $300, 16(, %r9, 4), %rdx
	imulq 1234, %rax
	imulq $3, 1234, %r13
	imulq $300, 1234, %rdx
	imulq glob(%rip), %rax
	imulq $3, glob(%rip), %r13
	imulq $300, glob(%rip), %rdx
	imulq loc+8(%rip), %rax
	imulq $3, loc+8(%rip), %r13
	imulq $300, loc+8(%rip), %rdx
	imulq ext-4(%rip), %rax
	imulq $3, ext-4(%rip), %r13
	imulq $300, ext-4(%rip), %rdx
	imulq glob(%rax), %rax
	imulq $3, glob(%rax), %r13
	imulq $300, glob(%rax), %rdx
	imulq ext+16(%rbx, %rcx, 2), %rax
	imulq $3, ext+16(%rbx, %rcx, 2), %r13
	imulq $300, ext+16(%rbx, %rcx, 2), %rdx
	imulq %fs:0, %rax
	imulq $3, %fs:0, %r13
	imulq $300, %fs:0, %rdx
	imulq %fs:8(%rax), %rax
	imulq $3, %fs:8(%rax), %r13
	imulq $300, %fs:8(%rax), %rdx
	imulq $1, %rax, %rdi
	imulq $1, %rax
	imulq $1, %rcx, %rdi
	imulq $1, %rcx
	imulq $1, %rdx, %rdi
	imulq $1, %rdx
	imulq $1, %rbx, %rdi
	imulq $1, %rbx
	imulq $1, %rsp, %rdi
	imulq $1, %rsp
	imulq $1, %rbp, %rdi
	imulq $1, %rbp
	imulq $1, %rsi, %rdi
	imulq $1, %rsi
	imulq $1, %rdi, %rdi
	imulq $1, %rdi
	imulq $1, %r8, %rdi
	imulq $1, %r8
	imulq $1, %r9, %rdi
	imulq $1, %r9
	imulq $1, %r10, %rdi
	imulq $1, %r10
	imulq $1, %r11, %rdi
	imulq $1, %r11
	imulq $1, %r12, %rdi
	imulq $1, %r12
	imulq $1, %r13, %rdi
	imulq $1, %r13
	imulq $1, %r14, %rdi
	imulq $1, %r14
	imulq $1, %r15, %rdi
	imulq $1, %r15
	imulq $-1, %rax, %rdi
	imulq $-1, %rax
	imulq $-1, %rcx, %rdi
	imulq $-1, %rcx
	imulq $-1, %rdx, %rdi
	imulq $-1, %rdx
	imulq $-1, %rbx, %rdi
	imulq $-1, %rbx
	imulq $-1, %rsp, %rdi
	imulq $-1, %rsp
	imulq $-1, %rbp, %rdi
	imulq $-1, %rbp
	imulq $-1, %rsi, %rdi
	imulq $-1, %rsi
	imulq $-1, %rdi, %rdi
	imulq $-1, %rdi
	imulq $-1, %r8, %rdi
	imulq $-1, %r8
	imulq $-1, %r9, %rdi
	imulq $-1, %r9
	imulq $-1, %r10, %rdi
	imulq $-1, %r10
	imulq $-1, %r11, %rdi
	imulq $-1, %r11
	imulq $-1, %r12, %rdi
	imulq $-1, %r12
	imulq $-1, %r13, %rdi
	imulq $-1, %r13
	imulq $-1, %r14, %rdi
	imulq $-1, %r14
	imulq $-1, %r15, %rdi
	imulq $-1, %r15
	imulq $127, %rax, %rdi
	imulq $127, %rax
	imulq $127, %rcx, %rdi
	imulq $127, %rcx
	imulq $127, %rdx, %rdi
	imulq $127, %rdx
	imulq $127, %rbx, %rdi
	imulq $127, %rbx
	imulq $127, %rsp, %rdi
	imulq $127, %rsp
	imulq $127, %rbp, %rdi
	imulq $127, %rbp
	imulq $127, %rsi, %rdi
	imulq $127, %rsi
	imulq $127, %rdi, %rdi
	imulq $127, %rdi
	imulq $127, %r8, %rdi
	imulq $127, %r8
	imulq $127, %r9, %rdi
	imulq $127, %r9
	imulq $127, %r10, %rdi
	imulq $127, %r10
	imulq $127, %r11, %rdi
	imulq $127, %r11
	imulq $127, %r12, %rdi
	imulq $127, %r12
	imulq $127, %r13, %rdi
	imulq $127, %r13
	imulq $127, %r14, %rdi
	imulq $127, %r14
	imulq $127, %r15, %rdi
	imulq $127, %r15
	imulq $128, %rax, %rdi
	imulq $128, %rax
	imulq $128, %rcx, %rdi
	imulq $128, %rcx
	imulq $128, %rdx, %rdi
	imulq $128, %rdx
	imulq $128, %rbx, %rdi
	imulq $128, %rbx
	imulq $128, %rsp, %rdi
	imulq $128, %rsp
	imulq $128, %rbp, %rdi
	imulq $128, %rbp
	imulq $128, %rsi, %rdi
	imulq $128, %rsi
	imulq $128, %rdi, %rdi
	imulq $128, %rdi
	imulq $128, %r8, %rdi
	imulq $128, %r8
	imulq $128, %r9, %rdi
	imulq $128, %r9
	imulq $128, %r10, %rdi
	imulq $128, %r10
	imulq $128, %r11, %rdi
	imulq $128, %r11
	imulq $128, %r12, %rdi
	imulq $128, %r12
	imulq $128, %r13, %rdi
	imulq $128, %r13
	imulq $128, %r14, %rdi
	imulq $128, %r14
	imulq $128, %r15, %rdi
	imulq $128, %r15
	imulq $-129, %rax, %rdi
	imulq $-129, %rax
	imulq $-129, %rcx, %rdi
	imulq $-129, %rcx
	imulq $-129, %rdx, %rdi
	imulq $-129, %rdx
	imulq $-129, %rbx, %rdi
	imulq $-129, %rbx
	imulq $-129, %rsp, %rdi
	imulq $-129, %rsp
	imulq $-129, %rbp, %rdi
	imulq $-129, %rbp
	imulq $-129, %rsi, %rdi
	imulq $-129, %rsi
	imulq $-129, %rdi, %rdi
	imulq $-129, %rdi
	imulq $-129, %r8, %rdi
	imulq $-129, %r8
	imulq $-129, %r9, %rdi
	imulq $-129, %r9
	imulq $-129, %r10, %rdi
	imulq $-129, %r10
	imulq $-129, %r11, %rdi
	imulq $-129, %r11
	imulq $-129, %r12, %rdi
	imulq $-129, %r12
	imulq $-129, %r13, %rdi
	imulq $-129, %r13
	imulq $-129, %r14, %rdi
	imulq $-129, %r14
	imulq $-129, %r15, %rdi
	imulq $-129, %r15
	imulq $4096, %rax, %rdi
	imulq $4096, %rax
	imulq $4096, %rcx, %rdi
	imulq $4096, %rcx
	imulq $4096, %rdx, %rdi
	imulq $4096, %rdx
	imulq $4096, %rbx, %rdi
	imulq $4096, %rbx
	imulq $4096, %rsp, %rdi
	imulq $4096, %rsp
	imulq $4096, %rbp, %rdi
	imulq $4096, %rbp
	imulq $4096, %rsi, %rdi
	imulq $4096, %rsi
	imulq $4096, %rdi, %rdi
	imulq $4096, %rdi
	imulq $4096, %r8, %rdi
	imulq $4096, %r8
	imulq $4096, %r9, %rdi
	imulq $4096, %r9
	imulq $4096, %r10, %rdi
	imulq $4096, %r10
	imulq $4096, %r11, %rdi
	imulq $4096, %r11
	imulq $4096, %r12, %rdi
	imulq $4096, %r12
	imulq $4096, %r13, %rdi
	imulq $4096, %r13
	imulq $4096, %r14, %rdi
	imulq $4096, %r14
	imulq $4096, %r15, %rdi
	imulq $4096, %r15
	shlb $1, %al
	shlb $5, %al
	shlb %cl, %al
	shlb $1, %cl
	shlb $5, %cl
	shlb %cl, %cl
	shlb $1, %dl
	shlb $5, %dl
	shlb %cl, %dl
	shlb $1, %bl
	shlb $5, %bl
	shlb %cl, %bl
	shlb $1, %spl
	shlb $5, %spl
	shlb %cl, %spl
	shlb $1, %bpl
	shlb $5, %bpl
	shlb %cl, %bpl
	shlb $1, %sil
	shlb $5, %sil
	shlb %cl, %sil
	shlb $1, %dil
	shlb $5, %dil
	shlb %cl, %dil
	shlb $1, %r8b
	shlb $5, %r8b
	shlb %cl, %r8b
	shlb $1, %r9b
	shlb $5, %r9b
	shlb %cl, %r9b
	shlb $1, %r10b
	shlb $5, %r10b
	shlb %cl, %r10b
	shlb $1, %r11b
	shlb $5, %r11b
	shlb %cl, %r11b
	shlb $1, %r12b
	shlb $5, %r12b
	shlb %cl, %r12b
	shlb $1, %r13b
	shlb $5, %r13b
	shlb %cl, %r13b
	shlb $1, %r14b
	shlb $5, %r14b
	shlb %cl, %r14b
	shlb $1, %r15b
	shlb $5, %r15b
	shlb %cl, %r15b
	shlb $1, (%rax)
	shlb $63, (%rax)
	shlb %cl, (%rax)
	shlb $1, (%rsp)
	shlb $63, (%rsp)
	shlb %cl, (%rsp)
	shlb $1, (%rbp)
	shlb $63, (%rbp)
	shlb %cl, (%rbp)
	shlb $1, (%r12)
	shlb $63, (%r12)
	shlb %cl, (%r12)
	shlb $1, (%r13)
	shlb $63, (%r13)
	shlb %cl, (%r13)
	shlb $1, (%r15)
	shlb $63, (%r15)
	shlb %cl, (%r15)
	shlb $1, 8(%rbx)
	shlb $63, 8(%rbx)
	shlb %cl, 8(%rbx)
	shlb $1, -8(%rbp)
	shlb $63, -8(%rbp)
	shlb %cl, -8(%rbp)
	shlb $1, -128(%rcx)
	shlb $63, -128(%rcx)
	shlb %cl, -128(%rcx)
	shlb $1, -129(%rdx)
	shlb $63, -129(%rdx)
	shlb %cl, -129(%rdx)
	shl %cl, %bl
	shl $2, %r9b
	shlw $1, %ax
	shlw $5, %ax
	shlw %cl, %ax
	shlw $1, %cx
	shlw $5, %cx
	shlw %cl, %cx
	shlw $1, %dx
	shlw $5, %dx
	shlw %cl, %dx
	shlw $1, %bx
	shlw $5, %bx
	shlw %cl, %bx
	shlw $1, %sp
	shlw $5, %sp
	shlw %cl, %sp
	shlw $1, %bp
	shlw $5, %bp
	shlw %cl, %bp
	shlw $1, %si
	shlw $5, %si
	shlw %cl, %si
	shlw $1, %di
	shlw $5, %di
	shlw %cl, %di
	shlw $1, %r8w
	shlw $5, %r8w
	shlw %cl, %r8w
	shlw $1, %r9w
	shlw $5, %r9w
	shlw %cl, %r9w
	shlw $1, %r10w
	shlw $5, %r10w
	shlw %cl, %r10w
	shlw $1, %r11w
	shlw $5, %r11w
	shlw %cl, %r11w
	shlw $1, %r12w
	shlw $5, %r12w
	shlw %cl, %r12w
	shlw $1, %r13w
	shlw $5, %r13w
	shlw %cl, %r13w
	shlw $1, %r14w
	shlw $5, %r14w
	shlw %cl, %r14w
	shlw $1, %r15w
	shlw $5, %r15w
	shlw %cl, %r15w
	shlw $1, (%rax)
	shlw $63, (%rax)
	shlw %cl, (%rax)
	shlw $1, (%rsp)
	shlw $63, (%rsp)
	shlw %cl, (%rsp)
	shlw $1, (%rbp)
	shlw $63, (%rbp)
	shlw %cl, (%rbp)
	shlw $1, (%r12)
	shlw $63, (%r12)
	shlw %cl, (%r12)
	shlw $1, (%r13)
	shlw $63, (%r13)
	shlw %cl, (%r13)
	shlw $1, (%r15)
	shlw $63, (%r15)
	shlw %cl, (%r15)
	shlw $1, 8(%rbx)
	shlw $63, 8(%rbx)
	shlw %cl, 8(%rbx)
	shlw $1, -8(%rbp)
	shlw $63, -8(%rbp)
	shlw %cl, -8(%rbp)
	shlw $1, -128(%rcx)
	shlw $63, -128(%rcx)
	shlw %cl, -128(%rcx)
	shlw $1, -129(%rdx)
	shlw $63, -129(%rdx)
	shlw %cl, -129(%rdx)
	shl %cl, %bx
	shl $2, %r9w
	shll $1, %eax
	shll $5, %eax
	shll %cl, %eax
	shll $1, %ecx
	shll $5, %ecx
	shll %cl, %ecx
	shll $1, %edx
	shll $5, %edx
	shll %cl, %edx
	shll $1, %ebx
	shll $5, %ebx
	shll %cl, %ebx
	shll $1, %esp
	shll $5, %esp
	shll %cl, %esp
	shll $1, %ebp
	shll $5, %ebp
	shll %cl, %ebp
	shll $1, %esi
	shll $5, %esi
	shll %cl, %esi
	shll $1, %edi
	shll $5, %edi
	shll %cl, %edi
	shll $1, %r8d
	shll $5, %r8d
	shll %cl, %r8d
	shll $1, %r9d
	shll $5, %r9d
	shll %cl, %r9d
	shll $1, %r10d
	shll $5, %r10d
	shll %cl, %r10d
	shll $1, %r11d
	shll $5, %r11d
	shll %cl, %r11d
	shll $1, %r12d
	shll $5, %r12d
	shll %cl, %r12d
	shll $1, %r13d
	shll $5, %r13d
	shll %cl, %r13d
	shll $1, %r14d
	shll $5, %r14d
	shll %cl, %r14d
	shll $1, %r15d
	shll $5, %r15d
	shll %cl, %r15d
	shll $1, (%rax)
	shll $63, (%rax)
	shll %cl, (%rax)
	shll $1, (%rsp)
	shll $63, (%rsp)
	shll %cl, (%rsp)
	shll $1, (%rbp)
	shll $63, (%rbp)
	shll %cl, (%rbp)
	shll $1, (%r12)
	shll $63, (%r12)
	shll %cl, (%r12)
	shll $1, (%r13)
	shll $63, (%r13)
	shll %cl, (%r13)
	shll $1, (%r15)
	shll $63, (%r15)
	shll %cl, (%r15)
	shll $1, 8(%rbx)
	shll $63, 8(%rbx)
	shll %cl, 8(%rbx)
	shll $1, -8(%rbp)
	shll $63, -8(%rbp)
	shll %cl, -8(%rbp)
	shll $1, -128(%rcx)
	shll $63, -128(%rcx)
	shll %cl, -128(%rcx)
	shll $1, -129(%rdx)
	shll $63, -129(%rdx)
	shll %cl, -129(%rdx)
	shl %cl, %ebx
	shl $2, %r9d
	shlq $1, %rax
	shlq $5, %rax
	shlq %cl, %rax
	shlq $1, %rcx
	shlq $5, %rcx
	shlq %cl, %rcx
	shlq $1, %rdx
	shlq $5, %rdx
	shlq %cl, %rdx
	shlq $1, %rbx
	shlq $5, %rbx
	shlq %cl, %rbx
	shlq $1, %rsp
	shlq $5, %rsp
	shlq %cl, %rsp
	shlq $1, %rbp
	shlq $5, %rbp
	shlq %cl, %rbp
	shlq $1, %rsi
	shlq $5, %rsi
	shlq %cl, %rsi
	shlq $1, %rdi
	shlq $5, %rdi
	shlq %cl, %rdi
	shlq $1, %r8
	shlq $5, %r8
	shlq %cl, %r8
	shlq $1, %r9
	shlq $5, %r9
	shlq %cl, %r9
	shlq $1, %r10
	shlq $5, %r10
	shlq %cl, %r10
	shlq $1, %r11
	shlq $5, %r11
	shlq %cl, %r11
	shlq $1, %r12
	shlq $5, %r12
	shlq %cl, %r12
	shlq $1, %r13
	shlq $5, %r13
	shlq %cl, %r13
	shlq $1, %r14
	shlq $5, %r14
	shlq %cl, %r14
	shlq $1, %r15
	shlq $5, %r15
	shlq %cl, %r15
	shlq $1, (%rax)
	shlq $63, (%rax)
	shlq %cl, (%rax)
	shlq $1, (%rsp)
	shlq $63, (%rsp)
	shlq %cl, (%rsp)
	shlq $1, (%rbp)
	shlq $63, (%rbp)
	shlq %cl, (%rbp)
	shlq $1, (%r12)
	shlq $63, (%r12)
	shlq %cl, (%r12)
	shlq $1, (%r13)
	shlq $63, (%r13)
	shlq %cl, (%r13)
	shlq $1, (%r15)
	shlq $63, (%r15)
	shlq %cl, (%r15)
	shlq $1, 8(%rbx)
	shlq $63, 8(%rbx)
	shlq %cl, 8(%rbx)
	shlq $1, -8(%rbp)
	shlq $63, -8(%rbp)
	shlq %cl, -8(%rbp)
	shlq $1, -128(%rcx)
	shlq $63, -128(%rcx)
	shlq %cl, -128(%rcx)
	shlq $1, -129(%rdx)
	shlq $63, -129(%rdx)
	shlq %cl, -129(%rdx)
	shl %cl, %rbx
	shl $2, %r9
	salb $1, %al
	salb $5, %al
	salb %cl, %al
	salb $1, %cl
	salb $5, %cl
	salb %cl, %cl
	salb $1, %dl
	salb $5, %dl
	salb %cl, %dl
	salb $1, %bl
	salb $5, %bl
	salb %cl, %bl
	salb $1, %spl
	salb $5, %spl
	salb %cl, %spl
	salb $1, %bpl
	salb $5, %bpl
	salb %cl, %bpl
	salb $1, %sil
	salb $5, %sil
	salb %cl, %sil
	salb $1, %dil
	salb $5, %dil
	salb %cl, %dil
	salb $1, %r8b
	salb $5, %r8b
	salb %cl, %r8b
	salb $1, %r9b
	salb $5, %r9b
	salb %cl, %r9b
	salb $1, %r10b
	salb $5, %r10b
	salb %cl, %r10b
	salb $1, %r11b
	salb $5, %r11b
	salb %cl, %r11b
	salb $1, %r12b
	salb $5, %r12b
	salb %cl, %r12b
	salb $1, %r13b
	salb $5, %r13b
	salb %cl, %r13b
	salb $1, %r14b
	salb $5, %r14b
	salb %cl, %r14b
	salb $1, %r15b
	salb $5, %r15b
	salb %cl, %r15b
	salb $1, (%rax)
	salb $63, (%rax)
	salb %cl, (%rax)
	salb $1, (%rsp)
	salb $63, (%rsp)
	salb %cl, (%rsp)
	salb $1, (%rbp)
	salb $63, (%rbp)
	salb %cl, (%rbp)
	salb $1, (%r12)
	salb $63, (%r12)
	salb %cl, (%r12)
	salb $1, (%r13)
	salb $63, (%r13)
	salb %cl, (%r13)
	salb $1, (%r15)
	salb $63, (%r15)
	salb %cl, (%r15)
	salb $1, 8(%rbx)
	salb $63, 8(%rbx)
	salb %cl, 8(%rbx)
	salb $1, -8(%rbp)
	salb $63, -8(%rbp)
	salb %cl, -8(%rbp)
	salb $1, -128(%rcx)
	salb $63, -128(%rcx)
	salb %cl, -128(%rcx)
	salb $1, -129(%rdx)
	salb $63, -129(%rdx)
	salb %cl, -129(%rdx)
	sal %cl, %bl
	sal $2, %r9b
	salw $1, %ax
	salw $5, %ax
	salw %cl, %ax
	salw $1, %cx
	salw $5, %cx
	salw %cl, %cx
	salw $1, %dx
	salw $5, %dx
	salw %cl, %dx
	salw $1, %bx
	salw $5, %bx
	salw %cl, %bx
	salw $1, %sp
	salw $5, %sp
	salw %cl, %sp
	salw $1, %bp
	salw $5, %bp
	salw %cl, %bp
	salw $1, %si
	salw $5, %si
	salw %cl, %si
	salw $1, %di
	salw $5, %di
	salw %cl, %di
	salw $1, %r8w
	salw $5, %r8w
	salw %cl, %r8w
	salw $1, %r9w
	salw $5, %r9w
	salw %cl, %r9w
	salw $1, %r10w
	salw $5, %r10w
	salw %cl, %r10w
	salw $1, %r11w
	salw $5, %r11w
	salw %cl, %r11w
	salw $1, %r12w
	salw $5, %r12w
	salw %cl, %r12w
	salw $1, %r13w
	salw $5, %r13w
	salw %cl, %r13w
	salw $1, %r14w
	salw $5, %r14w
	salw %cl, %r14w
	salw $1, %r15w
	salw $5, %r15w
	salw %cl, %r15w
	salw $1, (%rax)
	salw $63, (%rax)
	salw %cl, (%rax)
	salw $1, (%rsp)
	salw $63, (%rsp)
	salw %cl, (%rsp)
	salw $1, (%rbp)
	salw $63, (%rbp)
	salw %cl, (%rbp)
	salw $1, (%r12)
	salw $63, (%r12)
	salw %cl, (%r12)
	salw $1, (%r13)
	salw $63, (%r13)
	salw %cl, (%r13)
	salw $1, (%r15)
	salw $63, (%r15)
	salw %cl, (%r15)
	salw $1, 8(%rbx)
	salw $63, 8(%rbx)
	salw %cl, 8(%rbx)
	salw $1, -8(%rbp)
	salw $63, -8(%rbp)
	salw %cl, -8(%rbp)
	salw $1, -128(%rcx)
	salw $63, -128(%rcx)
	salw %cl, -128(%rcx)
	salw $1, -129(%rdx)
	salw $63, -129(%rdx)
	salw %cl, -129(%rdx)
	sal %cl, %bx
	sal $2, %r9w
	sall $1, %eax
	sall $5, %eax
	sall %cl, %eax
	sall $1, %ecx
	sall $5, %ecx
	sall %cl, %ecx
	sall $1, %edx
	sall $5, %edx
	sall %cl, %edx
	sall $1, %ebx
	sall $5, %ebx
	sall %cl, %ebx
	sall $1, %esp
	sall $5, %esp
	sall %cl, %esp
	sall $1, %ebp
	sall $5, %ebp
	sall %cl, %ebp
	sall $1, %esi
	sall $5, %esi
	sall %cl, %esi
	sall $1, %edi
	sall $5, %edi
	sall %cl, %edi
	sall $1, %r8d
	sall $5, %r8d
	sall %cl, %r8d
	sall $1, %r9d
	sall $5, %r9d
	sall %cl, %r9d
	sall $1, %r10d
	sall $5, %r10d
	sall %cl, %r10d
	sall $1, %r11d
	sall $5, %r11d
	sall %cl, %r11d
	sall $1, %r12d
	sall $5, %r12d
	sall %cl, %r12d
	sall $1, %r13d
	sall $5, %r13d
	sall %cl, %r13d
	sall $1, %r14d
	sall $5, %r14d
	sall %cl, %r14d
	sall $1, %r15d
	sall $5, %r15d
	sall %cl, %r15d
	sall $1, (%rax)
	sall $63, (%rax)
	sall %cl, (%rax)
	sall $1, (%rsp)
	sall $63, (%rsp)
	sall %cl, (%rsp)
	sall $1, (%rbp)
	sall $63, (%rbp)
	sall %cl, (%rbp)
	sall $1, (%r12)
	sall $63, (%r12)
	sall %cl, (%r12)
	sall $1, (%r13)
	sall $63, (%r13)
	sall %cl, (%r13)
	sall $1, (%r15)
	sall $63, (%r15)
	sall %cl, (%r15)
	sall $1, 8(%rbx)
	sall $63, 8(%rbx)
	sall %cl, 8(%rbx)
	sall $1, -8(%rbp)
	sall $63, -8(%rbp)
	sall %cl, -8(%rbp)
	sall $1, -128(%rcx)
	sall $63, -128(%rcx)
	sall %cl, -128(%rcx)
	sall $1, -129(%rdx)
	sall $63, -129(%rdx)
	sall %cl, -129(%rdx)
	sal %cl, %ebx
	sal $2, %r9d
	salq $1, %rax
	salq $5, %rax
	salq %cl, %rax
	salq $1, %rcx
	salq $5, %rcx
	salq %cl, %rcx
	salq $1, %rdx
	salq $5, %rdx
	salq %cl, %rdx
	salq $1, %rbx
	salq $5, %rbx
	salq %cl, %rbx
	salq $1, %rsp
	salq $5, %rsp
	salq %cl, %rsp
	salq $1, %rbp
	salq $5, %rbp
	salq %cl, %rbp
	salq $1, %rsi
	salq $5, %rsi
	salq %cl, %rsi
	salq $1, %rdi
	salq $5, %rdi
	salq %cl, %rdi
	salq $1, %r8
	salq $5, %r8
	salq %cl, %r8
	salq $1, %r9
	salq $5, %r9
	salq %cl, %r9
	salq $1, %r10
	salq $5, %r10
	salq %cl, %r10
	salq $1, %r11
	salq $5, %r11
	salq %cl, %r11
	salq $1, %r12
	salq $5, %r12
	salq %cl, %r12
	salq $1, %r13
	salq $5, %r13
	salq %cl, %r13
	salq $1, %r14
	salq $5, %r14
	salq %cl, %r14
	salq $1, %r15
	salq $5, %r15
	salq %cl, %r15
	salq $1, (%rax)
	salq $63, (%rax)
	salq %cl, (%rax)
	salq $1, (%rsp)
	salq $63, (%rsp)
	salq %cl, (%rsp)
	salq $1, (%rbp)
	salq $63, (%rbp)
	salq %cl, (%rbp)
	salq $1, (%r12)
	salq $63, (%r12)
	salq %cl, (%r12)
	salq $1, (%r13)
	salq $63, (%r13)
	salq %cl, (%r13)
	salq $1, (%r15)
	salq $63, (%r15)
	salq %cl, (%r15)
	salq $1, 8(%rbx)
	salq $63, 8(%rbx)
	salq %cl, 8(%rbx)
	salq $1, -8(%rbp)
	salq $63, -8(%rbp)
	salq %cl, -8(%rbp)
	salq $1, -128(%rcx)
	salq $63, -128(%rcx)
	salq %cl, -128(%rcx)
	salq $1, -129(%rdx)
	salq $63, -129(%rdx)
	salq %cl, -129(%rdx)
	sal %cl, %rbx
	sal $2, %r9
	shrb $1, %al
	shrb $5, %al
	shrb %cl, %al
	shrb $1, %cl
	shrb $5, %cl
	shrb %cl, %cl
	shrb $1, %dl
	shrb $5, %dl
	shrb %cl, %dl
	shrb $1, %bl
	shrb $5, %bl
	shrb %cl, %bl
	shrb $1, %spl
	shrb $5, %spl
	shrb %cl, %spl
	shrb $1, %bpl
	shrb $5, %bpl
	shrb %cl, %bpl
	shrb $1, %sil
	shrb $5, %sil
	shrb %cl, %sil
	shrb $1, %dil
	shrb $5, %dil
	shrb %cl, %dil
	shrb $1, %r8b
	shrb $5, %r8b
	shrb %cl, %r8b
	shrb $1, %r9b
	shrb $5, %r9b
	shrb %cl, %r9b
	shrb $1, %r10b
	shrb $5, %r10b
	shrb %cl, %r10b
	shrb $1, %r11b
	shrb $5, %r11b
	shrb %cl, %r11b
	shrb $1, %r12b
	shrb $5, %r12b
	shrb %cl, %r12b
	shrb $1, %r13b
	shrb $5, %r13b
	shrb %cl, %r13b
	shrb $1, %r14b
	shrb $5, %r14b
	shrb %cl, %r14b
	shrb $1, %r15b
	shrb $5, %r15b
	shrb %cl, %r15b
	shrb $1, (%rax)
	shrb $63, (%rax)
	shrb %cl, (%rax)
	shrb $1, (%rsp)
	shrb $63, (%rsp)
	shrb %cl, (%rsp)
	shrb $1, (%rbp)
	shrb $63, (%rbp)
	shrb %cl, (%rbp)
	shrb $1, (%r12)
	shrb $63, (%r12)
	shrb %cl, (%r12)
	shrb $1, (%r13)
	shrb $63, (%r13)
	shrb %cl, (%r13)
	shrb $1, (%r15)
	shrb $63, (%r15)
	shrb %cl, (%r15)
	shrb $1, 8(%rbx)
	shrb $63, 8(%rbx)
	shrb %cl, 8(%rbx)
	shrb $1, -8(%rbp)
	shrb $63, -8(%rbp)
	shrb %cl, -8(%rbp)
	shrb $1, -128(%rcx)
	shrb $63, -128(%rcx)
	shrb %cl, -128(%rcx)
	shrb $1, -129(%rdx)
	shrb $63, -129(%rdx)
	shrb %cl, -129(%rdx)
	shr %cl, %bl
	shr $2, %r9b
	shrw $1, %ax
	shrw $5, %ax
	shrw %cl, %ax
	shrw $1, %cx
	shrw $5, %cx
	shrw %cl, %cx
	shrw $1, %dx
	shrw $5, %dx
	shrw %cl, %dx
	shrw $1, %bx
	shrw $5, %bx
	shrw %cl, %bx
	shrw $1, %sp
	shrw $5, %sp
	shrw %cl, %sp
	shrw $1, %bp
	shrw $5, %bp
	shrw %cl, %bp
	shrw $1, %si
	shrw $5, %si
	shrw %cl, %si
	shrw $1, %di
	shrw $5, %di
	shrw %cl, %di
	shrw $1, %r8w
	shrw $5, %r8w
	shrw %cl, %r8w
	shrw $1, %r9w
	shrw $5, %r9w
	shrw %cl, %r9w
	shrw $1, %r10w
	shrw $5, %r10w
	shrw %cl, %r10w
	shrw $1, %r11w
	shrw $5, %r11w
	shrw %cl, %r11w
	shrw $1, %r12w
	shrw $5, %r12w
	shrw %cl, %r12w
	shrw $1, %r13w
	shrw $5, %r13w
	shrw %cl, %r13w
	shrw $1, %r14w
	shrw $5, %r14w
	shrw %cl, %r14w
	shrw $1, %r15w
	shrw $5, %r15w
	shrw %cl, %r15w
	shrw $1, (%rax)
	shrw $63, (%rax)
	shrw %cl, (%rax)
	shrw $1, (%rsp)
	shrw $63, (%rsp)
	shrw %cl, (%rsp)
	shrw $1, (%rbp)
	shrw $63, (%rbp)
	shrw %cl, (%rbp)
	shrw $1, (%r12)
	shrw $63, (%r12)
	shrw %cl, (%r12)
	shrw $1, (%r13)
	shrw $63, (%r13)
	shrw %cl, (%r13)
	shrw $1, (%r15)
	shrw $63, (%r15)
	shrw %cl, (%r15)
	shrw $1, 8(%rbx)
	shrw $63, 8(%rbx)
	shrw %cl, 8(%rbx)
	shrw $1, -8(%rbp)
	shrw $63, -8(%rbp)
	shrw %cl, -8(%rbp)
	shrw $1, -128(%rcx)
	shrw $63, -128(%rcx)
	shrw %cl, -128(%rcx)
	shrw $1, -129(%rdx)
	shrw $63, -129(%rdx)
	shrw %cl, -129(%rdx)
	shr %cl, %bx
	shr $2, %r9w
	shrl $1, %eax
	shrl $5, %eax
	shrl %cl, %eax
	shrl $1, %ecx
	shrl $5, %ecx
	shrl %cl, %ecx
	shrl $1, %edx
	shrl $5, %edx
	shrl %cl, %edx
	shrl $1, %ebx
	shrl $5, %ebx
	shrl %cl, %ebx
	shrl $1, %esp
	shrl $5, %esp
	shrl %cl, %esp
	shrl $1, %ebp
	shrl $5, %ebp
	shrl %cl, %ebp
	shrl $1, %esi
	shrl $5, %esi
	shrl %cl, %esi
	shrl $1, %edi
	shrl $5, %edi
	shrl %cl, %edi
	shrl $1, %r8d
	shrl $5, %r8d
	shrl %cl, %r8d
	shrl $1, %r9d
	shrl $5, %r9d
	shrl %cl, %r9d
	shrl $1, %r10d
	shrl $5, %r10d
	shrl %cl, %r10d
	shrl $1, %r11d
	shrl $5, %r11d
	shrl %cl, %r11d
	shrl $1, %r12d
	shrl $5, %r12d
	shrl %cl, %r12d
	shrl $1, %r13d
	shrl $5, %r13d
	shrl %cl, %r13d
	shrl $1, %r14d
	shrl $5, %r14d
	shrl %cl, %r14d
	shrl $1, %r15d
	shrl $5, %r15d
	shrl %cl, %r15d
	shrl $1, (%rax)
	shrl $63, (%rax)
	shrl %cl, (%rax)
	shrl $1, (%rsp)
	shrl $63, (%rsp)
	shrl %cl, (%rsp)
	shrl $1, (%rbp)
	shrl $63, (%rbp)
	shrl %cl, (%rbp)
	shrl $1, (%r12)
	shrl $63, (%r12)
	shrl %cl, (%r12)
	shrl $1, (%r13)
	shrl $63, (%r13)
	shrl %cl, (%r13)
	shrl $1, (%r15)
	shrl $63, (%r15)
	shrl %cl, (%r15)
	shrl $1, 8(%rbx)
	shrl $63, 8(%rbx)
	shrl %cl, 8(%rbx)
	shrl $1, -8(%rbp)
	shrl $63, -8(%rbp)
	shrl %cl, -8(%rbp)
	shrl $1, -128(%rcx)
	shrl $63, -128(%rcx)
	shrl %cl, -128(%rcx)
	shrl $1, -129(%rdx)
	shrl $63, -129(%rdx)
	shrl %cl, -129(%rdx)
	shr %cl, %ebx
	shr $2, %r9d
	shrq $1, %rax
	shrq $5, %rax
	shrq %cl, %rax
	shrq $1, %rcx
	shrq $5, %rcx
	shrq %cl, %rcx
	shrq $1, %rdx
	shrq $5, %rdx
	shrq %cl, %rdx
	shrq $1, %rbx
	shrq $5, %rbx
	shrq %cl, %rbx
	shrq $1, %rsp
	shrq $5, %rsp
	shrq %cl, %rsp
	shrq $1, %rbp
	shrq $5, %rbp
	shrq %cl, %rbp
	shrq $1, %rsi
	shrq $5, %rsi
	shrq %cl, %rsi
	shrq $1, %rdi
	shrq $5, %rdi
	shrq %cl, %rdi
	shrq $1, %r8
	shrq $5, %r8
	shrq %cl, %r8
	shrq $1, %r9
	shrq $5, %r9
	shrq %cl, %r9
	shrq $1, %r10
	shrq $5, %r10
	shrq %cl, %r10
	shrq $1, %r11
	shrq $5, %r11
	shrq %cl, %r11
	shrq $1, %r12
	shrq $5, %r12
	shrq %cl, %r12
	shrq $1, %r13
	shrq $5, %r13
	shrq %cl, %r13
	shrq $1, %r14
	shrq $5, %r14
	shrq %cl, %r14
	shrq $1, %r15
	shrq $5, %r15
	shrq %cl, %r15
	shrq $1, (%rax)
	shrq $63, (%rax)
	shrq %cl, (%rax)
	shrq $1, (%rsp)
	shrq $63, (%rsp)
	shrq %cl, (%rsp)
	shrq $1, (%rbp)
	shrq $63, (%rbp)
	shrq %cl, (%rbp)
	shrq $1, (%r12)
	shrq $63, (%r12)
	shrq %cl, (%r12)
	shrq $1, (%r13)
	shrq $63, (%r13)
	shrq %cl, (%r13)
	shrq $1, (%r15)
	shrq $63, (%r15)
	shrq %cl, (%r15)
	shrq $1, 8(%rbx)
	shrq $63, 8(%rbx)
	shrq %cl, 8(%rbx)
	shrq $1, -8(%rbp)
	shrq $63, -8(%rbp)
	shrq %cl, -8(%rbp)
	shrq $1, -128(%rcx)
	shrq $63, -128(%rcx)
	shrq %cl, -128(%rcx)
	shrq $1, -129(%rdx)
	shrq $63, -129(%rdx)
	shrq %cl, -129(%rdx)
	shr %cl, %rbx
	shr $2, %r9
	sarb $1, %al
	sarb $5, %al
	sarb %cl, %al
	sarb $1, %cl
	sarb $5, %cl
	sarb %cl, %cl
	sarb $1, %dl
	sarb $5, %dl
	sarb %cl, %dl
	sarb $1, %bl
	sarb $5, %bl
	sarb %cl, %bl
	sarb $1, %spl
	sarb $5, %spl
	sarb %cl, %spl
	sarb $1, %bpl
	sarb $5, %bpl
	sarb %cl, %bpl
	sarb $1, %sil
	sarb $5, %sil
	sarb %cl, %sil
	sarb $1, %dil
	sarb $5, %dil
	sarb %cl, %dil
	sarb $1, %r8b
	sarb $5, %r8b
	sarb %cl, %r8b
	sarb $1, %r9b
	sarb $5, %r9b
	sarb %cl, %r9b
	sarb $1, %r10b
	sarb $5, %r10b
	sarb %cl, %r10b
	sarb $1, %r11b
	sarb $5, %r11b
	sarb %cl, %r11b
	sarb $1, %r12b
	sarb $5, %r12b
	sarb %cl, %r12b
	sarb $1, %r13b
	sarb $5, %r13b
	sarb %cl, %r13b
	sarb $1, %r14b
	sarb $5, %r14b
	sarb %cl, %r14b
	sarb $1, %r15b
	sarb $5, %r15b
	sarb %cl, %r15b
	sarb $1, (%rax)
	sarb $63, (%rax)
	sarb %cl, (%rax)
	sarb $1, (%rsp)
	sarb $63, (%rsp)
	sarb %cl, (%rsp)
	sarb $1, (%rbp)
	sarb $63, (%rbp)
	sarb %cl, (%rbp)
	sarb $1, (%r12)
	sarb $63, (%r12)
	sarb %cl, (%r12)
	sarb $1, (%r13)
	sarb $63, (%r13)
	sarb %cl, (%r13)
	sarb $1, (%r15)
	sarb $63, (%r15)
	sarb %cl, (%r15)
	sarb $1, 8(%rbx)
	sarb $63, 8(%rbx)
	sarb %cl, 8(%rbx)
	sarb $1, -8(%rbp)
	sarb $63, -8(%rbp)
	sarb %cl, -8(%rbp)
	sarb $1, -128(%rcx)
	sarb $63, -128(%rcx)
	sarb %cl, -128(%rcx)
	sarb $1, -129(%rdx)
	sarb $63, -129(%rdx)
	sarb %cl, -129(%rdx)
	sar %cl, %bl
	sar $2, %r9b
	sarw $1, %ax
	sarw $5, %ax
	sarw %cl, %ax
	sarw $1, %cx
	sarw $5, %cx
	sarw %cl, %cx
	sarw $1, %dx
	sarw $5, %dx
	sarw %cl, %dx
	sarw $1, %bx
	sarw $5, %bx
	sarw %cl, %bx
	sarw $1, %sp
	sarw $5, %sp
	sarw %cl, %sp
	sarw $1, %bp
	sarw $5, %bp
	sarw %cl, %bp
	sarw $1, %si
	sarw $5, %si
	sarw %cl, %si
	sarw $1, %di
	sarw $5, %di
	sarw %cl, %di
	sarw $1, %r8w
	sarw $5, %r8w
	sarw %cl, %r8w
	sarw $1, %r9w
	sarw $5, %r9w
	sarw %cl, %r9w
	sarw $1, %r10w
	sarw $5, %r10w
	sarw %cl, %r10w
	sarw $1, %r11w
	sarw $5, %r11w
	sarw %cl, %r11w
	sarw $1, %r12w
	sarw $5, %r12w
	sarw %cl, %r12w
	sarw $1, %r13w
	sarw $5, %r13w
	sarw %cl, %r13w
	sarw $1, %r14w
	sarw $5, %r14w
	sarw %cl, %r14w
	sarw $1, %r15w
	sarw $5, %r15w
	sarw %cl, %r15w
	sarw $1, (%rax)
	sarw $63, (%rax)
	sarw %cl, (%rax)
	sarw $1, (%rsp)
	sarw $63, (%rsp)
	sarw %cl, (%rsp)
	sarw $1, (%rbp)
	sarw $63, (%rbp)
	sarw %cl, (%rbp)
	sarw $1, (%r12)
	sarw $63, (%r12)
	sarw %cl, (%r12)
	sarw $1, (%r13)
	sarw $63, (%r13)
	sarw %cl, (%r13)
	sarw $1, (%r15)
	sarw $63, (%r15)
	sarw %cl, (%r15)
	sarw $1, 8(%rbx)
	sarw $63, 8(%rbx)
	sarw %cl, 8(%rbx)
	sarw $1, -8(%rbp)
	sarw $63, -8(%rbp)
	sarw %cl, -8(%rbp)
	sarw $1, -128(%rcx)
	sarw $63, -128(%rcx)
	sarw %cl, -128(%rcx)
	sarw $1, -129(%rdx)
	sarw $63, -129(%rdx)
	sarw %cl, -129(%rdx)
	sar %cl, %bx
	sar $2, %r9w
	sarl $1, %eax
	sarl $5, %eax
	sarl %cl, %eax
	sarl $1, %ecx
	sarl $5, %ecx
	sarl %cl, %ecx
	sarl $1, %edx
	sarl $5, %edx
	sarl %cl, %edx
	sarl $1, %ebx
	sarl $5, %ebx
	sarl %cl, %ebx
	sarl $1, %esp
	sarl $5, %esp
	sarl %cl, %esp
	sarl $1, %ebp
	sarl $5, %ebp
	sarl %cl, %ebp
	sarl $1, %esi
	sarl $5, %esi
	sarl %cl, %esi
	sarl $1, %edi
	sarl $5, %edi
	sarl %cl, %edi
	sarl $1, %r8d
	sarl $5, %r8d
	sarl %cl, %r8d
	sarl $1, %r9d
	sarl $5, %r9d
	sarl %cl, %r9d
	sarl $1, %r10d
	sarl $5, %r10d
	sarl %cl, %r10d
	sarl $1, %r11d
	sarl $5, %r11d
	sarl %cl, %r11d
	sarl $1, %r12d
	sarl $5, %r12d
	sarl %cl, %r12d
	sarl $1, %r13d
	sarl $5, %r13d
	sarl %cl, %r13d
	sarl $1, %r14d
	sarl $5, %r14d
	sarl %cl, %r14d
	sarl $1, %r15d
	sarl $5, %r15d
	sarl %cl, %r15d
	sarl $1, (%rax)
	sarl $63, (%rax)
	sarl %cl, (%rax)
	sarl $1, (%rsp)
	sarl $63, (%rsp)
	sarl %cl, (%rsp)
	sarl $1, (%rbp)
	sarl $63, (%rbp)
	sarl %cl, (%rbp)
	sarl $1, (%r12)
	sarl $63, (%r12)
	sarl %cl, (%r12)
	sarl $1, (%r13)
	sarl $63, (%r13)
	sarl %cl, (%r13)
	sarl $1, (%r15)
	sarl $63, (%r15)
	sarl %cl, (%r15)
	sarl $1, 8(%rbx)
	sarl $63, 8(%rbx)
	sarl %cl, 8(%rbx)
	sarl $1, -8(%rbp)
	sarl $63, -8(%rbp)
	sarl %cl, -8(%rbp)
	sarl $1, -128(%rcx)
	sarl $63, -128(%rcx)
	sarl %cl, -128(%rcx)
	sarl $1, -129(%rdx)
	sarl $63, -129(%rdx)
	sarl %cl, -129(%rdx)
	sar %cl, %ebx
	sar $2, %r9d
	sarq $1, %rax
	sarq $5, %rax
	sarq %cl, %rax
	sarq $1, %rcx
	sarq $5, %rcx
	sarq %cl, %rcx
	sarq $1, %rdx
	sarq $5, %rdx
	sarq %cl, %rdx
	sarq $1, %rbx
	sarq $5, %rbx
	sarq %cl, %rbx
	sarq $1, %rsp
	sarq $5, %rsp
	sarq %cl, %rsp
	sarq $1, %rbp
	sarq $5, %rbp
	sarq %cl, %rbp
	sarq $1, %rsi
	sarq $5, %rsi
	sarq %cl, %rsi
	sarq $1, %rdi
	sarq $5, %rdi
	sarq %cl, %rdi
	sarq $1, %r8
	sarq $5, %r8
	sarq %cl, %r8
	sarq $1, %r9
	sarq $5, %r9
	sarq %cl, %r9
	sarq $1, %r10
	sarq $5, %r10
	sarq %cl, %r10
	sarq $1, %r11
	sarq $5, %r11
	sarq %cl, %r11
	sarq $1, %r12
	sarq $5, %r12
	sarq %cl, %r12
	sarq $1, %r13
	sarq $5, %r13
	sarq %cl, %r13
	sarq $1, %r14
	sarq $5, %r14
	sarq %cl, %r14
	sarq $1, %r15
	sarq $5, %r15
	sarq %cl, %r15
	sarq $1, (%rax)
	sarq $63, (%rax)
	sarq %cl, (%rax)
	sarq $1, (%rsp)
	sarq $63, (%rsp)
	sarq %cl, (%rsp)
	sarq $1, (%rbp)
	sarq $63, (%rbp)
	sarq %cl, (%rbp)
	sarq $1, (%r12)
	sarq $63, (%r12)
	sarq %cl, (%r12)
	sarq $1, (%r13)
	sarq $63, (%r13)
	sarq %cl, (%r13)
	sarq $1, (%r15)
	sarq $63, (%r15)
	sarq %cl, (%r15)
	sarq $1, 8(%rbx)
	sarq $63, 8(%rbx)
	sarq %cl, 8(%rbx)
	sarq $1, -8(%rbp)
	sarq $63, -8(%rbp)
	sarq %cl, -8(%rbp)
	sarq $1, -128(%rcx)
	sarq $63, -128(%rcx)
	sarq %cl, -128(%rcx)
	sarq $1, -129(%rdx)
	sarq $63, -129(%rdx)
	sarq %cl, -129(%rdx)
	sar %cl, %rbx
	sar $2, %r9
	negb %al
	negb %cl
	negb %dl
	negb %bl
	negb %spl
	negb %bpl
	negb %sil
	negb %dil
	negb %r8b
	negb %r9b
	negb %r10b
	negb %r11b
	negb %r12b
	negb %r13b
	negb %r14b
	negb %r15b
	negb (%rax)
	negb (%rsp)
	negb (%rbp)
	negb (%r12)
	negb (%r13)
	negb (%r15)
	negb 8(%rbx)
	negb -8(%rbp)
	negb -128(%rcx)
	negb -129(%rdx)
	negb 127(%rsi)
	negb 128(%rdi)
	negb 4096(%r8)
	negb (%rax, %rcx)
	negb (%rax,%rcx,2)
	negb 8(%rsp, %rbp, 4)
	negb -16(%r13, %r14, 8)
	negb (, %rcx, 8)
	negb 16(, %r9, 4)
	negb 1234
	negb glob(%rip)
	negb loc+8(%rip)
	negb ext-4(%rip)
	negb glob(%rax)
	negb ext+16(%rbx, %rcx, 2)
	negb %fs:0
	negb %fs:8(%rax)
	negw %ax
	negw %cx
	negw %dx
	negw %bx
	negw %sp
	negw %bp
	negw %si
	negw %di
	negw %r8w
	negw %r9w
	negw %r10w
	negw %r11w
	negw %r12w
	negw %r13w
	negw %r14w
	negw %r15w
	negw (%rax)
	negw (%rsp)
	negw (%rbp)
	negw (%r12)
	negw (%r13)
	negw (%r15)
	negw 8(%rbx)
	negw -8(%rbp)
	negw -128(%rcx)
	negw -129(%rdx)
	negw 127(%rsi)
	negw 128(%rdi)
	negw 4096(%r8)
	negw (%rax, %rcx)
	negw (%rax,%rcx,2)
	negw 8(%rsp, %rbp, 4)
	negw -16(%r13, %r14, 8)
	negw (, %rcx, 8)
	negw 16(, %r9, 4)
	negw 1234
	negw glob(%rip)
	negw loc+8(%rip)
	negw ext-4(%rip)
	negw glob(%rax)
	negw ext+16(%rbx, %rcx, 2)
	negw %fs:0
	negw %fs:8(%rax)
	negl %eax
	negl %ecx
	negl %edx
	negl %ebx
	negl %esp
	negl %ebp
	negl %esi
	negl %edi
	negl %r8d
	negl %r9d
	negl %r10d
	negl %r11d
	negl %r12d
	negl %r13d
	negl %r14d
	negl %r15d
	negl (%rax)
	negl (%rsp)
	negl (%rbp)
	negl (%r12)
	negl (%r13)
	negl (%r15)
	negl 8(%rbx)
	negl -8(%rbp)
	negl -128(%rcx)
	negl -129(%rdx)
	negl 127(%rsi)
	negl 128(%rdi)
	negl 4096(%r8)
	negl (%rax, %rcx)
	negl (%rax,%rcx,2)
	negl 8(%rsp, %rbp, 4)
	negl -16(%r13, %r14, 8)
	negl (, %rcx, 8)
	negl 16(, %r9, 4)
	negl 1234
	negl glob(%rip)
	negl loc+8(%rip)
	negl ext-4(%rip)
	negl glob(%rax)
	negl ext+16(%rbx, %rcx, 2)
	negl %fs:0
	negl %fs:8(%rax)
	negq %rax
	negq %rcx
	negq %rdx
	negq %rbx
	negq %rsp
	negq %rbp
	negq %rsi
	negq %rdi
	negq %r8
	negq %r9
	negq %r10
	negq %r11
	negq %r12
	negq %r13
	negq %r14
	negq %r15
	negq (%rax)
	negq (%rsp)
	negq (%rbp)
	negq (%r12)
	negq (%r13)
	negq (%r15)
	negq 8(%rbx)
	negq -8(%rbp)
	negq -128(%rcx)
	negq -129(%rdx)
	negq 127(%rsi)
	negq 128(%rdi)
	negq 4096(%r8)
	negq (%rax, %rcx)
	negq (%rax,%rcx,2)
	negq 8(%rsp, %rbp, 4)
	negq -16(%r13, %r14, 8)
	negq (, %rcx, 8)
	negq 16(, %r9, 4)
	negq 1234
	negq glob(%rip)
	negq loc+8(%rip)
	negq ext-4(%rip)
	negq glob(%rax)
	negq ext+16(%rbx, %rcx, 2)
	negq %fs:0
	negq %fs:8(%rax)
	notb %al
	notb %cl
	notb %dl
	notb %bl
	notb %spl
	notb %bpl
	notb %sil
	notb %dil
	notb %r8b
	notb %r9b
	notb %r10b
	notb %r11b
	notb %r12b
	notb %r13b
	notb %r14b
	notb %r15b
	notb (%rax)
	notb (%rsp)
	notb (%rbp)
	notb (%r12)
	notb (%r13)
	notb (%r15)
	notb 8(%rbx)
	notb -8(%rbp)
	notb -128(%rcx)
	notb -129(%rdx)
	notb 127(%rsi)
	notb 128(%rdi)
	notb 4096(%r8)
	notb (%rax, %rcx)
	notb (%rax,%rcx,2)
	notb 8(%rsp, %rbp, 4)
	notb -16(%r13, %r14, 8)
	notb (, %rcx, 8)
	notb 16(, %r9, 4)
	notb 1234
	notb glob(%rip)
	notb loc+8(%rip)
	notb ext-4(%rip)
	notb glob(%rax)
	notb ext+16(%rbx, %rcx, 2)
	notb %fs:0
	notb %fs:8(%rax)
	notw %ax
	notw %cx
	notw %dx
	notw %bx
	notw %sp
	notw %bp
	notw %si
	notw %di
	notw %r8w
	notw %r9w
	notw %r10w
	notw %r11w
	notw %r12w
	notw %r13w
	notw %r14w
	notw %r15w
	notw (%rax)
	notw (%rsp)
	notw (%rbp)
	notw (%r12)
	notw (%r13)
	notw (%r15)
	notw 8(%rbx)
	notw -8(%rbp)
	notw -128(%rcx)
	notw -129(%rdx)
	notw 127(%rsi)
	notw 128(%rdi)
	notw 4096(%r8)
	notw (%rax, %rcx)
	notw (%rax,%rcx,2)
	notw 8(%rsp, %rbp, 4)
	notw -16(%r13, %r14, 8)
	notw (, %rcx, 8)
	notw 16(, %r9, 4)
	notw 1234
	notw glob(%rip)
	notw loc+8(%rip)
	notw ext-4(%rip)
	notw glob(%rax)
	notw ext+16(%rbx, %rcx, 2)
	notw %fs:0
	notw %fs:8(%rax)
	notl %eax
	notl %ecx
	notl %edx
	notl %ebx
	notl %esp
	notl %ebp
	notl %esi
	notl %edi
	notl %r8d
	notl %r9d
	notl %r10d
	notl %r11d
	notl %r12d
	notl %r13d
	notl %r14d
	notl %r15d
	notl (%rax)
	notl (%rsp)
	notl (%rbp)
	notl (%r12)
	notl (%r13)
	notl (%r15)
	notl 8(%rbx)
	notl -8(%rbp)
	notl -128(%rcx)
	notl -129(%rdx)
	notl 127(%rsi)
	notl 128(%rdi)
	notl 4096(%r8)
	notl (%rax, %rcx)
	notl (%rax,%rcx,2)
	notl 8(%rsp, %rbp, 4)
	notl -16(%r13, %r14, 8)
	notl (, %rcx, 8)
	notl 16(, %r9, 4)
	notl 1234
	notl glob(%rip)
	notl loc+8(%rip)
	notl ext-4(%rip)
	notl glob(%rax)
	notl ext+16(%rbx, %rcx, 2)
	notl %fs:0
	notl %fs:8(%rax)
	notq %rax
	notq %rcx
	notq %rdx
	notq %rbx
	notq %rsp
	notq %rbp
	notq %rsi
	notq %rdi
	notq %r8
	notq %r9
	notq %r10
	notq %r11
	notq %r12
	notq %r13
	notq %r14
	notq %r15
	notq (%rax)
	notq (%rsp)
	notq (%rbp)
	notq (%r12)
	notq (%r13)
	notq (%r15)
	notq 8(%rbx)
	notq -8(%rbp)
	notq -128(%rcx)
	notq -129(%rdx)
	notq 127(%rsi)
	notq 128(%rdi)
	notq 4096(%r8)
	notq (%rax, %rcx)
	notq (%rax,%rcx,2)
	notq 8(%rsp, %rbp, 4)
	notq -16(%r13, %r14, 8)
	notq (, %rcx, 8)
	notq 16(, %r9, 4)
	notq 1234
	notq glob(%rip)
	notq loc+8(%rip)
	notq ext-4(%rip)
	notq glob(%rax)
	notq ext+16(%rbx, %rcx, 2)
	notq %fs:0
	notq %fs:8(%rax)
	divb %al
	divb %cl
	divb %dl
	divb %bl
	divb %spl
	divb %bpl
	divb %sil
	divb %dil
	divb %r8b
	divb %r9b
	divb %r10b
	divb %r11b
	divb %r12b
	divb %r13b
	divb %r14b
	divb %r15b
	divb (%rax)
	divb (%rsp)
	divb (%rbp)
	divb (%r12)
	divb (%r13)
	divb (%r15)
	divb 8(%rbx)
	divb -8(%rbp)
	divb -128(%rcx)
	divb -129(%rdx)
	divb 127(%rsi)
	divb 128(%rdi)
	divb 4096(%r8)
	divb (%rax, %rcx)
	divb (%rax,%rcx,2)
	divb 8(%rsp, %rbp, 4)
	divb -16(%r13, %r14, 8)
	divb (, %rcx, 8)
	divb 16(, %r9, 4)
	divb 1234
	divb glob(%rip)
	divb loc+8(%rip)
	divb ext-4(%rip)
	divb glob(%rax)
	divb ext+16(%rbx, %rcx, 2)
	divb %fs:0
	divb %fs:8(%rax)
	divw %ax
	divw %cx
	divw %dx
	divw %bx
	divw %sp
	divw %bp
	divw %si
	divw %di
	divw %r8w
	divw %r9w
	divw %r10w
	divw %r11w
	divw %r12w
	divw %r13w
	divw %r14w
	divw %r15w
	divw (%rax)
	divw (%rsp)
	divw (%rbp)
	divw (%r12)
	divw (%r13)
	divw (%r15)
	divw 8(%rbx)
	divw -8(%rbp)
	divw -128(%rcx)
	divw -129(%rdx)
	divw 127(%rsi)
	divw 128(%rdi)
	divw 4096(%r8)
	divw (%rax, %rcx)
	divw (%rax,%rcx,2)
	divw 8(%rsp, %rbp, 4)
	divw -16(%r13, %r14, 8)
	divw (, %rcx, 8)
	divw 16(, %r9, 4)
	divw 1234
	divw glob(%rip)
	divw loc+8(%rip)
	divw ext-4(%rip)
	divw glob(%rax)
	divw ext+16(%rbx, %rcx, 2)
	divw %fs:0
	divw %fs:8(%rax)
	divl %eax
	divl %ecx
	divl %edx
	divl %ebx
	divl %esp
	divl %ebp
	divl %esi
	divl %edi
	divl %r8d
	divl %r9d
	divl %r10d
	divl %r11d
	divl %r12d
	divl %r13d
	divl %r14d
	divl %r15d
	divl (%rax)
	divl (%rsp)
	divl (%rbp)
	divl (%r12)
	divl (%r13)
	divl (%r15)
	divl 8(%rbx)
	divl -8(%rbp)
	divl -128(%rcx)
	divl -129(%rdx)
	divl 127(%rsi)
	divl 128(%rdi)
	divl 4096(%r8)
	divl (%rax, %rcx)
	divl (%rax,%rcx,2)
	divl 8(%rsp, %rbp, 4)
	divl -16(%r13, %r14, 8)
	divl (, %rcx, 8)
	divl 16(, %r9, 4)
	divl 1234
	divl glob(%rip)
	divl loc+8(%rip)
	divl ext-4(%rip)
	divl glob(%rax)
	divl ext+16(%rbx, %rcx, 2)
	divl %fs:0
	divl %fs:8(%rax)
	divq %rax
	divq %rcx
	divq %rdx
	divq %rbx
	divq %rsp
	divq %rbp
	divq %rsi
	divq %rdi
	divq %r8
	divq %r9
	divq %r10
	divq %r11
	divq %r12
	divq %r13
	divq %r14
	divq %r15
	divq (%rax)
	divq (%rsp)
	divq (%rbp)
	divq (%r12)
	divq (%r13)
	divq (%r15)
	divq 8(%rbx)
	divq -8(%rbp)
	divq -128(%rcx)
	divq -129(%rdx)
	divq 127(%rsi)
	divq 128(%rdi)
	divq 4096(%r8)
	divq (%rax, %rcx)
	divq (%rax,%rcx,2)
	divq 8(%rsp, %rbp, 4)
	divq -16(%r13, %r14, 8)
	divq (, %rcx, 8)
	divq 16(, %r9, 4)
	divq 1234
	divq glob(%rip)
	divq loc+8(%rip)
	divq ext-4(%rip)
	divq glob(%rax)
	divq ext+16(%rbx, %rcx, 2)
	divq %fs:0
	divq %fs:8(%rax)
	idivb %al
	idivb %cl
	idivb %dl
	idivb %bl
	idivb %spl
	idivb %bpl
	idivb %sil
	idivb %dil
	idivb %r8b
	idivb %r9b
	idivb %r10b
	idivb %r11b
	idivb %r12b
	idivb %r13b
	idivb %r14b
	idivb %r15b
	idivb (%rax)
	idivb (%rsp)
	idivb (%rbp)
	idivb (%r12)
	idivb (%r13)
	idivb (%r15)
	idivb 8(%rbx)
	idivb -8(%rbp)
	idivb -128(%rcx)
	idivb -129(%rdx)
	idivb 127(%rsi)
	idivb 128(%rdi)
	idivb 4096(%r8)
	idivb (%rax, %rcx)
	idivb (%rax,%rcx,2)
	idivb 8(%rsp, %rbp, 4)
	idivb -16(%r13, %r14, 8)
	idivb (, %rcx, 8)
	idivb 16(, %r9, 4)
	idivb 1234
	idivb glob(%rip)
	idivb loc+8(%rip)
	idivb ext-4(%rip)
	idivb glob(%rax)
	idivb ext+16(%rbx, %rcx, 2)
	idivb %fs:0
	idivb %fs:8(%rax)
	idivw %ax
	idivw %cx
	idivw %dx
	idivw %bx
	idivw %sp
	idivw %bp
	idivw %si
	idivw %di
	idivw %r8w
	idivw %r9w
	idivw %r10w
	idivw %r11w
	idivw %r12w
	idivw %r13w
	idivw %r14w
	idivw %r15w
	idivw (%rax)
	idivw (%rsp)
	idivw (%rbp)
	idivw (%r12)
	idivw (%r13)
	idivw (%r15)
	idivw 8(%rbx)
	idivw -8(%rbp)
	idivw -128(%rcx)
	idivw -129(%rdx)
	idivw 127(%rsi)
	idivw 128(%rdi)
	idivw 4096(%r8)
	idivw (%rax, %rcx)
	idivw (%rax,%rcx,2)
	idivw 8(%rsp, %rbp, 4)
	idivw -16(%r13, %r14, 8)
	idivw (, %rcx, 8)
	idivw 16(, %r9, 4)
	idivw 1234
	idivw glob(%rip)
	idivw loc+8(%rip)
	idivw ext-4(%rip)
	idivw glob(%rax)
	idivw ext+16(%rbx, %rcx, 2)
	idivw %fs:0
	idivw %fs:8(%rax)
	idivl %eax
	idivl %ecx
	idivl %edx
	idivl %ebx
	idivl %esp
	idivl %ebp
	idivl %esi
	idivl %edi
	idivl %r8d
	idivl %r9d
	idivl %r10d
	idivl %r11d
	idivl %r12d
	idivl %r13d
	idivl %r14d
	idivl %r15d
	idivl (%rax)
	idivl (%rsp)
	idivl (%rbp)
	idivl (%r12)
	idivl (%r13)
	idivl (%r15)
	idivl 8(%rbx)
	idivl -8(%rbp)
	idivl -128(%rcx)
	idivl -129(%rdx)
	idivl 127(%rsi)
	idivl 128(%rdi)
	idivl 4096(%r8)
	idivl (%rax, %rcx)
	idivl (%rax,%rcx,2)
	idivl 8(%rsp, %rbp, 4)
	idivl -16(%r13, %r14, 8)
	idivl (, %rcx, 8)
	idivl 16(, %r9, 4)
	idivl 1234
	idivl glob(%rip)
	idivl loc+8(%rip)
	idivl ext-4(%rip)
	idivl glob(%rax)
	idivl ext+16(%rbx, %rcx, 2)
	idivl %fs:0
	idivl %fs:8(%rax)
	idivq %rax
	idivq %rcx
	idivq %rdx
	idivq %rbx
	idivq %rsp
	idivq %rbp
	idivq %rsi
	idivq %rdi
	idivq %r8
	idivq %r9
	idivq %r10
	idivq %r11
	idivq %r12
	idivq %r13
	idivq %r14
	idivq %r15
	idivq (%rax)
	idivq (%rsp)
	idivq (%rbp)
	idivq (%r12)
	idivq (%r13)
	idivq (%r15)
	idivq 8(%rbx)
	idivq -8(%rbp)
	idivq -128(%rcx)
	idivq -129(%rdx)
	idivq 127(%rsi)
	idivq 128(%rdi)
	idivq 4096(%r8)
	idivq (%rax, %rcx)
	idivq (%rax,%rcx,2)
	idivq 8(%rsp, %rbp, 4)
	idivq -16(%r13, %r14, 8)
	idivq (, %rcx, 8)
	idivq 16(, %r9, 4)
	idivq 1234
	idivq glob(%rip)
	idivq loc+8(%rip)
	idivq ext-4(%rip)
	idivq glob(%rax)
	idivq ext+16(%rbx, %rcx, 2)
	idivq %fs:0
	idivq %fs:8(%rax)
	mulb %al
	mulb %cl
	mulb %dl
	mulb %bl
	mulb %spl
	mulb %bpl
	mulb %sil
	mulb %dil
	mulb %r8b
	mulb %r9b
	mulb %r10b
	mulb %r11b
	mulb %r12b
	mulb %r13b
	mulb %r14b
	mulb %r15b
	mulb (%rax)
	mulb (%rsp)
	mulb (%rbp)
	mulb (%r12)
	mulb (%r13)
	mulb (%r15)
	mulb 8(%rbx)
	mulb -8(%rbp)
	mulb -128(%rcx)
	mulb -129(%rdx)
	mulb 127(%rsi)
	mulb 128(%rdi)
	mulb 4096(%r8)
	mulb (%rax, %rcx)
	mulb (%rax,%rcx,2)
	mulb 8(%rsp, %rbp, 4)
	mulb -16(%r13, %r14, 8)
	mulb (, %rcx, 8)
	mulb 16(, %r9, 4)
	mulb 1234
	mulb glob(%rip)
	mulb loc+8(%rip)
	mulb ext-4(%rip)
	mulb glob(%rax)
	mulb ext+16(%rbx, %rcx, 2)
	mulb %fs:0
	mulb %fs:8(%rax)
	mulw %ax
	mulw %cx
	mulw %dx
	mulw %bx
	mulw %sp
	mulw %bp
	mulw %si
	mulw %di
	mulw %r8w
	mulw %r9w
	mulw %r10w
	mulw %r11w
	mulw %r12w
	mulw %r13w
	mulw %r14w
	mulw %r15w
	mulw (%rax)
	mulw (%rsp)
	mulw (%rbp)
	mulw (%r12)
	mulw (%r13)
	mulw (%r15)
	mulw 8(%rbx)
	mulw -8(%rbp)
	mulw -128(%rcx)
	mulw -129(%rdx)
	mulw 127(%rsi)
	mulw 128(%rdi)
	mulw 4096(%r8)
	mulw (%rax, %rcx)
	mulw (%rax,%rcx,2)
	mulw 8(%rsp, %rbp, 4)
	mulw -16(%r13, %r14, 8)
	mulw (, %rcx, 8)
	mulw 16(, %r9, 4)
	mulw 1234
	mulw glob(%rip)
	mulw loc+8(%rip)
	mulw ext-4(%rip)
	mulw glob(%rax)
	mulw ext+16(%rbx, %rcx, 2)
	mulw %fs:0
	mulw %fs:8(%rax)
	mull %eax
	mull %ecx
	mull %edx
	mull %ebx
	mull %esp
	mull %ebp
	mull %esi
	mull %edi
	mull %r8d
	mull %r9d
	mull %r10d
	mull %r11d
	mull %r12d
	mull %r13d
	mull %r14d
	mull %r15d
	mull (%rax)
	mull (%rsp)
	mull (%rbp)
	mull (%r12)
	mull (%r13)
	mull (%r15)
	mull 8(%rbx)
	mull -8(%rbp)
	mull -128(%rcx)
	mull -129(%rdx)
	mull 127(%rsi)
	mull 128(%rdi)
	mull 4096(%r8)
	mull (%rax, %rcx)
	mull (%rax,%rcx,2)
	mull 8(%rsp, %rbp, 4)
	mull -16(%r13, %r14, 8)
	mull (, %rcx, 8)
	mull 16(, %r9, 4)
	mull 1234
	mull glob(%rip)
	mull loc+8(%rip)
	mull ext-4(%rip)
	mull glob(%rax)
	mull ext+16(%rbx, %rcx, 2)
	mull %fs:0
	mull %fs:8(%rax)
	mulq %rax
	mulq %rcx
	mulq %rdx
	mulq %rbx
	mulq %rsp
	mulq %rbp
	mulq %rsi
	mulq %rdi
	mulq %r8
	mulq %r9
	mulq %r10
	mulq %r11
	mulq %r12
	mulq %r13
	mulq %r14
	mulq %r15
	mulq (%rax)
	mulq (%rsp)
	mulq (%rbp)
	mulq (%r12)
	mulq (%r13)
	mulq (%r15)
	mulq 8(%rbx)
	mulq -8(%rbp)
	mulq -128(%rcx)
	mulq -129(%rdx)
	mulq 127(%rsi)
	mulq 128(%rdi)
	mulq 4096(%r8)
	mulq (%rax, %rcx)
	mulq (%rax,%rcx,2)
	mulq 8(%rsp, %rbp, 4)
	mulq -16(%r13, %r14, 8)
	mulq (, %rcx, 8)
	mulq 16(, %r9, 4)
	mulq 1234
	mulq glob(%rip)
	mulq loc+8(%rip)
	mulq ext-4(%rip)
	mulq glob(%rax)
	mulq ext+16(%rbx, %rcx, 2)
	mulq %fs:0
	mulq %fs:8(%rax)
	seto %al
	seto %dil
	seto %r11b
	seto -1(%rbp)
	cmovo %cx, %ax
	cmovo %r14w, %si
	cmovo -8(%rbp), %r10w
	cmovo %ecx, %eax
	cmovo %r14d, %esi
	cmovo -8(%rbp), %r10d
	cmovo %rcx, %rax
	cmovo %r14, %rsi
	cmovo -8(%rbp), %r10
	setno %al
	setno %dil
	setno %r11b
	setno -1(%rbp)
	cmovno %cx, %ax
	cmovno %r14w, %si
	cmovno -8(%rbp), %r10w
	cmovno %ecx, %eax
	cmovno %r14d, %esi
	cmovno -8(%rbp), %r10d
	cmovno %rcx, %rax
	cmovno %r14, %rsi
	cmovno -8(%rbp), %r10
	setb %al
	setb %dil
	setb %r11b
	setb -1(%rbp)
	cmovb %cx, %ax
	cmovb %r14w, %si
	cmovb -8(%rbp), %r10w
	cmovb %ecx, %eax
	cmovb %r14d, %esi
	cmovb -8(%rbp), %r10d
	cmovb %rcx, %rax
	cmovb %r14, %rsi
	cmovb -8(%rbp), %r10
	setc %al
	setc %dil
	setc %r11b
	setc -1(%rbp)
	cmovc %cx, %ax
	cmovc %r14w, %si
	cmovc -8(%rbp), %r10w
	cmovc %ecx, %eax
	cmovc %r14d, %esi
	cmovc -8(%rbp), %r10d
	cmovc %rcx, %rax
	cmovc %r14, %rsi
	cmovc -8(%rbp), %r10
	setnae %al
	setnae %dil
	setnae %r11b
	setnae -1(%rbp)
	cmovnae %cx, %ax
	cmovnae %r14w, %si
	cmovnae -8(%rbp), %r10w
	cmovnae %ecx, %eax
	cmovnae %r14d, %esi
	cmovnae -8(%rbp), %r10d
	cmovnae %rcx, %rax
	cmovnae %r14, %rsi
	cmovnae -8(%rbp), %r10
	setae %al
	setae %dil
	setae %r11b
	setae -1(%rbp)
	cmovae %cx, %ax
	cmovae %r14w, %si
	cmovae -8(%rbp), %r10w
	cmovae %ecx, %eax
	cmovae %r14d, %esi
	cmovae -8(%rbp), %r10d
	cmovae %rcx, %rax
	cmovae %r14, %rsi
	cmovae -8(%rbp), %r10
	setnb %al
	setnb %dil
	setnb %r11b
	setnb -1(%rbp)
	cmovnb %cx, %ax
	cmovnb %r14w, %si
	cmovnb -8(%rbp), %r10w
	cmovnb %ecx, %eax
	cmovnb %r14d, %esi
	cmovnb -8(%rbp), %r10d
	cmovnb %rcx, %rax
	cmovnb %r14, %rsi
	cmovnb -8(%rbp), %r10
	setnc %al
	setnc %dil
	setnc %r11b
	setnc -1(%rbp)
	cmovnc %cx, %ax
	cmovnc %r14w, %si
	cmovnc -8(%rbp), %r10w
	cmovnc %ecx, %eax
	cmovnc %r14d, %esi
	cmovnc -8(%rbp), %r10d
	cmovnc %rcx, %rax
	cmovnc %r14, %rsi
	cmovnc -8(%rbp), %r10
	sete %al
	sete %dil
	sete %r11b
	sete -1(%rbp)
	cmove %cx, %ax
	cmove %r14w, %si
	cmove -8(%rbp), %r10w
	cmove %ecx, %eax
	cmove %r14d, %esi
	cmove -8(%rbp), %r10d
	cmove %rcx, %rax
	cmove %r14, %rsi
	cmove -8(%rbp), %r10
	setz %al
	setz %dil
	setz %r11b
	setz -1(%rbp)
	cmovz %cx, %ax
	cmovz %r14w, %si
	cmovz -8(%rbp), %r10w
	cmovz %ecx, %eax
	cmovz %r14d, %esi
	cmovz -8(%rbp), %r10d
	cmovz %rcx, %rax
	cmovz %r14, %rsi
	cmovz -8(%rbp), %r10
	setne %al
	setne %dil
	setne %r11b
	setne -1(%rbp)
	cmovne %cx, %ax
	cmovne %r14w, %si
	cmovne -8(%rbp), %r10w
	cmovne %ecx, %eax
	cmovne %r14d, %esi
	cmovne -8(%rbp), %r10d
	cmovne %rcx, %rax
	cmovne %r14, %rsi
	cmovne -8(%rbp), %r10
	setnz %al
	setnz %dil
	setnz %r11b
	setnz -1(%rbp)
	cmovnz %cx, %ax
	cmovnz %r14w, %si
	cmovnz -8(%rbp), %r10w
	cmovnz %ecx, %eax
	cmovnz %r14d, %esi
	cmovnz -8(%rbp), %r10d
	cmovnz %rcx, %rax
	cmovnz %r14, %rsi
	cmovnz -8(%rbp), %r10
	setbe %al
	setbe %dil
	setbe %r11b
	setbe -1(%rbp)
	cmovbe %cx, %ax
	cmovbe %r14w, %si
	cmovbe -8(%rbp), %r10w
	cmovbe %ecx, %eax
	cmovbe %r14d, %esi
	cmovbe -8(%rbp), %r10d
	cmovbe %rcx, %rax
	cmovbe %r14, %rsi
	cmovbe -8(%rbp), %r10
	setna %al
	setna %dil
	setna %r11b
	setna -1(%rbp)
	cmovna %cx, %ax
	cmovna %r14w, %si
	cmovna -8(%rbp), %r10w
	cmovna %ecx, %eax
	cmovna %r14d, %esi
	cmovna -8(%rbp), %r10d
	cmovna %rcx, %rax
	cmovna %r14, %rsi
	cmovna -8(%rbp), %r10
	seta %al
	seta %dil
	seta %r11b
	seta -1(%rbp)
	cmova %cx, %ax
	cmova %r14w, %si
	cmova -8(%rbp), %r10w
	cmova %ecx, %eax
	cmova %r14d, %esi
	cmova -8(%rbp), %r10d
	cmova %rcx, %rax
	cmova %r14, %rsi
	cmova -8(%rbp), %r10
	setnbe %al
	setnbe %dil
	setnbe %r11b
	setnbe -1(%rbp)
	cmovnbe %cx, %ax
	cmovnbe %r14w, %si
	cmovnbe -8(%rbp), %r10w
	cmovnbe %ecx, %eax
	cmovnbe %r14d, %esi
	cmovnbe -8(%rbp), %r10d
	cmovnbe %rcx, %rax
	cmovnbe %r14, %rsi
	cmovnbe -8(%rbp), %r10
	sets %al
	sets %dil
	sets %r11b
	sets -1(%rbp)
	cmovs %cx, %ax
	cmovs %r14w, %si
	cmovs -8(%rbp), %r10w
	cmovs %ecx, %eax
	cmovs %r14d, %esi
	cmovs -8(%rbp), %r10d
	cmovs %rcx, %rax
	cmovs %r14, %rsi
	cmovs -8(%rbp), %r10
	setns %al
	setns %dil
	setns %r11b
	setns -1(%rbp)
	cmovns %cx, %ax
	cmovns %r14w, %si
	cmovns -8(%rbp), %r10w
	cmovns %ecx, %eax
	cmovns %r14d, %esi
	cmovns -8(%rbp), %r10d
	cmovns %rcx, %rax
	cmovns %r14, %rsi
	cmovns -8(%rbp), %r10
	setp %al
	setp %dil
	setp %r11b
	setp -1(%rbp)
	cmovp %cx, %ax
	cmovp %r14w, %si
	cmovp -8(%rbp), %r10w
	cmovp %ecx, %eax
	cmovp %r14d, %esi
	cmovp -8(%rbp), %r10d
	cmovp %rcx, %rax
	cmovp %r14, %rsi
	cmovp -8(%rbp), %r10
	setpe %al
	setpe %dil
	setpe %r11b
	setpe -1(%rbp)
	cmovpe %cx, %ax
	cmovpe %r14w, %si
	cmovpe -8(%rbp), %r10w
	cmovpe %ecx, %eax
	cmovpe %r14d, %esi
	cmovpe -8(%rbp), %r10d
	cmovpe %rcx, %rax
	cmovpe %r14, %rsi
	cmovpe -8(%rbp), %r10
	setnp %al
	setnp %dil
	setnp %r11b
	setnp -1(%rbp)
	cmovnp %cx, %ax
	cmovnp %r14w, %si
	cmovnp -8(%rbp), %r10w
	cmovnp %ecx, %eax
	cmovnp %r14d, %esi
	cmovnp -8(%rbp), %r10d
	cmovnp %rcx, %rax
	cmovnp %r14, %rsi
	cmovnp -8(%rbp), %r10
	setpo %al
	setpo %dil
	setpo %r11b
	setpo -1(%rbp)
	cmovpo %cx, %ax
	cmovpo %r14w, %si
	cmovpo -8(%rbp), %r10w
	cmovpo %ecx, %eax
	cmovpo %r14d, %esi
	cmovpo -8(%rbp), %r10d
	cmovpo %rcx, %rax
	cmovpo %r14, %rsi
	cmovpo -8(%rbp), %r10
	setl %al
	setl %dil
	setl %r11b
	setl -1(%rbp)
	cmovl %cx, %ax
	cmovl %r14w, %si
	cmovl -8(%rbp), %r10w
	cmovl %ecx, %eax
	cmovl %r14d, %esi
	cmovl -8(%rbp), %r10d
	cmovl %rcx, %rax
	cmovl %r14, %rsi
	cmovl -8(%rbp), %r10
	setnge %al
	setnge %dil
	setnge %r11b
	setnge -1(%rbp)
	cmovnge %cx, %ax
	cmovnge %r14w, %si
	cmovnge -8(%rbp), %r10w
	cmovnge %ecx, %eax
	cmovnge %r14d, %esi
	cmovnge -8(%rbp), %r10d
	cmovnge %rcx, %rax
	cmovnge %r14, %rsi
	cmovnge -8(%rbp), %r10
	setge %al
	setge %dil
	setge %r11b
	setge -1(%rbp)
	cmovge %cx, %ax
	cmovge %r14w, %si
	cmovge -8(%rbp), %r10w
	cmovge %ecx, %eax
	cmovge %r14d, %esi
	cmovge -8(%rbp), %r10d
	cmovge %rcx, %rax
	cmovge %r14, %rsi
	cmovge -8(%rbp), %r10
	setnl %al
	setnl %dil
	setnl %r11b
	setnl -1(%rbp)
	cmovnl %cx, %ax
	cmovnl %r14w, %si
	cmovnl -8(%rbp), %r10w
	cmovnl %ecx, %eax
	cmovnl %r14d, %esi
	cmovnl -8(%rbp), %r10d
	cmovnl %rcx, %rax
	cmovnl %r14, %rsi
	cmovnl -8(%rbp), %r10
	setle %al
	setle %dil
	setle %r11b
	setle -1(%rbp)
	cmovle %cx, %ax
	cmovle %r14w, %si
	cmovle -8(%rbp), %r10w
	cmovle %ecx, %eax
	cmovle %r14d, %esi
	cmovle -8(%rbp), %r10d
	cmovle %rcx, %rax
	cmovle %r14, %rsi
	cmovle -8(%rbp), %r10
	setng %al
	setng %dil
	setng %r11b
	setng -1(%rbp)
	cmovng %cx, %ax
	cmovng %r14w, %si
	cmovng -8(%rbp), %r10w
	cmovng %ecx, %eax
	cmovng %r14d, %esi
	cmovng -8(%rbp), %r10d
	cmovng %rcx, %rax
	cmovng %r14, %rsi
	cmovng -8(%rbp), %r10
	setg %al
	setg %dil
	setg %r11b
	setg -1(%rbp)
	cmovg %cx, %ax
	cmovg %r14w, %si
	cmovg -8(%rbp), %r10w
	cmovg %ecx, %eax
	cmovg %r14d, %esi
	cmovg -8(%rbp), %r10d
	cmovg %rcx, %rax
	cmovg %r14, %rsi
	cmovg -8(%rbp), %r10
	setnle %al
	setnle %dil
	setnle %r11b
	setnle -1(%rbp)
	cmovnle %cx, %ax
	cmovnle %r14w, %si
	cmovnle -8(%rbp), %r10w
	cmovnle %ecx, %eax
	cmovnle %r14d, %esi
	cmovnle -8(%rbp), %r10d
	cmovnle %rcx, %rax
	cmovnle %r14, %rsi
	cmovnle -8(%rbp), %r10
	xchgw %ax, %ax
	xchgw %ax, %ax
	xchgw %ax, %bp
	xchgw %bp, %ax
	xchgw %ax, %r12w
	xchgw %r12w, %ax
	xchgw %cx, %ax
	xchgw %ax, %cx
	xchgw %cx, %bp
	xchgw %bp, %cx
	xchgw %cx, %r12w
	xchgw %r12w, %cx
	xchgw %dx, %ax
	xchgw %ax, %dx
	xchgw %dx, %bp
	xchgw %bp, %dx
	xchgw %dx, %r12w
	xchgw %r12w, %dx
	xchgw %bx, %ax
	xchgw %ax, %bx
	xchgw %bx, %bp
	xchgw %bp, %bx
	xchgw %bx, %r12w
	xchgw %r12w, %bx
	xchgw %sp, %ax
	xchgw %ax, %sp
	xchgw %sp, %bp
	xchgw %bp, %sp
	xchgw %sp, %r12w
	xchgw %r12w, %sp
	xchgw %bp, %ax
	xchgw %ax, %bp
	xchgw %bp, %bp
	xchgw %bp, %bp
	xchgw %bp, %r12w
	xchgw %r12w, %bp
	xchgw %si, %ax
	xchgw %ax, %si
	xchgw %si, %bp
	xchgw %bp, %si
	xchgw %si, %r12w
	xchgw %r12w, %si
	xchgw %di, %ax
	xchgw %ax, %di
	xchgw %di, %bp
	xchgw %bp, %di
	xchgw %di, %r12w
	xchgw %r12w, %di
	xchgw %r8w, %ax
	xchgw %ax, %r8w
	xchgw %r8w, %bp
	xchgw %bp, %r8w
	xchgw %r8w, %r12w
	xchgw %r12w, %r8w
	xchgw %r9w, %ax
	xchgw %ax, %r9w
	xchgw %r9w, %bp
	xchgw %bp, %r9w
	xchgw %r9w, %r12w
	xchgw %r12w, %r9w
	xchgw %r10w, %ax
	xchgw %ax, %r10w
	xchgw %r10w, %bp
	xchgw %bp, %r10w
	xchgw %r10w, %r12w
	xchgw %r12w, %r10w
	xchgw %r11w, %ax
	xchgw %ax, %r11w
	xchgw %r11w, %bp
	xchgw %bp, %r11w
	xchgw %r11w, %r12w
	xchgw %r12w, %r11w
	xchgw %r12w, %ax
	xchgw %ax, %r12w
	xchgw %r12w, %bp
	xchgw %bp, %r12w
	xchgw %r12w, %r12w
	xchgw %r12w, %r12w
	xchgw %r13w, %ax
	xchgw %ax, %r13w
	xchgw %r13w, %bp
	xchgw %bp, %r13w
	xchgw %r13w, %r12w
	xchgw %r12w, %r13w
	xchgw %r14w, %ax
	xchgw %ax, %r14w
	xchgw %r14w, %bp
	xchgw %bp, %r14w
	xchgw %r14w, %r12w
	xchgw %r12w, %r14w
	xchgw %r15w, %ax
	xchgw %ax, %r15w
	xchgw %r15w, %bp
	xchgw %bp, %r15w
	xchgw %r15w, %r12w
	xchgw %r12w, %r15w
	xchgw %bx, (%rax)
	xchgw 8(%rsp), %r9w
	xchgl %eax, %eax
	xchgl %eax, %eax
	xchgl %eax, %ebp
	xchgl %ebp, %eax
	xchgl %eax, %r12d
	xchgl %r12d, %eax
	xchgl %ecx, %eax
	xchgl %eax, %ecx
	xchgl %ecx, %ebp
	xchgl %ebp, %ecx
	xchgl %ecx, %r12d
	xchgl %r12d, %ecx
	xchgl %edx, %eax
	xchgl %eax, %edx
	xchgl %edx, %ebp
	xchgl %ebp, %edx
	xchgl %edx, %r12d
	xchgl %r12d, %edx
	xchgl %ebx, %eax
	xchgl %eax, %ebx
	xchgl %ebx, %ebp
	xchgl %ebp, %ebx
	xchgl %ebx, %r12d
	xchgl %r12d, %ebx
	xchgl %esp, %eax
	xchgl %eax, %esp
	xchgl %esp, %ebp
	xchgl %ebp, %esp
	xchgl %esp, %r12d
	xchgl %r12d, %esp
	xchgl %ebp, %eax
	xchgl %eax, %ebp
	xchgl %ebp, %ebp
	xchgl %ebp, %ebp
	xchgl %ebp, %r12d
	xchgl %r12d, %ebp
	xchgl %esi, %eax
	xchgl %eax, %esi
	xchgl %esi, %ebp
	xchgl %ebp, %esi
	xchgl %esi, %r12d
	xchgl %r12d, %esi
	xchgl %edi, %eax
	xchgl %eax, %edi
	xchgl %edi, %ebp
	xchgl %ebp, %edi
	xchgl %edi, %r12d
	xchgl %r12d, %edi
	xchgl %r8d, %eax
	xchgl %eax, %r8d
	xchgl %r8d, %ebp
	xchgl %ebp, %r8d
	xchgl %r8d, %r12d
	xchgl %r12d, %r8d
	xchgl %r9d, %eax
	xchgl %eax, %r9d
	xchgl %r9d, %ebp
	xchgl %ebp, %r9d
	xchgl %r9d, %r12d
	xchgl %r12d, %r9d
	xchgl %r10d, %eax
	xchgl %eax, %r10d
	xchgl %r10d, %ebp
	xchgl %ebp, %r10d
	xchgl %r10d, %r12d
	xchgl %r12d, %r10d
	xchgl %r11d, %eax
	xchgl %eax, %r11d
	xchgl %r11d, %ebp
	xchgl %ebp, %r11d
	xchgl %r11d, %r12d
	xchgl %r12d, %r11d
	xchgl %r12d, %eax
	xchgl %eax, %r12d
	xchgl %r12d, %ebp
	xchgl %ebp, %r12d
	xchgl %r12d, %r12d
	xchgl %r12d, %r12d
	xchgl %r13d, %eax
	xchgl %eax, %r13d
	xchgl %r13d, %ebp
	xchgl %ebp, %r13d
	xchgl %r13d, %r12d
	xchgl %r12d, %r13d
	xchgl %r14d, %eax
	xchgl %eax, %r14d
	xchgl %r14d, %ebp
	xchgl %ebp, %r14d
	xchgl %r14d, %r12d
	xchgl %r12d, %r14d
	xchgl %r15d, %eax
	xchgl %eax, %r15d
	xchgl %r15d, %ebp
	xchgl %ebp, %r15d
	xchgl %r15d, %r12d
	xchgl %r12d, %r15d
	xchgl %ebx, (%rax)
	xchgl 8(%rsp), %r9d
	xchgq %rax, %rax
	xchgq %rax, %rax
	xchgq %rax, %rbp
	xchgq %rbp, %rax
	xchgq %rax, %r12
	xchgq %r12, %rax
	xchgq %rcx, %rax
	xchgq %rax, %rcx
	xchgq %rcx, %rbp
	xchgq %rbp, %rcx
	xchgq %rcx, %r12
	xchgq %r12, %rcx
	xchgq %rdx, %rax
	xchgq %rax, %rdx
	xchgq %rdx, %rbp
	xchgq %rbp, %rdx
	xchgq %rdx, %r12
	xchgq %r12, %rdx
	xchgq %rbx, %rax
	xchgq %rax, %rbx
	xchgq %rbx, %rbp
	xchgq %rbp, %rbx
	xchgq %rbx, %r12
	xchgq %r12, %rbx
	xchgq %rsp, %rax
	xchgq %rax, %rsp
	xchgq %rsp, %rbp
	xchgq %rbp, %rsp
	xchgq %rsp, %r12
	xchgq %r12, %rsp
	xchgq %rbp, %rax
	xchgq %rax, %rbp
	xchgq %rbp, %rbp
	xchgq %rbp, %rbp
	xchgq %rbp, %r12
	xchgq %r12, %rbp
	xchgq %rsi, %rax
	xchgq %rax, %rsi
	xchgq %rsi, %rbp
	xchgq %rbp, %rsi
	xchgq %rsi, %r12
	xchgq %r12, %rsi
	xchgq %rdi, %rax
	xchgq %rax, %rdi
	xchgq %rdi, %rbp
	xchgq %rbp, %rdi
	xchgq %rdi, %r12
	xchgq %r12, %rdi
	xchgq %r8, %rax
	xchgq %rax, %r8
	xchgq %r8, %rbp
	xchgq %rbp, %r8
	xchgq %r8, %r12
	xchgq %r12, %r8
	xchgq %r9, %rax
	xchgq %rax, %r9
	xchgq %r9, %rbp
	xchgq %rbp, %r9
	xchgq %r9, %r12
	xchgq %r12, %r9
	xchgq %r10, %rax
	xchgq %rax, %r10
	xchgq %r10, %rbp
	xchgq %rbp, %r10
	xchgq %r10, %r12
	xchgq %r12, %r10
	xchgq %r11, %rax
	xchgq %rax, %r11
	xchgq %r11, %rbp
	xchgq %rbp, %r11
	xchgq %r11, %r12
	xchgq %r12, %r11
	xchgq %r12, %rax
	xchgq %rax, %r12
	xchgq %r12, %rbp
	xchgq %rbp, %r12
	xchgq %r12, %r12
	xchgq %r12, %r12
	xchgq %r13, %rax
	xchgq %rax, %r13
	xchgq %r13, %rbp
	xchgq %rbp, %r13
	xchgq %r13, %r12
	xchgq %r12, %r13
	xchgq %r14, %rax
	xchgq %rax, %r14
	xchgq %r14, %rbp
	xchgq %rbp, %r14
	xchgq %r14, %r12
	xchgq %r12, %r14
	xchgq %r15, %rax
	xchgq %rax, %r15
	xchgq %r15, %rbp
	xchgq %rbp, %r15
	xchgq %r15, %r12
	xchgq %r12, %r15
	xchgq %rbx, (%rax)
	xchgq 8(%rsp), %r9
	xchgb %al, %cl
	xchgb %sil, %r9b
	pushq %rax
	popq %rax
	push %rax
	callq *%rax
	jmp *%rax
	pushq %rcx
	popq %rcx
	push %rcx
	callq *%rcx
	jmp *%rcx
	pushq %rdx
	popq %rdx
	push %rdx
	callq *%rdx
	jmp *%rdx
	pushq %rbx
	popq %rbx
	push %rbx
	callq *%rbx
	jmp *%rbx
	pushq %rsp
	popq %rsp
	push %rsp
	callq *%rsp
	jmp *%rsp
	pushq %rbp
	popq %rbp
	push %rbp
	callq *%rbp
	jmp *%rbp
	pushq %rsi
	popq %rsi
	push %rsi
	callq *%rsi
	jmp *%rsi
	pushq %rdi
	popq %rdi
	push %rdi
	callq *%rdi
	jmp *%rdi
	pushq %r8
	popq %r8
	push %r8
	callq *%r8
	jmp *%r8
	pushq %r9
	popq %r9
	push %r9
	callq *%r9
	jmp *%r9
	pushq %r10
	popq %r10
	push %r10
	callq *%r10
	jmp *%r10
	pushq %r11
	popq %r11
	push %r11
	callq *%r11
	jmp *%r11
	pushq %r12
	popq %r12
	push %r12
	callq *%r12
	jmp *%r12
	pushq %r13
	popq %r13
	push %r13
	callq *%r13
	jmp *%r13
	pushq %r14
	popq %r14
	push %r14
	callq *%r14
	jmp *%r14
	pushq %r15
	popq %r15
	push %r15
	callq *%r15
	jmp *%r15
	callq *8(%rax)
	callq *glob(%rip)
	ret
	leave
	ud2
	endbr64
	cltd
	cqto
	cltq
	nop
	callq ext
	callq ext@plt
	call glob
	callq here
here:
	callq here
	callq other
	callq gfun
.globl gfun
gfun:
	ret
.section .text.other,"ax",@progbits
other:
	callq here
	ret
.text
.data
.balign 8
.globl glob
glob:
	.quad 0
loc:
	.quad 0
.section .tbss,"awT",@nobits
.balign 8
tv:
	.fill 8,1,0
.section .note.GNU-stack,"",@progbits
