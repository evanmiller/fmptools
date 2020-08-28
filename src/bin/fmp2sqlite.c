#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

#include <sqlite3.h>

#include "../fmp.h"

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
        sqlite3_reset(ctx->insert_stmt);
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

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s [input file] [output file]\n", basename(argv[0]));
        exit(1);
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

    for (int i=0; i<tables->count; i++) {
        fmp_table_t *table = &tables->tables[i];
        fmp_column_array_t *columns = fmp_list_columns(file, table, &error);
        if (!columns) {
            fprintf(stderr, "Error code: %d\n", error);
            return 1;
        }
        char create_query[4096];
        char insert_query[4096];
        char *p = create_query;
        char *q = insert_query;
        p += snprintf(p, sizeof(create_query), "CREATE TABLE %s (", table->utf8_name);
        q += snprintf(q, sizeof(insert_query), "INSERT INTO %s (", table->utf8_name);
        for (int j=0; j<columns->count; j++) {
            fmp_column_t *column = &columns->columns[j];
            char *colname = strdup(column->utf8_name);
            size_t colname_len = strlen(colname);
            for (int k=0; k<colname_len; k++) {
                if (colname[k] == ' ')
                    colname[k] = '_';
            }
            p += snprintf(p, sizeof(create_query) - (p - create_query), "%s TEXT", colname);
            q += snprintf(q, sizeof(insert_query) - (q - insert_query), "%s", colname);
            if (j < columns->count - 1) {
                p += snprintf(p, sizeof(create_query) - (p - create_query), ", ");
                q += snprintf(q, sizeof(insert_query) - (q - insert_query), ", ");
            }
            free(colname);
        }
        p += snprintf(p, sizeof(create_query) - (p - create_query), ");");
        q += snprintf(q, sizeof(insert_query) - (q - insert_query), ") VALUES (");
        for (int j=0; j<columns->count; j++) {
            fmp_column_t *column = &columns->columns[j];
            q += snprintf(q, sizeof(insert_query) - (q - insert_query), "?%d", column->index);
            if (j < columns->count - 1)
                q += snprintf(q, sizeof(insert_query) - (q - insert_query), ", ");
        }
        q += snprintf(q, sizeof(insert_query) - (q - insert_query), ");");

        fprintf(stderr, "CREATE TABLE %s\n", table->utf8_name);
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
    }

    return 0;
}
