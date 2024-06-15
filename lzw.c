#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lzw.h"

// we want to build a mapping from keys to data-strings.
typedef struct lzw_node_tag {
  uint32_t key;
  struct lzw_node_tag *children[256];
} lzw_node_t, *lzw_node_p;
typedef struct {
  uint8_t *data;
  uint32_t len;
} lzw_data_t;

lzw_data_t *lzw_data = NULL;
lzw_node_p root = NULL;
lzw_node_p curr = NULL;

uint32_t lzw_length = 0;
uint32_t lzw_next_key = 0;
uint32_t lzw_max_key = 0;

FILE *lzw_input_file;
FILE *lzw_output_file;

uint64_t lzw_bytes_written = 0;
uint64_t lzw_bytes_read = 0;

const uint32_t BITREAD_BUFFER_MAX_SIZE = sizeof(uint32_t) * 8;
uint8_t BITWRITE_BUFFER_MAX_SIZE = sizeof(uint8_t) * 8;
uint64_t bitread_buffer = 0;
uint32_t bitread_buffer_size = 0;
uint8_t bitwrite_buffer = 0;
uint8_t bitwrite_buffer_size = 0;

// Before we get too much into executable code,
// we want to express the different modes we can run in.
// I.e., debug levels
int lzw_debug_level = 0;
/*
#define DEBUG(l, ...)                                                          \
  {                                                                            \
    if (lzw_debug_level >= l)                                                  \
      fprintf(stderr, __VA_ARGS__);                                            \
  }
*/
#define DEBUG(l, ...)

//#define ASSERT(x) assert(x)
 #define ASSERT(x)

// #define DEBUG_STMT(x) x
 #define DEBUG_STMT(x)

// The primary action of this table is to ingest
// the next byte, and maintain the correct encoding
// information for the implicit string seen-so-far.
// That's captured in this function:
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
  uint8_t *data = calloc(l + 1, sizeof(uint8_t));
  if (l) {
    memcpy(data, lzw_data[curr->key].data, l * sizeof(uint8_t));
  }
  data[l] = c;
  // allocate the new node
  curr->children[c] = calloc(1, sizeof(lzw_node_t));
  curr->children[c]->key = k;
  lzw_data[k].data = data;
  lzw_data[k].len = l + 1;
  return true;
}

// We also have book-keeping of when we have to
// update the length
void lzw_len_update() {
  DEBUG(1, "Updating length: %d %d -> %d\n", lzw_next_key, lzw_length,
        lzw_length + 1);
  lzw_length++;
  lzw_data_t *new_data = calloc(1 << lzw_length, sizeof(lzw_data_t));
  memcpy(new_data, lzw_data, sizeof(lzw_data_t) * (1 << (lzw_length - 1)));
  lzw_data_t *tmp = lzw_data;
  lzw_data = new_data;
  free(tmp);
}

// We also want to destroy our tree, ultimately
void lzw_destroy_tree(lzw_node_p t) {
  if (!t) return ;
  for (uint16_t i = 0; i < 256; ++i) {
    lzw_destroy_tree(t->children[i]);
  }
  free(t);
}

int32_t last_read = EOF;
void lzw_destroy_state(void) {
  lzw_destroy_tree(root);
  curr = NULL;
  root = NULL;
  for (uint32_t i = 0; i < lzw_next_key; ++i) {
    if (lzw_data[i].data)
      free(lzw_data[i].data);
  }
  if (lzw_data) {
    free(lzw_data);
    lzw_data = NULL;
  }
  lzw_next_key = 0;
  if (last_read != EOF) {
    ungetc(last_read, lzw_input_file);
    lzw_bytes_read--;
  }
  last_read = EOF;
  //if (last_read != EOF) ungetc(last_read, lzw_input_file);
}

// Reading v from "left to right", we
// emit the l bits of v.
void emit(uint32_t v, uint8_t l) {
  for (; l;) {
    // Do we have enough to get an emittable value?
    size_t max_bits_to_enbuffer =
        BITWRITE_BUFFER_MAX_SIZE - bitwrite_buffer_size;
    ASSERT(max_bits_to_enbuffer);

    // bits_to_write = min(l, max_bits_to_enbuffer);
    size_t bits_to_write = l;
    if (bits_to_write > max_bits_to_enbuffer) {
      bits_to_write = max_bits_to_enbuffer;
    }

    // Select the topmost of those bits:
    size_t w = v >> (l - bits_to_write);

    uint32_t mask = (1 << bits_to_write) - 1;
    bitwrite_buffer = (bitwrite_buffer << bits_to_write) | (w & mask);
    bitwrite_buffer_size += bits_to_write;
    if (bitwrite_buffer_size == 8) {
      fputc(bitwrite_buffer, lzw_output_file);
      lzw_bytes_written++;
      bitwrite_buffer = 0;
      bitwrite_buffer_size = 0;
    }

    ASSERT(bits_to_write <= l);
    l -= bits_to_write;
  }
}

