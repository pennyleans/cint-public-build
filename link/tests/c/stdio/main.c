/* LK-1 stdio: formatted output, both streams, conversions, sorting, the arguments. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

int main(int argc, char **argv) {
    int v[8] = {5, 3, 9, 1, 7, 2, 8, 6};
    char buf[64];
    qsort(v, 8, sizeof v[0], cmp);
    for (int i = 0; i < 8; i++) printf("%d ", v[i]);
    printf("\nargc %d first %s\n", argc, argc > 1 ? argv[1] : "-");
    snprintf(buf, sizeof buf, "%08x|%-6s|%+d|%lld", 0xbeefu, "ab", 42, -1234567890123LL);
    puts(buf);
    printf("strtol %ld len %zu\n", strtol("-0x7f", NULL, 16), strlen(buf));
    fprintf(stderr, "to stderr\n");
    return 0;
}
