#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>

void print_usage_and_exit(int argc, char *argv[]) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        printf("FMP Tools version %s\n", VERSION);
        printf("Copyright 2020 Evan Miller\n");
        printf("https://github.com/evanmiller/fmptools\n\n");
    }
    printf("Usage: %s [input file] [output file]\n", basename(argv[0]));
    exit(1);
}
