#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>
#include <stdarg.h>
#include <libgen.h>

#include "fmp.h"
#include "fmp_internal.h"

#define MAGICK "\x00\x01\x00\x00\x00\x02\x00\x01\x00\x05\x00\x02\x00\x02\xC0"

typedef struct fmp_ctx_s {
    int did_print_current_path;
    fmp_column_t *current_column;
    fmp_table_t *current_table;
    size_t current_row;
    size_t last_column;
    size_t num_tables;
    fmp_table_t *tables;
    size_t num_columns;
    fmp_column_t *columns;
    void *user_ctx;
} fmp_ctx_t;

void copy_pascal_string(char *dst, size_t dst_len, const void *buf) {
    const char *chars = (const char *)buf;
    unsigned char len = chars[0];
    memcpy(dst, &chars[1], len);
    dst[len] = '\0';
}

void debug(const char *fmt, ...) {
    va_list argp;
    va_start(argp, fmt);
    vfprintf(stdout, fmt, argp);
    va_end(argp);
}

fmp_error_t read_header(fmp_file_t *ctx) {
    char buf[1024];

    if (!fread(buf, sizeof(buf), 1, ctx->stream))
        return FMP_ERROR_READ;

    if (memcmp(buf, MAGICK, sizeof(MAGICK)-1)) {
        fprintf(stderr, "Bad magic number!\n");
        return FMP_ERROR_BAD_MAGIC_NUMBER;
    }

    if (memcmp(&buf[15], "HBAM7", 5) == 0) {
        // or 4096
        ctx->sector_size = 4096;
        ctx->version_num = 7;
        ctx->xor_mask = 0x5A;
        ctx->prev_sector_offset = 4;
        ctx->next_sector_offset = 8;
        ctx->payload_len_offset = -1;
        ctx->sector_head_len = 20;
        ctx->converter = iconv_open("UTF-8", "WINDOWS-1252");
        /* Big-endian flag somewhere? */
    } else {
        ctx->sector_size = 1024;
        ctx->prev_sector_offset = 2;
        ctx->next_sector_offset = 6;
        ctx->payload_len_offset = 12;
        ctx->sector_head_len = 14;
        ctx->sector_index_shift = 1;
        ctx->converter = iconv_open("UTF-8", "MACROMAN");
    }

    if (buf[521] == 0x1E) {
        ctx->version_num = 12;
    }

    copy_pascal_string(ctx->version_string, sizeof(ctx->version_string), &buf[541]);

    // Throwaway sector
    if (ctx->sector_size == 1024) {
        fseek(ctx->stream, 2 * ctx->sector_size, SEEK_SET);
    } else {
        fseek(ctx->stream, ctx->sector_size, SEEK_SET);
    }

    return FMP_OK;
}

uint64_t path_value(fmp_chunk_t *chunk, fmp_data_t *path) {
    if (!path)
        return 0;
    if (path->len == 1)
        return path->bytes[0];
    if (path->len == 2)
        return 0x80 + ((path->bytes[0] & 0x7F) << 8) + path->bytes[1];
    if (path->len == 3) {
        if (chunk->version_num < 7)
            return 0xC000 + ((path->bytes[0] & 0x3F) << 16) + (path->bytes[1] << 8) + path->bytes[2];
        return 0x80 + (path->bytes[1] << 8) + path->bytes[2];
    }
    return 0;
}

int path_is(fmp_chunk_t *chunk, fmp_data_t *path, uint64_t value) {
    return path_value(chunk, path) == value;
}

void convert(fmp_file_t *file, char *dst, size_t dst_len,
        uint8_t *src, size_t src_len) {
    char *input_bytes = (char *)src;
    char *output_bytes = dst;
    size_t input_bytes_left = src_len;
    size_t output_bytes_left = dst_len;
    if (file->xor_mask) {
        input_bytes = malloc(input_bytes_left);
        for (int i=0; i<input_bytes_left; i++) {
            input_bytes[i] = file->xor_mask ^ src[i];
        }
        src = (uint8_t *)input_bytes;
    }
    while (input_bytes_left && input_bytes[0] == ' ') {
        input_bytes++;
        input_bytes_left--;
    }
    iconv(file->converter, &input_bytes, &input_bytes_left, &output_bytes, &output_bytes_left);
    if (file->xor_mask) {
        free(src);
    }
    dst[dst_len-output_bytes_left] = '\0';
}

