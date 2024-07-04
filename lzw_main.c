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

bool do_decode = false;
bool do_encode = false;
bool trace_ratio = false;

size_t page_size = 0;
size_t reset_limit = 0;
size_t decompressed_byte_limit = 0;

uint64_t prev_bytes_written = 0;
uint64_t prev_bytes_read = 0;

void ratio_tracker() {
  if (trace_ratio) {
    uint64_t bytes_written = lzw_bytes_written - prev_bytes_written;
    uint64_t bytes_read = lzw_bytes_read - prev_bytes_read;
    prev_bytes_written = lzw_bytes_written;
    prev_bytes_read = lzw_bytes_read;
    double compression_ratio = (double)bytes_written / (double)bytes_read;
    fprintf(stderr, "compression_ratio: %f\tbytes_read: %zu\n",
            compression_ratio, lzw_bytes_read);
  }
}

void process_stream(size_t page_size, size_t limit) {
  lzw_init();
  size_t total_processed = 0;
  size_t processed = 0;

  do {
    if (do_encode) {
      processed = lzw_encode(page_size);
    }
    else {
      processed = lzw_decode(page_size);
    }
    ratio_tracker();
    total_processed += processed;
  } while (processed != 0 && (!limit || total_processed < limit));

  if (do_encode) {
    lzw_encode_end();
  }
  lzw_destroy_state();
}

int main(int argc, char *argv[]) {
  char c;
  while ((c = getopt(argc, argv, "deg:m:p:r:ql:")) != -1) {
    switch (c) {
    case 'd':
      do_decode = true;
      break;
    case 'e':
      do_encode = true;
      break;
    case 'g':
      lzw_debug_level = atoi(optarg);
      break;
    case 'm':
      lzw_max_key = atoi(optarg);
      break;
    case 'p':
      page_size = atoi(optarg);
      break;
    case 'r':
      reset_limit = atoi(optarg);
      break;
    case 'q':
      trace_ratio = true;
      break;
    case 'l':
      decompressed_byte_limit = atoi(optarg);
      break;
    default:
      break;
    }
  }
  if (do_encode == do_decode) {
    printf("Error, must uniquely choose encode or decode\n");
    return 1;
  }
  if (lzw_max_key && lzw_max_key < 256) {
    printf("Error, max key too small (need >= 256, got %u)\n", lzw_max_key);
    return 2;
  }

  if (page_size == 0) {
    page_size = 1024;
  }

  lzw_input_file = stdin;
  lzw_output_file = stdout;

  while (!feof(lzw_input_file)) {
    process_stream(page_size, reset_limit);
  }

  return 0;
}
