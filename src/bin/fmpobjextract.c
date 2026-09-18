#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <ctype.h>

#include "../fmp.h"
#include "../fmp_internal.h"

#define FIELD_SIMPLE_CAP 4096
#define FOURCC_CAP 32

typedef struct {
    uint8_t *bytes;
    size_t len;
} blob_t;

typedef struct {
    uint64_t target_record;
    char target_fourcc[5];
    char out_path[512];
    char debug_prefix[512];
    blob_t *segments;
    size_t segments_cap;
    size_t max_segment_index;
    blob_t field_simple[FIELD_SIMPLE_CAP];
    int saw_record;
    int saw_subpath;
    int write_debug;
} extract_ctx_t;

typedef struct {
    uint64_t target_record;
    char fourccs[FOURCC_CAP][5];
    size_t fourcc_count;
} scan_ctx_t;

static int path_matches_record(fmp_chunk_t *chunk, uint64_t record) {
    return chunk->path_level >= 4
        && path_value(chunk, chunk->path[0]) == 129
        && path_value(chunk, chunk->path[1]) == 31
        && path_value(chunk, chunk->path[2]) == 5
        && path_value(chunk, chunk->path[3]) == record;
}

static int path_matches_subkey(fmp_chunk_t *chunk, const char *fourcc) {
    if (chunk->path_level < 5 || chunk->path[4]->len != 4) {
        return 0;
    }
    return memcmp(chunk->path[4]->bytes, fourcc, 4) == 0;
}

static void save_blob(const char *path, const uint8_t *bytes, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Could not open %s for writing\n", path);
        exit(1);
    }
    fwrite(bytes, 1, len, f);
    fclose(f);
}

static uint64_t big_endian_uint(const uint8_t *bytes, size_t len) {
    uint64_t out = 0;
    for (size_t i = 0; i < len; i++) {
        out = (out << 8) | bytes[i];
    }
    return out;
}

static int has_fourcc(const scan_ctx_t *ctx, const char *fourcc) {
    for (size_t i = 0; i < ctx->fourcc_count; i++) {
        if (memcmp(ctx->fourccs[i], fourcc, 4) == 0) {
            return 1;
        }
    }
    return 0;
}

static void add_fourcc(scan_ctx_t *ctx, const char *fourcc) {
    if (ctx->fourcc_count >= FOURCC_CAP || has_fourcc(ctx, fourcc)) {
        return;
    }
    memcpy(ctx->fourccs[ctx->fourcc_count], fourcc, 4);
    ctx->fourccs[ctx->fourcc_count][4] = '\0';
    ctx->fourcc_count++;
}

static chunk_status_t scan_record_chunk(fmp_chunk_t *chunk, void *user_ctx) {
    scan_ctx_t *ctx = (scan_ctx_t *)user_ctx;
    if (!path_matches_record(chunk, ctx->target_record)) {
        return CHUNK_NEXT;
    }
    if (chunk->path_level >= 5 && chunk->path[4]->len == 4) {
        add_fourcc(ctx, (const char *)chunk->path[4]->bytes);
    }
    return CHUNK_NEXT;
}

static const char *pick_preferred_fourcc(const scan_ctx_t *ctx) {
    static const char *preferred[] = {
        "PNGf",
        "JPEG",
        "BMPf",
        "GIFf",
        "PDF ",
        "PICT",
        "META",
        "EMBO",
    };
    for (size_t i = 0; i < sizeof(preferred) / sizeof(preferred[0]); i++) {
        if (has_fourcc(ctx, preferred[i])) {
            return preferred[i];
        }
    }
    if (ctx->fourcc_count == 0) {
        return NULL;
    }
    return ctx->fourccs[0];
}

