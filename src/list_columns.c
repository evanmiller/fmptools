#include "fmp_internal.h"

chunk_status_t handle_chunk_list_columns_v3(fmp_ctx_t *ctx, fmp_chunk_t *chunk) {
    if (path_value(ctx, ctx->path[0]) > 3)
        return CHUNK_DONE;

    if (chunk->type != FMP_CHUNK_FIELD_REF_SIMPLE)
        return CHUNK_NEXT;

    if (table_path_match_start2(ctx, 3, 3, 5)) {
        ctx->current_column = calloc(1, sizeof(fmp_column_t));
        if (chunk->ref_simple == 1) {
            convert(ctx, ctx->current_column->utf8_name, sizeof(ctx->current_column->utf8_name),
                    chunk->data.bytes, chunk->data.len);
            ctx->current_column->index = column_index;
        } else if (chunk->ref_simple == 2) {
            if (chunk->data.bytes[1] == 0x01) {
                ctx->current_column->type = FMP_COLUMN_TYPE_STRING;
            } else if (chunk->data.bytes[1] == 0x02) {
                ctx->current_column->type = FMP_COLUMN_TYPE_NUMBER;
            }
            fmp_handler_status_t status = ctx->handle_column(ctx->current_column, ctx->user_ctx);
            free(ctx->current_column);
            ctx->current_column = NULL;
            if (status == FMP_HANDLER_ABORT)
                return CHUNK_ABORT;
        }
    }
    return CHUNK_NEXT;
}

chunk_status_t handle_chunk_list_columns_v7(fmp_ctx_t *ctx, fmp_chunk_t *chunk) {
    if (path_value(ctx, ctx->path[0]) - 128 > ctx->target_table_index)
        return CHUNK_DONE;
    if (path_value(ctx, ctx->path[0]) - 128 < ctx->target_table_index)
        return CHUNK_NEXT;
    if (chunk->type != FMP_CHUNK_FIELD_REF_SIMPLE)
        return CHUNK_NEXT;

    if (table_path_match_start2(ctx, 3, 3, 5)) {
        fmp_data_t *column_path = ctx->path[ctx->path_level-1];
        size_t column_index = copy_int(column_path->bytes, column_path->len);
        if (column_index > ctx->num_columns) {
            ctx->num_columns = column_index;
            ctx->columns = realloc(ctx->columns, ctx->num_columns * sizeof(fmp_column_t));
        }
        ctx->current_column = ctx->columns + column_index - 1;
        if (chunk->ref_simple == 16) {
            fmp_column_t *column = calloc(1, sizeof(fmp_column_t));
            convert(ctx, column->utf8_name, sizeof(column->utf8_name),
                    chunk->data.bytes, chunk->data.len);
            column->index = column_index;
            fmp_handler_status_t status = ctx->handle_column(ctx->current_column, ctx->user_ctx);
            free(column);
            if (status == FMP_HANDLER_ABORT)
                return CHUNK_DONE;
        }
    }
    return CHUNK_NEXT;
}

fmp_column_array_t *fmp_list_columns(fmp_file_t *file, fmp_table_t *table, fmp_error_t *errorCode) {
}
