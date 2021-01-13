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
#include <libgen.h>
#include <xlsxwriter.h>

#include "../fmp.h"
#include "usage.h"

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
        print_usage_and_exit(argc, argv);
    }

    fmp_error_t error = FMP_OK;
    lxw_workbook *wb = workbook_new(argv[2]);
    if (!wb) {
        fprintf(stderr, "Error opening workbook at %s\n", argv[2]);
        return 1;
    }
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
        if (!ws) {
            fprintf(stderr, "Error adding workbook named %s\n", table->utf8_name);
            return 1;
        }
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
