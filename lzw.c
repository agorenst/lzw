#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char getopt(int, char*[], const char*);

bool debugMode = false;

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

void print_uint8_t_bin(uint8_t x) {
  for (uint8_t i = 8; i >= 0; i--) {
    fprintf(stderr, "%s", (x & (1 << i)) ? "1" : "0");
  }
}
void print_uint32_t_bin(uint32_t x) {
  for (int32_t i = 30; i >= 0; i--) {
    fprintf(stderr, "%s", (x & (1 << i)) ? "1" : "0");
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

void debug_key(int key, int len);
bool lzw_next_char(uint8_t c) {
  if (curr->children[c] == NULL) {
    const uint32_t l = curr->key == -1 ? 0 : lzw_key_to_len[curr->key];
    const uint32_t k = lzw_next_key++;
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

    // we DON'T update curr here.
    return true;
  }
  else {
    curr = curr->children[c];
    return false;
  }
}

void do_len_update() {
    fprintf(stderr, "Updating length! %d %d -> %d\n", lzw_next_key, lzw_length, lzw_length+1);
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
  fprintf(stderr, "lzw_length = %d\n", lzw_length);
  //assert(lzw_length == 9);
}

void encode() {
  init_lzw();

  int c = getchar();
  while (c != EOF) {
    bool newString = lzw_next_char(c);
    if (newString) {
      if (debugMode) fprintf(stderr, "capping char: 0x%X\n", (uint8_t) c);
      if (debugMode) debug_emit(curr->key, lzw_length, stdout);
      else emit(curr->key, lzw_length, stdout);
      curr = root;
      ungetc(c, stdin);
      update_length();
    }
    c = getchar();
  }
  if (curr != root) {
    if (debugMode) debug_emit(curr->key, lzw_length, stdout);
    else emit(curr->key, lzw_length, stdout);
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
    //fprintf(stderr, "Read in byte: %X\n", c);
    //?
    if (c == EOF) {
      if (readBufferLength != 0) {
        fprintf(stderr, "Error, nonempty read buffer on EOF: %X %d\n",
            readBuffer, readBufferLength);
        readBuffer <<= (8 - readBufferLength);
        readBufferLength = 8;
      }
      return false;
    }
    else {
      //fprintf(stderr, "readBuffer before: %X\n", readBuffer);
      readBuffer <<= 8;
      readBufferLength += 8;
      readBuffer |= c;
      //fprintf(stderr, "readBuffer after: %X\n", readBuffer);
    }
  }
  uint32_t readBufferCopy = readBuffer;
  // move the data we care about to the lower-order bits.
  readBufferCopy >>= (readBufferLength - l);
  //fprintf(stderr, "readBufferCopy after slide (%d): %X\n", l - readBufferLength, readBufferCopy);
  // erase the topmost bits
  readBufferCopy &= (1 << l) - 1;
  //readBufferCopy <<= MAX_BUFFER_LEN - l;
  //readBufferCopy >>= MAX_BUFFER_LEN - l;
  //fprintf(stderr, "readBufferCopy after erase: %X\n", readBufferCopy);
  // copy the result!
  *v = readBufferCopy;
  // we can leave the garbage in our buffer.
  readBufferLength -= l;
  return true;
}
void pushbits(uint32_t oldkey, uint8_t oldlen) {
  //fprintf(stdout, "\n%X %d %X %d\n", readBuffer, readBufferLength, oldkey, oldlen);
  assert(MAX_BUFFER_LEN - readBufferLength >= oldlen);
  // TODO assert that if we leftshift oldkey, we won't lose any bits (oldkey is small enough);
  oldkey <<= readBufferLength;
  readBuffer |= oldkey;
  readBufferLength += oldlen;
  //fprintf(stdout, "\n%X %d\n", readBuffer, readBufferLength);
}

bool lzw_valid_key(uint32_t k) {
  assert(k < (1 << (lzw_length)));
  return lzw_key_to_str[k] != NULL;
}

void debug_key(int key, int len) {
  debug_emit(key, len, stderr);
}

void decode() {
  init_lzw();

  for (;;) {
    uint32_t currKey;
    readbits(&currKey, lzw_length, stdin);
    uint8_t* currString = lzw_key_to_str[currKey];
    const uint32_t l = lzw_key_to_len[currKey];
    for (int i = 0; i < l; ++i) {
      if (!debugMode) fputc(currString[i], stdout);
      bool b = lzw_next_char(currString[i]);
      assert(!b);
    }

    uint32_t nextKey;

    // we have to update the length in anticipation.
    if (biggest_one(lzw_next_key+1) > lzw_length) {
      do_len_update();
    }

    bool nextBits = readbits(&nextKey, lzw_length, stdin);

    uint8_t* nextString = currString;
    if (lzw_valid_key(nextKey)) {
      nextString = lzw_key_to_str[nextKey];
    }

    uint8_t c = nextString[0];
    lzw_next_char(c);
    if (debugMode) fprintf(stderr, "capping char: 0x%X\n", c);
    if (debugMode) debug_key(currKey, lzw_length);
    // put the newkey back.
    pushbits(nextKey, lzw_length);
    bool uplen = update_length();
    assert(!uplen);
    curr = root;

    if (!nextBits) {
      break;
    }
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
      case 'g': debugMode = true; break;
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
