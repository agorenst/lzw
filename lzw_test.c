#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "lzw.h"


// This is a shared correctness routine/helper
void init_streams(char *inbuffer, size_t insize, char **outbuffer,
                  size_t *outsize) {
  FILE *out = open_memstream(outbuffer, outsize);
  FILE *in = fmemopen(inbuffer, insize, "r");
  lzw_input_file = in;
  lzw_output_file = out;
}
void close_streams(void) {
  fclose(lzw_input_file);
  fclose(lzw_output_file);
}

uint64_t total_stream_read = 0;
uint64_t total_stream_written = 0;

void dumpbytes(const char *d, size_t c) {
  for (size_t i = 0; i < c; i++) {
    if (i > 0 && i % 20 == 0) {
      fprintf(stderr, "\n");
    }
    fprintf(stderr, "%02x ", (unsigned char)d[i]);
  }
      fprintf(stderr, "\n");
}

char* Data;
size_t Size;

const int page_size = 1;
void do_encode_stream() {
  total_stream_read = 0;
  total_stream_written = 0;
  fprintf(stderr, "ENCODING\n");
  lzw_init();
  while (lzw_encode(page_size)) {
    if (!feof(lzw_input_file)) {
      lzw_emit_clear_code();
      total_stream_read += lzw_bytes_read;
      total_stream_written += lzw_bytes_written;
      lzw_destroy_state();
      lzw_init();
    }
  }

  close_streams();
  lzw_destroy_state();
}

void do_decode_stream() {
  fprintf(stderr, "DECODING\n");
  total_stream_read = 0;
  total_stream_written = 0;
  lzw_init();
  // Decode is guaranteed to make progress (even in presence of clear-codes)
  while (lzw_decode(page_size));
  total_stream_read += lzw_bytes_read;
  total_stream_written += lzw_bytes_written;
  close_streams();
  lzw_destroy_state();
}


int main(int argc, char* argv[]) {
  assert(argc == 2);
  // these are globals
  Data = strdup(argv[1]);
  Size = strlen(Data);
  dumpbytes(Data, Size);

  lzw_set_debug_string("ksb");

  char *encodechunks = NULL;
  size_t encodechunks_size = 0;

  init_streams((char*)Data, Size, &encodechunks, &encodechunks_size);
  do_encode_stream();
  dumpbytes(encodechunks, encodechunks_size);
  if (total_stream_read != Size) { fprintf(stderr, "total_stream_read=%lu\tSize=%zu\n", total_stream_read, Size); }
  //assert(total_stream_read == Size);
  //assert(total_stream_written == encodechunks_size);

  char *decodechunks = NULL;
  size_t decodechunks_size;
  init_streams(encodechunks, encodechunks_size, &decodechunks,
               &decodechunks_size);
  do_decode_stream();
  dumpbytes(decodechunks, decodechunks_size);
  //assert(total_stream_read == encodechunks_size);
  //assert(total_stream_written == decodechunks_size);
  //fprintf(stderr, "encodechunks_size=%zu\n",encodechunks_size);
  //fprintf(stderr, "decodechunks_size=%zu\ttotal_stream_written=%zu\tSize=%zu\n",decodechunks_size, total_stream_written, Size);


  //if (total_stream_written != Size) { fprintf(stderr, "total_stream_written=%lu\tSize=%zu\n", total_stream_written, Size);}
  //assert(total_stream_written == Size);

  if (decodechunks_size != Size) { fprintf(stderr, "decodechunks_size=%lu\tSize=%zu\n", decodechunks_size, Size);}
  assert(Size == decodechunks_size);
  assert(!memcmp(decodechunks, Data, Size));

  free(encodechunks);
  free(decodechunks);
  free(Data);
}