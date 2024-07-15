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
bool do_ratio = false;
bool trace_ratio = false;
char *ratio_log_filename = NULL;

FILE *user_input;
FILE *user_output;

int verbosity = 0;

size_t page_size = 4096;

uint64_t total_stream_read = 0;
uint64_t total_stream_written = 0;

uint64_t prev_bytes_written = 0;
uint64_t prev_bytes_read = 0;
uint64_t page_bytes_written = 0;
uint64_t page_bytes_read = 0;
double next_ratio() {
  page_bytes_written = lzw_bytes_written - prev_bytes_written;
  page_bytes_read = lzw_bytes_read - prev_bytes_read;
  prev_bytes_written = lzw_bytes_written;
  prev_bytes_read = lzw_bytes_read;
  // fprintf(stderr, "page_bytes_written: %zu\n", page_bytes_written);
  // fprintf(stderr, "page_bytes_read:    %zu\n", page_bytes_read);
  assert(page_bytes_written);
  double compression_ratio =
      (double)page_bytes_written / (double)page_bytes_read;
  if (do_decode) {
    compression_ratio = (double)page_bytes_read / (double)page_bytes_written;
  }
  return compression_ratio;
}

void reset_written() {
  prev_bytes_written = 0;
  prev_bytes_read = 0;
}

int lzw_echo_reader(void) {
  static int reader_count = 0;
  char c = fgetc(lzw_input_file);
  fprintf(stderr, "READ\t%x\t%d\n", c, reader_count++);
  return c;
}
void lzw_echo_emitter(char c) {
  static int writer_count = 0;
  fprintf(stderr, "WRITE\t%x\t%d\n", c, writer_count++);
  fputc(c, lzw_output_file);
}

void decode_stream() {
  total_stream_read = 0;
  total_stream_written = 0;
  lzw_init();
  // Decode is guaranteed to make progress (even in presence of clear-codes)
  while (lzw_decode(page_size))
    ;
  total_stream_read += lzw_bytes_read;
  total_stream_written += lzw_bytes_written;
  lzw_destroy_state();
}

void encode_stream() {
  total_stream_read = 0;
  total_stream_written = 0;
  FILE *ratio_log_file = stderr;
  if (ratio_log_filename) {
    ratio_log_file = fopen(ratio_log_filename, "w");
  }

  for (int block_count = 0;; block_count++) {
    reset_written();
    double ema_slow = 0.0;
    double ema_slow_alpha = 0.0005;
    double ema_fast = 0.0;
    double ema_fast_alpha = 0.05;

    lzw_init();

    for (int page_count = 0;; page_count++) {
      // fprintf(stderr, "processing page: %d\n", page_count);
      size_t bytes_processed = lzw_encode(page_size);
      // We've hit EOF.
      if (!bytes_processed) {
        total_stream_read += lzw_bytes_read;
        total_stream_written += lzw_bytes_written;
        lzw_destroy_state();
        return;
      }

      // We've processed a page's worth of data, now
      // evaluate our compression ratio and windows.
      double compression_ratio = next_ratio();
      if (page_count < 64) {
        ema_slow = compression_ratio;
        ema_fast = compression_ratio;
      } else {
        ema_slow += ema_slow_alpha * (compression_ratio - ema_slow);
        ema_fast += ema_fast_alpha * (compression_ratio - ema_fast);
      }
      if (trace_ratio) {
        fprintf(ratio_log_file,
                "%s ratio: %f\tema_slow=%f\tema_fast=%f\tpage_bytes_read: "
                "%zu\tpage_bytes_written: %zu\n",
                do_encode ? "compression  " : "decompression",
                compression_ratio, ema_slow, ema_fast, page_bytes_read,
                page_bytes_written);
      }

      // Now consume our ratio information: should we start a new block?
      if (do_ratio && page_count >= 64 &&
          (ema_slow * 1.5 < ema_fast || compression_ratio > 0.8)) {
        fprintf(ratio_log_file, "resetting %d\n", page_count);
        lzw_emit_clear_code();
        lzw_destroy_state();
        lzw_init();
        break;
      }
    }
    total_stream_read += lzw_bytes_read;
    total_stream_written += lzw_bytes_written;
    lzw_destroy_state();
  }
}

// process_stream consumes all the globally-set parameters
void process_stream() {
  if (do_decode) {
    decode_stream();
  } else {
    encode_stream();
  }
}

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

void round_trip() {
  assert(user_output == stdout);
  user_output = fopen("roundtrip_result.dat", "w");
  assert(user_output);
  do_encode = true;
  do_decode = false;
  FILE *intermediate = tmpfile();
  lzw_output_file = intermediate;
  fprintf(stderr, "Encoding stream\n");
  process_stream();
  fflush(intermediate);
  fseek(intermediate, 0, SEEK_SET);
  lzw_input_file = intermediate;
  lzw_output_file = user_output;

  do_encode = false;
  do_decode = true;
  fprintf(stderr, "Decoding stream\n");
  process_stream();

  fclose(intermediate);
}

