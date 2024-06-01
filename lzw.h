typedef struct lzw_stream_t lzw_stream_t, *lzw_stream_p;

// We encode the output as a sequence of bits, which can cause
// complications if we need to say, emit, 13 bits.
// We store things 8 bits at a time, to match fputc.
typedef void (*lzw_emitter_t)(uint8_t);
typedef uint32_t (*lzw_reader_t)(void);

void lzw_default_emitter(uint8_t b);
uint32_t lzw_default_reader(void);

size_t lzw_encode(lzw_stream_p, size_t);
size_t lzw_decode(lzw_stream_p, size_t);
lzw_stream_p lzw_init(int, lzw_reader_t, lzw_emitter_t);
void lzw_destroy_state(lzw_stream_p);

extern int lzw_debug_level;