static int decode_container_ref_hex(const char *input, uint8_t **out_bytes, size_t *out_len) {
    size_t raw_len = strlen(input);
    char *hex = malloc(raw_len + 1);
    if (!hex) {
        return 0;
    }

    size_t hex_len = 0;
    for (size_t i = 0; i < raw_len; i++) {
        unsigned char ch = (unsigned char)input[i];
        if (isxdigit(ch)) {
            hex[hex_len++] = (char)ch;
            continue;
        }
        if (ch < 0x80) {
            free(hex);
            return 0;
        }
    }
    hex[hex_len] = '\0';

    if (hex_len == 0 || hex_len % 2 != 0) {
        free(hex);
        return 0;
    }

    uint8_t *bytes = malloc(hex_len / 2);
    if (!bytes) {
        free(hex);
        return 0;
    }
    for (size_t i = 0; i < hex_len; i += 2) {
        char byte_hex[3] = {hex[i], hex[i + 1], '\0'};
        bytes[i / 2] = (uint8_t)strtoul(byte_hex, NULL, 16);
    }

    *out_bytes = bytes;
    *out_len = hex_len / 2;
    free(hex);
    return 1;
}

static uint64_t decode_container_ref_value(const char *input, int *ok) {
    uint8_t *bytes = NULL;
    size_t len = 0;
    fmp_chunk_t fake_chunk = {0};
    fmp_data_t data = {0};
    uint64_t value = 0;

    if (!decode_container_ref_hex(input, &bytes, &len)) {
        *ok = 0;
        return 0;
    }

    fake_chunk.version_num = 12;
    data.len = len;
    data.bytes = bytes;
    value = path_value(&fake_chunk, &data);
    free(bytes);

    *ok = value > 0;
    return value;
}

static chunk_status_t handle_chunk(fmp_chunk_t *chunk, void *user_ctx) {
    extract_ctx_t *ctx = (extract_ctx_t *)user_ctx;
    if (!path_matches_record(chunk, ctx->target_record)) {
        return CHUNK_NEXT;
    }

    ctx->saw_record = 1;

    if (chunk->path_level == 5 && path_matches_subkey(chunk, ctx->target_fourcc)) {
        ctx->saw_subpath = 1;

        if (chunk->type == FMP_CHUNK_DATA_SEGMENT) {
            if (chunk->segment_index >= ctx->segments_cap) {
                size_t new_cap = ctx->segments_cap ? ctx->segments_cap : 16;
                while (new_cap <= chunk->segment_index) {
                    new_cap *= 2;
                }
                ctx->segments = realloc(ctx->segments, new_cap * sizeof(blob_t));
                if (!ctx->segments) {
                    fprintf(stderr, "Out of memory allocating segments\n");
                    exit(1);
                }
                for (size_t i = ctx->segments_cap; i < new_cap; i++) {
                    ctx->segments[i].bytes = NULL;
                    ctx->segments[i].len = 0;
                }
                ctx->segments_cap = new_cap;
            }

            blob_t *slot = &ctx->segments[chunk->segment_index];
            free(slot->bytes);
            slot->bytes = malloc(chunk->data.len);
            if (!slot->bytes) {
                fprintf(stderr, "Out of memory copying segment bytes\n");
                exit(1);
            }
            memcpy(slot->bytes, chunk->data.bytes, chunk->data.len);
            slot->len = chunk->data.len;
            if (chunk->segment_index > ctx->max_segment_index) {
                ctx->max_segment_index = chunk->segment_index;
            }
        } else if (chunk->type == FMP_CHUNK_FIELD_REF_SIMPLE && chunk->ref_simple < FIELD_SIMPLE_CAP) {
            blob_t *slot = &ctx->field_simple[chunk->ref_simple];
            free(slot->bytes);
            slot->bytes = malloc(chunk->data.len);
            if (!slot->bytes) {
                fprintf(stderr, "Out of memory copying field bytes\n");
                exit(1);
            }
            memcpy(slot->bytes, chunk->data.bytes, chunk->data.len);
            slot->len = chunk->data.len;
        } else if (chunk->type == FMP_CHUNK_FIELD_REF_LONG) {
            uint64_t field_index = path_value(chunk, &chunk->ref_long);
            if (field_index >= FIELD_SIMPLE_CAP) {
                return CHUNK_NEXT;
            }
            blob_t *slot = &ctx->field_simple[field_index];
            free(slot->bytes);
            slot->bytes = malloc(chunk->data.len);
            if (!slot->bytes) {
                fprintf(stderr, "Out of memory copying field bytes\n");
                exit(1);
            }
            memcpy(slot->bytes, chunk->data.bytes, chunk->data.len);
            slot->len = chunk->data.len;
        }
    }

    return CHUNK_NEXT;
}

