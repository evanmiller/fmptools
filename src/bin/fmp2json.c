#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <yajl/yajl_gen.h>

#include "../fmp.h"
#include "../fmp_internal.h"

typedef struct my_ctx_s {
    yajl_gen g;
    int last_row;
} my_ctx_t;

fmp_handler_status_t handle_value(int row, fmp_column_t *column, const char *value, void *ws) {
    my_ctx_t *ctx = (my_ctx_t *)ws;
    if (row != ctx->last_row) {
        if (ctx->last_row)
            yajl_gen_map_close(ctx->g);
        yajl_gen_map_open(ctx->g);
    }
    yajl_gen_string(ctx->g, (const unsigned char *)column->utf8_name, strlen(column->utf8_name));
    yajl_gen_string(ctx->g, (const unsigned char *)value, strlen(value));
    ctx->last_row = row;
    return FMP_HANDLER_OK;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s [input file] [output file]\n", basename(argv[0]));
        exit(1);
    }

    fmp_error_t error = FMP_OK;
    yajl_gen g = yajl_gen_alloc(NULL);
    my_ctx_t ctx = { .g = g };

    fmp_file_t *file = fmp_open_file(argv[1], &error);
    if (!file) {
        fprintf(stderr, "Error code: %d\n", error);
        return 1;
    }

    fmp_table_array_t *tables = fmp_list_tables(file, &error);
    if (!tables) {
        fprintf(stderr, "Error code: %d\n", error);
        return 1;
    }

    yajl_gen_config(ctx.g, yajl_gen_beautify, 1);

    yajl_gen_array_open(g);
    for (int j=0; j<tables->count; j++) {
        fmp_table_t *table = &tables->tables[j];
        yajl_gen_map_open(g);
        yajl_gen_string(g, (const unsigned char *)"name", sizeof("name")-1);
        yajl_gen_string(g, (const unsigned char *)table->utf8_name, strlen(table->utf8_name));
        yajl_gen_string(g, (const unsigned char *)"columns", sizeof("columns")-1);
        yajl_gen_array_open(g);
        fmp_column_array_t *columns = fmp_list_columns(file, table, &error);
        for (int k=0; k<columns->count; k++) {
            yajl_gen_string(g, (const unsigned char *)columns->columns[k].utf8_name,
                    strlen(columns->columns[k].utf8_name));
        }
        fmp_free_columns(columns);
        yajl_gen_array_close(g);
        yajl_gen_string(g, (const unsigned char *)"values", sizeof("values")-1);

        yajl_gen_array_open(g);
        ctx.last_row = 0;
        error = fmp_read_values(file, table, &handle_value, &ctx);
        if (error != FMP_OK) {
            fprintf(stderr, "Error code: %d\n", error);
            return 1;
        }
        if (ctx.last_row)
            yajl_gen_map_close(g);
        yajl_gen_array_close(g);

        yajl_gen_map_close(g);
    }
    yajl_gen_array_close(g);
    fmp_free_tables(tables):
    fmp_close_file(file);

    FILE *stream = NULL;
    if (strcmp(argv[2], "-")) {
        stream = fopen(argv[2], "w");
        if (!stream) {
            fprintf(stderr, "Couldn't open file for writing: %s\n", argv[2]);
            return 1;
        }
    }
    const unsigned char * buf = NULL;
    size_t len = 0;
    yajl_gen_get_buf(g, &buf, &len);
    fwrite(buf, len, 1, stream ? stream : stdout);
    yajl_gen_free(ctx.g);
    if (stream)
        fclose(stream);

    return 0;
}
