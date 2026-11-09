int shared;
int counter = 100;
char block[200];
void bump(void) { counter++; shared *= 2; }
