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
                free(fmp_list_columns(file, table, &error));
                fmp_read_values(file, table, NULL, NULL);
            }
            free(tables);
        }
        fmp_close_file(file);
    }
    return 0;
}