bool key_requires_bigger_length(uint32_t k) { return k >= (1 << lzw_length); }
bool update_length() {
  if (key_requires_bigger_length(lzw_next_key)) {
    lzw_len_update();
    return true;
  }
  return false;
}

void lzw_init() {
  last_read = EOF;
  root = (lzw_node_p)calloc(1, sizeof(lzw_node_t));
  curr = root;
  root->key = -1;
  lzw_length = 1;
  lzw_next_key = 1;
  lzw_data = calloc(1 << lzw_length, sizeof(lzw_data_t));
  for (uint16_t i = 0; i < 256; ++i) {
    lzw_next_char(i);
    update_length();
  }
  lzw_bytes_read = 0;
  lzw_bytes_written = 0;

  bitread_buffer = 0;
  bitread_buffer_size = 0;

  bitwrite_buffer = 0;
  bitwrite_buffer_size = 0;

  lzw_bytes_read = 0;
  lzw_bytes_written = 0;
}

size_t lzw_encode(size_t l) {
  size_t i = 0;
  for (; i < l; i++) {
    int c = fgetc(lzw_input_file);
    if (c == EOF)
      break;
    lzw_bytes_read++;
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

void lzw_encode_end(void) {
  if (curr != root) {
    emit(curr->key, lzw_length);
    curr = root;
  }
  if (bitwrite_buffer_size != 0) {
    emit(0, BITWRITE_BUFFER_MAX_SIZE - bitwrite_buffer_size);
  }
}

// This will read the next bits up to our buffer.
bool readbits(uint32_t *v) {
  while (bitread_buffer_size < lzw_length) {
    uint32_t c = fgetc(lzw_input_file);
    last_read = c;
    if (c != EOF) {
      lzw_bytes_read++;
      bitread_buffer <<= 8;
      bitread_buffer |= (uint8_t)c;
      bitread_buffer_size += 8;
    } else {
      if (bitread_buffer_size != 0) {
        // Read in the trailing zeros.
        bitread_buffer <<= (lzw_length - bitread_buffer_size);
        bitread_buffer_size = lzw_length;
        *v = bitread_buffer;
        *v &= (1 << bitread_buffer_size) - 1;
        bitread_buffer_size = 0;
        return *v != 0;
      }
      return false;
    }
  }
  uint32_t bitread_buffer_copy = bitread_buffer;
  bitread_buffer_copy >>= (bitread_buffer_size - lzw_length);
  bitread_buffer_copy &= (1 << lzw_length) - 1;
  *v = bitread_buffer_copy;
  bitread_buffer_size -= lzw_length;
  return true;
}

void pushbits(uint32_t oldkey, uint8_t oldlen) {
  if (!(BITREAD_BUFFER_MAX_SIZE - bitread_buffer_size >= oldlen)) {
    fprintf(stderr, "MAX_BUFFER_LEN = %X, readBufferLength = %X, oldLen = %X\n",
            BITREAD_BUFFER_MAX_SIZE, bitread_buffer_size, oldlen);
  }
  ASSERT(BITREAD_BUFFER_MAX_SIZE - bitread_buffer_size >= oldlen);
  bitread_buffer_size += oldlen;
}

bool lzw_valid_key(uint32_t k) {
  if (k > (1 << (lzw_length))) {
    fprintf(stderr, "Error, invalid key: %X\n", k);
  }
  ASSERT(k < (1 << (lzw_length)));
  return lzw_data[k].data != NULL;
}

size_t lzw_decode(size_t limit) {
  uint32_t curr_key;
  size_t read = 0;
  while (read < limit && readbits(&curr_key)) {
    DEBUG(2, "key %X of %u bits\n", curr_key, lzw_length);
    ASSERT(lzw_valid_key(curr_key));
    // emit that string:
    uint8_t *s = lzw_data[curr_key].data;
    uint32_t l = lzw_data[curr_key].len;
    ASSERT(l);
    for (uint32_t i = 0; i < l; ++i) {
      fputc(s[i], lzw_output_file);
      lzw_bytes_written++;
      DEBUG_STMT(bool b =)
      lzw_next_char(s[i]);
      ASSERT(!b);
    }
    read += l;

    if (key_requires_bigger_length(lzw_next_key + 1)) {
      lzw_len_update();
    }

    // peek at the next string:
    bool valid = readbits(&curr_key);
    if (!valid)
      break;
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
    //} else {
    //  fprintf(stderr, "lzw_length: %u, curr_key: %u\n", lzw_length, curr_key);
    //  ASSERT(lzw_data[curr_key - 1].data != NULL);
    }
    DEBUG_STMT(bool b =)
    lzw_next_char(s[0]);
    ASSERT(b);
    curr = root;
  }
  return read;
}
