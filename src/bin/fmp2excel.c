#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <xlsxwriter.h>

#include "../fmp.h"

fmp_handler_status_t handle_value(int row, fmp_column_t *column, const char *value, void *ctxp) {
    lxw_worksheet *ws = (lxw_worksheet *)ctxp;
    if (row == 1) {
        worksheet_write_string(ws, 0, column->index-1, column->utf8_name, NULL);
    }
    worksheet_write_string(ws, row, column->index-1, value, NULL);
    return FMP_HANDLER_OK;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s [input file] [output file]\n", basename(argv[0]));
        exit(1);
    }

    fmp_error_t error = FMP_OK;
    lxw_workbook *wb = workbook_new(argv[2]);
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
    for (int i=0; i<tables->count; i++) {
        fmp_table_t *table = &tables->tables[i];
        lxw_worksheet *ws = workbook_add_worksheet(wb, table->utf8_name);
        error = fmp_read_values(file, table, &handle_value, ws);
        if (error != FMP_OK) {
            fprintf(stderr, "Error code: %d\n", error);
            return 1;
        }
    }
    workbook_close(wb);
    fmp_free_tables(tables);
    fmp_close_file(file);

    return 0;
}
