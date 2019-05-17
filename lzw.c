#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// we want to build a mapping from keys to data-strings.
typedef struct lzw_node_tag {
  uint32_t key;
  struct lzw_node_tag* children[256];
} lzw_node_t, *lzw_node_p;
typedef struct {
  uint8_t* data;
  uint32_t len;
} lzw_data_t;
lzw_data_t* lzw_data = NULL;
lzw_node_p root = NULL;
lzw_node_p curr = NULL;
uint32_t lzw_length = 1;
uint32_t lzw_next_key = 0;


char getopt(int, char*[], const char*);

bool debugMode = false;
void debug_emit(uint32_t v, uint8_t l, FILE* o);


uint8_t buffer = 0;
uint8_t bufferSize = 0;
uint8_t MAX_BUFFER_SIZE = sizeof(uint8_t) * 8;
void emit(uint32_t v, uint8_t l, FILE* o) {
  if (debugMode) fprintf(stderr, "key %X of %u bits\n", v, l);
  for (uint8_t i = 0; i < l; ++i) {
    assert(bufferSize < 8);
    buffer <<= 1;
    uint32_t mask = 1 << (l - (i+1));
    uint32_t bit = v & mask;
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

int biggest_one(int v) {
  for (int i = 0; i < 64; ++i) {
    if (!v) {
      return i;
    }
    v >>= 1;
  }
  assert(0);
  return -1;
}



// this is a bit tricky: is it the next 
bool key_requires_bigger_length(uint32_t k) {
  return biggest_one(k) > lzw_length;
}
uint32_t get_next_key() {
  return lzw_next_key++;
}

void print_uint32_t_bin(uint32_t x) {
  for (int32_t i = 31; i >= 0; i--) {
    fprintf(stderr, "%s", (x & ((uint32_t)1 << i)) ? "1" : "0");
  }
}
void print_uint8_t_array(uint8_t* a, uint32_t l) {
  for (uint32_t i = 0; i < l; i++) {
    fprintf(stderr, "0x%X", a[i]);
  }
}
void debug_emit(uint32_t v, uint8_t l, FILE* o) {
  print_uint32_t_bin(v);
  fprintf(stderr, " %u ", l);
  print_uint8_t_array(lzw_data[v].data, lzw_data[v].len);
  fprintf(stderr, "\n");
}

bool lzw_next_char(uint8_t c) {
  if (debugMode) fprintf(stderr, "nextchar: 0x%X\n", c);
  if (curr->children[c] == NULL) {
    const uint32_t l = curr->key == -1 ? 0 : lzw_data[curr->key].len;
    const uint32_t k = get_next_key();
    curr->children[c] = calloc(1, sizeof(lzw_node_t));
    curr->children[c]->key = k;
    lzw_data[k].len = l+1;
    uint8_t** table_string = &(lzw_data[k].data);

    *table_string = calloc(l+1, sizeof(uint8_t));
    if (l) {
      uint8_t* currString = lzw_data[curr->key].data;
      memcpy(*table_string, currString, l*sizeof(uint8_t));
    }
    (*table_string)[l] = c;

    // we DON'T update curr here.
    return true;
  }
  else {
    if (debugMode) fprintf(stderr, "key %u -> key %u\n", curr->key, curr->children[c]->key);
    curr = curr->children[c];
    return false;
  }
}

void do_len_update() {
  if (debugMode) fprintf(stderr, "Updating length! %d %d -> %d\n", lzw_next_key, lzw_length, lzw_length+1);

  //if (lzw_length == 10) return; // use very little memory!
  lzw_length++;

  {
    lzw_data_t* new_data = calloc(1<<lzw_length, sizeof(lzw_data_t));
    memcpy(new_data, lzw_data, sizeof(lzw_data_t)*(1<<(lzw_length-1)));
    lzw_data_t* tmp = lzw_data;
    lzw_data = new_data;
    free(tmp);
  }

}

bool update_length() {
  if (biggest_one(lzw_next_key) > lzw_length) {
    do_len_update();
    return true;
  }
  assert(lzw_length >= biggest_one(lzw_next_key));
  return false;
}

void init_lzw() {
  root = (lzw_node_p) calloc(1, sizeof(lzw_node_t));
  curr = root;
  root->key = -1;
  lzw_data = calloc(1<<lzw_length, sizeof(lzw_data_t));
  for (uint16_t i = 0; i < 256; ++i) {
    lzw_next_char(i);
    update_length();
  }
  if (debugMode) fprintf(stderr, "lzw_length = %d\n", lzw_length);
  //assert(lzw_length == 9);
}

void encode() {
  init_lzw();

  int c = getchar();
  while (c != EOF) {
    bool newString = lzw_next_char(c);
    if (newString) {
      emit(curr->key, lzw_length, stdout);
      curr = root;
      ungetc(c, stdin);
      update_length();
    }
    c = getchar();
  }
  if (curr != root) {
    emit(curr->key, lzw_length, stdout);
    curr = root;
  }
  end(stdout);
}

uint32_t readBuffer = 0;
uint32_t readBufferLength = 0;
const uint32_t MAX_BUFFER_LEN = sizeof(uint32_t)*8;
bool readbits(uint32_t* v, uint8_t l, FILE* i) {
  while (readBufferLength < l) {
    int c = fgetc(i);
    if (c == EOF) {
      if (readBufferLength != 0) {
        readBuffer <<= (8 - readBufferLength);
        readBufferLength = 8;
      }
      return false;
    }
    else {
      readBuffer <<= 8;
      readBufferLength += 8;
      readBuffer |= c;
    }
  }
  uint32_t readBufferCopy = readBuffer;
  readBufferCopy >>= (readBufferLength - l);
  readBufferCopy &= (1 << l) - 1;
  *v = readBufferCopy;
  readBufferLength -= l;
  return true;
}
void pushbits(uint32_t oldkey, uint8_t oldlen) {
  assert(MAX_BUFFER_LEN - readBufferLength >= oldlen);
  oldkey <<= readBufferLength;
  readBuffer |= oldkey;
  readBufferLength += oldlen;
}

bool lzw_valid_key(uint32_t k) {
  assert(k < (1 << (lzw_length)));
  return lzw_data[k].data != NULL;
}

void decode() {
  init_lzw();

  bool nextBits = true;
  while (nextBits) {
    uint32_t currKey;
    nextBits = readbits(&currKey, lzw_length, stdin);
    if (debugMode) fprintf(stderr, "key %X of %u bits\n", currKey, lzw_length);
    assert(lzw_valid_key(currKey));
    uint8_t* currString = lzw_data[currKey].data;
    const uint32_t l = lzw_data[currKey].len;
    assert(l);
    for (int i = 0; i < l; ++i) {
      fputc(currString[i], stdout);
      bool b = lzw_next_char(currString[i]);
      assert(!b);
    }

    if (!nextBits) return;

    uint32_t nextKey;

    // we have to update the length in anticipation.
    if (key_requires_bigger_length(lzw_next_key+1)) {
      do_len_update();
    }

    nextBits = readbits(&nextKey, lzw_length, stdin);
    // put the newkey back.
    pushbits(nextKey, lzw_length);

    uint8_t* nextString = currString;
    if (lzw_valid_key(nextKey)) {
      nextString = lzw_data[nextKey].data;
    }

    uint8_t c = nextString[0];
    bool b = lzw_next_char(c);
    assert(b || !nextBits);
    bool uplen = update_length();
    assert(!uplen);
    curr = root;
  }
}

int main(int argc, char* argv[]) {
  bool doDecode = false;
  bool doEncode = false;
  char c;
  while ((c = getopt(argc, argv, "deg")) != -1) {
    switch (c) {
      case 'd': doDecode = true; break;
      case 'e': doEncode = true; break;
      //case 'g': debugMode = true; break;
      default: break;
    }
  }
  if (doEncode == doDecode) {
    printf("Error, must uniquely choose encode or decode\n");
    return 1;
  }
  if (doEncode) {
    encode();
  } else {
    decode();
  }
}
