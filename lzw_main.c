#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "lzw.h"

char getopt(int, char*[], const char*);
char* optarg;

bool doStats = false;

// block size is 1,000,000
int block_size = 1000000;

int bytes_read = 0;
uint32_t lzw_block_reader(void) {
  if (bytes_read == block_size) {
    return EOF;
  }
  bytes_read++;
  return getchar();
}

int bytes_written = 0;
void lzw_count_emitter(uint8_t b) {
  bytes_written++;
  fputc(b, stdout);
}

int bytes_decoded = 0;

int main(int argc, char* argv[]) {
  bool doDecode = false;
  bool doEncode = false;
  char c;
  while ((c = getopt(argc, argv, "degvsm:")) != -1) {
    switch (c) {
      case 'd': doDecode = true; break;
      case 'e': doEncode = true; break;
      case 'g': lzw_debug_level = atoi(optarg); break;
      case 's': doStats = true; break;
      case 'm': lzw_max_key = atoi(optarg); break;
      default: break;
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
  if (doEncode) {
    lzw_encode();
  } else {
    lzw_decode();
  }
  if (doStats) {
    fprintf(stderr, "Stats: lzw_length = %u, lzw_next_key = %u\n", lzw_length, lzw_next_key);
  }
  lzw_destroy_state();
  return 0;
}
