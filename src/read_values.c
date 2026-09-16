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

/* FileMaker joins the repetitions of a repeating field with the ASCII group
 * separator when it exports or displays them as a single string. */
#define FMP_REPETITION_SEPARATOR '\x1D'

typedef struct fmp_read_values_ctx_s {
    size_t current_row;
    size_t last_row;
    size_t last_column;
    size_t last_repetition;
    int have_value;               /* a value is buffered but not yet delivered */
    unsigned char *raw_buf;       /* raw bytes of the repetition being assembled */
    size_t raw_capacity;
    size_t raw_used;
    char *utf8_buf;               /* UTF-8 text of the buffered value so far */
    size_t utf8_capacity;
    size_t utf8_used;
    size_t target_table_index;
    size_t num_columns;
    fmp_file_t *file;
    fmp_column_t *columns;
    fmp_value_handler handle_value;
    void *user_ctx;
} fmp_read_values_ctx_t;

static int path_is_table_data(fmp_chunk_t *chunk) {
    return table_path_match_start1(chunk, 2, 5);
}

static int path_is_column_data(fmp_chunk_t *chunk) {
    return table_path_match_start1(chunk, 3, 5);
}

static int path_row(fmp_chunk_t *chunk) {
    if (chunk->version_num < 7)
        return path_value(chunk, path_at(chunk, 1));
    return path_value(chunk, path_at(chunk, 2));
}

/* Decode the key (or path component) that identifies a value within a record.
 *
 * A key that is a valid column index refers to the first repetition of that
 * column. In fmp12 files, a two-byte key that exceeds the column count encodes
 * a (column, repetition) pair: the first byte is the column index and the
 * second byte is the repetition number (2 and up). See issue #20.
 *
 * Returns 0 in *column if the key does not identify a column. */
static void decode_column_key(fmp_chunk_t *chunk, uint64_t key, size_t key_len,
        size_t num_columns, size_t *column, size_t *repetition) {
    *column = 0;
    *repetition = 1;
    if (key >= 1 && key <= num_columns) {
        *column = key;
    } else if (chunk->version_num >= 7 && key_len == 2 && key >= 0x80) {
        size_t candidate = (key - 0x80) >> 8;
        size_t rep = (key - 0x80) & 0xFF;
        if (candidate >= 1 && candidate <= num_columns && rep >= 1) {
            *column = candidate;
            *repetition = rep;
        }
    }
}

static size_t key_len_for_chunk(fmp_chunk_t *chunk) {
    if (chunk->version_num < 7)
        return 1;
    if (chunk->type == FMP_CHUNK_DATA_SEGMENT)
        return chunk->code == 0x0F ? 2 : 1;
    return (chunk->code >= 0x09 && chunk->code <= 0x0E) ? 2 : 1;
}

/* Long values live at [table].[5].[row].[column]. Values of other kinds also
 * appear at that depth, so require the row to advance in step with the column. */
static int path_is_long_string(fmp_chunk_t *chunk, fmp_read_values_ctx_t *ctx, size_t column_index) {
    if (ctx->last_column == 0 || column_index < ctx->last_column) {
        return path_row(chunk) > ctx->last_row;
    }
    return path_row(chunk) == ctx->last_row;
}

static void append_bytes(unsigned char **buf, size_t *capacity, size_t *used,
        const void *bytes, size_t len) {
    if (*buf == NULL || *capacity < *used + len + 1) {
        *capacity = 2 * (*used + len + 1);
        *buf = realloc(*buf, *capacity);
    }
    memcpy(*buf + *used, bytes, len);
    *used += len;
    (*buf)[*used] = '\0';
}

/* Convert the raw bytes of the current repetition and append them to the
 * UTF-8 value being assembled. */
static void finish_repetition(fmp_read_values_ctx_t *ctx) {
    size_t utf8_len = 4 * ctx->raw_used + 1;
    if (ctx->utf8_buf == NULL || ctx->utf8_capacity < ctx->utf8_used + utf8_len) {
        ctx->utf8_capacity = 2 * (ctx->utf8_used + utf8_len);
        ctx->utf8_buf = realloc(ctx->utf8_buf, ctx->utf8_capacity);
    }
    convert(ctx->file->converter, ctx->file->xor_mask,
            ctx->utf8_buf + ctx->utf8_used, utf8_len, ctx->raw_buf, ctx->raw_used);
    ctx->utf8_used += strlen(ctx->utf8_buf + ctx->utf8_used);
    ctx->raw_used = 0;
}

static void append_separators(fmp_read_values_ctx_t *ctx, size_t count) {
    unsigned char separator = FMP_REPETITION_SEPARATOR;
    while (count--) {
        append_bytes((unsigned char **)&ctx->utf8_buf, &ctx->utf8_capacity, &ctx->utf8_used,
                &separator, 1);
    }
}

/* Deliver the buffered value to the handler. */
static chunk_status_t flush_value(fmp_read_values_ctx_t *ctx) {
    if (!ctx->have_value)
        return CHUNK_NEXT;
    finish_repetition(ctx);
    ctx->have_value = 0;
    ctx->utf8_used = 0;
    if (ctx->handle_value && ctx->handle_value(ctx->current_row,
                &ctx->columns[ctx->last_column-1], ctx->utf8_buf, ctx->user_ctx) == FMP_HANDLER_ABORT)
        return CHUNK_ABORT;
    return CHUNK_NEXT;
}

