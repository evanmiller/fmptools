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
#include <sys/errno.h>
#include <stdint.h>
#include <stdio.h>

enum {
    SQ0 = 0x01, SQ7 = 0x08,
    SDX = 0x0B,
    SQU = 0x0E,
    SCU = 0x0F,
    SC0 = 0x10, SC7 = 0x17,
    SD0 = 0x18, SD7 = 0x1F };

enum {
    UC0 = 0xE0, UC7 = 0x0E7,
    UD0 = 0xE8, UD7 = 0xEF,
    UQU = 0xF0,
    UDX = 0xF1 };

static uint16_t offset_table(uint8_t x) {
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
    return offset;
}

static uint32_t extended_offset(uint8_t hbyte, uint8_t lbyte) {
    return 10000 + 80 * ((hbyte & 0x1F) * 100 + lbyte);
}

/* Implementation of A Standard Compression Scheme for Unicode
 * https://www.unicode.org/reports/tr6/tr6-4.html */
size_t convert_scsu_to_utf8(
        char **restrict inbuf, size_t *restrict inbytesleft,
        char **restrict outbuf, size_t *restrict outbytesleft) {
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
    uint32_t dynamic_window_offsets[] = {
        0x0080, /* Latin-1 Supplement */
        0x00C0, /* partial Latin-1 Supplemenet + Latin Extended A */
        0x0400, /* Cyrillic */
        0x0600, /* Arabic */
        0x0900, /* Devanagari */
        0x3040, /* Hiragana */
        0x30A0, /* Katakana */
        0xFF00, /* Fullwidth ASCII */
    };

    uint8_t *src = *(uint8_t **)inbuf;
    uint8_t *dst = *(uint8_t **)outbuf;

    uint8_t shift = 0;
    uint8_t unicode = 0;
    uint8_t active_window = 0;
    errno = 0;
    while (*inbytesleft && *outbytesleft) {
        uint8_t c = *src++; *inbytesleft -= 1;
        uint32_t u = 0; // Unicode code point
        uint16_t high_surrogate = 0; // For UTF-16 surrogate pairs
        if (unicode) {
            if (c == UQU) {
                if (*inbytesleft >= 2) {
                    u = (*src++ << 8);
                    u += *src++;
                    *inbytesleft -= 2;
                } else { errno = EINVAL; break; }
            } else if (c >= UC0 && c <= UC7) {
                active_window = (c - UC0);
                unicode = 0;
                continue;
            } else if (c >= UD0 && c <= UD7) {
                if (*inbytesleft >= 1) {
                    dynamic_window_offsets[active_window = (c - UD0)] = offset_table(*src++);
                    *inbytesleft -= 1;
                    unicode = 0;
                    continue;
                } else { errno = EINVAL; break; }
            } else if (c == UDX) {
                if (*inbytesleft >= 2) {
                    dynamic_window_offsets[active_window = ((c & 0xE0) >> 5)] =
                        extended_offset(src[0], src[1]);
                    src += 2;
                    *inbytesleft -= 2;
                    unicode = 0;
                    continue;
                } else { errno = EINVAL; break; }
            } else {
                if (*inbytesleft >= 1) {
                    u = (c << 8) + *src++;
                    *inbytesleft -= 1;
                } else { errno = EINVAL; break; }
            }
        } else if (shift) {
            u = static_window_offsets[shift - SQ0] + c;
            shift = 0;
        } else if (c == SCU) {
            unicode = 1;
            continue;
        } else if (c == SQU) {
            if (*inbytesleft >= 2) {
                u = (*src++ << 8);
                u += *src++;
                *inbytesleft -= 2;
            } else { errno = EINVAL; break; }
        } else if (c >= SQ0 && c <= SQ7) {
            shift = c;
            continue;
        } else if (c >= SC0 && c <= SC7) {
            active_window = (c - SC0);
            continue;
        } else if (c >= SD0 && c <= SD7) {
            if (*inbytesleft >= 1) {
                dynamic_window_offsets[active_window = (c - SD0)] = offset_table(*src++);
                *inbytesleft -= 1;
                continue;
            } else { errno = EINVAL; break; }
        } else if (c == SDX) {
            if (*inbytesleft >= 2) {
                dynamic_window_offsets[active_window = ((c & 0xE0) >> 5)] =
                    extended_offset(src[0], src[1]);
                src += 2;
                *inbytesleft -= 2;
                continue;
            } else { errno = EINVAL; break; }
        } else if (c == 0x0A || c == 0x0D || c == 0x09) {
            u = ' '; /* Encode as space, hack */
        } else if (c >= 0x20 && c <= 0x7F) { /* ASCII, pass through */
            u = c;
        } else if (c >= 0x80) {
            u = dynamic_window_offsets[active_window] + (c - 0x80);
        } else {
            u = 0xFFFD;
        }

        /* UTF-16 surrogate pair */
        if (u >= 0xDC00 && u <= 0xDFFF) {
            u = (high_surrogate << 10) + (u & 0x3FF);
        } else if (u >= 0xD800 && u <= 0xDBFF) {
            high_surrogate = (u & 0x3FF);
            continue;
        }

        high_surrogate = 0;

        /* Encode u as UTF-8 */
        if (u >= 0x10000) { /* Four bytes */
            if (*outbytesleft >= 4) {
                *dst++ = 0xF0 | ((u & 0x1C0000) >> 18);
                *dst++ = 0x80 | ((u & 0x03F000) >> 12);
                *dst++ = 0x80 | ((u & 0x000FC0) >> 6);
                *dst++ = 0x80 | ((u & 0x00003F) >> 0);
                *outbytesleft -= 4;
            } else { errno = E2BIG; break; }
        } else if (u >= 0x0800) { /* Three bytes */
            if (*outbytesleft >= 3) {
                *dst++ = 0xE0 | ((u & 0xF000) >> 12);
                *dst++ = 0x80 | ((u & 0x0FC0) >> 6);
                *dst++ = 0x80 | ((u & 0x003F) >> 0);
                *outbytesleft -= 3;
            } else { errno = E2BIG; break; }
        } else if (u >= 0x0080) { /* Two bytes */
            if (*outbytesleft >= 2) {
                *dst++ = 0xC0 | ((u & 0x07C0) >> 6);
                *dst++ = 0x80 | ((u & 0x003F) >> 0);
                *outbytesleft -= 2;
            } else { errno = E2BIG; break; }
        } else if (u > 0) {
            *dst++ = (u & 0x7F);
            *outbytesleft -= 1;
        }
    }
    *outbuf = (char *)dst;
    *inbuf = (char *)src;

    return errno ? (size_t)-1 : 0;
}
