#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lzw.h"

typedef struct lzw_node_tag *lzw_node_p;
typedef struct lzw_childen_set_tag {
  bool use_array;
    struct {
      int index;
      struct {
        uint8_t key;
        lzw_node_p value;
      } immediate[4];
    } local;
    lzw_node_p *all;
} lzw_children_set_t;

lzw_node_p children_set_find(lzw_children_set_t* s, uint8_t k) {
  if (!s->use_array) {
    int max_index = s->local.index;
    for (int i = 0; i < max_index; i++) {
      if (s->local.immediate[i].key == k) {
        return s->local.immediate[i].value;
      }
    }
    return NULL;
  }
  return s->all[k];
}


// we want to build a mapping from keys to data-strings.
typedef struct lzw_node_tag {
  lzw_children_set_t children;
  uint32_t key;
} lzw_node_t, *lzw_node_p;

lzw_node_p children_set_allocate(lzw_children_set_t* s, uint8_t c, uint32_t k) {
  lzw_node_p r = calloc(1, sizeof(lzw_node_t));
  r->key = k;
  if (!s->use_array) {
    int n = s->local.index++;
    if (n < 4) {
    s->local.immediate[n].key = c;
    s->local.immediate[n].value = r;
    return r;
    }
    s->use_array = true;
    s->all = calloc(256, sizeof(lzw_node_p));
    for (int i = 0; i < n; i++) {
      s->all[s->local.immediate[i].key] = s->local.immediate[i].value;
    }
  }
  s->all[c] = r;
  return r;
}

