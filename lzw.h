#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

size_t lzw_encode(size_t);
void lzw_encode_end(void);
size_t lzw_decode(size_t);
void lzw_init(void);
void lzw_destroy_state(void);
void lzw_write_clear_code(void);

void lzw_set_debug_string(const char*);

extern FILE* lzw_input_file;
extern FILE* lzw_output_file;
extern uint32_t lzw_max_key;

extern uint64_t lzw_bytes_written;
extern uint64_t lzw_bytes_read;