int table_path_depth(fmp_chunk_t *chunk) {
    if (chunk->version_num < 7)
        return chunk->path_level;
    return chunk->path_level - 1;
}

int table_path_match_start1(fmp_chunk_t *chunk, int depth, int val) {
    if (table_path_depth(chunk) != depth)
        return 0;
    if (chunk->version_num < 7)
        return path_is(chunk, chunk->path[0], val);
    return path_value(chunk, chunk->path[0]) >= 128 && path_is(chunk, chunk->path[1], val);
}

int table_path_match_start2(fmp_chunk_t *chunk, int depth, int val1, int val2) {
    if (table_path_depth(chunk) != depth)
        return 0;
    if (chunk->version_num < 7)
        return path_is(chunk, chunk->path[0], val1) && path_is(chunk, chunk->path[1], val2);
    return (path_value(chunk, chunk->path[0]) >= 128 &&
            path_is(chunk, chunk->path[1], val1) && path_is(chunk, chunk->path[2], val2));
}

chunk_status_t process_chunk(fmp_file_t *file, fmp_chunk_t *chunk,
        chunk_handler handle_chunk, void *user_ctx) {
    chunk->path = file->path;
    chunk->path_level = file->path_level;
    chunk->version_num = file->version_num;
    if (chunk->type == FMP_CHUNK_PATH_POP) {
        if (file->path_level)
            file->path_level--;
    }
    if (chunk->type == FMP_CHUNK_PATH_PUSH) {
        file->path[file->path_level++] = &chunk->data;
    }
    return handle_chunk(chunk, user_ctx);
}

fmp_error_t process_chunk_chain(fmp_file_t *file, fmp_chunk_t *chunk,
        chunk_handler handle_chunk, void *user_ctx) {
    file->path_level = 0;
    while (chunk) {
        chunk_status_t status = process_chunk(file, chunk, handle_chunk, user_ctx);
        if (status == CHUNK_ABORT)
            return FMP_ERROR_USER_ABORTED;
        if (status == CHUNK_DONE)
            break;
        if (status == CHUNK_NEXT)
            chunk = chunk->next;
    }
    return FMP_OK;
}

fmp_error_t process_blocks(fmp_file_t *file,
        block_handler handle_block,
        chunk_handler handle_chunk,
        void *user_ctx) {
    fmp_error_t retval = FMP_OK;
    fmp_block_t *block = file->blocks[0];
    /*
    process_block(file, block);
    if (!handle_block || handle_block(block, user_ctx))
        process_chunk_chain(file, block->chunk, handle_chunk, user_ctx);
        */
    int next_block = 2;
    do {
        fmp_block_t *block = file->blocks[next_block-1];
        process_block(file, block);
        block->this_id = next_block;
        if (!handle_block || handle_block(block, user_ctx))
            retval = process_chunk_chain(file, block->chunk, handle_chunk, user_ctx);
        next_block = block->next_id;
    } while (next_block != 0 && next_block - 1 < file->num_blocks && retval == FMP_OK);

    return retval;
}

fmp_file_t *fmp_open_file(const char *path, fmp_error_t *errorCode) {
    FILE *stream = fopen(path, "r");
    if (!stream) {
        if (errorCode)
            *errorCode = FMP_ERROR_OPEN;
        return NULL;
    }
    fmp_file_t *file = calloc(1, sizeof(fmp_file_t));
    file->stream = stream;

    char *path_copy = strdup(path);
    snprintf(file->filename, sizeof(file->filename), "%s", basename(path_copy));
    free(path_copy);

    fmp_error_t retval = read_header(file);
    if (errorCode)
        *errorCode = retval;

    uint8_t *sector = malloc(file->sector_size);
    fread(sector, file->sector_size, 1, file->stream);

    fmp_block_t *block = new_block_from_sector(file, sector);

    file = realloc(file, sizeof(fmp_file_t) + block->next_id * sizeof(fmp_block_t *));
    file->num_blocks = block->next_id;
    file->blocks[0] = block;

    int index = 1;
    while (fread(sector, file->sector_size, 1, file->stream)) {
        file->blocks[index++] = new_block_from_sector(file, sector);
    }

    free(sector);

    return file;
}

void fmp_close_file(fmp_file_t *file) {
    if (file->stream)
        fclose(file->stream);
    for (int i=0; i<file->num_blocks; i++) {
        free(file->blocks[i]);
    }
    free(file);
}
