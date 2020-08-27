#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "fmp.h"
#include "fmp_internal.h"

typedef struct fmp_list_tables_ctx_s {
    fmp_file_t *file;
    fmp_table_array_t *array;
} fmp_list_tables_ctx_t;

chunk_status_t handle_chunk_list_tables_v7(fmp_chunk_t *chunk, void *ctxp) {
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
        if (table_index > array->num_tables) {
            size_t old_num_tables = array->num_tables;
            array->num_tables = table_index;
            array->tables = realloc(array->tables, array->num_tables * sizeof(fmp_table_t));
            memset(&array->tables[old_num_tables], 0, (table_index - old_num_tables) * sizeof(fmp_table_t));
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
        for (int i=0; i<array->num_tables; i++) {
            if (array->tables[i].index) {
                if (i!=j) {
                    memcpy(&array->tables[j], &array->tables[i], sizeof(fmp_table_t));
                }
                j++;
            }
        }
        array->num_tables = j;
    } else {
        array->num_tables = 1;
        array->tables = calloc(1, sizeof(fmp_table_t));
        array->tables[0].index = 1;
        snprintf(array->tables[0].utf8_name, sizeof(array->tables[0].utf8_name),
                "%s", file->filename);
    }

    if (errorCode)
        *errorCode = retval;
    if (retval != FMP_OK) {
        free(array);
        return NULL;
    }
    return array;
}
