#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fmp_ctx_s {
    int     version;
    int     little_endian;
    int     sector_size;
    int     fd;

    size_t  sector_index_shift;

    size_t  sector_head_len;
    off_t   prev_sector_offset;
    off_t   next_sector_offset;
    off_t   sector_len_offset;
} fmp_ctx_t;

typedef enum {
    FMP_OK = 0,
    FMP_ERROR_OPEN,
    FMP_ERROR_READ,
    FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE,
    FMP_ERROR_INCOMPLETE_SECTOR,
    FMP_ERROR_UNRECOGNIZED_CODE
} fmp_error_t;

fmp_error_t read_sector(int sector_idx, unsigned char *buffer, fmp_ctx_t *ctx) {
    if (lseek(ctx->fd, sector_idx * ctx->sector_size, SEEK_SET) == -1) {
        dprintf(STDERR_FILENO, "Failed to seek\n");
        return FMP_ERROR_READ;
    }

    if (read(ctx->fd, buffer, ctx->sector_size) != ctx->sector_size) {
        dprintf(STDERR_FILENO, "Incomplete sector %d\n", sector_idx);
        return FMP_ERROR_INCOMPLETE_SECTOR;
    }

    return FMP_OK;
}

fmp_error_t copy_sector_to_stdout(int sector_idx, fmp_ctx_t *ctx) {
    fmp_error_t error = FMP_OK;
    unsigned char *buffer = malloc(ctx->sector_size);

    error = read_sector(sector_idx, buffer, ctx);
    if (error != FMP_OK)
        goto cleanup;

    write(STDOUT_FILENO, buffer, ctx->sector_size);

cleanup:
    free(buffer);

    return error;
}

fmp_error_t write_sector(unsigned char *buffer, fmp_ctx_t *ctx) {
    size_t full_data_size = ctx->sector_size;

    if (ctx->sector_len_offset != -1) {
        size_t data_len = (buffer[ctx->sector_len_offset] << 8) + buffer[ctx->sector_len_offset+1];
        full_data_size = ctx->sector_head_len + data_len;
    }

    if (full_data_size > ctx->sector_size)
        return FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;

    memset(buffer + full_data_size, 0, ctx->sector_size - full_data_size);
    write(STDOUT_FILENO, buffer, ctx->sector_size);

    return FMP_OK;
}

void print_spaces(int level) {
    if (level < 0) {
        dprintf(STDERR_FILENO, "WARNING: level is %d!\n", level);
    }
    for (int i=0; i<level; i++) {
        dprintf(STDERR_FILENO, " ");
    }
}

void handle_number(const unsigned char *p, int len, int code, int level) {
    int number = 0;
    for (int i=0; i<len; i++) {
        number = (number << 8) + p[i];
    }
    print_spaces(level);
    dprintf(STDERR_FILENO, "Number: %d", number);
    if (code != -1) {
        dprintf(STDERR_FILENO, " (Code %d)", code);
    }
    dprintf(STDERR_FILENO, "\n");
}

void handle_number_or_string(const unsigned char *p, int len, int code, int level) {
    char string_buffer[256];
    long number = -1;
    int could_be_text = 1;
    for (int i=0; i<len; i++) {
        if (p[i] < 0x20 || p[i] >= 0x80) {
            could_be_text = 0;
            break;
        }
    }
    if ((len == 1 || len == 2 || len == 4) && !could_be_text) {
        return handle_number(p, len, code, level);
    }
    print_spaces(level);
    if (could_be_text) {
        memcpy(string_buffer, p, len);
        string_buffer[len] = '\0';
        dprintf(STDERR_FILENO, "String: %s", string_buffer);
    } else {
        dprintf(STDERR_FILENO, "(Unrecognized %d-byte sequence)", len);
    }
    if (code != -1) {
        dprintf(STDERR_FILENO, " (Code %d)", code);
    }
    dprintf(STDERR_FILENO, "\n");
}

