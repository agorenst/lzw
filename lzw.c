#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lzw.h"

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
int lzw_debug_level = 0;
#define DEBUG(l, ...) { if (lzw_debug_level >= l) fprintf(stderr, __VA_ARGS__); }

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



// We encode the output as a sequence of bits, which can cause
// complications if we need to say, emit, 13 bits.
// We store things 8 bits at a time, to match fputc.
void lzw_default_emitter(uint8_t b) {
  fputc(b, stdout);
}
lzw_emitter_t lzw_emitter = lzw_default_emitter;

uint32_t lzw_default_reader(void) {
  return getchar();
}
lzw_reader_t lzw_reader = lzw_default_reader;

uint8_t buffer = 0;
uint8_t bufferSize = 0;
uint8_t MAX_BUFFER_SIZE = sizeof(uint8_t) * 8;

char *as_binary(uint32_t x, uint32_t offset) {
  static char s[33];
  for (int i = 1; i < 33; i++) {
    if (x&1) {
      s[32 - i] = '1';
    } else {
      s[32 - i] = '0';
    }
    x >>= 1;
  }
  s[32] = '\0';
  return s+(32-offset);
}

// Reading v from "left to right", we
// emit the l bits of v.
void emit(uint32_t v, uint8_t l) {
  //fprintf(stderr, "emit: %s, %u\n", as_binary(v, l), l);
  //fprintf(stderr, "buffer: %s (%d)\n", as_binary(buffer, bufferSize), bufferSize);
  for (;l;) {
    // Do we have enough to get an emittable value?
    size_t max_bits_to_enbuffer = MAX_BUFFER_SIZE - bufferSize;
    assert(max_bits_to_enbuffer);

    // bits_to_write = min(l, max_bits_to_enbuffer);
    size_t bits_to_write = l;
    if (bits_to_write > max_bits_to_enbuffer) {
      bits_to_write = max_bits_to_enbuffer;
    }

    // Select the topmost of those bits:
    size_t w = v >> (l - bits_to_write);

    //assert(bits_to_write);
    uint32_t mask = (1 << bits_to_write) - 1;
    //assert(__builtin_popcount(mask) == bits_to_write);
    buffer = (buffer << bits_to_write) | (w & mask);
    bufferSize += bits_to_write;
    //assert(bufferSize <= 8);
    if (bufferSize == 8) {
      //fprintf(stderr, "emitting: %s\n", as_binary(buffer, bufferSize));
      lzw_emitter(buffer);
      buffer = 0;
      bufferSize = 0;
    }

    assert(bits_to_write <= l);
    l -= bits_to_write;
  }
  //fprintf(stderr, "buffer: %s (%d)\n", as_binary(buffer, bufferSize), bufferSize);
}

void emit_old(uint32_t v, uint8_t l) {
  // DEBUG(2, "key %X of %u bits\n", v, l);
  fprintf(stderr, "emit: %s, %u\n", as_binary(v, l), l);
  fprintf(stderr, "buffer: %s (%d)\n", as_binary(buffer, bufferSize), bufferSize);
  for (uint8_t i = 0; i < l; ++i) {
    assert(bufferSize < 8);
    buffer <<= 1;
    uint32_t mask = 1 << (l - (i+1));
    uint32_t bit = v & mask;
    bit >>= (l-i)-1;
    buffer |= bit;
    bufferSize++;

    if (bufferSize == 8) {
      fprintf(stderr, "emitting: %s\n", as_binary(buffer, bufferSize));
      lzw_emitter(buffer);
      buffer = 0;
      bufferSize = 0;
    }
  }
  fprintf(stderr, "buffer: %s (%d)\n", as_binary(buffer, bufferSize), bufferSize);
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
  DEBUG(2, "lzw_length = %d\n", lzw_length);
  //assert(lzw_length == 9);
}

size_t lzw_encode(size_t l) {
  size_t i = 0;
  for (; i < l; i++) {
    int c = lzw_reader();
    if (c == EOF) break;
    bool new_string = lzw_next_char(c);
    if (new_string) {
      // Flush the current string
      emit(curr->key, lzw_length);
      curr = root;
      update_length();
      // Now actually start with c
      lzw_next_char(c);
    }
  }
  return i;
}

