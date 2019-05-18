void encode(void);
void decode(void);
void lzw_init(void);
void lzw_destroy_state(void);

typedef void (*lzw_emitter_t)(uint8_t);
typedef uint32_t (*lzw_reader_t)(void);
extern lzw_emitter_t lzw_emitter;
extern lzw_reader_t lzw_reader;
extern lzw_emitter_t lzw_unget;

extern bool debugMode;
extern bool verboseMode;
extern bool doStats;
extern uint32_t lzw_max_key;
extern uint32_t lzw_length;
extern uint32_t lzw_next_key;
