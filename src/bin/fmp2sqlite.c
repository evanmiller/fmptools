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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

#include <sqlite3.h>

#include "../fmp.h"
#include "usage.h"

typedef struct fmp_sqlite_ctx_s {
    sqlite3 *db;
    sqlite3_stmt *insert_stmt;
    char *table_name;
    int last_row;
} fmp_sqlite_ctx_t;

fmp_handler_status_t handle_value(int row, fmp_column_t *column, const char *value, void *ctxp) {
    fmp_sqlite_ctx_t *ctx = (fmp_sqlite_ctx_t *)ctxp;
    if (ctx->last_row != row && ctx->last_row > 0) {
        int rc = sqlite3_step(ctx->insert_stmt);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "Error inserting data into SQLite table: %s\n", sqlite3_errmsg(ctx->db));
            return FMP_HANDLER_ABORT;
        }
        rc = sqlite3_reset(ctx->insert_stmt);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Error resetting INSERT statement: %s\n", sqlite3_errmsg(ctx->db));
            return FMP_HANDLER_ABORT;
        }
        sqlite3_clear_bindings(ctx->insert_stmt);
    }
    int rc = sqlite3_bind_text(ctx->insert_stmt, column->index, value, strlen(value), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error binding parameter: %s\n", sqlite3_errmsg(ctx->db));
        return FMP_HANDLER_ABORT;
    }
    ctx->last_row = row;
    return FMP_HANDLER_OK;
}

static size_t create_query_length(fmp_table_t *table, fmp_column_array_t *columns) {
    size_t len = 0;
    len += sizeof("CREATE TABLE \"\" ();");
    len += strlen(table->utf8_name);
    for (int j=0; j<columns->count; j++) {
        len += sizeof("\"\" TEXT")-1;
        len += strlen(columns->columns[j].utf8_name);
        if (j < columns->count) {
            len += sizeof(", ")-1;
        }
    }
    return len;
}

static size_t insert_query_length(fmp_table_t *table, fmp_column_array_t *columns) {
    size_t len = 0;
    len += sizeof("INSERT INTO \"\" () VALUES ();");
    len += strlen(table->utf8_name);
    for (int j=0; j<columns->count; j++) {
        len += sizeof("\"\"")-1;
        len += strlen(columns->columns[j].utf8_name);
        len += sizeof("\"\"")-1;
        len += sizeof("?NNNNN")-1;
        if (j < columns->count) {
            len += sizeof(", ")-1;
            len += sizeof(", ")-1;
        }
    }
    return len;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        print_usage_and_exit(argc, argv);
    }

    sqlite3 *db = NULL;
    char *zErrMsg = NULL;
    fmp_error_t error = FMP_OK;
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

    int rc = sqlite3_open_v2(argv[2], &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error opening SQLite file\n");
        return 1;
    }

    rc = sqlite3_exec(db, "PRAGMA journal_mode = OFF;\n", NULL, NULL, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error setting journal_mode = OFF\n");
        return 1;
    }

    rc = sqlite3_exec(db, "PRAGMA synchronous = 0;\n", NULL, NULL, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Error setting synchronous = 0\n");
        return 1;
    }

    char *create_query = NULL;
    char *insert_query = NULL;

    for (int i=0; i<tables->count; i++) {
        fmp_table_t *table = &tables->tables[i];
        fmp_column_array_t *columns = fmp_list_columns(file, table, &error);
        if (!columns) {
            fprintf(stderr, "Error code: %d\n", error);
            return 1;
        }
        size_t create_query_len = create_query_length(table, columns);
        size_t insert_query_len = insert_query_length(table, columns);
        create_query = realloc(create_query, create_query_len);
        insert_query = realloc(insert_query, insert_query_len);

        char *p = create_query;
        char *q = insert_query;
        p += snprintf(p, create_query_len, "CREATE TABLE \"%s\" (", table->utf8_name);
        q += snprintf(q, insert_query_len, "INSERT INTO \"%s\" (", table->utf8_name);
        for (int j=0; j<columns->count; j++) {
            fmp_column_t *column = &columns->columns[j];
            char *colname = strdup(column->utf8_name);
            size_t colname_len = strlen(colname);
            for (int k=0; k<colname_len; k++) {
                if (colname[k] == ' ')
                    colname[k] = '_';
            }
            p += snprintf(p, create_query_len - (p - create_query), "\"%s\" TEXT", colname);
            q += snprintf(q, insert_query_len - (q - insert_query), "\"%s\"", colname);
            if (j < columns->count - 1) {
                p += snprintf(p, create_query_len - (p - create_query), ", ");
                q += snprintf(q, insert_query_len - (q - insert_query), ", ");
            }
            free(colname);
        }
        p += snprintf(p, create_query_len - (p - create_query), ");");
        q += snprintf(q, insert_query_len - (q - insert_query), ") VALUES (");
        for (int j=0; j<columns->count; j++) {
            fmp_column_t *column = &columns->columns[j];
            q += snprintf(q, insert_query_len - (q - insert_query), "?%d", column->index);
            if (j < columns->count - 1)
                q += snprintf(q, insert_query_len - (q - insert_query), ", ");
        }
        q += snprintf(q, insert_query_len - (q - insert_query), ");");

        fprintf(stderr, "CREATE TABLE \"%s\"\n", table->utf8_name);
        rc = sqlite3_exec(db, create_query, NULL, NULL, &zErrMsg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Error creating SQL table: %s\n", zErrMsg);
            fprintf(stderr, "Statement was: %s\n", create_query);
            return 1;
        }

        sqlite3_stmt *stmt = NULL;
        rc = sqlite3_prepare_v2(db, insert_query, -1, &stmt, NULL);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "Error preparing SQL statement: %d\n", rc);
            fprintf(stderr, "Statement was: %s\n", insert_query);
            return 1;
        }

        fmp_sqlite_ctx_t ctx = { .db = db, .table_name = table->utf8_name, .insert_stmt = stmt };
        fmp_read_values(file, table, &handle_value, &ctx);
        if (ctx.last_row) {
            int rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE) {
                fprintf(stderr, "Error inserting data into SQLite table: %s\n", sqlite3_errmsg(db));
                return 1;
            }
        }
        sqlite3_finalize(stmt);
        fmp_free_columns(columns);
    }

    free(create_query);
    free(insert_query);
    fmp_free_tables(tables);
    sqlite3_close(db);
    fmp_close_file(file);

    return 0;
}