void lzw_encode_end_stream(void) {
  if (curr != root) {
    emit(curr->key, lzw_length);
    curr = root;
  }
  // we can do this as a single constant, but hold on.
  while (bufferSize != 0) {
    emit(0, MAX_BUFFER_SIZE-bufferSize);
  }
}

uint64_t readBuffer = 0;
uint32_t readBufferLength = 0;
const uint32_t MAX_BUFFER_LEN = sizeof(uint32_t)*8;

// This will read the next bits up to our buffer.
bool readbits(uint32_t *v) {
  while (readBufferLength < lzw_length) {
    uint32_t c = lzw_reader();
    if (c != EOF) {
      readBuffer <<= 8;
      readBuffer |= (uint8_t)c;
      readBufferLength += 8;
    } else {
      if (readBufferLength != 0) {
        // Read in the trailing zeros.
        readBuffer <<= (lzw_length - readBufferLength);
        readBufferLength = lzw_length;
        *v = readBuffer;
        *v &= (1 << readBufferLength) - 1;
        readBufferLength = 0;
        return *v != 0;
      }
      return false;
    }
  }
  uint32_t readBufferCopy = readBuffer;
  readBufferCopy >>= (readBufferLength - lzw_length);
  readBufferCopy &= (1 << lzw_length) - 1;
  *v = readBufferCopy;
  readBufferLength -= lzw_length;
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

uint64_t lzw_bytes_emitted = 0;

void lzw_decode(void) {
  uint32_t curr_key;
  while (readbits(&curr_key)) {
    DEBUG(2, "key %X of %u bits\n", curr_key, lzw_length);
    assert(lzw_valid_key(curr_key));
    // emit that string:
    uint8_t* s = lzw_data[curr_key].data;
    uint32_t l = lzw_data[curr_key].len;
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
    bool valid = readbits(&curr_key);
    if (!valid) break;
    pushbits(curr_key, lzw_length);

    // Find the next character.
    // If the next key is valid, that means
    // the next key is already something we've seen,
    // otherwise it's going to be the key for the new
    // string. Either way, we take the next step
    // on that character, but then manually reset
    // our curr node back to the root in prep for
    // really reading the next string.
    if (lzw_valid_key(curr_key)) {
      s = lzw_data[curr_key].data;
    } else {
      assert(lzw_data[curr_key-1].data != NULL);
    }
    bool b = lzw_next_char(s[0]);
    assert(b);
    curr = root;
  }
}

uint8_t* lzw_emit_buffer = NULL;
size_t lzw_emit_buffer_end = 0;
size_t emit_buffer_max = 0;
float emit_buffer_growth = 0.0;
void lzw_emit_buffer_init(size_t initial, float factor) {
  assert(factor > 1);
  assert(initial > 1);
  lzw_emit_buffer_end = initial;
  emit_buffer_growth = factor;
  lzw_emit_buffer = realloc(lzw_emit_buffer, emit_buffer_max);
}
void lzw_buffer_emitter(uint8_t b) {
  if (emit_buffer_max <= lzw_emit_buffer_end) {
    emit_buffer_max *= emit_buffer_growth;
    lzw_emit_buffer = realloc(lzw_emit_buffer, emit_buffer_max);
  }
  lzw_emit_buffer[lzw_emit_buffer_end++] = b;
}

uint8_t* lzw_read_buffer = NULL;
size_t lzw_read_buffer_end = 0;
size_t lzw_read_buffer_index = 0;
uint32_t lzw_buffer_reader(void) {
  if (lzw_read_buffer_index == lzw_read_buffer_end) return EOF;
  uint32_t r = lzw_read_buffer[lzw_read_buffer_index++];
  return r;
}
void lzw_initialize_reader_buffer(char *buff, size_t len) {
  lzw_read_buffer = (uint8_t*) buff;
  lzw_read_buffer_end = len;
  lzw_read_buffer_index = 0;
}
