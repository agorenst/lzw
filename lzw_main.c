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

int verbosity = 0;

size_t page_size = 0;
size_t reset_limit = 0;
size_t decompressed_byte_limit = 0;

bool trim_input = false;
int ratio_based_reader(void) {
  if (trim_input) {
    return EOF;
  }
  return fgetc(lzw_input_file);
}
void ratio_based_emitter(char b) { fputc(b, lzw_output_file); }

uint64_t prev_bytes_written = 0;
uint64_t prev_bytes_read = 0;
double next_ratio() {
  uint64_t bytes_written = lzw_bytes_written - prev_bytes_written;
  uint64_t bytes_read = lzw_bytes_read - prev_bytes_read;
  prev_bytes_written = lzw_bytes_written;
  prev_bytes_read = lzw_bytes_read;
  //fprintf(stderr, "bytes_written: %zu\n", bytes_written);
  //fprintf(stderr, "bytes_read:    %zu\n", bytes_read);
  assert(bytes_written);
  double compression_ratio = (double)bytes_written / (double)bytes_read;
  return compression_ratio;
}

void reset_written() {
  prev_bytes_written = 0;
  prev_bytes_read = 0;
}

int main(int argc, char *argv[]) {
  char c;
  bool do_ratio = false;
  while ((c = getopt(argc, argv, "deg:m:p:r:ql:v:x")) != -1) {
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
    case 'v':
      verbosity = atoi(optarg);
      break;
      case 'x':
      do_ratio = true;
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

  if (verbosity) {
    fprintf(stderr, "lzw_max_key: %d\n", lzw_max_key);
    fprintf(stderr, "page_size  : %zu\n", page_size);
  }

  lzw_input_file = stdin;
  lzw_output_file = stdout;
  if (do_ratio) {
    lzw_reader = ratio_based_reader;
    lzw_emitter = ratio_based_emitter;
  }

  lzw_init();
  while (!feof(lzw_input_file)) {
    if (do_encode) {
      lzw_encode(page_size);
    } else {
      lzw_decode(page_size);
    }
    double compression_ratio = next_ratio();
    if (trace_ratio) {
      fprintf(stderr, "compression_ratio: %f\tlzw_bytes_read: %zu\n",
              compression_ratio, lzw_bytes_read);
    }
    if (do_ratio && (compression_ratio > 1)) {
      trim_input = true;
      // Force a flush, basically.
      if (do_encode) {
        lzw_encode(1024);
      } else {
        lzw_decode(1024);
      }
      trim_input = false;
      // and restart?
      lzw_destroy_state();
      lzw_init();
      reset_written();
    }
  }
  lzw_destroy_state();

  return 0;
}
