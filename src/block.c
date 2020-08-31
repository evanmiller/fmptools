#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "fmp.h"
#include "fmp_internal.h"

// big-endian
uint64_t copy_int(const void *buf, size_t int_len) {
    const uint8_t *chars = (const uint8_t *)buf;
    if (int_len == 1)
        return chars[0];
    if (int_len == 2)
        return (chars[0] << 8) + chars[1];
    if (int_len == 4)
        return (copy_int(&chars[0], 2) << 16) + copy_int(&chars[2], 2);
    return 0;
}

fmp_error_t process_block_v7(fmp_block_t *block) {
    fmp_chunk_t *last_chunk = NULL;
    fmp_chunk_t *first_chunk = NULL;
    unsigned char *p = block->payload;
    unsigned char *end = block->payload + block->payload_len;
    fmp_error_t retval = FMP_OK;
    unsigned char c;
    while (p < block->payload + block->payload_len) {
        c = *p;
        fmp_chunk_t *chunk = calloc(1, sizeof(fmp_chunk_t));
        chunk->code = c;
        if (c == 0x00) {
            chunk->type = FMP_CHUNK_DATA_SIMPLE;
            p++;
            if (p >= end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            if (*p == 0x00) {
                free(chunk); // done
                break;
            }
            chunk->data.bytes = p;
            chunk->data.len = c + 1;
            p += chunk->data.len;
        } else if (c <= 0x05) {
            chunk->type = FMP_CHUNK_FIELD_REF_SIMPLE;
            p++;
            if (p >= end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->ref_simple = *p++;
            chunk->data.bytes = p;
            chunk->data.len = c == 0x01 ? 1 : 2*c-2;
            p += chunk->data.len;
        } else if (c == 0x06) {
            chunk->type = FMP_CHUNK_FIELD_REF_SIMPLE;
            p++;
            if (p + 2 > end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->ref_simple = *p++;
            chunk->data.len = *p++;
            chunk->data.bytes = p;
            p += chunk->data.len;
        } else if (c == 0x07) {
            chunk->type = FMP_CHUNK_DATA_SEGMENT;
            p++;
            if (p + 3 > end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->segment_index = *p++;
            chunk->data.len = copy_int(p, 2);
            p += 2;
            chunk->data.bytes = p;
            p += chunk->data.len;
        } else if (c == 0x08) {
            chunk->type = FMP_CHUNK_DATA_SIMPLE;
            p++;
            chunk->data.bytes = p;
            chunk->data.len = 2;
            p += chunk->data.len;
        } else if (c == 0x09) {
            chunk->type = FMP_CHUNK_DATA_SIMPLE;
            p++;
            chunk->data.bytes = p;
            chunk->data.len = 3;
            p += chunk->data.len;
        } else if (c <= 0x0B || (c == 0x0E && p[1] == 0xFF)) {
            chunk->type = FMP_CHUNK_DATA_SIMPLE;
            p++;
            chunk->data.bytes = p;
            chunk->data.len = 6;
            p += chunk->data.len;
        } else if (c <= 0x0D) {
            chunk->type = FMP_CHUNK_FIELD_REF_SIMPLE;
            p++;
            if (p + 2 > end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->ref_simple = copy_int(p, 2);
            p += 2;
            chunk->data.bytes = p;
            chunk->data.len = 8;
            p += chunk->data.len;
        } else if (c == 0x0E) {
            chunk->type = FMP_CHUNK_FIELD_REF_SIMPLE;
            p++;
            if (p + 3 > end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->ref_simple = copy_int(p, 2);
            p += 2;
            chunk->data.len = *p++;
            chunk->data.bytes = p;
            p += chunk->data.len;
        } else if (c == 0x0F && (p[1] & 0x80)) {
            chunk->type = FMP_CHUNK_DATA_SEGMENT;
            p += 2;
            if (p + 3 > end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->segment_index = *p++;
            chunk->data.len = copy_int(p, 2);
            p += 2;
            chunk->data.bytes = p;
            p += chunk->data.len;
        } else if (c == 0x10) {
            chunk->type = FMP_CHUNK_DATA_SIMPLE;
            p++;
            chunk->data.bytes = p;
            chunk->data.len = 3;
            p += chunk->data.len;
        } else if (c > 0x10 && c <= 0x15) {
            chunk->type = FMP_CHUNK_DATA_SIMPLE;
            p++;
            chunk->data.bytes = p;
            chunk->data.len = 2*(c-0x10)+1;
            p += chunk->data.len;
        } else if (c == 0x16) {
            chunk->type = FMP_CHUNK_FIELD_REF_LONG;
            p++;
            chunk->ref_long.bytes = p;
            chunk->ref_long.len = 3;
            p += chunk->ref_long.len;
            if (p >= end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->data.len = *p++;
            chunk->data.bytes = p;
            p += chunk->data.len;
        } else if (c == 0x19) {
            chunk->type = FMP_CHUNK_DATA_SIMPLE;
            p++;
            chunk->data.bytes = p;
            chunk->data.len = 9;
            p += chunk->data.len;
        } else if (c > 0x19 && c <= 0x1D) {
            chunk->type = FMP_CHUNK_DATA_SIMPLE;
            p++;
            if (p >= end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->data.len = *p++;
            chunk->data.bytes = p;
            p += chunk->data.len + 2*(c-0x19);
        } else if (c == 0x1E) {
            chunk->type = FMP_CHUNK_FIELD_REF_LONG;
            p++;
            if (p >= end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->ref_long.len = *p++;
            chunk->ref_long.bytes = p;
            p += chunk->ref_long.len;
            if (p >= end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->data.len = *p++;
            chunk->data.bytes = p;
            p += chunk->data.len;
        } else if (c == 0x1F) {
            chunk->type = FMP_CHUNK_FIELD_REF_LONG;
            p++;
            if (p >= end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->ref_long.len = *p++;
            chunk->ref_long.bytes = p;
            p += chunk->ref_long.len;
            if (p + 2 > end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->data.len = copy_int(p, 2);
            p += 2;
            chunk->data.bytes = p;
            p += chunk->data.len;
        } else if (c == 0x20 || c == 0xE0) {
            chunk->type = FMP_CHUNK_PATH_PUSH;
            p++;
            if (p >= end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            if (*p == 0xFE) {
                p++;
                chunk->data.len = 8;
            } else {
                chunk->data.len = 1;
            }
            chunk->data.bytes = p;
            p += chunk->data.len;
        } else if (c == 0x28) {
            chunk->type = FMP_CHUNK_PATH_PUSH;
            p++;
            chunk->data.bytes = p;
            chunk->data.len = 2;
            p += chunk->data.len;
        } else if (c == 0x30) {
            chunk->type = FMP_CHUNK_PATH_PUSH;
            p++;
            chunk->data.bytes = p;
            chunk->data.len = 3;
            p += chunk->data.len;
        } else if (c == 0x38) {
            chunk->type = FMP_CHUNK_PATH_PUSH;
            p++;
            if (p >= end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->data.len = *p++;
            chunk->data.bytes = p;
            p += chunk->data.len;
        } else if (c == 0x40) {
            chunk->type = FMP_CHUNK_PATH_POP;
            p++;
        } else if (c == 0x80) {
            p++;
            free(chunk);
            continue;
        } else {
            debug(" **** UNRECOGNIZED CODE 0x%02x @ [%llu] *****\n", c, p - block->payload);
            free(chunk);
            retval = FMP_ERROR_UNRECOGNIZED_CODE;
            break;
        }
        if (last_chunk) {
            last_chunk->next = chunk;
        }
        if (!first_chunk) {
            first_chunk = chunk;
        }
        last_chunk = chunk;
    }
    if (p > block->payload + block->payload_len) {
        retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
    }
    block->chunk = first_chunk;
    return retval;
}

fmp_error_t process_block_v3(fmp_block_t *block) {
    fmp_chunk_t *last_chunk = NULL;
    fmp_chunk_t *first_chunk = NULL;
    unsigned char *p = block->payload;
    unsigned char *end = block->payload + block->payload_len;
    fmp_error_t retval = FMP_OK;
    while (p < end) {
        unsigned char c = *p;
        fmp_chunk_t *chunk = calloc(1, sizeof(fmp_chunk_t));
        chunk->code = c;
        if (c == 0x00) {
            chunk->type = FMP_CHUNK_FIELD_REF_SIMPLE;
            p++;
            if (p >= end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->data.len = *p++;
            chunk->data.bytes = p;
            p += chunk->data.len;
        } else if (c == 0x01 && p[1] == 0xFF && p[2] == 0x05) {
            p += 8; // length check
            free(chunk);
            continue;
        } else if (c < 0x40) {
            chunk->type = FMP_CHUNK_FIELD_REF_LONG;
            chunk->ref_long.len = *p++;
            chunk->ref_long.bytes = p;
            p += chunk->ref_long.len;
            if (p >= end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->data.len = *p++;
            chunk->data.bytes = p;
            p += chunk->data.len;
        } else if (c < 0x80) {
            chunk->type = FMP_CHUNK_FIELD_REF_SIMPLE;
            chunk->ref_simple = *(p++) - 0x40;
            if (p >= end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            chunk->data.len = *p++;
            chunk->data.bytes = p;
            p += chunk->data.len;
        } else if (c < 0xC0) {
            chunk->type = FMP_CHUNK_DATA_SIMPLE;
            chunk->data.len = *(p++) - 0x80;
            chunk->data.bytes = p;
            p += chunk->data.len;
        } else if (c == 0xC0) {
            chunk->type = FMP_CHUNK_PATH_POP;
            p++;
        } else if (c < 0xFF) {
            chunk->type = FMP_CHUNK_PATH_PUSH;
            chunk->data.len = *(p++) - 0xC0;
            chunk->data.bytes = p;
            p += chunk->data.len;
        } else { // c == 0xFF
            if (p >= end) {
                retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                free(chunk);
                break;
            }
            c = *++p;
            if (!c) {
                fprintf(stderr, "Bad 0xFF chunk: %02x!\n", c);
                free(chunk);
                break;
            } else if (c <= 0x04) {
                chunk->type = FMP_CHUNK_FIELD_REF_LONG;
                chunk->ref_long.len = *p++;
                chunk->ref_long.bytes = p;
                p += chunk->ref_long.len;
                if (p + 1 >= end) {
                    retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                    free(chunk);
                    break;
                }
                chunk->data.len = copy_int(p, 2);
                chunk->data.bytes = (p += 2);
                p += chunk->data.len;
            } else if (c >= 0x40 && c <= 0x80) {
                chunk->type = FMP_CHUNK_FIELD_REF_SIMPLE;
                chunk->ref_simple = *(p++) - 0x40;
                if (p + 1 >= end) {
                    retval = FMP_ERROR_DATA_EXCEEDS_SECTOR_SIZE;
                    free(chunk);
                    break;
                }
                chunk->data.len = copy_int(p, 2);
                chunk->data.bytes = (p += 2);
                p += chunk->data.len;
            } else {
                fprintf(stderr, "Bad 0xFF chunk: %02x!\n", c);
                free(chunk);
                break;
            }
            chunk->extended = 1;
        }
        if (last_chunk) {
            last_chunk->next = chunk;
        }
        if (!first_chunk) {
            first_chunk = chunk;
        }
        last_chunk = chunk;
    }
    if (p != block->payload + block->payload_len) {
        retval = FMP_ERROR_BAD_SECTOR;
    }
    block->chunk = first_chunk;
    return retval;
}

fmp_error_t process_block(fmp_file_t *file, fmp_block_t *block) {
    if (!block)
        return FMP_ERROR_BAD_SECTOR;

    if (block->chunk) // already processed
        return FMP_OK;

    if (file->version_num >= 7)
        return process_block_v7(block);
    return process_block_v3(block);
}

fmp_block_t *new_block_from_sector(fmp_file_t *file, const uint8_t *sector, fmp_error_t *errorCode) {
    size_t payload_len = file->sector_size - file->sector_head_len;
    if (file->payload_len_offset != -1)
        payload_len = copy_int(&sector[file->payload_len_offset], 2);
    if (payload_len > file->sector_size - file->sector_head_len) {
        if (errorCode)
            *errorCode = FMP_ERROR_BAD_SECTOR;
        return NULL;
    }
    fmp_block_t *block = calloc(1, sizeof(fmp_block_t) + payload_len);
    block->payload_len = payload_len;
    block->deleted = sector[0];
    block->level = sector[1];
    block->prev_id = copy_int(&sector[file->prev_sector_offset], 4);
    block->next_id = copy_int(&sector[file->next_sector_offset], 4);
    memcpy(&block->payload, &sector[file->sector_head_len], payload_len);
    return block;
}