static chunk_status_t process_value(fmp_chunk_t *chunk, fmp_read_values_ctx_t *ctx) {
    fmp_column_t *column = NULL;
    int long_string = 0;
    size_t column_index = 0;
    size_t repetition = 1;
    if (path_is_column_data(chunk)) {
        fmp_data_t *column_path = path_at(chunk, chunk->path_level-1);
        decode_column_key(chunk, path_value(chunk, column_path), column_path->len,
                ctx->num_columns, &column_index, &repetition);
        if (column_index == 0 || !path_is_long_string(chunk, ctx, column_index))
            return CHUNK_NEXT;
        if (chunk->type == FMP_CHUNK_FIELD_REF_SIMPLE && chunk->ref_simple == 0)
            return CHUNK_NEXT; /* Rich-text formatting */
        long_string = 1;
    } else if (path_is_table_data(chunk)) {
        if (chunk->type == FMP_CHUNK_FIELD_REF_SIMPLE) {
            if (chunk->ref_simple == 252) /* Special metadata value? */
                return CHUNK_NEXT;
            decode_column_key(chunk, chunk->ref_simple, key_len_for_chunk(chunk),
                    ctx->num_columns, &column_index, &repetition);
        } else if (chunk->type == FMP_CHUNK_DATA_SEGMENT) {
            decode_column_key(chunk, chunk->segment_index, key_len_for_chunk(chunk),
                    ctx->num_columns, &column_index, &repetition);
        }
    }
    if (column_index == 0) {
        return CHUNK_NEXT;
    }

    column = &ctx->columns[column_index-1];

    if (column->index == 0) {
        /* No name was found for this column; consumers address columns by
         * index and name, so there is nothing sensible to deliver */
        return CHUNK_NEXT;
    }

    size_t row = path_row(chunk);
    if (ctx->have_value && (column_index != ctx->last_column || row != ctx->last_row)) {
        /* A different field: deliver what we have */
        if (flush_value(ctx) == CHUNK_ABORT)
            return CHUNK_ABORT;
    } else if (ctx->have_value && repetition != ctx->last_repetition) {
        /* Next repetition of the same field */
        finish_repetition(ctx);
        append_separators(ctx, repetition > ctx->last_repetition ? repetition - ctx->last_repetition : 1);
    } else if (ctx->have_value && !long_string) {
        /* A second complete value for the same field: deliver the first on its own */
        if (flush_value(ctx) == CHUNK_ABORT)
            return CHUNK_ABORT;
    }
    if (row != ctx->last_row || column_index < ctx->last_column) {
        ctx->current_row++;
    }
    if (!ctx->have_value) {
        /* Leave empty slots for any repetitions that precede this one */
        append_separators(ctx, repetition - 1);
        ctx->have_value = 1;
    }
    append_bytes(&ctx->raw_buf, &ctx->raw_capacity, &ctx->raw_used, chunk->data.bytes, chunk->data.len);
    ctx->last_row = row;
    ctx->last_column = column_index;
    ctx->last_repetition = repetition;
    return CHUNK_NEXT;
}

static chunk_status_t handle_chunk_read_values_v3(fmp_chunk_t *chunk, fmp_read_values_ctx_t *ctx) {
    if (path_value(chunk, path_at(chunk, 0)) > 5)
        return CHUNK_DONE;

    if (chunk->type != FMP_CHUNK_FIELD_REF_SIMPLE)
        return CHUNK_NEXT;

    if (table_path_match_start2(chunk, 3, 3, 5)) {
        fmp_data_t *column_path = path_at(chunk, chunk->path_level-1);
        size_t column_index = path_value(chunk, column_path);
        if (column_index == 0 || column_index > FMP_MAX_INDEX)
            return CHUNK_NEXT;
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
            if (chunk->data.bytes[1] <= FMP_COLUMN_TYPE_GLOBAL) {
                current_column->type = chunk->data.bytes[1];
            } else {
                current_column->type = FMP_COLUMN_TYPE_UNKNOWN;
            }
        }
        return CHUNK_NEXT;
    }
    return process_value(chunk, ctx);
}

static chunk_status_t handle_chunk_read_values_v7(fmp_chunk_t *chunk, fmp_read_values_ctx_t *ctx) {
    if (path_value(chunk, path_at(chunk, 0)) > ctx->target_table_index + 128)
        return CHUNK_DONE;
    if (path_value(chunk, path_at(chunk, 0)) < ctx->target_table_index + 128)
        return CHUNK_NEXT;
    if (chunk->type != FMP_CHUNK_FIELD_REF_SIMPLE && chunk->type != FMP_CHUNK_DATA_SEGMENT)
        return CHUNK_NEXT;

    int first = 0;
    if (chunk->path_level >= 4 && path_is(chunk, path_at(chunk, 1), 3) && path_is(chunk, path_at(chunk, 2), 5) &&
            name_chunk(chunk, 4, &first)) {
        size_t column_index = path_value(chunk, path_at(chunk, 3));
        if (column_index == 0 || column_index > FMP_MAX_INDEX)
            return CHUNK_NEXT;
        if (column_index > ctx->num_columns) {
            size_t old_num_columns = ctx->num_columns;
            ctx->num_columns = column_index;
            ctx->columns = realloc(ctx->columns, ctx->num_columns * sizeof(fmp_column_t));
            memset(&ctx->columns[old_num_columns], 0, (column_index - old_num_columns) * sizeof(fmp_column_t));
        }
        fmp_column_t *current_column = ctx->columns + column_index - 1;
        append_name(ctx->file, chunk, first, current_column->utf8_name, sizeof(current_column->utf8_name));
        current_column->index = column_index;
        return CHUNK_NEXT;
    }
    if (table_path_match_start2(chunk, 3, 3, 5) || table_path_match_start2(chunk, 4, 3, 5))
        return CHUNK_NEXT; /* other column metadata */

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
    if (retval != FMP_ERROR_USER_ABORTED)
        flush_value(ctx);
    free(ctx->raw_buf);
    free(ctx->utf8_buf);
    free(ctx->columns);
    free(ctx);
    return retval;
}
