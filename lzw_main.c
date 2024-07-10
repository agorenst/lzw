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
char *ratio_log_filename = NULL;

int verbosity = 0;
int block_extraction = -1;

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

FILE* copy_lzw_reader_file;
int copy_lzw_reader(void) {
  if (trim_input) {
    return EOF;
  }
  int c = fgetc(lzw_input_file);
  if (c != EOF) {
    fputc(c, copy_lzw_reader_file);
  }
  return c;
}

// process_stream consumes all the globally-set parameters
void process_stream() {
  FILE *ratio_log_file = stderr;
  if (ratio_log_filename) {
    ratio_log_file = fopen(ratio_log_filename, "w");
  }

  for (int block_count = 0;; block_count++) {
    if (block_count == block_extraction) {
      char blockfile_name[256];
      sprintf(blockfile_name, "block_%d.lzw", block_extraction);
      lzw_output_file = fopen(blockfile_name, "w");
    }
    fprintf(stderr, "processing block: %d\n", block_count);
    reset_written();
    lzw_init();

    double ema_slow = 0.0;
    double ema_slow_alpha = 0.0005;
    double ema_fast = 0.0;
    double ema_fast_alpha = 0.05;

    for (int page_count = 0;; page_count++) {
      size_t bytes_processed = 0;
      if (do_encode) {
        bytes_processed = lzw_encode(page_size);
      } else {
        bytes_processed = lzw_decode(page_size);
      }
      // We've hit EOF.
      if (!bytes_processed) {
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
                //"%s ratio: %f\tlzw_bytes_read: %zu\tlzw_bytes_written: %zu\n",
                "%s ratio: %f\tema_slow=%f\tema_fast=%f\tpage_bytes_read: "
                "%zu\tpage_bytes_written: %zu\n",
                do_encode ? "compression  " : "decompression",
                compression_ratio, ema_slow, ema_fast, page_bytes_read,
                page_bytes_written);
      }

      // Now consume our ratio information: should we start a new block?
      if (page_count >= 64 &&
          (ema_slow * 1.5 < ema_fast || compression_ratio > 0.8)) {
        fprintf(ratio_log_file, "resetting %d\n", page_count);
        trim_input = true;
        // Force a flush, basically.
        if (do_encode) {
          // assert(lzw_encode(page_size));
          while (lzw_encode(page_size))
            ;
        } else {
          // assert(lzw_decode(page_size));
          while (lzw_decode(page_size))
            ;
        }
        trim_input = false;
        break;
      }
    }
    lzw_destroy_state();
    if (block_count == block_extraction) {
      return;
    }
  }
}

int main(int argc, char *argv[]) {
  char c;
  bool do_ratio = false;
  bool correctness_roundtrip = false;
  while ((c = getopt(argc, argv, "deg:m:p:r:q:l:v:xc:b:")) != -1) {
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
      case 'b':
      block_extraction = atoi(optarg);
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
      // if optarg is anything but '-'
      if (strcmp(optarg, "-")) {
        ratio_log_filename = strdup(optarg);
      }
      break;
    case 'l':
      decompressed_byte_limit = atoi(optarg);
      break;
    case 'v':
      verbosity = atoi(optarg);
      break;
    case 'x':
      do_ratio = true;
    break;
    case 'c': // for "correctness"
    correctness_roundtrip = true;
    default:
      break;
    }
  }
  if (lzw_max_key && lzw_max_key < 256) {
    printf("Error, max key too small (need >= 256, got %u)\n", lzw_max_key);
    return 2;
  }

  if (do_ratio != trace_ratio) {
    fprintf(stderr, "Warning, do_ratio=%s, trace_ratio=%s, unexpected behavior",
    do_ratio ? "true" : "false",
    trace_ratio ? "true" : "false");
  }

  if (page_size == 0) {
    page_size = 4096;
  }

  if (verbosity) {
    fprintf(stderr, "lzw_max_key: %d\n", lzw_max_key);
    fprintf(stderr, "page_size  : %zu\n", page_size);
  }

  if (correctness_roundtrip) {
    assert(!do_encode && !do_decode);
    // Have us emit to an in-memory buffer.
    char *compressedbuffer = NULL;
    size_t compressedbuffer_size = 0;
    lzw_output_file = open_memstream(&compressedbuffer, &compressedbuffer_size);
    // Listen in to our input and copy it to a local file.
    char *inputbuffer = NULL;
    size_t inputbuffer_size = 0;
    lzw_input_file = stdin;
    copy_lzw_reader_file = open_memstream(&inputbuffer, &inputbuffer_size);
    lzw_reader = copy_lzw_reader;

    // Run the compression
    fprintf(stderr, "Doing encoding\n");
    do_encode = true;
    process_stream();
    fclose(lzw_output_file);
    fclose(copy_lzw_reader_file);

    // Now we restart: our input is now the compressed buffer
    FILE *in = fmemopen(compressedbuffer, compressedbuffer_size, "r");
    lzw_input_file = in;
    if (do_ratio) {
      lzw_reader = ratio_based_reader;
    } else {
      lzw_reader = lzw_default_reader;
    }
    // We output to an in-memory buffer took
    char *decompress_out = NULL;
    size_t decompress_out_size = 0;
    lzw_output_file = open_memstream(&decompress_out, &decompress_out_size);
    assert(lzw_output_file);
    do_encode = false;
    do_decode = true;
    fprintf(stderr, "Doing decoding\n");
    process_stream();
    fclose(lzw_input_file);
    fclose(lzw_output_file);

    fprintf(stderr, "Doing final comparison\n");
    // Now do the final comparison
    assert(decompress_out_size == inputbuffer_size);
    assert(!memcmp(inputbuffer, decompress_out, decompress_out_size));
    free(compressedbuffer);
    free(inputbuffer);
    free(decompress_out);
  } else {
    if (do_encode == do_decode) {
      printf("Error, must uniquely choose encode or decode\n");
      return 1;
    }
    lzw_input_file = stdin;
    lzw_output_file = stdout;
    if (do_ratio) {
      lzw_reader = ratio_based_reader;
    }
    process_stream();
  }

  return 0;
}
