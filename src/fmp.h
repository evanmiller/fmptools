#include <iconv.h>

typedef enum {
    FMP_OK = 0,
    FMP_ERROR_OPEN,
    FMP_ERROR_READ,
    FMP_ERROR_BAD_MAGIC_NUMBER,
    FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE,
    FMP_ERROR_INCOMPLETE_SECTOR,
    FMP_ERROR_UNRECOGNIZED_CODE,
    FMP_ERROR_USER_ABORTED
} fmp_error_t;

typedef enum {
    FMP_COLUMN_TYPE_STRING,
    FMP_COLUMN_TYPE_NUMBER
} fmp_column_type_e;

typedef enum {
    FMP_HANDLER_OK,
    FMP_HANDLER_SKIP,
    FMP_HANDLER_ABORT
} fmp_handler_status_t;

typedef enum {
    FMP_CHUNK_PATH_PUSH,
    FMP_CHUNK_PATH_POP,
    FMP_CHUNK_DATA_SIMPLE,
    FMP_CHUNK_FIELD_REF_SIMPLE,
    FMP_CHUNK_FIELD_REF_LONG,
    FMP_CHUNK_DATA_SEGMENT,
} fmp_chunk_type_t;

typedef struct fmp_column_s {
    int index;
    fmp_column_type_e type;
    char utf8_name[64];
} fmp_column_t;

typedef struct fmp_column_array_s {
    size_t num_columns;
    fmp_column_t columns[];
} fmp_column_array_t;

typedef struct fmp_table_s {
    int index;
    int skip;
    char utf8_name[64];
} fmp_table_t;

typedef struct fmp_table_array_s {
    size_t num_tables;
    fmp_table_t *tables;
} fmp_table_array_t;

typedef struct fmp_data_s {
    size_t len;
    uint8_t *bytes;
} fmp_data_t;

typedef struct fmp_chunk_s {
    struct fmp_chunk_s *next;
    fmp_data_t ref_long;
    fmp_data_t data;
    fmp_chunk_type_t type;
    fmp_data_t **path;
    uint8_t path_level;
    uint8_t version_num;
    uint8_t code;
    uint8_t segment_index;
    uint16_t ref_simple;
    unsigned int extended:1;
} fmp_chunk_t;

typedef struct fmp_block_s {
    int deleted;
    int level;
    int id;
    int next_id;
    int prev_id;
    int this_id;
    fmp_chunk_t *chunk;
    size_t payload_len;
    uint8_t payload[];
} fmp_block_t;

typedef struct fmp_file_s {
    FILE *stream;
    char version_string[10];
    char filename[64];
    int version_num;
    size_t  sector_size;
    size_t  sector_index_shift;
    size_t  sector_head_len;
    off_t   prev_sector_offset;
    off_t   next_sector_offset;
    off_t   payload_len_offset;
    iconv_t converter;
    unsigned char    xor_mask;
    size_t path_level;
    fmp_data_t *path[10];
    size_t num_blocks;
    fmp_block_t *blocks[];
} fmp_file_t;

typedef fmp_handler_status_t (*fmp_value_handler)(int row, fmp_column_t *column, const char *value, void *ctx);

fmp_file_t *fmp_open_file(const char *path, fmp_error_t *errorCode);
fmp_table_array_t *fmp_list_tables(fmp_file_t *file, fmp_error_t *errorCode);
fmp_column_array_t *fmp_list_columns(fmp_file_t *file, fmp_table_t *table, fmp_error_t *errorCode);
fmp_error_t fmp_read_values(fmp_file_t *file, fmp_table_t *table, fmp_value_handler handle_value, void *ctx);
fmp_error_t fmp_dump_file(fmp_file_t *file);
void fmp_close_file(fmp_file_t *file);
