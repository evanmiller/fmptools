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

#include <iconv.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "fmp.h"
#include "fmp_internal.h"

typedef struct fmp_dump_ctx_s {
    int did_print_current_path;
    unsigned char xor_mask;
    iconv_t converter;
} fmp_dump_ctx_t;

static void dump_data(fmp_chunk_t *chunk, fmp_data_t *data, fmp_dump_ctx_t *ctx) {
    uint8_t *bytes = data->bytes;
    size_t len = data->len;
    if (len == 1 || ((bytes[0] ^ ctx->xor_mask)  >= 0x80 && len <= 3 )) {
        uint64_t val = path_value(chunk, data);
        printf("[%" PRIu64 "]", val);
    } else if (((bytes[0] ^ ctx->xor_mask) < 0x20 || (bytes[0] ^ ctx->xor_mask) >= 0x80) && len <= 4) {
        uint64_t val = bytes[0];
        int i=0;
        while (++i<len) {
            val <<= 8;
            val += bytes[i];
        }
        printf("[%" PRIu64 "]", val);
    } else {
        size_t utf8_len = 4*len+1;
        char *utf8 = malloc(utf8_len);
        convert(ctx->converter, ctx->xor_mask, utf8, utf8_len, bytes, len);
        printf("\"%s\"", utf8);
        free(utf8);
    }
}

static void dump_path_value(fmp_chunk_t *chunk, fmp_data_t *path, fmp_dump_ctx_t *ctx) {
    if (path->len <= 3) {
        printf("[%" PRIu64 "]", path_value(chunk, path));
    } else {
        dump_data(chunk, path, ctx);
    }
}

static void dump_path(fmp_chunk_t *chunk, fmp_dump_ctx_t *ctx) {
    for (int i=0; i<chunk->path_level; i++) {
        dump_path_value(chunk, chunk->path[i], ctx);
        printf(".");
    }
}

static chunk_status_t dump_chunk(fmp_chunk_t *chunk, void *the_ctx) {
    fmp_dump_ctx_t *ctx = (fmp_dump_ctx_t *)the_ctx;
    if (chunk->type == FMP_CHUNK_PATH_POP) {
        ctx->did_print_current_path = 0;
        printf("-- POP 0x%02X --\n", chunk->code);
    } else if (chunk->type == FMP_CHUNK_PATH_PUSH) {
        printf("-- PUSH 0x%02X [ ", chunk->code);
        for (int i=0; i<chunk->data.len; i++) {
            printf("0x%02X ", chunk->data.bytes[i]);
        }
        printf(" ] --\n");
        ctx->did_print_current_path = 0;
    } else {
        if (!ctx->did_print_current_path && chunk->path_level) {
            dump_path(chunk, ctx);
            printf("\n");
            ctx->did_print_current_path = 1;
        }
        printf("%*s", (int)chunk->path_level, "");
    }
    if (chunk->type == FMP_CHUNK_DATA_SIMPLE) {
        printf("-- data simple (0x%02X): ", chunk->code);
        unsigned char mask = ctx->xor_mask;
        ctx->xor_mask = 0;
        dump_data(chunk, &chunk->data, ctx);
        ctx->xor_mask = mask;
        printf(" --\n");
    }
    if (chunk->type == FMP_CHUNK_FIELD_REF_SIMPLE) {
        printf("-- field (0x%02X): [%d] => ", chunk->code, chunk->ref_simple);
        dump_data(chunk, &chunk->data, ctx);
        printf(" --\n");
    }
    if (chunk->type == FMP_CHUNK_FIELD_REF_LONG) {
        printf("-- field (0x%02X): ", chunk->code);
        dump_data(chunk, &chunk->ref_long, ctx);
        printf(" => ");
        dump_data(chunk, &chunk->data, ctx);
        printf(" --\n");
    }
    if (chunk->type == FMP_CHUNK_DATA_SEGMENT) {
        printf("-- segment #%d (%zu bytes) --\n", chunk->segment_index, chunk->data.len);
    }
    if (chunk->extended)
        printf("   => EXTENDED <= \n");
    return CHUNK_NEXT;
}

static int start_block(fmp_block_t *block, void *ctx) {
    ((fmp_dump_ctx_t *)ctx)->did_print_current_path = 0;
    if (block->this_id == 0) {
        debug("=== [ INDEX BLOCK ] ===\n");
        debug("   # blocks: %d\n", block->next_id);
    } else {
        debug("== %d -> [ BLOCK %d ] -> %d ==\n", block->prev_id, block->this_id, block->next_id);
        debug("        [ Len: %zu ]\n", block->payload_len);
    }
    return 1;
}

fmp_error_t fmp_dump_file(fmp_file_t *file) {
    fmp_dump_ctx_t ctx = { 0 };
    ctx.converter = file->converter;
    ctx.xor_mask = file->xor_mask;

    debug("Version: File Maker %s\n", file->version_string);
    if (file->version_date.tm_mon) {
        debug("Released: %04d-%02d-%02d\n",
                file->version_date.tm_year+1900,
                file->version_date.tm_mon+1,
                file->version_date.tm_mday);
    }

    return process_blocks(file, &start_block, &dump_chunk, &ctx);
}
