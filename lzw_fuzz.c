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

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size, size_t readChunk,
                           size_t writeChunk, unsigned char copyCount) {
  if (Size == 0) {
    return 0;
  }

  copyCount = (copyCount) % 3 + 2;
  fprintf(stderr, "Making %d copies of %zu bytes\n", copyCount, Size);

  char *compressed_buffer = NULL;
  size_t compressed_buffer_size = 0;
  lzw_output_file = open_memstream(&compressed_buffer, &compressed_buffer_size);
  for (int i = 0; i < copyCount; i++) {
    lzw_input_file = fmemopen((uint8_t *)Data, Size, "r");
    lzw_init();
    while (lzw_encode(1))
      ;
    lzw_encode_end();
    lzw_destroy_state();
    fprintf(stderr, "read: %zu, wrote: %zu\n", lzw_bytes_read, lzw_bytes_written);
    fclose(lzw_input_file);
  }
  fclose(lzw_output_file);
  print_byte_array((char*) Data, Size);
  print_byte_array(compressed_buffer, compressed_buffer_size);

  lzw_input_file = fmemopen(compressed_buffer, compressed_buffer_size, "r");
  char *decompressed_buffer = NULL;
  size_t decompressed_buffer_size = 0;
  lzw_output_file =
      open_memstream(&decompressed_buffer, &decompressed_buffer_size);
  for (int i = 0; i < copyCount; i++) {
    lzw_init();
    while (lzw_decode(1))
      ;
    fprintf(stderr, "DONE WITH FIRST\n");
    lzw_destroy_state();
  }
  fclose(lzw_input_file);
  fclose(lzw_output_file);

  if (decompressed_buffer_size == 0) {
    assert(Size == 0);
    return 0;
  }

  assert(decompressed_buffer_size == Size * copyCount);
  print_byte_array(decompressed_buffer, decompressed_buffer_size);
  print_byte_array((char*)Data, Size);
  for (int i = 0; i < copyCount; i++ ){
   fprintf(stderr, "checking slot: %d (%zu)\n", i, Size);
   assert(!memcmp(Data, decompressed_buffer+(Size*i), Size));
  }

  free(compressed_buffer);
  free(decompressed_buffer);

  return 0;
}
