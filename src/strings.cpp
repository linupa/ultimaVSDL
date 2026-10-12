/* strings.cpp - the game's text. See strings.h. */
#include <stdio.h>
#include <string.h>
#include "gamedata.h"
#include "strings.h"

/* Where DATA.OVL keeps text: [start, end) ds offsets, each run of strings with only the zero bytes
 * that align them in between. Every Id lies in one of these. */
const Strings::Region Strings::regions_[] = {
    {0x0042, 0x1393},
    {0x174C, 0x17F5},
    {0x2626, 0x2651},
    {0x265A, 0x2742},
    {0x2768, 0x27DC},
    {0x27E2, 0x28C5},
    {0x2946, 0x29F5},
    {0x29FE, 0x2A4A},
    {0x2A53, 0x2B35},
    {0x2B3E, 0x2BC0},
    {0x2C58, 0x2C76},
    {0x2C7C, 0x2DE8},
    {0x2F31, 0x2F97},
    {0x3105, 0x31A4},
    {0x31B5, 0x325C},
    {0x3278, 0x3303},
    {0x3306, 0x33A1},
    {0x33B8, 0x3458},
    {0x345E, 0x348A},
    {0x3495, 0x34C3},
    {0x34CF, 0x3508},
    {0x3514, 0x353E},
    {0x354A, 0x356F},
    {0x357B, 0x359A},
    {0x359F, 0x35A4},
    {0x35AE, 0x35CE},
    {0x35D3, 0x35D8},
    {0x35E2, 0x3602},
    {0x360C, 0x3655},
    {0x3676, 0x367D},
    {0x3976, 0x399F},
    {0x39A8, 0x39F1},
    {0x39FB, 0x3A05},
    {0x3A11, 0x3A26},
    {0x3D5A, 0x3D85},
    {0x41D4, 0x4449},
    {0x4450, 0x4501},
    {0x451A, 0x4592},
    {0x459A, 0x4882},
    {0x4895, 0x4A9F},
    {0x4D84, 0x4DC4},
    {0x4DCA, 0x4E6E},
    {0x4E86, 0x50EA},
    {0x5338, 0x5350},
    {0x54B6, 0x54D3},
    {0x5554, 0x559E},
    {0x6ADA, 0x70D8},
    {0x70E2, 0x7688},
    {0x769A, 0x76C5},
    {0x76D6, 0x77EE},
    {0x77F8, 0x9609},
    {0x9618, 0xA0E0},
    {0xA110, 0xA3BA},
    {0xA3C4, 0xA44A},
};

const int Strings::region_count_ = (int)(sizeof regions_ / sizeof regions_[0]);

Strings &Strings::instance()
{
    static Strings strings;
    return strings;
}

Strings &strings = Strings::instance();

Strings::Strings()
{
    Data &data = Data::instance();   /* not a global: this runs before main, in no set order */
    unsigned total = 0;
    for (int i = 0; i < region_count_; i++) total += regions_[i].end - regions_[i].start;
    text_.resize(total);
    unsigned at = 0;
    for (int i = 0; i < region_count_; i++) {
        unsigned size = regions_[i].end - regions_[i].start;
        data.add(regions_[i].start, size, [this, at](const uint8_t *b, unsigned n) { memcpy(&text_[at], b, n); }, "strings");
        at += size;
    }
}

const char *Strings::get(Id id) const
{
    if (id == NONE) return "";
    unsigned at = 0;
    for (int i = 0; i < region_count_; i++) {
        if (id >= regions_[i].start && id < regions_[i].end) return &text_[at + (id - regions_[i].start)];
        at += regions_[i].end - regions_[i].start;
    }
    fprintf(stderr, "Strings: no string 0x%04X\n", (unsigned)id);
    return "";
}
