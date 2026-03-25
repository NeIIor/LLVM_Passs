#include <stdio.h>

static int sign(int x) {
  if (x < 0)
    return -1;
  if (x > 0)
    return 1;
  return 0;
}

int main(void) {
  int a = 7;
  int b = sign(a - 10);
  printf("a=%d b=%d sum=%d\n", a, b, a + b);
  return 0;
}