typedef struct {
  uint8_t *data;
  uint32_t len;
  lzw_node_p parent;
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

// Before we get too much into executable code,
// we want to express the different modes we can run in.
// I.e., debug levels
#ifdef NDEBUG
#define DTRACE(k, ...)
#define ASSERT(x)
#define DEBUG_STMT(x)
#else
enum {
  DB_STATE,
  DB_BYTE_STREAM,
  DB_KEY_STREAM,
  DB_DICTIONARY,
  DB_MAX,
};
int DB_KEYS_SET[DB_MAX] = {};
#define DTRACE(k, ...)                                                         \
  if (DB_KEYS_SET[k]) {                                                        \
    fprintf(stderr, __VA_ARGS__);                                              \
  }
#define ASSERT(x) assert(x)
#define DEBUG_STMT(x) x
#endif

void lzw_set_debug_string(const char *s) {
#ifdef NDEBUG
#else
  for (int i = 0; i < strlen(s); i++) {
    switch (s[i]) {
    case 's':
      DB_KEYS_SET[DB_STATE] = 1;
      break;
    case 'b':
      DB_KEYS_SET[DB_BYTE_STREAM] = 1;
      break;
    case 'k':
      DB_KEYS_SET[DB_KEY_STREAM] = 1;
      break;
    case 'd':
      DB_KEYS_SET[DB_DICTIONARY] = 1;
      break;
    }
  }
#endif
}

#ifndef NDEBUG
static const char *asbits(uint64_t v, uint8_t l) {
  static char format[(1 << 8 * sizeof(uint8_t)) + 1];
  // Emit bits backwards
  for (int i = 0; i < l; i++) {
    format[l - (i + 1)] = v % 2 ? '1' : '0';
    v >>= 1;
  }
  format[l] = '\0';
  return format;
}
#endif

enum { NEXT_CHAR_CONTINUE = 0, NEXT_CHAR_MAX = 1, NEXT_CHAR_NEW = 2 };
const uint32_t lzw_clear_code = 256;

// The primary action of this table is to ingest
// the next byte, and maintain the correct encoding
// information for the implicit string seen-so-far.
// That's captured in this function:
int lzw_next_char(uint8_t c) {
  lzw_node_p next = children_set_find(&curr->children, c);
  if (next) {
    DTRACE(DB_STATE, "APPEND(%#x)\t\tSTATE\t%u\t%u\n", c, curr->key, next->key);
    curr = next;
    return NEXT_CHAR_CONTINUE;
  }
  // we have reached the end of the string.
  if (lzw_max_key && lzw_next_key >= lzw_max_key) {
    DTRACE(DB_STATE, "APPEND(%#x)\tMAXKEY\n", c);
    return NEXT_CHAR_MAX;
  }
  // Create the new fields for the new node
  const uint32_t k = lzw_next_key++;
  next = children_set_allocate(&curr->children, c, k);
  const uint32_t l = curr->key == -1 ? 0 : lzw_data[curr->key].len;
  uint8_t *data = calloc(l + 1, sizeof(uint8_t));
  if (l) {
    memcpy(data, lzw_data[curr->key].data, l * sizeof(uint8_t));
  }
  data[l] = c;
  lzw_data[k].data = data;
  lzw_data[k].len = l + 1;
  lzw_data[k].parent = next;
  DTRACE(DB_DICTIONARY, "DICT\tADD\t%u\t%u\t%u\t%#x\n", k, curr->key, l + 1, c);
  DTRACE(DB_STATE, "APPEND(%#x)\t\tNEWSTATE %u\n", c, k);
  return NEXT_CHAR_NEW;
}

// We also have book-keeping of when we have to
// update the length
static size_t lzw_data_size(void) {
  return (1 << lzw_length) * sizeof(lzw_data_t);
}

void lzw_len_update() {
  DTRACE(DB_STATE, "INCLENGTH %d->%d\n", lzw_length, lzw_length + 1)
  size_t old_length = lzw_data_size();
  lzw_length++;
  size_t new_length = lzw_data_size();
  ASSERT(old_length * 2 == new_length);
  lzw_data = realloc(lzw_data, new_length);
  memset((char *)lzw_data + old_length, 0, old_length);
}

void lzw_destroy_tree(lzw_node_p t, bool free_node) ;
void lzw_destroy_tree_children(lzw_node_p t) {
  if (t->children.use_array) {
    for (uint16_t i = 0; i < 256; ++i) {
      lzw_destroy_tree(t->children.all[i], true);
    }
    free(t->children.all);
  } else {
    int n = t->children.local.index;
    for (int i = 0; i < n; i++ ){
      lzw_destroy_tree(t->children.local.immediate[i].value, true);
    }
  }
}

void lzw_destroy_tree(lzw_node_p t, bool free_node) {
  if (!t)
    return;
  lzw_destroy_tree_children(t);
  if (free_node) free(t);
}

uint32_t bitread_buffer_pop_bits(uint32_t bitcount);

void lzw_destroy_state(void) {
  lzw_destroy_tree(root, true);
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
  ASSERT((bitread_buffer & ((1 << bitread_buffer_size) - 1)) == 0);
  ASSERT(bitwrite_buffer_size == 0);
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

#ifndef IO_BUFFER_SIZE
#define IO_BUFFER_SIZE 4096
#endif
static uint8_t fwrite_buffer[IO_BUFFER_SIZE];
static int emit_buffer_next = 0;
void emit_buffer_flush() {
  fwrite(fwrite_buffer, 1, emit_buffer_next, lzw_output_file);
  emit_buffer_next = 0;
}
void lzw_emit_byte(uint8_t c) {
  if (emit_buffer_next == sizeof(fwrite_buffer)) {
    emit_buffer_flush();
  }
  fwrite_buffer[emit_buffer_next++] = c;
}

// Reading v from "left to right", we
// emit the l bits of v.
void emit(uint32_t v, uint8_t l) {
  ASSERT((v & ((1 << l) - 1)) == v); // v doesn't have extra bits
  DTRACE(DB_KEY_STREAM, "EMITKEY(%d):\t\t%d\t%s\n", l, v, asbits(v, l));
  bitwrite_buffer_push_bits(v, l);
  while (bitwrite_buffer_size >= 8) {
    uint8_t c = bitwrite_buffer_pop_byte();
    DTRACE(DB_BYTE_STREAM, "EMITBYTE(encode):\t%#x\t%s\n", c, asbits(c, 8));
    lzw_emit_byte(c);
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
  lzw_next_key = 0;
  lzw_data = calloc(1 << lzw_length, sizeof(lzw_data_t));

#ifndef NDEBUG
  int old_state_key = DB_KEYS_SET[DB_STATE];
  DB_KEYS_SET[DB_STATE] = 0;
  int old_dict_key = DB_KEYS_SET[DB_DICTIONARY];
  DB_KEYS_SET[DB_DICTIONARY] = 0;
#endif
  for (uint16_t i = 0; i < 256; ++i) {
    lzw_next_char(i);
    update_length();
  }
#ifndef NDEBUG
  DB_KEYS_SET[DB_STATE] = old_state_key;
  DB_KEYS_SET[DB_DICTIONARY] = old_dict_key;
#endif

  ASSERT(lzw_clear_code == lzw_next_key);
  lzw_next_key++; // reserve 256 for the clear-code.
  update_length();

  bitread_buffer = 0;
  bitread_buffer_size = 0;

  bitwrite_buffer = 0;
  bitwrite_buffer_size = 0;

  lzw_bytes_read = 0;
  lzw_bytes_written = 0;
}

static uint8_t fread_buffer[IO_BUFFER_SIZE];
static int read_buffer_next = 0;
static int read_buffer_max = 0;
uint32_t lzw_read_byte(void) {
  if (read_buffer_next == read_buffer_max) {
    read_buffer_max =
        fread(fread_buffer, 1, sizeof(fread_buffer), lzw_input_file);
    read_buffer_next = 0;
  }
  if (read_buffer_max == 0) {
    return EOF;
  }
  return fread_buffer[read_buffer_next++];
}

bool input_eof(void) { return read_buffer_max == 0 && feof(lzw_input_file); }

size_t lzw_encode(size_t l) {
  size_t i = 0;
  for (;;) {
    int c = lzw_read_byte();
    if (c == EOF) {
      DTRACE(DB_BYTE_STREAM, "READBYTE(encode):\t%d\n", c);
      DTRACE(DB_STATE, "lzw_encode:eof\n");
      lzw_encode_end();
      break;
    }
    i++;
    DTRACE(DB_BYTE_STREAM, "READBYTE(encode):\t%#x\t%s\n", c, asbits(c, 8));
    if (lzw_next_char(c) != NEXT_CHAR_CONTINUE) {
      emit(curr->key, lzw_length);
      curr = root;
      update_length();
      lzw_next_char(c);
      if (i > l) {
        break;
      }
    }
  }
  lzw_bytes_read += i;
  return i;
}

void lzw_emit_clear_code(void) {
  DTRACE(DB_STATE, "CLEAR_CODE\t%zu\t%d\n", lzw_bytes_written, input_eof());
  if (input_eof()) {
    return; // don't bother
  }
  if (curr != root) {
    emit(curr->key, lzw_length);
    curr = root;
    // When we read in a code, we always assume that it's a new key
    // (unless if we're at the max). So our reader preemptively
    // updates the length at the key boundaries---we need to do that
    // here too, even though this key isn't new.
    if (key_requires_bigger_length(lzw_next_key + 1)) {
      lzw_len_update();
    }
  }
  emit(lzw_clear_code, lzw_length);
  lzw_encode_end();
}

void lzw_encode_end(void) {
  // if we haven't done anything yet, make that more explicit
  DTRACE(DB_STATE, "ENCODE_END\t%u\t%zu\t%d\n", bitwrite_buffer_size,
         lzw_bytes_written, curr == root);
  if (bitwrite_buffer_size == 0 && lzw_bytes_written == 0 && curr == root) {
    return;
  }
  if (curr != root) {
    emit(curr->key, lzw_length);
    curr = root;
  }
  if (bitwrite_buffer_size != 0) {
    // We want to finish emitting our last key.
    // cap off our buffer: there are (say) 3 valid bits left,
    // we just need to pad it so we can emit those 3 bits as part
    // of a larger byte.
    uint8_t bits_to_add = 8 - (bitwrite_buffer_size % 8);
    emit(0, bits_to_add);
    ASSERT(bitwrite_buffer_size == 0);
  }
  emit_buffer_flush();
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
    uint32_t c = lzw_read_byte();
    if (c == EOF) {
      DTRACE(DB_BYTE_STREAM, "READBYTE(decode):\t%d\n", c);
      return false;
    }
    DTRACE(DB_BYTE_STREAM, "READBYTE(decode):\t%#x\t%s\n", c, asbits(c, 8));
    lzw_bytes_read++;
    bitread_buffer_push_byte(c);
  }
  *v = bitread_buffer_pop_bits(lzw_length);
  return true;
}

bool lzw_valid_key(uint32_t k) {
  ASSERT(k < (1 << (lzw_length)));
  return lzw_data[k].data != NULL;
}

size_t lzw_decode(size_t limit) {
  uint32_t curr_key;
  size_t read = 0;
  while (read < limit && readbits(&curr_key)) {
    DTRACE(DB_KEY_STREAM, "READKEY(%d):\t\t%d\t%s\n", lzw_length, curr_key,
           asbits(curr_key, lzw_length));
    if (curr_key == lzw_clear_code) {
      DTRACE(DB_STATE, "DECODE\tCLEAR_CODE\n");
      lzw_destroy_state();
      // Curious thing: we can return a value greater than lzw_bytes_read,
      // as lzw_init() set that back to 0. We continue because we also
      // promise to always emit something when we're called.
      lzw_init();
      continue;
    }
    ASSERT(lzw_valid_key(curr_key));

    // emit that string:
    uint8_t *s = lzw_data[curr_key].data;
    uint32_t l = lzw_data[curr_key].len;
    ASSERT(l);
    for (uint32_t i = 0; i < l; ++i) {
      DTRACE(DB_BYTE_STREAM, "EMITBYTE(decode):\t%#x\n", s[i]);
      lzw_emit_byte(s[i]);
    }
    lzw_bytes_written += l;
    read += l;
    curr = lzw_data[curr_key].parent;

    if (key_requires_bigger_length(lzw_next_key + 1)) {
      lzw_len_update();
    }

    // peek at the next string:
    DTRACE(DB_STATE, "DECODE(peek)\n");
    if (!readbits(&curr_key)) {
      DTRACE(DB_STATE, "DECODE(break)\n");
      // We're at EOF, so just early-out
      break;
    }
    if (curr_key == lzw_clear_code) {
      // Skip our checks: we don't want to evolve our state.
      bitread_buffer_size += lzw_length;
      continue;
    }
    DTRACE(DB_STATE, "DECODE(continue)\n");
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
      ASSERT(curr_key - 1 == lzw_clear_code || lzw_valid_key(curr_key - 1));
    }
    DEBUG_STMT(int b =)
    lzw_next_char(s[0]);
    ASSERT(b != NEXT_CHAR_CONTINUE);
    curr = root;
  }
  emit_buffer_flush();
  return read;
}
