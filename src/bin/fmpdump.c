#include <stdio.h>
#include <stdlib.h>

#include "../fmp.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s [file]\n", argv[0]);
        exit(1);
    }

    int i;
    fmp_error_t error = FMP_OK;
    for (i=1; i<argc; i++) {
        fmp_file_t *file = fmp_open_file(argv[i], &error);
        if (!file) {
            fprintf(stderr, "Error code: %d\n", error);
            return 1;
        }
        error = fmp_dump_file(file);
        if (error != FMP_OK) {
            fprintf(stderr, "Error code: %d\n", error);
            return 1;
        }
    }

    return 0;
}
