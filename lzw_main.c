#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "lzw.h"

int getopt(int, char*const[], const char*);
char* optarg;

int main(int argc, char* argv[]) {
  bool doDecode = false;
  bool doEncode = false;
  char c;
  while ((c = getopt(argc, argv, "deg:m:")) != -1) {
    switch (c) {
      case 'd': doDecode = true; break;
      case 'e': doEncode = true; break;
      case 'g': lzw_debug_level = atoi(optarg); break;
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
  lzw_input_file = stdin;
  lzw_output_file = stdout;
  if (doEncode) {
    while(lzw_encode(1));
    lzw_encode_end();
  } else {
    while(lzw_decode(1));
  }
  lzw_destroy_state();
  return 0;
}
