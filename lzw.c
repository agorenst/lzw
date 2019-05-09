#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

uint8_t buffer = 0;
uint8_t bufferSize = 0;
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

// this is for debugging purposes
void debug_emit(uint32_t v, uint8_t l, FILE* o) {
  for (uint8_t i = 0; i < l; ++i) {
    uint32_t mask = 1 << (l - (i+1));
    uint32_t bit = v & mask;
    bit >>= (l-i)-1;
    fputc(bit ? '1' : '0', o);
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
  char* str;
  struct lzw_node_tag* children[256];
} lzw_node_t;
typedef lzw_node_t* lzw_node_p;

int lzw_length = 1;
int lzw_next_key = 0;
lzw_node_p root = NULL;
lzw_node_p curr = NULL;
char** lzw_key_to_str = NULL;

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

    // copy the string. Very wasteful!
    uint32_t l = strlen(curr->str);
    curr->children[c]->str = calloc(l+2, 1);
    strcpy(curr->children[c]->str, curr->str);
    curr->children[c]->str[l] = (char) c;
    curr->children[c]->str[l+1] = '\0';

    char* childstring = curr->children[c]->str;
    char** table_string = &lzw_key_to_str[curr->children[c]->key];
    assert(*table_string == NULL);
    *table_string = calloc(1, strlen(childstring)+1);
    strcpy(*table_string, childstring);

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
    //printf("Updating length! %d %d -> %d\n", lzw_next_key, lzw_length, lzw_length+1);
    lzw_length++;
    char** new_buff = calloc(1<<lzw_length, sizeof(char*));
    memcpy(new_buff, lzw_key_to_str, sizeof(char*)*(1<<(lzw_length-1)));
    char** t = lzw_key_to_str;
    lzw_key_to_str = new_buff;
    free(t);
    return true;
  }
  assert(lzw_length >= biggest_one(lzw_next_key));
  return false;
}

void init_lzw() {
  root = (lzw_node_p) calloc(1, sizeof(lzw_node_t));
  curr = root;
  root->key = -1;
  root->str = calloc(1, 1);
  root->str[0] = '\0';
  lzw_key_to_str = calloc(1<<lzw_length, sizeof(char*));
  for (int i = 0; i < 256; ++i) {
    lzw_next_char((char)i);
    update_length();
  }
  fprintf(stderr, "lzw_length = %d\n", lzw_length);
  assert(lzw_length == 9);
}

void encode() {
  init_lzw();

  int c = getchar();
  while (c != EOF) {
    bool newString = lzw_next_char(c);
    if (newString) {
      //printf("encoding: %s (%d): ", curr->str, curr->key);
      emit(curr->key, lzw_length, stdout);
      //printf("\n");
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
    //fprintf(stderr, "Read in byte: %X\n", c);
    //?
    if (c == EOF) {
      assert(readBufferLength == 0);
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
  readBufferCopy <<= MAX_BUFFER_LEN - l;
  readBufferCopy >>= MAX_BUFFER_LEN - l;
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
  assert(k < (1 << (lzw_length+1)));
  return lzw_key_to_str[k] != NULL;
}

void debug_key(int key, int len) {
  printf("\nencoding: ");
  int mask = 1 << (len-1);
  for (int i = 0; i < len; ++i) {
    if (mask & key) {
      printf("1");
    }
    else {
      printf("0");
    }
    mask >>= 1;
  }
  printf("\n");
}

void decode() {
  init_lzw();

  uint32_t currKey, nextKey;
  readbits(&currKey, lzw_length, stdin);
  //fprintf(stderr, "Read initial key: %d\n", currKey);
  assert(biggest_one(currKey) <= 8);
  char* currString = lzw_key_to_str[currKey];
  assert(strlen(currString) == 1);

  for (;;) {
    for (int i = 0; i < strlen(currString); ++i) {
      fputc(currString[i], stdout);
      bool newString = lzw_next_char(currString[i]);
      assert(!newString);
      fflush(stdout);
    }
    bool newBits = readbits(&nextKey, lzw_length, stdin);
    if (!newBits) break;

    char newChar = '\0';
    bool refresh = false;
    if (lzw_valid_key(nextKey)) {
      newChar = lzw_key_to_str[nextKey][0];
    }
    else {
      refresh = true;
      newChar = currString[0];
    }
    bool newString = lzw_next_char(newChar);
    assert(newString);
    refresh |= update_length();
    curr = root;

    // we have to look up the key as the new length
    if (refresh) {
      pushbits(nextKey, lzw_length-1);
      readbits(&nextKey, lzw_length, stdin);
    }
    assert(lzw_valid_key(nextKey));
    //debug_key(nextKey, lzw_length);
    currString = lzw_key_to_str[nextKey];
  }
}

int main(int argc, char* argv[]) {
  decode();
}