fmp_error_t interpret_codes_in_sector(const unsigned char *buffer, int skip, fmp_ctx_t *ctx) {
    if (ctx->version == 7)
        return FMP_OK;

    size_t full_data_size = ctx->sector_size;

    if (ctx->sector_len_offset != -1) {
        size_t data_len = (buffer[ctx->sector_len_offset] << 8) + buffer[ctx->sector_len_offset+1];
        full_data_size = ctx->sector_head_len + data_len;
    }

    const unsigned char *p = buffer + ctx->sector_head_len + skip;
    int level = 0;
    while (p < buffer + full_data_size) {
        if (*p == 0x00 || *p == 0xFF) {
            break;
        } else if ((*p & 0xC0) == 0xC0) {
            int len = (*p & 0x0F);
            p++;
            if (len == 1) {
                handle_number(p, len, -1, level);
                level++;
            } else if (len) {
                handle_number_or_string(p, len, -1, level);
                level++;
            } else {
                level--;
            }
            p += len;
        } else if ((*p & 0x80) == 0x80) {
            int len = (*p & 0x0F);
            p++;
            if (len == 1) {
                handle_number(p, len, -1, level);
            } else {
                handle_number_or_string(p, len, -1, level);
            }
            p += len;
        } else if (*p >= 0x40 && *p < 0x80) {
            int code = (*p & 0x0F);
            p += 1;
            int len = *p++;
            handle_number_or_string(p, len, code, level);
            p += len;
        } else if (*p == 0x01 && (*(p+1) & 0xF0) == 0xF0) {
            int code = 16 + (*(p+1) & 0x0F);
            p += 2;
            int len = *p++;
            handle_number_or_string(p, len, code, level);
            p += len;
        } else if (*p == 0x02 && (*(p+1) & 0xF0) == 0xF0  && (*(p+2) & 0xF0) == 0xF0) {
            p += 3;
            int len = *p++;
            handle_number_or_string(p, len, -1, level);
            p += len;
            /*
        } else if (*p == 0x02 && *(p+1) == 0x00 && *(p+2) == 0x01) {
            p += 3;
            int len = *p++;
            handle_number_or_string(p, len, -1, level);
            p += len;
            */
        } else if (*p < 0x20) {
            int len = *p++;
            handle_number_or_string(p, len, -1, level);
            p += len;
        } else if (*p >= 0x20 && *p < 0x40) {
            p++;
            int len = *p++;
            handle_number_or_string(p, len, -1, level);
            p += len;
        } else {
            dprintf(STDERR_FILENO, "Unrecognized code: %02x\n", *p);
            return FMP_ERROR_UNRECOGNIZED_CODE;
        }
    }
    return FMP_OK;
}

fmp_error_t read_write_and_interpret(int sector_idx, unsigned char *buffer, int skip, fmp_ctx_t *ctx) {
    fmp_error_t error = FMP_OK;

    error = read_sector(sector_idx, buffer, ctx);
    if (error != FMP_OK)
        goto cleanup;

    error = write_sector(buffer, ctx);
    if (error != FMP_OK)
        goto cleanup;

    error = interpret_codes_in_sector(buffer, skip, ctx);
    if (error != FMP_OK)
        goto cleanup;

cleanup:
    return error;
}

fmp_error_t process_file(char path[]) {
    int sector_count = 0;
    unsigned char *buffer = NULL;
    fmp_ctx_t *ctx = calloc(1, sizeof(fmp_ctx_t));
    fmp_error_t error = FMP_OK;

    ctx->fd = open(path, O_RDONLY);
    ctx->sector_size = 1024;

    if (ctx->fd == -1) {
        dprintf(STDERR_FILENO, "Failed to open file %s: %s\n", path, strerror(errno));
        error = FMP_ERROR_OPEN;
        goto cleanup;
    }

    buffer = malloc(ctx->sector_size);

    read_sector(0, buffer, ctx);

    if (memcmp(&buffer[15], "HBAM7", 5) == 0) {
        // or 4096
        ctx->sector_size = 4096;
        ctx->version = 7;
        ctx->prev_sector_offset = 6;
        ctx->next_sector_offset = 10;
        ctx->sector_len_offset = 14;
        ctx->sector_head_len = 16;
        /* Big-endian flag somewhere? */
    } else {
        ctx->prev_sector_offset = 4;
        ctx->next_sector_offset = 8;
        ctx->sector_len_offset = 12;
        ctx->sector_head_len = 14;
        ctx->sector_index_shift = 1;
    }

    if (buffer[521] == 0x1E) {
        ctx->version = 12;
        ctx->sector_len_offset = -1;
    }

    copy_sector_to_stdout(sector_count++, ctx);

    int last_sector = 0;

    if (ctx->sector_size == 1024) {
        copy_sector_to_stdout(sector_count++, ctx);
    }

    error = read_write_and_interpret(sector_count++, buffer, 6, ctx);
    if (error != FMP_OK)
        goto cleanup;

    last_sector = ((buffer[ctx->next_sector_offset] << 8) + buffer[ctx->next_sector_offset+1]) + ctx->sector_index_shift;

    int prev_sector = 0;
    int next_sector = sector_count;

    uint32_t last_code = 0;

    do {
        dprintf(STDERR_FILENO, "Reading sector %d...\n", next_sector);

        error = read_write_and_interpret(next_sector, buffer, (next_sector == 3 ? 16 : 0), ctx);
        if (error != FMP_OK)
            goto cleanup;

        prev_sector = (buffer[ctx->prev_sector_offset] << 8) + buffer[ctx->prev_sector_offset+1] + ctx->sector_index_shift;
        next_sector = (buffer[ctx->next_sector_offset] << 8) + buffer[ctx->next_sector_offset+1] + ctx->sector_index_shift;

        sector_count++;
    } while (next_sector != ctx->sector_index_shift);

cleanup:
    dprintf(STDERR_FILENO, "%d sectors found. Last sector: %d\n", sector_count, last_sector);
    if (error != FMP_OK)
        dprintf(STDERR_FILENO, "Error processing: %d\n", error);

    if (ctx->fd != -1)
        close(ctx->fd);

    if (ctx)
        free(ctx);

    if (buffer)
        free(buffer);

    return error;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s [file]\n", argv[0]);
        exit(1);
    }

    int i;
    fmp_error_t error = FMP_OK;
    for (i=1; i<argc; i++) {
        error = process_file(argv[i]);
        if (error != FMP_OK)
            return 1;
    }

    return 0;
}