int main(int argc, char *argv[]) {
    const char *file_arg = NULL;
    const char *ref_arg = NULL;
    const char *out_arg = NULL;
    const char *debug_arg = NULL;

    if (argc == 4 || argc == 6) {
        file_arg = argv[1];
        ref_arg = argv[2];
        out_arg = argv[3];
        if (argc == 6) {
            if (strcmp(argv[4], "--debug-prefix") != 0) {
                fprintf(stderr, "Usage: %s <file> <container-value> <output-file> [--debug-prefix <prefix>]\n", argv[0]);
                return 1;
            }
            debug_arg = argv[5];
        }
    } else {
        fprintf(stderr, "Usage: %s <file> <container-value> <output-file> [--debug-prefix <prefix>]\n", argv[0]);
        return 1;
    }

    fmp_error_t error = FMP_OK;
    fmp_file_t *file = fmp_open_file(file_arg, &error);
    if (!file) {
        fprintf(stderr, "Error opening file: %d\n", error);
        return 1;
    }

    extract_ctx_t ctx = {0};
    int ok = 0;
    scan_ctx_t scan = {0};
    const char *picked = NULL;

    ctx.target_record = decode_container_ref_value(ref_arg, &ok);
    if (!ok) {
        fprintf(stderr, "Could not decode container reference: %s\n", ref_arg);
        fmp_close_file(file);
        return 1;
    }

    scan.target_record = ctx.target_record;
    error = process_blocks(file, NULL, scan_record_chunk, &scan);
    if (error != FMP_OK) {
        fprintf(stderr, "Error scanning record: %d\n", error);
        fmp_close_file(file);
        return 1;
    }

    picked = pick_preferred_fourcc(&scan);
    if (!picked) {
        fprintf(stderr, "Did not find any extractable subpath under record %" PRIu64 "\n", ctx.target_record);
        fmp_close_file(file);
        return 1;
    }
    snprintf(ctx.target_fourcc, sizeof(ctx.target_fourcc), "%s", picked);
    fprintf(stderr, "Decoded container ref %s -> record %" PRIu64 ", selected %s\n",
            ref_arg, ctx.target_record, ctx.target_fourcc);
    snprintf(ctx.out_path, sizeof(ctx.out_path), "%s", out_arg);
    if (debug_arg) {
        ctx.write_debug = 1;
        snprintf(ctx.debug_prefix, sizeof(ctx.debug_prefix), "%s", debug_arg);
    }

    error = process_blocks(file, NULL, handle_chunk, &ctx);
    if (error != FMP_OK) {
        fprintf(stderr, "Error walking blocks: %d\n", error);
        fmp_close_file(file);
        return 1;
    }

    if (!ctx.saw_record) {
        fprintf(stderr, "Did not find record %" PRIu64 "\n", ctx.target_record);
        fmp_close_file(file);
        return 1;
    }
    if (!ctx.saw_subpath) {
        fprintf(stderr, "Did not find subpath %s under record %" PRIu64 "\n", ctx.target_fourcc, ctx.target_record);
        fmp_close_file(file);
        return 1;
    }

    size_t concat_len = 0;
    for (size_t i = 1; i <= ctx.max_segment_index; i++) {
        concat_len += ctx.segments[i].len;
    }
    uint8_t *concat = malloc(concat_len ? concat_len : 1);
    if (!concat) {
        fprintf(stderr, "Out of memory concatenating payload\n");
        fmp_close_file(file);
        return 1;
    }
    size_t pos = 0;
    for (size_t i = 1; i <= ctx.max_segment_index; i++) {
        if (!ctx.segments[i].bytes) {
            continue;
        }
        memcpy(concat + pos, ctx.segments[i].bytes, ctx.segments[i].len);
        pos += ctx.segments[i].len;
    }

    size_t extra_len = 0;
    for (size_t i = 1; i < FIELD_SIMPLE_CAP; i++) {
        if (ctx.field_simple[i].bytes) {
            extra_len += ctx.field_simple[i].len;
        }
    }
    uint8_t *combined = malloc(pos + extra_len ? pos + extra_len : 1);
    if (!combined) {
        fprintf(stderr, "Out of memory combining payload\n");
        fmp_close_file(file);
        return 1;
    }
    memcpy(combined, concat, pos);
    size_t combined_len = pos;
    for (size_t i = 1; i < FIELD_SIMPLE_CAP; i++) {
        if (!ctx.field_simple[i].bytes) {
            continue;
        }
        memcpy(combined + combined_len, ctx.field_simple[i].bytes, ctx.field_simple[i].len);
        combined_len += ctx.field_simple[i].len;
    }
    const uint8_t *final_bytes = combined;
    size_t final_len = combined_len;
    if (ctx.field_simple[0].bytes && ctx.field_simple[0].len <= 8) {
        uint64_t wanted = big_endian_uint(ctx.field_simple[0].bytes, ctx.field_simple[0].len);
        if (wanted > 0 && wanted <= combined_len) {
            final_len = (size_t)wanted;
        }
    }
    save_blob(ctx.out_path, final_bytes, final_len);

    if (ctx.write_debug) {
        char path[1024];
        snprintf(path, sizeof(path), "%s_segments.bin", ctx.debug_prefix);
        save_blob(path, concat, pos);
        snprintf(path, sizeof(path), "%s_combined.bin", ctx.debug_prefix);
        save_blob(path, combined, combined_len);

        FILE *manifest = NULL;
        snprintf(path, sizeof(path), "%s_manifest.txt", ctx.debug_prefix);
        manifest = fopen(path, "w");
        if (!manifest) {
            fprintf(stderr, "Could not open manifest for writing\n");
            fmp_close_file(file);
            return 1;
        }

        fprintf(manifest, "record=%" PRIu64 "\n", ctx.target_record);
        fprintf(manifest, "fourcc=%s\n", ctx.target_fourcc);
        fprintf(manifest, "segment_count=%zu\n", ctx.max_segment_index);
        fprintf(manifest, "concat_len=%zu\n", pos);
        fprintf(manifest, "combined_len=%zu\n", combined_len);
        fprintf(manifest, "final_output=%s\n", ctx.out_path);
        fprintf(manifest, "final_len=%zu\n", final_len);
        for (size_t i = 0; i < FIELD_SIMPLE_CAP; i++) {
            if (!ctx.field_simple[i].bytes) {
                continue;
            }
            fprintf(manifest, "field_%zu_len=%zu\n", i, ctx.field_simple[i].len);
            snprintf(path, sizeof(path), "%s_field_%zu.bin", ctx.debug_prefix, i);
            save_blob(path, ctx.field_simple[i].bytes, ctx.field_simple[i].len);
        }

        if (ctx.field_simple[0].bytes && ctx.field_simple[0].len <= 8) {
            uint64_t wanted = big_endian_uint(ctx.field_simple[0].bytes, ctx.field_simple[0].len);
            fprintf(manifest, "field_0_big_endian=%" PRIu64 "\n", wanted);
            if (wanted > 0 && wanted <= pos) {
                snprintf(path, sizeof(path), "%s_trimmed.bin", ctx.debug_prefix);
                save_blob(path, concat, (size_t)wanted);
                fprintf(manifest, "trimmed_len=%" PRIu64 "\n", wanted);
            }
            if (wanted > 0 && wanted <= combined_len) {
                snprintf(path, sizeof(path), "%s_combined_trimmed.bin", ctx.debug_prefix);
                save_blob(path, combined, (size_t)wanted);
                fprintf(manifest, "combined_trimmed_len=%" PRIu64 "\n", wanted);
            }
        }
        fclose(manifest);
    }

    free(combined);
    free(concat);
    for (size_t i = 0; i < ctx.segments_cap; i++) {
        free(ctx.segments[i].bytes);
    }
    free(ctx.segments);
    for (size_t i = 0; i < FIELD_SIMPLE_CAP; i++) {
        free(ctx.field_simple[i].bytes);
    }
    fmp_close_file(file);
    return 0;
}
