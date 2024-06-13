#include "lzw.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void partial_encode_stream(char* inbuffer, size_t insize, FILE* out) {
    lzw_init();
    lzw_input_file = fmemopen(inbuffer, insize, "r");
    while (lzw_encode(1));
    lzw_encode_end();
    lzw_destroy_state();
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
  char input[] = "a";

  printf("input:");
  for (int i = 0; i < strlen(input); i++) {
    printf("%#2x ", input[i]);
  }
  printf("\n");

  char *outbuffer = NULL;
  size_t outsize;
  encode_stream(input, strlen(input), &outbuffer, &outsize);

  printf("encoded:");
  for (int i = 0; i < outsize; i++) {
    printf("%#2x ", outbuffer[i]);
  }
  printf("\n");

  char *decodebuffer = NULL;
  size_t decodesize;
  decode_stream(outbuffer, outsize, &decodebuffer, &decodesize);

  printf("decoded (%zu):", decodesize);
  for (int i = 0; i < decodesize; i++) {
    printf("%#2x ", decodebuffer[i]);
  }
  printf("\n");


  free(outbuffer);
  free(decodebuffer);
}