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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "fmp.h"
#include "fmp_internal.h"

typedef struct fmp_read_values_ctx_s {
    size_t current_row;
    size_t last_row;
    char *long_string_buf;
    size_t long_string_len;
    size_t long_string_used;
    size_t target_table_index;
    size_t last_column;
    size_t num_columns;
    fmp_file_t *file;
    fmp_column_t *columns;
    fmp_value_handler handle_value;
    void *user_ctx;
} fmp_read_values_ctx_t;

static int path_is_long_string(fmp_chunk_t *chunk, fmp_read_values_ctx_t *ctx) {
    if (!table_path_match_start1(chunk, 3, 5))
        return 0;
    if (chunk->version_num < 7) {
        uint64_t column_index = path_value(chunk, chunk->path[2]);
        return path_is(chunk, chunk->path[1], ctx->last_row - (column_index == 1));
    }
    uint64_t column_index = path_value(chunk, chunk->path[3]);
    return path_is(chunk, chunk->path[2], ctx->last_row + (column_index == 1));
}

static int path_row(fmp_chunk_t *chunk) {
    if (chunk->version_num < 7)
        return path_value(chunk, chunk->path[1]);
    return path_value(chunk, chunk->path[2]);
}

static int path_is_table_data(fmp_chunk_t *chunk) {
    return table_path_match_start1(chunk, 2, 5);
}

static chunk_status_t process_value(fmp_chunk_t *chunk, fmp_read_values_ctx_t *ctx) {
    fmp_column_t *column = NULL;
    int long_string = 0;
    size_t column_index = 0;
    if (path_is_long_string(chunk, ctx)) {
        if (chunk->ref_simple == 0)
            return CHUNK_NEXT; /* Rich-text formatting */
        long_string = 1;
        column_index = path_value(chunk, chunk->path[chunk->path_level-1]);
    } else if (path_is_table_data(chunk) && chunk->ref_simple <= ctx->num_columns &&
               chunk->ref_simple != 252 /* Special metadata value? */) {
        column_index = chunk->ref_simple;
    }
    if (column_index == 0 || column_index > ctx->num_columns)
        return CHUNK_NEXT;

    column = &ctx->columns[column_index-1];

    if (column->index != ctx->last_column && ctx->long_string_used) {
        if (ctx->handle_value && ctx->handle_value(ctx->current_row,
                    &ctx->columns[ctx->last_column-1],
                    ctx->long_string_buf, ctx->user_ctx) == FMP_HANDLER_ABORT)
            return CHUNK_ABORT;

        ctx->long_string_used = 0;
    }
    if (path_row(chunk) != ctx->last_row || column->index < ctx->last_column) {
        ctx->current_row++;
    }
    char utf8_value[chunk->data.len*4+1];
    convert(ctx->file->converter, ctx->file->xor_mask,
            utf8_value, sizeof(utf8_value), chunk->data.bytes, chunk->data.len);
    if (long_string) {
        if (ctx->long_string_buf == NULL ||
                ctx->long_string_len < ctx->long_string_used + strlen(utf8_value) + 1) {
            ctx->long_string_len = ctx->long_string_used + strlen(utf8_value) + 1;
            ctx->long_string_buf = realloc(ctx->long_string_buf, ctx->long_string_len);
        }
        memcpy(&ctx->long_string_buf[ctx->long_string_used],
                utf8_value, strlen(utf8_value));
        ctx->long_string_used += strlen(utf8_value);
        ctx->long_string_buf[ctx->long_string_used] = '\0';
    } else if (ctx->handle_value) {
        if (ctx->handle_value(ctx->current_row, column, utf8_value, ctx->user_ctx) == FMP_HANDLER_ABORT)
            return CHUNK_ABORT;
    }
    ctx->last_row = path_row(chunk);
    ctx->last_column = column->index;
    return CHUNK_NEXT;
}

