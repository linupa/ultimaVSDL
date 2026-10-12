/* items.cpp - the item tables. See items.h. */
#include "gamedata.h"
#include "items.h"

Items &Items::instance()
{
    static Items the_items;
    return the_items;
}

Items &items = Items::instance();

Items::Items()
{
    Data &data = Data::instance();   /* not a global: this runs before main, in no set order */
    data.add(0x15FC, damage_, "items.damage", Data::SHARED);
    data.add(0x1664, range_, "items.range", Data::SHARED);
    data.add(0x169C, missile_, "items.missile", Data::SHARED);
    data.add(0x17F6, names_, "items.names");
    data.add(0x1916, special_names_, "items.special_names");
    data.add(0x1962, short_names_, "items.short_names");
    data.add(0x1A7E, hands_, "items.hands", Data::SHARED);
    data.add(0x1AAE, weight_, "items.weight", Data::SHARED);
    data.add(0x1AE8, ready_mark_, "items.ready_mark", Data::SHARED);
    data.add(0x3A82, prices_, "items.prices");
    data.add(0x3CCE, trade_names_, "items.trade_names");
    data.add(0x419C, found_names_, "items.found_names");
}

Strings::Id Items::short_name_of(int item) { return items.short_name_id((unsigned)item); }
Strings::Id Items::special_name_of(int n) { return items.special_name_id((unsigned)n); }

uint8_t Items::damage_[256];   /* ds:15FC */

uint8_t Items::range_[256];   /* ds:1664 */

uint8_t Items::missile_[256];   /* ds:169C */

uint8_t Items::hands_[256];   /* ds:1A7E */

uint8_t Items::weight_[256];   /* ds:1AAE */

uint8_t Items::ready_mark_[256];   /* ds:1AE8 */

Strings::Id Items::names_[Items::COUNT];   /* ds:17F6 */

Strings::Id Items::short_names_[Items::SHORT_NAMES];   /* ds:1962 */

Strings::Id Items::trade_names_[Items::COUNT];   /* ds:3CCE */

uint16_t Items::prices_[Items::COUNT];   /* ds:3A82 */

Strings::Id Items::special_names_[Items::SPECIALS];   /* ds:1916 */

Strings::Id Items::found_names_[16];   /* ds:419C, ds:41AC */
