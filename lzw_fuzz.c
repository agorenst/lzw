#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include "lzw.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size == 0) {
  return 0;
  }

  lzw_input_file = fmemopen((uint8_t*)Data, Size, "r");
  char* compressed_buffer = NULL;
  size_t compressed_buffer_size = 0;
  lzw_output_file = open_memstream(&compressed_buffer, &compressed_buffer_size);
  lzw_init();
  lzw_encode();
  lzw_destroy_state();
  fclose(lzw_input_file);
  fclose(lzw_output_file);

  lzw_input_file = fmemopen(compressed_buffer, compressed_buffer_size, "r");
  char* decompressed_buffer = NULL;
  size_t decompressed_buffer_size = 0;
  lzw_output_file = open_memstream(&decompressed_buffer, &decompressed_buffer_size);
  lzw_init();
  lzw_decode();
  lzw_destroy_state();
  fclose(lzw_input_file);
  fclose(lzw_output_file);

  if (decompressed_buffer_size == 0) {
    assert(Size == 0);
    return 0;
  }

  assert(decompressed_buffer_size == Size);
  assert(!memcmp(Data, decompressed_buffer, Size));

  free(compressed_buffer);
  free(decompressed_buffer);

  return 0;
}
