/* misc.cpp - small main-program utilities. */
#include <string.h>
#include <time.h>
#include "misc.h"

void add_byte_capped(uint8_t max, uint8_t n, uint8_t *p)
{
    *p = (unsigned)*p + n < max ? (uint8_t)(*p + n) : max;
}

void add_word_capped(uint16_t max, uint16_t n, uint16_t *p)
{
    *p = (int16_t)(*p + n) < (int16_t)max ? (uint16_t)(*p + n) : max;
}

void sub_byte_floor0(uint8_t n, uint8_t *p)
{
    *p = n < *p ? (uint8_t)(*p - n) : 0;
}

void sub_word_floor0(uint16_t n, uint16_t *p)
{
    *p = (int16_t)n < (int16_t)*p ? (uint16_t)(*p - n) : 0;
}

static int fold(int c)
{
    c &= 0x7F;
    return c > 0x60 ? c & 0x5F : c;
}

/* Case-insensitive search ignoring bit 7. Not a plain search: after a partial match it resumes
 * past the matched characters (offset += matched + 1), so e.g. "AB" isn't found in "AAB". An empty
 * needle isn't handled by the original (it runs off the end); the port returns -1. */
int str_find_nocase(const char *haystack, const char *needle)
{
    int nlen = (int)strlen(needle), hlen = (int)strlen(haystack);
    if (nlen > hlen || nlen == 0) return -1;
    int limit = hlen - nlen, pos = 0;
    do {
        int matched = 0;
        while (matched < nlen && fold((unsigned char)haystack[pos + matched]) == fold((unsigned char)needle[matched]))
            matched++;
        if (matched == nlen) return pos;
        pos += matched + 1;
    } while (pos <= limit);
    return -1;
}

/* DOS time: ((sec << 11 | hundredths) + (hour << 9 | min << 2)) ^ 0x91EB, low 12 bits. */
unsigned time_seed(void)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    unsigned hundredths = (unsigned)(clock() / (CLOCKS_PER_SEC / 100 ? CLOCKS_PER_SEC / 100 : 1)) % 100;
    unsigned a = (unsigned)((t->tm_sec << 3) & 0xFF) << 8 | hundredths;
    unsigned b = (unsigned)((t->tm_hour << 1) & 0xFF) << 8 | ((t->tm_min << 2) & 0xFF);
    return ((a + b) ^ 0x91EB) & 0xFFF;
}

/* The random number generator's state (ds:5420): seeded from the clock at start-up. */
static uint16_t rand_state = 0;

void set_random_seed(uint16_t seed) { rand_state = seed; }

/* state = ror3(state + 0x9248) ^ 0x9248 + 0x11; result = (state & 0x7FFF) % (hi - lo + 1) + lo */
int random_between(int hi, int lo)
{
    uint16_t a = (uint16_t)(rand_state + 0x9248);
    a = (uint16_t)(a >> 3 | a << 13);
    rand_state = (uint16_t)((a ^ 0x9248) + 0x11);
    return (int)((rand_state & 0x7FFF) % (uint16_t)(hi - lo + 1)) + lo;
}
