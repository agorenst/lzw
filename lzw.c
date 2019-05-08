#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

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

typedef struct lzw_node_tag {
  int key;
  struct lzw_node_tag* children[256];
} lzw_node_t;
typedef lzw_node_t* lzw_node_p;

int lzw_length = 1;
int lzw_next_key = 0;
lzw_node_p root = NULL;
lzw_node_p curr = NULL;

int biggest_one(int v) {
  //int o_v = v;
  for (int i = 0; i < 64; ++i) {
    if (!v) {
      //fprintf(stderr, "biggest one of %d is %d\n", o_v, i);
      return i;
    }
    v >>= 1;
  }
  assert(0);
  return -1;
}

bool lzw_next_char(uint8_t c) {
  if (curr->children[c] == NULL) {
    curr->children[c] = calloc(1, sizeof(lzw_node_t));
    curr->children[c]->key = lzw_next_key++;
    // we DON'T update curr here.
    return true;
  }
  else {
    curr = curr->children[c];
    return false;
  }
}

bool update_length() {
  if (biggest_one(lzw_next_key) > lzw_length) {
    lzw_length++;
    return true;
  }
  assert(lzw_length >= biggest_one(lzw_next_key));
  return false;
}

void encode() {
  root = (lzw_node_p) calloc(1, sizeof(lzw_node_t));
  curr = root;
  root->key = -1;
  for (int i = 0; i < 256; ++i) {
    lzw_next_char((char)i);
    update_length();
  }
  //fprintf(stderr, "lzw_length = %d\n", lzw_length);
  assert(lzw_length == 9);

  int c = getchar();
  while (c != EOF) {
    bool newString = lzw_next_char(c);
    if (newString) {
      emit(curr->key, lzw_length, stdout);
      curr = root;
    }
    update_length();
    c = getchar();
  }
  if (curr != root) {
    emit(curr->key, lzw_length, stdout);
    curr = root;
  }
  end(stdout);
}

int main(int argc, char* argv[]) {
  //encode();
}
