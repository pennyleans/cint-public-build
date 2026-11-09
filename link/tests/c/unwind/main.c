/* LK-1 unwind: objects with .eh_frame, which the linker drops. */
#include <stdio.h>
static int depth(int n) { return n == 0 ? 0 : 1 + depth(n - 1); }
int main(void) { printf("depth %d\n", depth(100)); return 0; }
