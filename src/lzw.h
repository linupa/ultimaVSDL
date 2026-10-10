/* lzw.h - the LZW decompression used for the game's .16/.4 graphics files (read_file_buffered,
 * 0000:135a). */
#ifndef U5_LZW_H
#define U5_LZW_H

#include <stddef.h>
#include <stdint.h>

/* File: 4-byte little-endian decompressed size, then LZW codes packed least-significant bit first.
 * Codes start 9 bits wide and grow to 12; 0x100 resets the dictionary, 0x101 ends the data.
 * Writes at most out_size bytes; returns the number of bytes produced, or -1 if the data is bad. */
long lzw_decompress(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size);

/* Reads a whole compressed game file into a new malloc'd buffer (*size = decompressed size). */
uint8_t *lzw_load_file(const char *name, size_t *size);

#endif