void dumpbytes(const char *d, size_t c) {
  for (size_t i = 0; i < c; i++) {
    if (i > 0 && i % 20 == 0) {
      fprintf(stderr, "\n");
    }
    fprintf(stderr, "%02x ", (unsigned char)d[i]);
  }
  fprintf(stderr, "\n");
}

void round_trip_in_memory(const char *Data, size_t Size) {
  char *encodechunks = NULL;
  size_t encodechunks_size = 0;
  init_streams((char *)Data, Size, &encodechunks, &encodechunks_size);
  do_encode = true;
  do_decode = false;
  // fprintf(stderr, "ENCODING\n");
  process_stream();
  close_streams();
  assert(total_stream_read == Size);
  assert(total_stream_written == encodechunks_size);

  char *decodechunks = NULL;
  size_t decodechunks_size;
  init_streams(encodechunks, encodechunks_size, &decodechunks,
               &decodechunks_size);
  do_encode = false;
  do_decode = true;
  // fprintf(stderr, "DECODING\n");
  process_stream();
  close_streams();
  // assert(total_stream_read == encodechunks_size);
  // fprintf(stderr, "encodechunks_size=%zu\n",encodechunks_size);
  // fprintf(stderr,
  // "decodechunks_size=%zu\ttotal_stream_written=%zu\tSize=%zu\n",decodechunks_size,
  // total_stream_written, Size); dumpbytes(Data, Size); dumpbytes(encodechunks,
  // encodechunks_size); dumpbytes(decodechunks, decodechunks_size);
  // assert(total_stream_written == Size);

  assert(Size == decodechunks_size);
  assert(!memcmp(decodechunks, Data, Size));

  free(encodechunks);
  free(decodechunks);
}

#ifdef FUZZ_MODE
int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  if (Size == 0) {
    return 0;
  }
  page_size = 7;
  lzw_max_key = 512;
  do_ratio = true;
  round_trip_in_memory((const char *)Data, Size);
  return 0;
}
#else
int main(int argc, char *argv[]) {
  char c;
  bool correctness_roundtrip = false;
  bool correctness_roundtrip_memory = false;

  user_input = stdin;
  user_output = stdout;

  while ((c = getopt(argc, argv, "deg:m:p:r:q:l:v:xcCb:i:o:")) != -1) {
    switch (c) {
    case 'd':
      do_decode = true;
      break;
    case 'e':
      do_encode = true;
      break;
    case 'g':
      lzw_set_debug_string(optarg);
      break;
    case 'm':
      lzw_max_key = atoi(optarg);
      break;
    case 'p':
      page_size = atoi(optarg);
      break;
    case 'q':
      trace_ratio = true;
      // if optarg is anything but '-'
      if (strcmp(optarg, "-")) {
        ratio_log_filename = strdup(optarg);
      }
      break;
    case 'v':
      verbosity = atoi(optarg);
      break;
    case 'x':
      do_ratio = true;
      break;
    case 'c': // for "correctness"
      correctness_roundtrip = true;
      break;
    case 'C': // for "correctness"
      correctness_roundtrip_memory = true;
      break;
    case 'i':
      user_input = fopen(optarg, "r");
      assert(user_input);
      break;
    case 'o':
      user_output = fopen(optarg, "wx");
      assert(user_output);
      break;
    default:
      break;
    }
  }

  if (correctness_roundtrip && correctness_roundtrip_memory) {
    printf("Error, can't do both in-memory and through-file roundtrip (cC)\n");
    return 2;
  }
  if (lzw_max_key && lzw_max_key < 256) {
    printf("Error, max key too small (need >= 256, got %u)\n", lzw_max_key);
    return 2;
  }

  if (trace_ratio && !do_ratio) {
    fprintf(stderr,
            "Warning, do_ratio=%s, trace_ratio=%s, unexpected behavior\n",
            do_ratio ? "true" : "false", trace_ratio ? "true" : "false");
  }

  if (verbosity) {
    fprintf(stderr, "lzw_max_key: %d\n", lzw_max_key);
    fprintf(stderr, "page_size  : %zu\n", page_size);
  }

  lzw_input_file = user_input;
  lzw_output_file = user_output;

  if (correctness_roundtrip) {
    round_trip();
  } else if (correctness_roundtrip_memory) {
    char *inputbuffer = NULL;
    size_t inputbuffer_size = 0;
    FILE *copy_file = open_memstream(&inputbuffer, &inputbuffer_size);
    char buffer[1028 * 1028];
    size_t n = 0;
    while ((n = fread(buffer, 1, sizeof(buffer), lzw_input_file)) > 0) {
      fwrite(buffer, 1, n, copy_file);
    }
    fclose(lzw_input_file);
    fclose(copy_file);
    round_trip_in_memory(inputbuffer, inputbuffer_size);
    free(inputbuffer);
  } else {
    if (do_encode == do_decode) {
      printf("Error, must uniquely choose encode or decode\n");
      return 1;
    }
    process_stream();
  }

  return 0;
}
#endif