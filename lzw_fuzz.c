#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include "lzw.h"

uint8_t* CompressedBuffer = NULL;
size_t cbwriter = 0;
size_t cbMAX = 0;
size_t cbcounter = 0;
void lzw_fuzzer_compress_emitter(uint8_t b) {
  if (cbMAX <= cbwriter) {
    cbMAX += 1024;
    CompressedBuffer = realloc(CompressedBuffer, cbMAX);
  }
  //fprintf(stderr, "%s (%lu) %X\n", __FUNCTION__, ++cbcounter, b);
  CompressedBuffer[cbwriter++] = b;
}

size_t fuzzerReader = 0;
size_t FuzzerSize = 0;
const uint8_t* FuzzerData = NULL;
uint32_t lzw_fuzzer_compress_reader() {
  if (fuzzerReader == FuzzerSize) return EOF;
  uint32_t r = FuzzerData[fuzzerReader++];
  //fprintf(stderr, "%s %X\n", __FUNCTION__, r);
  return r;
}

size_t cbreader = 0;
uint32_t lzw_fuzzer_decompress_reader() {
  if (cbreader == cbwriter) return EOF;
  uint32_t r = CompressedBuffer[cbreader++];
  //fprintf(stderr, "%s %X\n", __FUNCTION__, r);
  return r;
}

uint8_t* DecompressedBuffer = NULL;
size_t dbwriter = 0;
size_t dbMAX = 0;
void lzw_fuzzer_decompress_emitter(uint8_t b) {
  if (dbMAX <= dbwriter) {
    dbMAX += 1024;
    DecompressedBuffer = realloc(DecompressedBuffer, dbMAX);
  }
  //fprintf(stderr, "%s %X\n", __FUNCTION__, b);
  DecompressedBuffer[dbwriter++] = b;
}

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  FuzzerData = Data;
  FuzzerSize = Size;

  lzw_emitter = lzw_fuzzer_compress_emitter;
  lzw_reader = lzw_fuzzer_compress_reader;
  lzw_init();
  while (lzw_encode(1024));
  lzw_encode_end_stream();
  lzw_destroy_state();

  lzw_emitter = lzw_fuzzer_decompress_emitter;
  lzw_reader = lzw_fuzzer_decompress_reader;
  lzw_init();
  lzw_decode();
  lzw_destroy_state();

  if (!DecompressedBuffer) {
    assert(Size == 0);
    return 0;
  }

  //fprintf(stderr, "UncompressedCurrentSize = %lu, Size = %lu\n", dbwriter, Size);
  assert(dbwriter == Size);
  assert(!memcmp(Data, DecompressedBuffer, Size));

  cbwriter = 0;
  cbreader = 0;
  cbcounter = 0;
  dbwriter = 0;
  fuzzerReader = 0;
  return 0;
}
