/* LK-1 common: tentative definitions merged across units, one of them initialized elsewhere. */
#include <stdio.h>
int shared;
int counter;
char block[300];
void bump(void);
int main(void) {
    shared = 5;
    bump();
    bump();
    printf("shared %d counter %d block %d\n", shared, counter, block[299]);
    return 0;
}
