/* misc.h - small main-program utilities: capped arithmetic, string search, time seed and random numbers. */
#ifndef U5_MISC_H
#define U5_MISC_H

#include <stdint.h>

void add_byte_capped(uint8_t max, uint8_t n, uint8_t *p);     /* 3ef0 */
void add_word_capped(uint16_t max, uint16_t n, uint16_t *p);  /* 3f14: signed compare */
void sub_byte_floor0(uint8_t n, uint8_t *p);                  /* 3f36 */
void sub_word_floor0(uint16_t n, uint16_t *p);                /* 3f54: signed compare */
int  str_find_nocase(const char *haystack, const char *needle);   /* 6f1e: offset or -1 (see misc.cpp) */
unsigned time_seed(void);                                     /* 2056: from the clock, 0-0xFFF */
void set_random_seed(uint16_t seed);                          /* 207e */
int  random_between(int hi, int lo);                          /* 2092: lo..hi inclusive */

#endif
