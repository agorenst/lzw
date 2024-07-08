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

// There's an implicit invariant that our lzw_length can never be more than 32,
// and this means our buffers always need at least 2 uses before overflowing.
// Some of our iteration depends on that (in particular emit, which
// unconditionally enbuffers something before trying to drain it).
uint64_t bitread_buffer = 0;
uint32_t bitread_buffer_size = 0;
const uint32_t BITREAD_BUFFER_MAX_SIZE = sizeof(bitread_buffer) * 8;
uint64_t bitwrite_buffer = 0;
uint32_t bitwrite_buffer_size = 0;
const uint32_t BITWRITE_BUFFER_MAX_SIZE = sizeof(bitwrite_buffer) * 8;

// We encode the output as a sequence of bits, which can cause
// complications if we need to say, emit, 13 bits.
// We store things 8 bits at a time, to match fputc.
void lzw_default_emitter(char b) {
  fputc(b, lzw_output_file);
}
lzw_emitter_t lzw_emitter = lzw_default_emitter;

const int MAX_BUFFER = 1024*1024;
uint8_t default_reader_buffer[1024*1024];

int default_reader_buffer_length = 0;
int default_reader_buffer_index = 0;
int lzw_default_reader(void) {
  //return fgetc(lzw_input_file);
  //fprintf(stderr,
  //        "default_reader_buffer_index:%d, default_reader_buffer_length:%d\n",
  //        default_reader_buffer_index, default_reader_buffer_length);
  //if (default_reader_buffer_length < MAX_BUFFER) {
    //fprintf(stderr, "len:%d,index:%d\n", default_reader_buffer_length, default_reader_buffer_index);
  //}
  if (default_reader_buffer_index < default_reader_buffer_length) {
    //fprintf(stderr, "returning read\n");
    return default_reader_buffer[default_reader_buffer_index++];
  }
  default_reader_buffer_index = 0;
  default_reader_buffer_length =
      fread(default_reader_buffer, sizeof(default_reader_buffer[0]), MAX_BUFFER,
            lzw_input_file);
  //fprintf(stderr, "just fread(%d)\n", default_reader_buffer_length);
  if (default_reader_buffer_length == 0) {
    //fprintf(stderr, "returning feof, right? %d %d\n", ferror(lzw_input_file),
            //feof(lzw_input_file));
    assert(default_reader_buffer_index == 0);
    assert(!ferror(lzw_input_file));
    assert(feof(lzw_input_file));
    return EOF;
  }
  return lzw_default_reader();
}
lzw_reader_t lzw_reader = lzw_default_reader;

// Before we get too much into executable code,
// we want to express the different modes we can run in.
// I.e., debug levels
int lzw_debug_level = 0;

#ifdef NDEBUG
#define DEBUG(l, ...)
#define ASSERT(x)
#define DEBUG_STMT(x)
#else
#define DEBUG(l, ...)                                                          \
  {                                                                            \
    if (lzw_debug_level >= l)                                                  \
      fprintf(stderr, __VA_ARGS__);                                            \
  }
#define ASSERT(x) assert(x)
#define DEBUG_STMT(x) x
#endif


// The primary action of this table is to ingest
// the next byte, and maintain the correct encoding
// information for the implicit string seen-so-far.
// That's captured in this function:
bool lzw_next_char(uint8_t c) {
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
  if (!t)
    return;
  for (uint16_t i = 0; i < 256; ++i) {
    lzw_destroy_tree(t->children[i]);
  }
  free(t);
}

uint32_t bitread_buffer_pop_bits(uint32_t bitcount);

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
  ASSERT((bitread_buffer & ((1 << bitread_buffer_size)-1)) == 0);
}

void bitwrite_buffer_push_bits(uint32_t v, uint8_t l) {
  ASSERT(bitwrite_buffer_size + l < BITWRITE_BUFFER_MAX_SIZE);
  uint32_t mask = (1 << l) - 1;
  bitwrite_buffer = (bitwrite_buffer << l) | (v & mask);
  bitwrite_buffer_size += l;
}

uint8_t bitwrite_buffer_pop_byte(void) {
  ASSERT(bitwrite_buffer_size >= 8);
  bitwrite_buffer_size -= 8;
  uint8_t b = bitwrite_buffer >> bitwrite_buffer_size;
  return b;
}

// Reading v from "left to right", we
// emit the l bits of v.
void emit(uint32_t v, uint8_t l) {
  DEBUG(3, "emit, inloop: l=%d, bitwriter_buffer_size=%d\n", l,
        bitwrite_buffer_size);
  bitwrite_buffer_push_bits(v, l);
  while (bitwrite_buffer_size >= 8) {
    char c = bitwrite_buffer_pop_byte();
    lzw_emitter(c);
    lzw_bytes_written++;
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
    int c = lzw_reader();
    DEBUG(3, "lzw_encode:fgetc(lzw_input_file)=%#x\n", c);
    if (c == EOF) {
      lzw_encode_end();
      break;
    }
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
  DEBUG(3, "lzw_encode_end: curr!=root=%s, bitwrite_buffer_size!=0=%s\n",
        curr != root ? "true" : "false",
        bitwrite_buffer_size != 0 ? "true" : "false");
  if (curr != root) {
    emit(curr->key, lzw_length);
    curr = root;
  }
  if (bitwrite_buffer_size != 0) {
    // cap off our buffer
    emit(0, 8 - (bitwrite_buffer_size % 8));
    ASSERT(bitwrite_buffer_size == 0);
  }
}

void bitread_buffer_push_byte(uint8_t c) {
  ASSERT(bitread_buffer_size + 8 < BITREAD_BUFFER_MAX_SIZE);
  bitread_buffer <<= 8;
  bitread_buffer |= c;
  bitread_buffer_size += 8;
}
uint32_t bitread_buffer_pop_bits(uint32_t bitcount) {
  ASSERT(bitread_buffer_size >= bitcount);
  uint64_t bitread_buffer_copy = bitread_buffer;
  // slide down the "oldest" bits
  bitread_buffer_copy >>= (bitread_buffer_size - bitcount);
  bitread_buffer_copy &= (1 << bitcount) - 1;
  bitread_buffer_size -= bitcount;
  return bitread_buffer_copy;
}

// This will read the next bits up to our buffer.
bool readbits(uint32_t *v) {
  while (bitread_buffer_size < lzw_length) {
    uint32_t c = lzw_reader();
    DEBUG(3, "readbits:lzw_reader()=%#x\n", c);
    if (c == EOF) {
      return false;
    }
    lzw_bytes_read++;
    bitread_buffer_push_byte(c);
  }
  *v = bitread_buffer_pop_bits(lzw_length);
  return true;
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
      lzw_emitter(s[i]);
      DEBUG_STMT(bool b =)
      lzw_next_char(s[i]);
      ASSERT(!b);
    }
    lzw_bytes_written += l;
    read += l;

    if (key_requires_bigger_length(lzw_next_key + 1)) {
      lzw_len_update();
    }

    // peek at the next string:
    if (!readbits(&curr_key)) {
      // if we're at EOF our various checks will fail,
      // but we're about to early-out anyways.
      break;
    }
    bitread_buffer_size += lzw_length;

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
      //  fprintf(stderr, "lzw_length: %u, curr_key: %u\n", lzw_length,
      //  curr_key);
      ASSERT(lzw_valid_key(curr_key-1));
    }
    DEBUG_STMT(bool b =)
    lzw_next_char(s[0]);
    ASSERT(b);
    curr = root;
  }
  return read;
}
