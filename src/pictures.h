/* pictures.h - the names of the game's picture files, as the Pictures class.
 *
 * The original keeps them as a table of string pointers (ds:25EA) and, for the four-colour
 * drivers, cuts every name ending in ".16" down to ".4" in place before anything is loaded. The
 * same table tells the loader which files are compressed: exactly these. `pictures` is the one
 * Pictures object. */
#ifndef U5_PICTURES_H
#define U5_PICTURES_H

#include <string>

class Pictures {
public:
    /* In the table's order. */
    enum File {
        STORY_FONT,       /* PROPORT.PCS: the proportional font of the story pages */
        BRITISH_BITMAP,   /* BRITISH.BIT: Lord British's signature */
        TITLE_BITMAP,     /* TITLE.BIT */
        TILES,            /* TILES.16: the tile set */
        DUNGEON_DOORS,    /* ITEMS.16: doors and things in the dungeon view */
        DUNGEON_WALLS_1, DUNGEON_WALLS_2, DUNGEON_WALLS_3,   /* DNG1-3.16: walls, by dungeon style */
        MONSTER_0, MONSTER_1, MONSTER_2, MONSTER_3, MONSTER_4, MONSTER_5, MONSTER_6, MONSTER_7,   /* MON0-7.16 */
        CREATE,           /* CREATE.16: character creation */
        STORY_BACKGROUND, /* TEXT.16: the scroll behind the story text */
        STORY_1, STORY_2, STORY_3, STORY_4, STORY_5, STORY_6,   /* STORY1-6.16: the introduction */
        ENDING_1, ENDING_2,   /* END1-2.16 */
        LOGO,             /* ULTIMA.16: the title screen's logo and banner */
        START_SCROLL,     /* STARTSC.16: the acknowledgements scroll */
        END_SCROLL,       /* ENDSC.16 */
        COUNT
    };

    static Pictures &instance();

    /* The file's name as it is now ("TILES.16", or "TILES.4" after use_four_colour_files). */
    const char *name(unsigned file) const { return file < COUNT ? names_[file].c_str() : ""; }
    const char *dungeon(unsigned n) const { return name(DUNGEON_DOORS + (n & 3)); }   /* 0 doors, 1-3 walls of that style */
    const char *monster(unsigned n) const { return name(MONSTER_0 + (n & 7)); }       /* a dungeon monster, by its tile & 7 */
    const char *story(unsigned n) const { return n < 6 ? name(STORY_1 + n) : ""; }
    const char *ending(unsigned n) const { return name(ENDING_1 + (n & 1)); }

    /* For CGA and Hercules: every name ending in ".16" becomes ".4" (the start of intro_main). */
    void use_four_colour_files();
    /* Is this one of the picture files? Those are the compressed ones (any case). */
    bool has(const char *name) const;

    Pictures(const Pictures &) = delete;
    Pictures &operator=(const Pictures &) = delete;

private:
    Pictures();

    std::string names_[COUNT];
};

/* The one Pictures object (Pictures::instance()). */
extern Pictures &pictures;

#endif
