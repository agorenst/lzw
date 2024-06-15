#include "lzw.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int getopt(int, char *const[], const char *);
char *optarg;

int main(int argc, char *argv[]) {
  bool doDecode = false;
  bool doEncode = false;
  char c;
  while ((c = getopt(argc, argv, "deg:m:")) != -1) {
    switch (c) {
    case 'd':
      doDecode = true;
      break;
    case 'e':
      doEncode = true;
      break;
    case 'g':
      lzw_debug_level = atoi(optarg);
      break;
    case 'm':
      lzw_max_key = atoi(optarg);
      break;
    default:
      break;
    }
  }
  if (doEncode == doDecode) {
    printf("Error, must uniquely choose encode or decode\n");
    return 1;
  }
  if (lzw_max_key && lzw_max_key < 256) {
    printf("Error, max key too small (need >= 256, got %u)\n", lzw_max_key);
    return 2;
  }
  lzw_init();
  lzw_input_file = stdin;
  lzw_output_file = stdout;
  uint64_t prev_bytes_written = 0;
  uint64_t prev_bytes_read = 0;

  const int page_size = 1024;

  if (doEncode) {
    while (lzw_encode(page_size)) {
      uint64_t bytes_written = lzw_bytes_written - prev_bytes_written;
      uint64_t bytes_read = lzw_bytes_read - prev_bytes_read;
      prev_bytes_written = lzw_bytes_written;
      prev_bytes_read = lzw_bytes_read;
      double compression_ratio = (double)bytes_written/(double)bytes_read;
      fprintf(stderr, "compression_ratio:\t%f\n", compression_ratio);
    }
    lzw_encode_end();
  } else {
    while (lzw_decode(page_size))
      ;
  }
  lzw_destroy_state();
  return 0;
}
