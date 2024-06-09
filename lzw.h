#include <stdint.h>

void lzw_encode(void);
void lzw_decode(void);
void lzw_init(void);
void lzw_destroy_state(void);

extern FILE* lzw_input_file;
extern FILE* lzw_output_file;
extern uint32_t lzw_max_key;

extern uint64_t lzw_bytes_written;
extern uint64_t lzw_bytes_read;

extern int lzw_debug_level;
