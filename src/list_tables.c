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

typedef struct fmp_list_tables_ctx_s {
    fmp_file_t *file;
    fmp_table_array_t *array;
} fmp_list_tables_ctx_t;

static chunk_status_t handle_chunk_list_tables_v7(fmp_chunk_t *chunk, void *ctxp) {
    fmp_list_tables_ctx_t *ctx = (fmp_list_tables_ctx_t *)ctxp;

    if (path_value(chunk, chunk->path[0]) > 3)
        return CHUNK_DONE;

    if (chunk->type != FMP_CHUNK_FIELD_REF_SIMPLE)
        return CHUNK_NEXT;

    if (path_is(chunk, chunk->path[0], 3) && path_is(chunk, chunk->path[1], 16) &&
            path_is(chunk, chunk->path[2], 5) && path_value(chunk, chunk->path[3]) >= 128) {
        fmp_data_t *table_path = chunk->path[chunk->path_level-1];
        size_t table_index = path_value(chunk, table_path) - 128;
        fmp_table_array_t *array = ctx->array;
        if (table_index > array->count) {
            size_t old_count = array->count;
            array->count = table_index;
            array->tables = realloc(array->tables, array->count * sizeof(fmp_table_t));
            memset(&array->tables[old_count], 0, (table_index - old_count) * sizeof(fmp_table_t));
        }
        fmp_table_t *current_table = array->tables + table_index - 1;
        if (chunk->ref_simple == 16) {
            convert(ctx->file, current_table->utf8_name, sizeof(current_table->utf8_name),
                    chunk->data.bytes, chunk->data.len);
            current_table->index = table_index;
        }
    }
    return CHUNK_NEXT;
}

fmp_table_array_t *fmp_list_tables(fmp_file_t *file, fmp_error_t *errorCode) {
    fmp_table_array_t *array = calloc(1, sizeof(fmp_table_array_t));
    fmp_error_t retval = FMP_OK;
    if (file->version_num >= 7) {
        fmp_list_tables_ctx_t ctx = { .array = array, .file = file };
        retval = process_blocks(file, NULL, handle_chunk_list_tables_v7, &ctx);
        int j=0;
        for (int i=0; i<array->count; i++) {
            if (array->tables[i].index) {
                if (i!=j) {
                    memmove(&array->tables[j], &array->tables[i], sizeof(fmp_table_t));
                }
                j++;
            }
        }
        array->count = j;
    } else {
        array->count = 1;
        array->tables = calloc(1, sizeof(fmp_table_t));
        array->tables[0].index = 1;
        snprintf(array->tables[0].utf8_name, sizeof(array->tables[0].utf8_name),
                "%s", file->filename);

        // strip off extension
        size_t len = strlen(array->tables[0].utf8_name);
        for (int i=len-1; i>0; i--) {
            if (array->tables[0].utf8_name[i] == '.') {
                array->tables[0].utf8_name[i] = '\0';
                break;
            }
        }
    }

    if (errorCode)
        *errorCode = retval;
    if (retval != FMP_OK) {
        fmp_free_tables(array);
        return NULL;
    }
    return array;
}

void fmp_free_tables(fmp_table_array_t *array) {
    if (array) {
        free(array->tables);
        free(array);
    }
}
