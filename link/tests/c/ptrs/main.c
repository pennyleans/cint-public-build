/* LK-1 ptrs: function pointer tables, a string table of pointers, a const table, a switch. */
#include <stdio.h>
static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }
static int mul(int a, int b) { return a * b; }
int (*ops[])(int, int) = {add, sub, mul};
const char *names[] = {"add", "sub", "mul"};
static const char *const fixed[] = {"one", "two", "three", "four"};
static int sw(int x) {
    switch (x) {
    case 0: return 11; case 1: return 22; case 2: return 33; case 3: return 44;
    case 4: return 55; case 5: return 66; case 6: return 77; default: return -1;
    }
}
int main(void) {
    for (int i = 0; i < 3; i++) printf("%s %d\n", names[i], ops[i](7, 3));
    for (int i = 0; i < 4; i++) printf("%s %d\n", fixed[i], sw(i * 2));
    return 0;
}
