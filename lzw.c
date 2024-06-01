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

struct lzw_stream_t {
  lzw_data_t *data;
  lzw_node_p root;
  lzw_node_p curr;
  uint32_t data_length;
  uint32_t next_key;
  uint32_t max_key;

  lzw_emitter_t writer;
  lzw_reader_t reader;

  uint8_t buffer;
  uint8_t bufferSize;

  uint64_t readBuffer;
  uint32_t readBufferLength;
};

// Before we get too much into executable code,
// we want to express the different modes we can run in.
// I.e., debug levels
int lzw_debug_level = 0;
#define DEBUG(l, ...)                                                          \
  {                                                                            \
    if (lzw_debug_level >= l)                                                  \
      fprintf(stderr, __VA_ARGS__);                                            \
  }

// The primary action of this table is to ingest
// the next byte, and maintain the correct encoding
// information for the implicit string seen-so-far.
// That's capturedin this function:
bool lzw_next_char(lzw_stream_p s, uint8_t c) {
  DEBUG(3, "nextchar: 0x%X\n", c);
  if (s->curr->children[c]) {
    DEBUG(2, "key %u -> key %u\n", s->curr->key, s->curr->children[c]->key);
    s->curr = s->curr->children[c];
    return false;
  }
  // we have reached the end of the string.
  if (s->max_key && s->next_key >= s->max_key) {
    return true;
  }
  // Create the new fields for the new node
  const uint32_t k = s->next_key++;
  const uint32_t l = s->curr->key == -1 ? 0 : s->data[s->curr->key].len;
  uint8_t *data = calloc(l + 1, sizeof(uint8_t));
  if (l) {
    memcpy(data, s->data[s->curr->key].data, l * sizeof(uint8_t));
  }
  data[l] = c;
  // allocate the new node
  s->curr->children[c] = calloc(1, sizeof(lzw_node_t));
  s->curr->children[c]->key = k;
  s->data[k].data = data;
  s->data[k].len = l + 1;
  return true;
}

