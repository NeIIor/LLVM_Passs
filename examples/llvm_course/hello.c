/* From lisitsynSA/llvm_course LLVM_Pass/c_examples (Pass2–4 style demos). */
#include <stdio.h>

void func(int x) {
  if (x) {
    printf("Hello, world!\n");
  } else {
    printf("Hell, world!\n");
  }
}

int main(void) {
  func(1);
  return 0;
}
