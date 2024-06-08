#include <stdint.h>

size_t lzw_encode(size_t);
void lzw_encode_end_stream(void);
void lzw_decode(void);
void lzw_init(void);
void lzw_destroy_state(void);

typedef void (*lzw_emitter_t)(uint8_t);
typedef uint32_t (*lzw_reader_t)(void);
void lzw_default_emitter(uint8_t);
uint32_t lzw_default_reader(void);

uint32_t lzw_buffer_reader(void);
void lzw_initialize_reader_buffer(char*, size_t);

extern lzw_emitter_t lzw_emitter;
extern lzw_reader_t lzw_reader;
extern uint32_t lzw_max_key;
extern uint32_t lzw_length;
extern uint32_t lzw_next_key;

extern int lzw_debug_level;
