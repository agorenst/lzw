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
uint32_t lzw_next_key = 1;
uint32_t lzw_max_key = 0;

// Before we get too much into executable code,
// we want to express the different modes we can run in.
// I.e., debug levels
int debugLevel = 0;
#define DEBUG(l, ...) { if (debugLevel >= l) fprintf(stderr, __VA_ARGS__); }

// The primary action of this table is to ingest
// the next byte, and maintain the correct encoding
// information for the implicit string seen-so-far.
// That's capturedin this function:
bool lzw_next_char(uint8_t c) {
  DEBUG(3, "nextchar: 0x%X\n", c);
  if (curr->children[c]) {
    DEBUG(2, "key %u -> key %u\n", curr->key, curr->children[c]->key);
    curr = curr->children[c];
    return false;
  }
  // we have reached the end of the string.
  if (lzw_max_key && lzw_next_key >= lzw_max_key) {
    return true;
  }
  // Create the new fields for the new node
  const uint32_t k = lzw_next_key++;
  const uint32_t l = curr->key == -1 ? 0 : lzw_data[curr->key].len;
  uint8_t* data = calloc(l+1, sizeof(uint8_t));
  if (l) {
    memcpy(data, lzw_data[curr->key].data, l*sizeof(uint8_t));
  }
  data[l] = c;
  // allocate the new node
  curr->children[c] = calloc(1, sizeof(lzw_node_t));
  curr->children[c]->key = k;
  lzw_data[k].data = data;
  lzw_data[k].len = l+1;
  return true;
}

// We also have book-keeping of when we have to
// update the length
void lzw_len_update() {
  DEBUG(1, "Updating length: %d %d -> %d\n", lzw_next_key, lzw_length, lzw_length+1);
  lzw_length++;
  lzw_data_t* new_data = calloc(1<<lzw_length, sizeof(lzw_data_t));
  memcpy(new_data, lzw_data, sizeof(lzw_data_t)*(1<<(lzw_length-1)));
  lzw_data_t* tmp = lzw_data;
  lzw_data = new_data;
  free(tmp);
}

// We also want to destroy our tree, ultimately
void lzw_destroy_tree(lzw_node_p t) {
  for (uint16_t i = 0; i < 256; ++i) {
    if (t->children[i]) {
      lzw_destroy_tree(t->children[i]);
    }
  }
  free(t);
}

void lzw_destroy_state(void) {
  lzw_destroy_tree(root);
  curr = NULL;
  root = NULL;
  for (uint32_t i = 0; i < lzw_next_key; ++i) {
    if (lzw_data[i].data) free(lzw_data[i].data);
  }
  free(lzw_data);
  lzw_next_key = 1;
  lzw_length = 1;
}


bool debugMode = false;
bool verboseMode = false;
bool doStats = false;
void debug_emit(uint32_t v, uint8_t l);

// We encode the output as a sequence of bits, which can cause
// complications if we need to say, emit, 13 bits.
// We store things 8 bits at a time, to match fputc.
typedef void (*lzw_emitter_t)(uint8_t);
void lzw_default_emitter(uint8_t b) {
  fputc(b, stdout);
}
lzw_emitter_t lzw_emitter = lzw_default_emitter;

typedef uint32_t (*lzw_reader_t)(void);
uint32_t lzw_default_reader(void) {
  return getchar();
}
lzw_reader_t lzw_reader = lzw_default_reader;

uint8_t buffer = 0;
uint8_t bufferSize = 0;
uint8_t MAX_BUFFER_SIZE = sizeof(uint8_t) * 8;
void emit(uint32_t v, uint8_t l) {
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
      lzw_emitter(buffer);
      buffer = 0;
      bufferSize = 0;
    }
  }
}

void debug_emit(uint32_t v, uint8_t l);
void end(void) {
  // we can do this as a single constant, but hold on.
  while (bufferSize != 0) {
    if (debugMode) debug_emit(0, 1);
    emit(0, 1);
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
void debug_emit(uint32_t v, uint8_t l) {
  print_uint32_t_bin(v);
  fprintf(stderr, " %u ", l);
  print_uint8_t_array(lzw_data[v].data, lzw_data[v].len);
  fprintf(stderr, "\n");
}


bool update_length() {
  if (biggest_one(lzw_next_key) > lzw_length) {
    lzw_len_update();
    return true;
  }
  assert(lzw_length >= biggest_one(lzw_next_key));
  return false;
}

void lzw_init() {
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

void lzw_encode() {
  int c = lzw_reader();
  while (c != EOF) {
    bool newString = lzw_next_char(c);
    if (newString) {
      emit(curr->key, lzw_length);
      curr = root;
      update_length();
      // note that we don't update C here.
    }
    else {
      c = lzw_reader();
    }
  }
  if (curr != root) {
    emit(curr->key, lzw_length);
    curr = root;
  }
  end();
}

uint64_t readBuffer = 0;
uint32_t readBufferLength = 0;
const uint32_t MAX_BUFFER_LEN = sizeof(uint32_t)*8;

bool readbits(uint32_t* v, uint8_t l) {
  while (readBufferLength < l) {
    uint32_t c = lzw_reader();
    if (c == EOF) {
      if (readBufferLength != 0) {
        while (readBufferLength < l) {
          readBuffer <<= 1;
          readBufferLength++;
        }
        *v = readBuffer;
        *v &= (1 << readBufferLength) - 1;
        readBufferLength = 0;
        //fprintf(stderr, "Special EOF case: %X\n", *v);
        // this may be a bug... what if the final symbol to be encoded is
        // value 0?
        return *v != 0;
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
  if (k > (1 << (lzw_length))) {
    fprintf(stderr, "Error, invalid key: %X\n", k);
  }
  assert(k < (1 << (lzw_length)));
  return lzw_data[k].data != NULL;
}

void lzw_decode() {
  uint32_t currKey;
  while (readbits(&currKey, lzw_length)) {
    if (debugMode) fprintf(stderr, "key %X of %u bits\n", currKey, lzw_length);
    assert(lzw_valid_key(currKey));
    // emit that string:
    uint8_t* s = lzw_data[currKey].data;
    uint32_t l = lzw_data[currKey].len;
    assert(l);
    for (uint32_t i = 0; i < l; ++i) {
      lzw_emitter(s[i]);
      bool b = lzw_next_char(s[i]);
      assert(!b);
    }

    if (key_requires_bigger_length(lzw_next_key+1)) {
      lzw_len_update();
    }

    // peek at the next string:
    bool valid = readbits(&currKey, lzw_length);
    if (!valid) break;
    pushbits(currKey, lzw_length);

    if (lzw_valid_key(currKey)) {
      s = lzw_data[currKey].data;
    }

    bool b = lzw_next_char(s[0]);
    assert(b);
    curr = root;
  }
}
