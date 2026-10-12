/* spells.h - what the game knows about each spell: the read-only spell tables of the original
 * data segment, as the Spells class.
 *
 * A spell is 0-0x2F, six to a circle: 0 In Lor ... 0x2F An Tym; it is also the index into the
 * mixed spells (party.mixed_spells). `spells` is the one Spells object. */
#ifndef U5_SPELLS_H
#define U5_SPELLS_H

#include <stdint.h>
#include "strings.h"

class Spells {
public:
    enum { COUNT = 0x30 };
    /* Bits of where(): the places a spell can be cast in. */
    enum Where : uint8_t { COMBAT = 1, DUNGEONS = 2, TOWNS = 4, OUTDOORS = 8 };

    static Spells &instance();

    unsigned circle(unsigned spell) const { return spell / 6 + 1; }     /* 1-8: magic points and level needed */
    uint8_t where(unsigned spell) const { return spell < COUNT ? where_[spell] : 0; }       /* ds:1C90 */
    uint8_t recipe(unsigned spell) const { return spell < COUNT ? recipes_[spell] : 0; }    /* ds:1CC0  bit per reagent */

    Strings::Id name_id(unsigned spell) const { return spell < COUNT ? names_[spell] : Strings::NONE; }   /* ds:19E2 */
    const char *name(unsigned spell) const { return strings[name_id(spell)]; }
    /* The first letters of the spell's words in alphabetical order, as typed to cast it: "IL"
     * for In Lor (ds:1C30). */
    const char *letters(unsigned spell) const { return strings[spell < COUNT ? letters_[spell] : Strings::NONE]; }
    /* The word of power a letter stands for: 'A' "AN" ... 'Z' "ZU"; J and O have none
     * (ds:1BFC, which the original indexes from ds:1B7A by the letter's code). */
    const char *syllable(unsigned letter) const
    {
        return strings[letter >= 'A' && letter <= 'Z' ? syllables_[letter - 'A'] : Strings::NONE];
    }

    /* The eight reagents, 0 Sulfur Ash ... 7 Mandrake; bit 0x80 >> n of a recipe (ds:19D2). */
    enum { REAGENTS = 8 };
    Strings::Id reagent_name_id(unsigned reagent) const { return reagent < REAGENTS ? reagent_names_[reagent] : Strings::NONE; }
    const char *reagent_name(unsigned reagent) const { return strings[reagent_name_id(reagent)]; }

    /* How fast each of the 21 rays of a cone spell spreads sideways (ds:1CF0, trace_spell_cone). */
    enum { CONE_RAYS = 21 };
    int16_t cone_step(unsigned ray) const { return ray < CONE_RAYS ? cone_steps_[ray] : 0; }

    /* name_id and reagent_name_id as plain functions, for the list pages (stats.h NameTable). */
    static Strings::Id name_of(int spell);
    static Strings::Id reagent_name_of(int reagent);

    Spells(const Spells &) = delete;
    Spells &operator=(const Spells &) = delete;

private:
    Spells();

    static uint8_t where_[COUNT], recipes_[COUNT];
    static Strings::Id names_[COUNT], letters_[COUNT], syllables_[26], reagent_names_[REAGENTS];
    static int16_t cone_steps_[CONE_RAYS];
};

/* The one Spells object (Spells::instance()). */
extern Spells &spells;

#endif
