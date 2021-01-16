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

typedef struct fmp_list_columns_ctx_s {
    size_t target_table_index;
    fmp_file_t *file;
    fmp_column_array_t *array;
} fmp_list_columns_ctx_t;

static chunk_status_t handle_column(size_t column_index, fmp_data_t *name, fmp_list_columns_ctx_t *ctx) {
    fmp_column_array_t *array = ctx->array;
    if (column_index > array->count) {
        size_t old_num_columns = array->count;
        array->count = column_index;
        array->columns = realloc(array->columns, array->count * sizeof(fmp_column_t));
        memset(&array->columns[old_num_columns], 0, (column_index - old_num_columns) * sizeof(fmp_column_t));
    }
    fmp_column_t *current_column = array->columns + column_index - 1;
    convert(ctx->file->converter, ctx->file->xor_mask,
            current_column->utf8_name, sizeof(current_column->utf8_name),
            name->bytes, name->len);
    current_column->index = column_index;

    return CHUNK_NEXT;
}

static chunk_status_t handle_chunk_list_columns_v3(fmp_chunk_t *chunk, fmp_list_columns_ctx_t *ctx) {
    if (path_value(chunk, chunk->path[0]) > 3)
        return CHUNK_DONE;

    if (chunk->type != FMP_CHUNK_FIELD_REF_SIMPLE)
        return CHUNK_NEXT;

    if (table_path_match_start2(chunk, 3, 3, 5)) {
        fmp_data_t *column_path = chunk->path[chunk->path_level-1];
        size_t column_index = path_value(chunk, column_path);
        if (chunk->ref_simple == 1) {
            return handle_column(column_index, &chunk->data, ctx);
        }
        fmp_column_t *current_column = NULL;
        if (column_index > 0 && column_index <= ctx->array->count)
            current_column = ctx->array->columns + column_index - 1;
        if (current_column && chunk->ref_simple == 2) {
            if (chunk->data.bytes[1] <= FMP_COLUMN_TYPE_GLOBAL) {
                current_column->type = chunk->data.bytes[1];
            } else {
                current_column->type = FMP_COLUMN_TYPE_UNKNOWN;
            }
        }
    }
    return CHUNK_NEXT;
}

static chunk_status_t handle_chunk_list_columns_v7(fmp_chunk_t *chunk, fmp_list_columns_ctx_t *ctx) {
    if (path_value(chunk, chunk->path[0]) > ctx->target_table_index + 128)
        return CHUNK_DONE;
    if (path_value(chunk, chunk->path[0]) < ctx->target_table_index + 128)
        return CHUNK_NEXT;
    if (chunk->type != FMP_CHUNK_FIELD_REF_SIMPLE)
        return CHUNK_NEXT;

    if (table_path_match_start2(chunk, 3, 3, 5)) {
        fmp_data_t *column_path = chunk->path[chunk->path_level-1];
        size_t column_index = path_value(chunk, column_path);
        if (chunk->ref_simple == 16) {
            handle_column(column_index, &chunk->data, ctx);
        }
    }
    return CHUNK_NEXT;
}

static chunk_status_t handle_chunk_list_columns(fmp_chunk_t *chunk, void *ctx) {
    if (chunk->version_num >= 7)
        return handle_chunk_list_columns_v7(chunk, (fmp_list_columns_ctx_t *)ctx);

    return handle_chunk_list_columns_v3(chunk, (fmp_list_columns_ctx_t *)ctx);
}

fmp_column_array_t *fmp_list_columns(fmp_file_t *file, fmp_table_t *table, fmp_error_t *errorCode) {
    fmp_column_array_t *array = calloc(1, sizeof(fmp_column_array_t));
    fmp_list_columns_ctx_t ctx = {
        .array = array, .file = file,
        .target_table_index = table->index
    };
    fmp_error_t retval = process_blocks(file, NULL, &handle_chunk_list_columns, &ctx);
    int j=0; // squash
    for (int i=0; i<array->count; i++) {
        if (array->columns[i].index) {
            if (i!=j) {
                memmove(&array->columns[j], &array->columns[i], sizeof(fmp_column_t));
            }
            j++;
        }
    }
    array->count = j;
    if (errorCode)
        *errorCode = retval;
    return ctx.array;
}

void fmp_free_columns(fmp_column_array_t *array) {
    if (array) {
        free(array->columns);
        free(array);
    }
}