static chunk_status_t handle_chunk_read_values_v3(fmp_chunk_t *chunk, fmp_read_values_ctx_t *ctx) {
    if (path_value(chunk, chunk->path[0]) > 5)
        return CHUNK_DONE;

    if (chunk->type != FMP_CHUNK_FIELD_REF_SIMPLE)
        return CHUNK_NEXT;

    if (table_path_match_start2(chunk, 3, 3, 5)) {
        fmp_data_t *column_path = chunk->path[chunk->path_level-1];
        size_t column_index = path_value(chunk, column_path);
        if (column_index > ctx->num_columns) {
            ctx->num_columns = column_index;
            ctx->columns = realloc(ctx->columns, ctx->num_columns * sizeof(fmp_column_t));
        }
        fmp_column_t *current_column = ctx->columns + column_index - 1;
        if (chunk->ref_simple == 1) {
            convert(ctx->file->converter, ctx->file->xor_mask,
                    current_column->utf8_name, sizeof(current_column->utf8_name),
                    chunk->data.bytes, chunk->data.len);
            current_column->index = column_index;
        } else if (chunk->ref_simple == 2) {
            if (chunk->data.bytes[1] == 0x01) {
                current_column->type = FMP_COLUMN_TYPE_STRING;
            } else if (chunk->data.bytes[1] == 0x02) {
                current_column->type = FMP_COLUMN_TYPE_NUMBER;
            }
        }
        return CHUNK_NEXT;
    }
    return process_value(chunk, ctx);
}

static chunk_status_t handle_chunk_read_values_v7(fmp_chunk_t *chunk, fmp_read_values_ctx_t *ctx) {
    if (path_value(chunk, chunk->path[0]) > ctx->target_table_index + 128)
        return CHUNK_DONE;
    if (path_value(chunk, chunk->path[0]) < ctx->target_table_index + 128)
        return CHUNK_NEXT;
    if (chunk->type != FMP_CHUNK_FIELD_REF_SIMPLE)
        return CHUNK_NEXT;

    if (table_path_match_start2(chunk, 3, 3, 5)) {
        fmp_data_t *column_path = chunk->path[chunk->path_level-1];
        size_t column_index = path_value(chunk, column_path);
        if (column_index > ctx->num_columns) {
            ctx->num_columns = column_index;
            ctx->columns = realloc(ctx->columns, ctx->num_columns * sizeof(fmp_column_t));
        }
        fmp_column_t *current_column = ctx->columns + column_index - 1;
        if (chunk->ref_simple == 16) {
            convert(ctx->file->converter, ctx->file->xor_mask,
                    current_column->utf8_name, sizeof(current_column->utf8_name),
                    chunk->data.bytes, chunk->data.len);
            current_column->index = column_index;
        }
        return CHUNK_NEXT;
    }

    return process_value(chunk, ctx);
}

static chunk_status_t handle_chunk_read_values(fmp_chunk_t *chunk, void *ctx) {
    if (chunk->version_num >= 7)
        return handle_chunk_read_values_v7(chunk, ctx);
    return handle_chunk_read_values_v3(chunk, ctx);
}

fmp_error_t fmp_read_values(fmp_file_t *file, fmp_table_t *table, fmp_value_handler handle_value, void *user_ctx) {
    fmp_read_values_ctx_t *ctx = calloc(1, sizeof(fmp_read_values_ctx_t));
    ctx->target_table_index = table->index;
    ctx->handle_value = handle_value;
    ctx->file = file;
    ctx->user_ctx = user_ctx;
    fmp_error_t retval = process_blocks(file, NULL, handle_chunk_read_values, ctx);
    if (ctx->long_string_used && ctx->handle_value) {
        ctx->handle_value(ctx->current_row, &ctx->columns[ctx->last_column-1],
                ctx->long_string_buf, user_ctx);
        ctx->long_string_used = 0;
    }
    free(ctx->long_string_buf);
    free(ctx->columns);
    free(ctx);
    return retval;
}
