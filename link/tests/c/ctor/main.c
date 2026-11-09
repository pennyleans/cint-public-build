/* LK-1 ctor: constructors by priority, destructors, atexit, in their order. */
#include <stdio.h>
#include <stdlib.h>

__attribute__((constructor(200))) static void c200(void) { puts("ctor 200"); }
__attribute__((constructor(101))) static void c101(void) { puts("ctor 101"); }
__attribute__((constructor)) static void cplain(void) { puts("ctor plain"); }
__attribute__((destructor)) static void dplain(void) { puts("dtor plain"); }
__attribute__((destructor(150))) static void d150(void) { puts("dtor 150"); }
static void bye1(void) { puts("atexit 1"); }
static void bye2(void) { puts("atexit 2"); }

int main(void) {
    atexit(bye1);
    atexit(bye2);
    puts("main");
    return 3;
}
