#include <stdio.h>
#include <stdint.h>

/* Nested conditionals + arithmetic (several basic blocks). */
static int clamp_add(int a, int b, int limit) {
  long s = (long)a + (long)b;
  if (s > limit)
    return limit;
  if (s < -limit)
    return -limit;
  return (int)s;
}

/* switch lowers to switch IR — extra CFG successor edges. */
static int classify(int x) {
  switch (x & 3) {
  case 0:
    return x * 2;
  case 1:
    return x + 7;
  case 2:
    return clamp_add(x, 11, 90);
  default:
    return -x;
  }
}

/* Loop + chained calls — hot blocks vs cold, multiple call sites. */
static int32_t pipeline(int32_t seed) {
  int32_t acc = seed;
  for (int i = 0; i < 6; ++i) {
    acc = classify(acc ^ (i * 3));
    acc = clamp_add(acc, i - 2, 40);
  }
  return acc;
}

static void print_pair(int a, int b) {
  printf("a=%d b=%d -> cls=%d\n", a, b, classify(a ^ b));
}

int main(void) {
  int32_t r = pipeline(5);
  print_pair(r, r >> 1);
  printf("final=%d\n", clamp_add(r, classify(42), 200));
  return 0;
}
