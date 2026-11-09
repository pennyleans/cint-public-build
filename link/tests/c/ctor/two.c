#include <stdio.h>
__attribute__((constructor)) static void other(void) { puts("ctor two.c"); }
__attribute__((constructor(150))) static void other150(void) { puts("ctor 150 two.c"); }