// We also have book-keeping of when we have to
// update the length
void lzw_len_update(lzw_stream_p s) {
  DEBUG(1, "Updating length: %d %d -> %d\n", s->next_key, s->data_length,
        s->data_length + 1);
  s->data_length++;
  lzw_data_t *new_data = calloc(1 << s->data_length, sizeof(lzw_data_t));
  memcpy(new_data, s->data, sizeof(lzw_data_t) * (1 << (s->data_length - 1)));
  lzw_data_t *tmp = s->data;
  s->data = new_data;
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

void lzw_destroy_state(lzw_stream_p s) {
  lzw_destroy_tree(s->root);
  s->curr = NULL;
  s->root = NULL;
  for (uint32_t i = 0; i < s->next_key; ++i) {
    if (s->data[i].data)
      free(s->data[i].data);
  }
  free(s->data);
  free(s);
}

void lzw_default_emitter(uint8_t b) { fputc(b, stdout); }
uint32_t lzw_default_reader(void) { return getchar(); }

void emit(lzw_stream_p s, uint32_t v, uint8_t l) {
  DEBUG(2, "key %X of %u bits\n", v, l);
  for (uint8_t i = 0; i < l; ++i) {
    assert(s->bufferSize < 8);
    s->buffer <<= 1;
    uint32_t mask = 1 << (l - (i + 1));
    uint32_t bit = v & mask;
    bit >>= (l - i) - 1;
    s->buffer |= bit;
    s->bufferSize++;

    if (s->bufferSize == 8) {
      s->writer(s->buffer);
      s->buffer = 0;
      s->bufferSize = 0;
    }
  }
}

void debug_emit(lzw_stream_p s, uint32_t v, uint8_t l);
void end(lzw_stream_p s) {
  // we can do this as a single constant, but hold on.
  while (s->bufferSize != 0) {
    if (lzw_debug_level > 1)
      debug_emit(s, 0, 1);
    emit(s, 0, 1);
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
bool key_requires_bigger_length(lzw_stream_p s, uint32_t k) {
  return biggest_one(k) > s->data_length;
}

void print_uint32_t_bin(uint32_t x) {
  for (int32_t i = 31; i >= 0; i--) {
    fprintf(stderr, "%s", (x & ((uint32_t)1 << i)) ? "1" : "0");
  }
}
void print_uint8_t_array(uint8_t *a, uint32_t l) {
  for (uint32_t i = 0; i < l; i++) {
    fprintf(stderr, "0x%X", a[i]);
  }
}
void debug_emit(lzw_stream_p s, uint32_t v, uint8_t l) {
  print_uint32_t_bin(v);
  fprintf(stderr, " %u ", l);
  print_uint8_t_array(s->data[v].data, s->data[v].len);
  fprintf(stderr, "\n");
}

bool update_length(lzw_stream_p s) {
  if (biggest_one(s->next_key) > s->data_length) {
    lzw_len_update(s);
    return true;
  }
  assert(s->data_length >= biggest_one(s->next_key));
  return false;
}

lzw_stream_p lzw_init(int max_key, lzw_reader_t r, lzw_emitter_t w) {
  lzw_stream_p s = (lzw_stream_p)calloc(1, sizeof(lzw_stream_t));
  s->max_key = max_key;
  s->root = (lzw_node_p)calloc(1, sizeof(lzw_node_t));
  s->root->key = -1;
  s->curr = s->root;
  s->next_key = 1;
  s->data_length = 1;
  s->data = calloc(1 << s->data_length, sizeof(lzw_data_t));

  if (r == NULL) {
    r = lzw_default_reader;
  }
  if (w == NULL) {
    w = lzw_default_emitter;
  }
  s->reader = r;
  assert(s->reader);
  s->writer = w;
  assert(s->writer);

  for (uint16_t i = 0; i < 256; ++i) {
    lzw_next_char(s, i);
    update_length(s);
  }
  s->buffer = 0;
  s->bufferSize = 0;
  s->readBuffer = 0;
  s->readBufferLength = 0;
  DEBUG(2, "data_length = %d\n", s->data_length);
  return s;
}

void lzw_encode(lzw_stream_p s) {
  assert(s);
  assert(s->reader);
  int c = s->reader();
  while (c != EOF) {
    bool newString = lzw_next_char(s, c);
    if (newString) {
      emit(s, s->curr->key, s->data_length);
      s->curr = s->root;
      update_length(s);
      // note that we don't update C here.
    } else {
      c = s->reader();
    }
  }
  if (s->curr != s->root) {
    emit(s, s->curr->key, s->data_length);
    s->curr = s->root;
  }
  end(s);
}

const uint32_t MAX_BUFFER_LEN = sizeof(uint32_t) * 8;

bool readbits(lzw_stream_p s, uint32_t *v, uint8_t l) {
  while (s->readBufferLength < l) {
    uint32_t c = s->reader();
    if (c == EOF) {
      if (s->readBufferLength != 0) {
        while (s->readBufferLength < l) {
          s->readBuffer <<= 1;
          s->readBufferLength++;
        }
        *v = s->readBuffer;
        *v &= (1 << s->readBufferLength) - 1;
        s->readBufferLength = 0;
        // fprintf(stderr, "Special EOF case: %X\n", *v);
        //  this may be a bug... what if the final symbol to be encoded is
        //  value 0?
        return *v != 0;
      }
      return false;
    } else {
      s->readBuffer <<= 8;
      s->readBufferLength += 8;
      s->readBuffer |= (uint8_t)c;
    }
  }
  uint32_t readBufferCopy = s->readBuffer;
  readBufferCopy >>= (s->readBufferLength - l);
  readBufferCopy &= (1 << l) - 1;
  *v = readBufferCopy;
  s->readBufferLength -= l;
  return true;
}

void pushbits(lzw_stream_p s, uint32_t oldkey, uint8_t oldlen) {
  if (!(MAX_BUFFER_LEN - s->readBufferLength >= oldlen)) {
    fprintf(stderr, "MAX_BUFFER_LEN = %X, readBufferLength = %X, oldLen = %X\n",
            MAX_BUFFER_LEN, s->readBufferLength, oldlen);
  }
  assert(MAX_BUFFER_LEN - s->readBufferLength >= oldlen);
  // oldkey <<= readBufferLength;
  // readBuffer |= oldkey;
  s->readBufferLength += oldlen;
}

bool lzw_valid_key(lzw_stream_p s, uint32_t k) {
  if (k > (1 << (s->data_length))) {
    fprintf(stderr, "Error, invalid key: %X\n", k);
  }
  assert(k < (1 << (s->data_length)));
  return s->data[k].data != NULL;
}

void lzw_decode(lzw_stream_p s) {
  uint32_t currKey;
  while (readbits(s, &currKey, s->data_length)) {
    DEBUG(2, "key %X of %u bits\n", currKey, s->data_length);
    assert(lzw_valid_key(s, currKey));
    // emit that string:
    uint8_t *r = s->data[currKey].data;
    uint32_t l = s->data[currKey].len;
    assert(l);
    for (uint32_t i = 0; i < l; ++i) {
      s->writer(r[i]);
      bool b = lzw_next_char(s, r[i]);
      assert(!b);
    }

    if (key_requires_bigger_length(s, s->next_key + 1)) {
      lzw_len_update(s);
    }

    // peek at the next string:
    bool valid = readbits(s, &currKey, s->data_length);
    if (!valid)
      break;
    pushbits(s, currKey, s->data_length);

    if (lzw_valid_key(s, currKey)) {
      r = s->data[currKey].data;
    }

    bool b = lzw_next_char(s, r[0]);
    assert(b);
    s->curr = s->root;
  }
}
