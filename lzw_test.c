#include "lzw.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    MODE_ENCODE,
    MODE_DECODE
} encode_mode_t;

size_t bounded_op(FILE* in, FILE* out, size_t chunk, encode_mode_t m) {
    lzw_init();
    lzw_input_file = in;
    lzw_output_file = out;
    size_t res = 0;
    if (m == MODE_ENCODE) {
        res = lzw_encode(chunk);
        lzw_encode_end();
    } else if (m == MODE_DECODE) {
        res = lzw_decode(chunk);
    }
    lzw_destroy_state();
    return res;
}

const size_t chunksize = 1;
void decode_as_chunks(char *inbuffer, size_t insize, size_t target_size,
                      char **outbuffer, size_t *outsize) {
  fprintf(stderr, "Decoding: %zu total bytes\n", insize);
  FILE *out = open_memstream(outbuffer, outsize);
  FILE *in = fmemopen(inbuffer, insize, "r");
  while (target_size > 0) {
    fprintf(stderr, "decode insize: %zu\n", insize);
    target_size -= bounded_op(in, out, chunksize, MODE_DECODE);
  }
  fclose(in);
  fclose(out);
}
void encode_as_chunks(char *inbuffer, size_t insize, char **outbuffer,
                      size_t *outsize) {
  FILE *out = open_memstream(outbuffer, outsize);
  FILE *in = fmemopen(inbuffer, insize, "r");
  while (insize > 0) {
    fprintf(stderr, "encode insize: %zu\n", insize);
    insize -= bounded_op(in, out, chunksize, MODE_ENCODE);
  }
  fclose(in);
  fclose(out);
}

void encode_stream(char *inbuffer, size_t insize, char **outbuffer,
                   size_t *outsize) {
  assert(outbuffer);
  assert(outsize);
  assert(*outbuffer == NULL);
  lzw_init();
  lzw_input_file = fmemopen(inbuffer, insize, "r");
  lzw_output_file = open_memstream(outbuffer, outsize);
  while (lzw_encode(1))
    ;
  lzw_encode_end();
  fclose(lzw_input_file);
  fclose(lzw_output_file);
  assert(*outbuffer);
  lzw_destroy_state();
}


void decode_stream(char *inbuffer, size_t insize, char **outbuffer,
                   size_t *outsize) {
  assert(outbuffer);
  assert(outsize);
  assert(*outbuffer == NULL);
  lzw_init();
  lzw_input_file = fmemopen(inbuffer, insize, "r");
  lzw_output_file = open_memstream(outbuffer, outsize);
  while (lzw_decode(1))
    ;
  fclose(lzw_input_file);
  fclose(lzw_output_file);
  assert(*outbuffer);
  lzw_destroy_state();
}

int main(int argc, char *argv[]) {
  char input[] = "--";

  printf("input:");
  for (int i = 0; i < strlen(input); i++) {
    printf("%#2x ", input[i]);
  }
  printf("\n");

  char* encodechunks = NULL;
  size_t encodechunks_size;
  encode_as_chunks(input, strlen(input), &encodechunks, &encodechunks_size);
  printf("chunk-encoded (%zu):", encodechunks_size);
  for (int i = 0; i < encodechunks_size; i++) {
    printf("%#2x ", encodechunks[i]);
  }
  printf("\n");

  char* decodechunks = NULL;
  size_t decodechunks_size;
  decode_as_chunks(encodechunks, encodechunks_size, strlen(input), &decodechunks, &decodechunks_size);
  printf("chunk-decoded (%zu):", decodechunks_size);
  for (int i = 0; i < decodechunks_size; i++) {
    printf("%#2x ", decodechunks[i]);
  }
  printf("\n");

  assert(!memcmp(decodechunks, input, strlen(input)));
  assert(strlen(input) == decodechunks_size);



  //free(outbuffer);
  //free(decodebuffer);
  free(encodechunks);
  free(decodechunks);
}