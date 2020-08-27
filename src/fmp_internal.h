typedef enum {
    CHUNK_NEXT,
    CHUNK_DONE,
    CHUNK_ABORT
} chunk_status_t;

typedef int (*block_handler)(fmp_block_t *block, void *ctx);
typedef chunk_status_t (*chunk_handler)(fmp_chunk_t *chunk, void *ctx);

uint64_t path_value(fmp_chunk_t *chunk, fmp_data_t *path);
void debug(const char *fmt, ...);
fmp_error_t process_blocks(fmp_file_t *file,
        block_handler handle_block,
        chunk_handler handle_chunk,
        void *user_ctx);
fmp_error_t process_block(fmp_file_t *file, fmp_block_t *block);
fmp_block_t *new_block_from_sector(fmp_file_t *file, const uint8_t *sector);

void convert(fmp_file_t *file, char *dst, size_t dst_len,
        uint8_t *src, size_t src_len);

int table_path_match_start1(fmp_chunk_t *chunk, int depth, int val);
int table_path_match_start2(fmp_chunk_t *chunk, int depth, int val1, int val2);
int path_is(fmp_chunk_t *chunk, fmp_data_t *path, uint64_t value);
