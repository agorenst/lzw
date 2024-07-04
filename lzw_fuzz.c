#include "lzw.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* byte_to_string(char b) {
  static char buff[9] = {0,0,0,0,0,0,0,0,'\0'};
  for (int i = 0; i < 8; i++) {
    if (b & (1 << (7-i))) {
      buff[i] = '1';
    } else {
      buff[i] = '0';
    }
  }
  return buff;
}
void print_byte_array(const char* b, size_t c) {
  for (size_t i = 0; i < c; i++) {
    fprintf(stderr, "%s,", byte_to_string(b[i]));
  }
  fprintf(stderr, "\n");
}

typedef enum {
    MODE_ENCODE,
    MODE_DECODE
} encode_mode_t;

size_t bounded_op(FILE* in, FILE* out, size_t chunk, encode_mode_t m) {
    //lzw_init();
    lzw_input_file = in;
    lzw_output_file = out;
    size_t res = 0;
    if (m == MODE_ENCODE) {
        res = lzw_encode(chunk);
        lzw_encode_end();
    } else if (m == MODE_DECODE) {
        res = lzw_decode(chunk);
    }
    //lzw_destroy_state();
    return res;
}

void decode_as_chunks(char *inbuffer, size_t insize, size_t target_size,
                      char **outbuffer, size_t *outsize) {
    lzw_init();
  size_t chunksize = 1;
  FILE *out = open_memstream(outbuffer, outsize);
  FILE *in = fmemopen(inbuffer, insize, "r");
  while (target_size > 0) {
    target_size -= bounded_op(in, out, chunksize, MODE_DECODE);
  }
    lzw_destroy_state();
  fclose(in);
  fclose(out);
}
void encode_as_chunks(char *inbuffer, size_t insize, char **outbuffer,
                      size_t *outsize) {
    lzw_init();

  size_t chunksize = 1;
  FILE *out = open_memstream(outbuffer, outsize);
  FILE *in = fmemopen(inbuffer, insize, "r");
  while (insize > 0) {
    insize -= bounded_op(in, out, chunksize, MODE_ENCODE);
  }
    lzw_destroy_state();
  fclose(in);
  fclose(out);
}


int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size == 0) {
    return 0;
  }

  //uint8_t chunkSize = Data[Size-1];
  //Size--;

  char* encodechunks = NULL;
  size_t encodechunks_size;
  encode_as_chunks((char*)Data, Size, &encodechunks, &encodechunks_size);

  char* decodechunks = NULL;
  size_t decodechunks_size;
  decode_as_chunks(encodechunks, encodechunks_size, Size, &decodechunks, &decodechunks_size);

  assert(Size == decodechunks_size);
  assert(!memcmp(decodechunks, Data, Size));


  free(encodechunks);
  free(decodechunks);
return 0;
}
