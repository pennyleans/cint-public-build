/* LK-1 weak: a weak definition lost to a strong one, a weak one that stands, an undefined weak name. */
#include <stdio.h>
__attribute__((weak)) int pick(void) { return 1; }
__attribute__((weak)) int keep(void) { return 10; }
extern int missing(void) __attribute__((weak));
extern int missing_data __attribute__((weak));
int main(void) {
    printf("pick %d keep %d missing %s data %s\n", pick(), keep(), missing ? "yes" : "no",
           &missing_data ? "yes" : "no");
    return 0;
}
