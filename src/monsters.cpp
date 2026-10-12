/* monsters.cpp - the monster tables. See monsters.h. */
#include "gamedata.h"
#include "monsters.h"

Monsters &Monsters::instance()
{
    static Monsters the_monsters;
    return the_monsters;
}

Monsters &monsters = Monsters::instance();

Monsters::Monsters()
{
    Data &data = Data::instance();   /* not a global: this runs before main, in no set order */
    data.add(0x13BC, stats_, "monsters.stats", Data::SHARED);
    data.add(0x16D4, companion_, "monsters.companion", Data::SHARED);
    data.add(0x1734, ambushers_, "monsters.ambushers");
    data.add(0x153C, flags_, "monsters.flags", Data::SHARED);
    data.add(0x159C, attack_range_, "monsters.attack_range", Data::SHARED);
    data.add(0x15CC, missile_, "monsters.missile", Data::SHARED);
    data.add(0x1856, names_, "monsters.names");
    data.add(0x18B6, group_names_, "monsters.group_names");
}

uint8_t Monsters::stats_[256][8];   /* ds:13BC */

uint16_t Monsters::flags_[256];   /* ds:153C */

uint8_t Monsters::attack_range_[256];   /* ds:159C */

uint8_t Monsters::missile_[256];   /* ds:15CC */

uint8_t Monsters::companion_[256];   /* ds:16D4 */

uint8_t Monsters::ambushers_[8];   /* ds:1734 */

Strings::Id Monsters::names_[Monsters::COUNT];   /* ds:1856 */

Strings::Id Monsters::group_names_[Monsters::COUNT];   /* ds:18B6 */
