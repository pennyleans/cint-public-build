/* LK-1 heap: malloc, realloc, free, a large .bss and a large initialized table. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char big[1 << 20];
static int table[4096] = {1, 2, 3};

int main(void) {
    unsigned long sum = 0;
    char *p = NULL;
    for (int i = 1; i <= 64; i++) {
        p = realloc(p, (size_t)i * 1000);
        memset(p, i, (size_t)i * 1000);
    }
    for (int i = 0; i < 64000; i++) sum += (unsigned char)p[i];
    free(p);
    for (size_t i = 0; i < sizeof big; i++) big[i] = (unsigned char)(i * 7);
    for (size_t i = 0; i < sizeof big; i++) sum += big[i];
    for (int i = 0; i < 4096; i++) sum += (unsigned long)table[i];
    printf("sum %lu\n", sum);
    return 0;
}
