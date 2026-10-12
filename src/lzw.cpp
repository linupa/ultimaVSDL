/* lzw.cpp - LZW decompression of the game's graphics files. Port of read_file_buffered (0000:135a).
 *
 * The original reads the file through a 1 KB buffer, fetches each code as a 24-bit window at
 * bit_pos / 8 shifted right by bit_pos % 8 and masked by width (masks ds:53AC), and keeps the
 * dictionary as 3-byte entries (prefix code word, last byte) in a 0x3000-byte segment. When a
 * code isn't in the dictionary yet (the "KwKwK" case) it uses the previous string plus its first
 * byte. The width grows when the next free code reaches 2^width, up to 12 bits; past that the
 * original keeps adding entries without bound, which these files never reach. */
#include <stdlib.h>
#include "files.h"
#include "lzw.h"

#define MAX_CODES 4096

long lzw_decompress(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size)
{
    static uint16_t prefix[MAX_CODES];
    static uint8_t suffix[MAX_CODES];
    uint8_t stack[MAX_CODES];
    size_t bit_pos = 32, n = 0;   /* codes start after the 4-byte size */
    int width = 9, next_code = 0x102, limit = 0x200, old_code = -1;
    uint8_t first = 0;

    for (;;) {
        size_t byte = bit_pos / 8;
        if (byte >= in_size) return -1;     /* ran out of input without an end code */
        uint32_t window = in[byte];
        if (byte + 1 < in_size) window |= (uint32_t)in[byte + 1] << 8;
        if (byte + 2 < in_size) window |= (uint32_t)in[byte + 2] << 16;
        int code = (int)((window >> (bit_pos % 8)) & ((1u << width) - 1));
        bit_pos += (size_t)width;

        if (code == 0x101) return (long)n;
        if (code == 0x100) {
            width = 9;
            limit = 0x200;
            next_code = 0x102;
            old_code = -1;
            continue;
        }
        if (old_code < 0) {   /* first code after a reset is a literal */
            if (code > 0xFF) return -1;
            if (n < out_size) out[n] = (uint8_t)code;
            n++;
            old_code = code;
            first = (uint8_t)code;
            continue;
        }

        int sp = 0, c = code;
        if (code >= next_code) {   /* KwKwK: previous string + its first byte */
            stack[sp++] = first;
            c = old_code;
        }
        while (c > 0xFF) {
            if (c >= next_code || sp >= MAX_CODES - 1) return -1;
            stack[sp++] = suffix[c];
            c = prefix[c];
        }
        stack[sp++] = (uint8_t)c;
        first = (uint8_t)c;
        while (sp > 0) {
            if (n < out_size) out[n] = stack[sp - 1];
            n++;
            sp--;
        }
        if (next_code < MAX_CODES) {
            prefix[next_code] = (uint16_t)old_code;
            suffix[next_code] = first;
        }
        next_code++;
        old_code = code;
        if (next_code >= limit && width < 12) {
            width++;
            limit <<= 1;
        }
    }
}

uint8_t *lzw_load_file(const char *name, size_t *size)
{
    FILE *f = game_fopen(name, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *in = len > 4 ? (uint8_t *)malloc((size_t)len) : NULL;
    if (!in || fread(in, 1, (size_t)len, f) != (size_t)len) {
        free(in);
        fclose(f);
        return NULL;
    }
    fclose(f);
    size_t want = (size_t)in[0] | (size_t)in[1] << 8 | (size_t)in[2] << 16 | (size_t)in[3] << 24;
    uint8_t *out = want ? (uint8_t *)malloc(want) : NULL;
    long got = out ? lzw_decompress(in, (size_t)len, out, want) : -1;
    free(in);
    if (got != (long)want) {
        free(out);
        return NULL;
    }
    *size = want;
    return out;
}
