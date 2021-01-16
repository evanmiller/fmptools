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

#include <sys/types.h>
#include <stdint.h>

enum { SQ0 = 0x01, SQ1, SQ2, SQ3, SQ4, SQ5, SQ6, SQ7 };
enum { SQU = 0x0E };
enum { SCU = 0x0F };
enum { SC0 = 0x10, SC1, SC2, SC3, SC4, SC5, SC6, SC7 };
enum { SD0 = 0x18, SD1, SD2, SD3, SD4, SD5, SD6, SD7 };

/* Implementation of https://www.unicode.org/reports/tr6/tr6-4.html */
void convert_scsu_to_utf8(char *dst, size_t dst_len, uint8_t *src, size_t src_len) {
    uint8_t *output_bytes = (uint8_t *)dst;
    const uint16_t static_window_offsets[] = {
        0x0000, /* Quoting tags */
        0x0080, /* Latin-1 Supplement */
        0x0100, /* Latin Extended-A */
        0x0300, /* Combining Diacritical Marks */
        0x2000, /* General Punctuation */
        0x2080, /* Currency Symbols */
        0x2100, /* Letterlike Symbols and Number Forms */
        0x3000, /* CJK Symbols and Punctuation */
    };
    uint16_t dynamic_window_offsets[] = {
        0x0080, /* Latin-1 Supplement */
        0x00C0, /* partial Latin-1 Supplemenet + Latin Extended A */
        0x0400, /* Cyrillic */
        0x0600, /* Arabic */
        0x0900, /* Devanagari */
        0x3040, /* Hiragana */
        0x30A0, /* Katakana */
        0xFF00, /* Fullwidth ASCII */
    };
    uint8_t shift = 0;
    uint8_t active_window = 0;
    for (int i=0; i<src_len; i++) {
        uint8_t c = src[i];
        uint16_t u = 0; // Unicode code point
        if (shift) {
            u = static_window_offsets[shift - SQ0] + c;
            shift = 0;
        } else if (c == SQU && i + 2 < src_len) {
            u = (src[i+1] << 8) + src[i+2];
            i += 2;
        } else if (c >= SQ0 && c <= SQ7) {
            shift = c;
            continue;
        } else if (c >= SC0 && c <= SC7) {
            active_window = (c - SC0);
            continue;
        } else if (c >= SD0 && c <= SD7 && ++i < src_len) {
            uint8_t x = src[i];
            uint16_t offset = 0;
            if (x > 0 && x < 0x68) {
                offset = x*0x80;
            } else if (x < 0xA8) {
                offset = x*0x80 + 0xAC00;
            } else if (x == 0xF9) {
                offset = 0xC0;
            } else if (x == 0xFA) {
                offset = 0x0250;
            } else if (x == 0xFB) {
                offset = 0x0370;
            } else if (x == 0xFC) {
                offset = 0x0530;
            } else if (x == 0xFD) {
                offset = 0x3040;
            } else if (x == 0xFE) {
                offset = 0x30A0;
            } else if (x == 0xFF) {
                offset = 0xFF60;
            } else {
                /* Reserved */
            }
            dynamic_window_offsets[active_window = (c - SD0)] = offset;
            continue;
        } else if (c == 0x0A || c == 0x0D || c == 0x09) {
            u = ' '; /* Encode as space, hack */
        } else if (c >= 0x20 && c <= 0x7F) { /* ASCII, pass through */
            u = c;
        } else if (c >= 0x80) {
            u = dynamic_window_offsets[active_window] + (c - 0x80);
        } else {
            u = 0xFFFD;
        }

        /* Encode u as UTF-8 */
        if (u >= 0x0800) { /* Three bytes */
            *output_bytes++ = 0xE0 | ((u & 0xF000) >> 12);
            *output_bytes++ = 0x80 | ((u & 0x0FC0) >> 6);
            *output_bytes++ = 0x80 | ((u & 0x003F) >> 0);
        } else if (u >= 0x0080) { /* Two bytes */
            *output_bytes++ = 0xC0 | ((u & 0x07C0) >> 6);
            *output_bytes++ = 0x80 | ((u & 0x003F) >> 0);
        } else if (u > 0) {
            *output_bytes++ = (u & 0x7F);
        }
    }
    *output_bytes++ = '\0';
}
