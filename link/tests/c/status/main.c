/* LK-1 status: exit through exit() with buffered output pending. */
#include <stdio.h>
#include <stdlib.h>
int main(void) { printf("pending"); exit(42); }
