#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char getopt(int, char*[], const char*);
char* optarg;

bool debugMode = false;

uint32_t MAX_KEY = 0;

uint8_t buffer = 0;
uint8_t bufferSize = 0;
uint8_t MAX_BUFFER_SIZE = sizeof(uint8_t) * 8;
void emit(uint32_t v, uint8_t l, FILE* o) {
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

void debug_emit(uint32_t v, uint8_t l, FILE* o);
void end(FILE* o) {
  // we can do this as a single constant, but hold on.
  while (bufferSize != 0) {
    if (debugMode) debug_emit(0, 1, stderr);
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


typedef struct lzw_node_tag {
  uint32_t key;
  struct lzw_node_tag* children[256];
} lzw_node_t;
typedef lzw_node_t* lzw_node_p;

uint32_t lzw_length = 1;
uint32_t lzw_next_key = 0;
lzw_node_p root = NULL;
lzw_node_p curr = NULL;
uint8_t** lzw_key_to_str = NULL;
uint32_t* lzw_key_to_len = NULL;

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
  fprintf(stderr, " ");
  print_uint8_t_array(lzw_key_to_str[v], lzw_key_to_len[v]);
  fprintf(stderr, "\n");
}

bool table_has_space() {
  return biggest_one(lzw_next_key+1) < lzw_length;
}

bool lzw_next_char(uint8_t c) {
  if (curr->children[c] == NULL) {
    if (!MAX_KEY || lzw_next_key < MAX_KEY) {
      const uint32_t l = curr->key == -1 ? 0 : lzw_key_to_len[curr->key];
      const uint32_t k = get_next_key();
      curr->children[c] = calloc(1, sizeof(lzw_node_t));
      curr->children[c]->key = k;
      lzw_key_to_len[k] = l+1;
      uint8_t** table_string = &lzw_key_to_str[k];

      *table_string = calloc(l+1, sizeof(uint8_t));
      if (l) {
        uint8_t* currString = lzw_key_to_str[curr->key];
        memcpy(*table_string, currString, l*sizeof(uint8_t));
      }
      (*table_string)[l] = c;
    }

    // we DON'T update curr here.
    return true;
  }
  else {
    curr = curr->children[c];
    return false;
  }
}

void do_len_update() {
  if (debugMode) fprintf(stderr, "Updating length! %d %d -> %d\n", lzw_next_key, lzw_length, lzw_length+1);

  lzw_length++;

  {
    uint8_t** new_buff = calloc(1<<lzw_length, sizeof(uint8_t*));
    memcpy(new_buff, lzw_key_to_str, sizeof(uint8_t*)*(1<<(lzw_length-1)));
    uint8_t** t = lzw_key_to_str;
    lzw_key_to_str = new_buff;
    free(t);
  }

  {
    uint32_t* new_buff = calloc(1<<lzw_length, sizeof(uint32_t));
    memcpy(new_buff, lzw_key_to_len, sizeof(uint32_t)*(1<<(lzw_length-1)));
    uint32_t* t = lzw_key_to_len;
    lzw_key_to_len = new_buff;
    free(t);
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
  lzw_key_to_str = calloc(1<<lzw_length, sizeof(uint8_t*));
  lzw_key_to_len = calloc(1<<lzw_length, sizeof(uint32_t));
  for (uint8_t i = 0; i < (uint8_t)(i+1); ++i) {
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
      //if (debugMode) fprintf(stderr, "capping char: 0x%X\n", (uint8_t) c);
      if (debugMode) fprintf(stderr, "Key: %u\n", curr->key);

      if (debugMode) debug_emit(curr->key, lzw_length, stderr);
      emit(curr->key, lzw_length, stdout);

      curr = root;
      ungetc(c, stdin);
      update_length();
    }
    c = getchar();
  }
  if (curr != root) {
    if (debugMode) debug_emit(curr->key, lzw_length, stderr);

    emit(curr->key, lzw_length, stdout);
    curr = root;
  }
  end(stdout);
}

uint64_t readBuffer = 0;
uint32_t readBufferLength = 0;
const uint32_t MAX_BUFFER_LEN = sizeof(uint64_t)*8;
bool readbits(uint32_t* v, uint8_t l, FILE* i) {
  while (readBufferLength < l) {
    int c = fgetc(i);
    if (c == EOF) {
      if (readBufferLength != 0) {
        readBuffer <<= (l - readBufferLength);
        readBufferLength = l;
      }
      return false;
    }
    else {
      readBuffer <<= 8;
      readBufferLength += 8;
      readBuffer |= (uint8_t) c;
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
  if (!(MAX_BUFFER_LEN - readBufferLength >= oldlen)) {
    fprintf(stderr, "MAX_BUFFER_LEN = %X, readBufferLength = %X, oldLen = %X\n", MAX_BUFFER_LEN, readBufferLength, oldlen);
  }
  assert(MAX_BUFFER_LEN - readBufferLength >= oldlen);
  //oldkey <<= readBufferLength;
  //readBuffer |= oldkey;
  readBufferLength += oldlen;
}

bool lzw_valid_key(uint32_t k) {
  assert(k < (1 << (lzw_length)));
  return lzw_key_to_str[k] != NULL;
}

void decode() {
  init_lzw();

  uint32_t currKey;
  bool nextBits = readbits(&currKey, lzw_length, stdin);
  while (nextBits) {
    assert(lzw_valid_key(currKey));
    uint8_t* currString = lzw_key_to_str[currKey];
    const uint32_t l = lzw_key_to_len[currKey];
    for (int i = 0; i < l; ++i) {
      if (debugMode) fprintf(stderr, "Key: %u\n", currKey);
      fputc(currString[i], stdout);
      bool b = lzw_next_char(currString[i]);
      assert(!b);
    }

    // we have to update the length in anticipation.
    bool x = false;
    if (key_requires_bigger_length(lzw_next_key+1)) {
      do_len_update();
      x = true;
    }

    nextBits = readbits(&currKey, lzw_length, stdin);
    pushbits(currKey, x ? lzw_length - 1 : lzw_length);

    // what was the character that ended our "old" currString?
    // if we're a valid key, it's the beginning of this new string,
    // otherwise we're a repeat of our old value.
    //bool newString = false;
    if (lzw_valid_key(currKey)) {
      currString = lzw_key_to_str[currKey];
    }

    uint8_t c = currString[0];
    bool b = lzw_next_char(c);
    assert(b);
    if (debugMode) fprintf(stderr, "capping char: 0x%X\n", c);
    if (debugMode) debug_emit(currKey, lzw_length, stderr);

    assert(!update_length());
    curr = root;
  }
}

int main(int argc, char* argv[]) {
  bool doDecode = false;
  bool doEncode = false;
  char c;
  while ((c = getopt(argc, argv, "degm:")) != -1) {
    switch (c) {
      case 'd': doDecode = true; break;
      case 'e': doEncode = true; break;
      case 'g': debugMode = true; break;
      case 'm': MAX_KEY = atoi(optarg); break;
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
