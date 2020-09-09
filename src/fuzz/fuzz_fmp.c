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
#include "../fmp.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    fmp_error_t error = FMP_OK;
    fmp_file_t *file = fmp_open_buffer(Data, Size, &error);
    if (file) {
        fmp_table_array_t *tables = fmp_list_tables(file, &error);
        if (tables) {
            for (int i=0; i<tables->count; i++) {
                fmp_table_t *table = &tables->tables[i];
                fmp_free_columns(fmp_list_columns(file, table, &error));
                fmp_read_values(file, table, NULL, NULL);
            }
            fmp_free_tables(tables);
        }
        fmp_close_file(file);
    }
    return 0;
}
