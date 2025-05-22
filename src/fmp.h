/* FMP Tools - A library for reading FileMaker Pro databases
 * Copyright (c) 2020 Evan Miller (except where otherwise noted)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef INCLUDE_FMP_H
#define INCLUDE_FMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <iconv.h>
#include <stdint.h>
#include <time.h>

typedef enum {
    FMP_OK = 0,
    FMP_ERROR_OPEN,
    FMP_ERROR_READ,
    FMP_ERROR_SEEK,
    FMP_ERROR_MALLOC,
    FMP_ERROR_NO_FMEMOPEN,
    FMP_ERROR_BAD_MAGIC_NUMBER,
    FMP_ERROR_BAD_SECTOR,
    FMP_ERROR_BAD_SECTOR_COUNT,
    FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE,
    FMP_ERROR_INCOMPLETE_SECTOR,
    FMP_ERROR_UNRECOGNIZED_CODE,
    FMP_ERROR_UNSUPPORTED_CHARACTER_SET,
    FMP_ERROR_USER_ABORTED,
} fmp_error_t;

typedef enum {
    FMP_COLUMN_TYPE_UNKNOWN,
    FMP_COLUMN_TYPE_TEXT,
    FMP_COLUMN_TYPE_NUMBER,
    FMP_COLUMN_TYPE_DATE,
    FMP_COLUMN_TYPE_TIME,
    FMP_COLUMN_TYPE_CONTAINER,
    FMP_COLUMN_TYPE_CALC,
    FMP_COLUMN_TYPE_SUMMARY,
    FMP_COLUMN_TYPE_GLOBAL
} fmp_column_type_e;

typedef enum {
    FMP_COLLATION_ENGLISH = 0x00,
    FMP_COLLATION_FRENCH = 0x01,
    FMP_COLLATION_GERMAN = 0x03,
    FMP_COLLATION_ITALIAN = 0x04,
    FMP_COLLATION_DUTCH = 0x05,
    FMP_COLLATION_SWEDISH = 0x07,
    FMP_COLLATION_SPANISH = 0x08,
    FMP_COLLATION_DANISH = 0x09,
    FMP_COLLATION_PORTUGUESE = 0x0A,
    FMP_COLLATION_NORWEGIAN = 0x0C,
    FMP_COLLATION_FINNISH = 0x11,
    FMP_COLLATION_GREEK = 0x14,
    FMP_COLLATION_ICELANDIC = 0x15,
    FMP_COLLATION_TURKISH = 0x18,
    FMP_COLLATION_ROMANIAN = 0x27,
    FMP_COLLATION_POLISH = 0x2a,
    FMP_COLLATION_HUNGARIAN = 0x2b,
    FMP_COLLATION_RUSSIAN = 0x31,
    FMP_COLLATION_CZECH = 0x38,
    FMP_COLLATION_UKRAINIAN = 0x3e,
    FMP_COLLATION_CROATIAN = 0x42,
    FMP_COLLATION_CATALAN = 0x49,
    FMP_COLLATION_FINNISH_ALT = 0x62,
    FMP_COLLATION_SWEDISH_ALT = 0x63,
    FMP_COLLATION_GERMAN_ALT = 0x64,
    FMP_COLLATION_SPANISH_ALT = 0x65,
    FMP_COLLATION_ASCII = 0x66,
} fmp_column_collation_e;

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
    fmp_column_collation_e collation;
    char utf8_name[64];
} fmp_column_t;

typedef struct fmp_column_array_s {
    size_t count;
    fmp_column_t *columns;
} fmp_column_array_t;

typedef struct fmp_table_s {
    int index;
    int skip;
    char utf8_name[64];
} fmp_table_t;

typedef struct fmp_table_array_s {
    size_t count;
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
    uint16_t segment_index;
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
    char version_date_string[8];
    struct tm version_date;
    char filename[64];
    int version_num;
    size_t  file_size;
    size_t  sector_size;
    size_t  sector_index_shift;
    size_t  sector_head_len;
    size_t  prev_sector_offset;
    size_t  next_sector_offset;
    size_t  payload_len_offset;
    iconv_t converter;
    unsigned char    xor_mask;
    size_t path_level;
    size_t path_capacity;
    fmp_data_t **path;
    size_t num_blocks;
    fmp_block_t *blocks[];
} fmp_file_t;

typedef fmp_handler_status_t (*fmp_value_handler)(int row, fmp_column_t *column, const char *value, void *ctx);

fmp_file_t *fmp_open_file(const char *path, fmp_error_t *errorCode);
fmp_file_t *fmp_open_buffer(const void *buffer, size_t len, fmp_error_t *errorCode);

fmp_table_array_t *fmp_list_tables(fmp_file_t *file, fmp_error_t *errorCode);
fmp_column_array_t *fmp_list_columns(fmp_file_t *file, fmp_table_t *table, fmp_error_t *errorCode);
fmp_error_t fmp_read_values(fmp_file_t *file, fmp_table_t *table, fmp_value_handler handle_value, void *ctx);
fmp_error_t fmp_dump_file(fmp_file_t *file);

void fmp_close_file(fmp_file_t *file);
void fmp_free_tables(fmp_table_array_t *array);
void fmp_free_columns(fmp_column_array_t *array);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_FMP_H */
