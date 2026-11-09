/* LK-1 sections: every function and object in its own section. */
#include <stdio.h>
int a = 1, b = 2;
static const char msg[] = "sections";
int f(int x) { return x * a; }
int g(int x) { return f(x) + b; }
int main(void) { printf("%s %d %d\n", msg, f(3), g(4)); return 0; }
