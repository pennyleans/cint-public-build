/* LK-1 pie: position-independent objects, so extern data goes through GOTPCRELX loads. */
#include <stdio.h>
#include <errno.h>
extern int value;
extern int *where(void);
int main(void) {
    errno = 0;
    printf("value %d at same %d stdout %d\n", value, where() == &value, stdout != NULL);
    return 0;
}
