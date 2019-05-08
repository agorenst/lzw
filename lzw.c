#include <stdint.h>
#include <stdio.h>
#include <assert.h>

uint8_t buffer = 0;
uint8_t bufferSize = 0;
void emit(uint32_t v, uint8_t l, FILE* o) {
  for (uint8_t i = 0; i < l; ++i) {
    assert(bufferSize < 8);
    buffer <<= 1;
    uint32_t mask = 1 << (l - (i+1));
    uint8_t bit = v & mask;
    bit >>= (l-i)-1;
    buffer |= bit;
    bufferSize++;

    if (bufferSize == 8) {
      //fprintf(stderr, "Emitting bytes: %X\n", buffer);
      fputc(buffer, o);
      buffer = 0;
      bufferSize = 0;
    }
  }
}
void end(FILE* o) {
  // we can do this as a single constant, but hold on.
  while (bufferSize != 0) {
    emit(0, 1, o);
  }
}

// This was a test to make sure we were doing the bit
// arithmetic in "emit" correctly.
void exercise_emit() {
  // sanity check:
  int c = getchar();
  while (c != EOF) {
    //fprintf(stderr, "byte in: %X\n", c);
    emit(c, 8, stdout);
    c = getchar();
  }
  end(stdout);
}
int main(int argc, char* argv[]) {
}
