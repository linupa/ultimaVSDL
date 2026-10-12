/* items.h - what the game knows about each item: the read-only item tables of the original
 * data segment, as the Items class.
 *
 * An item id is the index into the inventory (party.inventory): 0x00-0x03 helms, 0x04-0x08 shields,
 * 0x09-0x0F armour, 0x10-0x29 weapons, 0x2A-0x2C rings, 0x2D-0x2F amulets; 0xFF is "none".
 * The original reads its byte tables with any byte as the index: the missile in flight
 * (combat.attack_item) goes past the last item for spells, and an empty hand (0xFF) is looked up as
 * it is. So each byte table here is the 256 bytes that follow its start in DATA.OVL, and gives
 * what the original read for every index. `items` is the one Items object. */
#ifndef U5_ITEMS_H
#define U5_ITEMS_H

#include <stdint.h>
#include "strings.h"

class Items {
public:
    enum { COUNT = 0x30, NONE = 0xFF };

    static Items &instance();

    uint8_t damage(uint8_t item) const { return damage_[item]; }      /* ds:15FC  a weapon's damage; 0 = not a weapon */
    uint8_t range(uint8_t item) const { return range_[item]; }        /* ds:1664  reach of an attack; 0 = hand to hand */
    uint8_t missile(uint8_t item) const { return missile_[item]; }    /* ds:169C  kind of missile drawn */
    uint8_t hands(uint8_t item) const { return hands_[item]; }        /* ds:1A7E  where it is worn or held (ready_item) */
    uint8_t weight(uint8_t item) const { return weight_[item]; }      /* ds:1AAE  strength it takes to carry */
    uint8_t ready_mark(uint8_t item) const { return ready_mark_[item]; }   /* ds:1AE8  rune shown by an item in use (Ready list) */

    /* The names (ds:17F6) and the short ones of the Arms page (ds:1962; 0x30-0x37 are the eight
     * colours). Out of range gives Strings::NONE. */
    Strings::Id name_id(unsigned item) const { return item < COUNT ? names_[item] : Strings::NONE; }
    Strings::Id short_name_id(unsigned item) const { return item < SHORT_NAMES ? short_names_[item] : Strings::NONE; }
    const char *name(unsigned item) const { return strings[name_id(item)]; }
    const char *short_name(unsigned item) const { return strings[short_name_id(item)]; }
    /* What an armourer calls the item when buying it (ds:3CCE); Strings::NONE = by its name. */
    Strings::Id trade_name_id(unsigned item) const { return item < COUNT ? trade_names_[item] : Strings::NONE; }
    /* Base price in gold (ds:3A82); 0 = not bought or sold. */
    uint16_t price(unsigned item) const { return item < COUNT ? prices_[item] : 0; }

    /* The other things carried, as listed on the Items page (special_list): 0-7 the
     * scrolls ("*VL"), 8-15 the potions ("!", shown by colour), then the carpet, the keys, Lord
     * British's things, the moonstones, the shards and the rest (ds:1916). */
    enum { SPECIALS = 0x26 };
    /* The Items page's list, filled by collect_special_items: a count per special item, or 0xFF
     * for one owned and shown without a count (ds:B9EE). */
    uint8_t special_list[SPECIALS] = {};
    Strings::Id special_name_id(unsigned n) const { return n < SPECIALS ? special_names_[n] : Strings::NONE; }
    /* A potion's colour, 0 "blue" ... 7 "white", and a scroll's spell in letters, 0 "VL" ... 7
     * "AT" (ds:419C, ds:41AC). The original runs the two tables together: colours 8-15 are the
     * scrolls. */
    const char *potion_colour(unsigned n) const { return strings[n < 16 ? found_names_[n] : Strings::NONE]; }
    const char *scroll_letters(unsigned n) const { return strings[found_names_[8 + (n & 7)]]; }

    /* short_name_id and special_name_id as plain functions, for the list pages (stats.h NameTable). */
    static Strings::Id short_name_of(int item);
    static Strings::Id special_name_of(int n);

    Items(const Items &) = delete;
    Items &operator=(const Items &) = delete;

private:
    Items();

    enum { SHORT_NAMES = 0x38 };
    static uint8_t damage_[256], range_[256], missile_[256], hands_[256], weight_[256], ready_mark_[256];
    static Strings::Id names_[COUNT], short_names_[SHORT_NAMES], trade_names_[COUNT];
    static Strings::Id special_names_[SPECIALS], found_names_[16];
    static uint16_t prices_[COUNT];
};

/* The one Items object (Items::instance()). */
extern Items &items;

#endif
