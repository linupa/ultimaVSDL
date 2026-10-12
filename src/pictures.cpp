/* pictures.cpp - the names of the picture files. See pictures.h. */
#include <ctype.h>
#include "pictures.h"

static const char *const initial_names[Pictures::COUNT] = {   /* ds:25EA */
    "PROPORT.PCS", "BRITISH.BIT", "TITLE.BIT", "TILES.16", "ITEMS.16", "DNG1.16", "DNG2.16", "DNG3.16",
    "MON0.16", "MON1.16", "MON2.16", "MON3.16", "MON4.16", "MON5.16", "MON6.16", "MON7.16",
    "CREATE.16", "TEXT.16", "STORY1.16", "STORY2.16", "STORY3.16", "STORY4.16", "STORY5.16", "STORY6.16",
    "END1.16", "END2.16", "ULTIMA.16", "STARTSC.16", "ENDSC.16",
};

Pictures::Pictures()
{
    for (int i = 0; i < COUNT; i++) names_[i] = initial_names[i];
}

Pictures &Pictures::instance()
{
    static Pictures the_pictures;
    return the_pictures;
}

Pictures &pictures = Pictures::instance();

void Pictures::use_four_colour_files()
{
    for (std::string &name : names_) {
        size_t dot = name.find('.');
        if (dot != std::string::npos && name[dot + 1] == '1') name = name.substr(0, dot + 1) + "4";
    }
}

bool Pictures::has(const char *name) const
{
    for (const std::string &entry : names_) {
        const char *a = entry.c_str(), *b = name;
        for (; *a && *b; a++, b++)
            if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) break;
        if (*a == *b) return true;
    }
    return false;
}
