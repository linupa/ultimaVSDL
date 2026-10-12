/* monsters.h - what the game knows about each kind of monster: the read-only monster tables of
 * the original data segment, as the Monsters class.
 *
 * A monster type is 0-0x2F: 0x00-0x0F are people (mage, bard, ... guard, Lord British), 0x10 on
 * the creatures; a map object's tile gives it as (tile - 0x40) >> 2 (tiles 0x100 up). The
 * original reads the byte and word tables with any byte as the index, so each of them here is
 * the 256 entries that follow its start in DATA.OVL and gives what the original read for every
 * index. `monsters` is the one Monsters object. */
#ifndef U5_MONSTERS_H
#define U5_MONSTERS_H

#include <stdint.h>
#include "strings.h"

class Monsters {
public:
    enum { COUNT = 0x30 };

    static Monsters &instance();

    /* The eight stats of a type (ds:13BC, 8 bytes each). */
    uint8_t strength(uint8_t type) const { return stats_[type][0]; }
    uint8_t speed(uint8_t type) const { return stats_[type][1]; }          /* dexterity: the combatant's speed, +-4 */
    uint8_t intelligence(uint8_t type) const { return stats_[type][2]; }
    uint8_t armour(uint8_t type) const { return stats_[type][3]; }
    uint8_t damage(uint8_t type) const { return stats_[type][4]; }
    uint8_t hp(uint8_t type) const { return stats_[type][5]; }             /* hit points; experience is hp / 4 + 1 */
    uint8_t group_size(uint8_t type) const { return stats_[type][6]; }     /* most that appear together */
    uint8_t treasure(uint8_t type) const { return stats_[type][7]; }       /* what is left behind */

    /* ds:153C: 0x8000 undead (turned), 0x2000 teleports, 0x1000 vanishes, 0x10 divides, ... */
    uint16_t flags(uint8_t type) const { return flags_[type]; }
    uint8_t attack_range(uint8_t type) const { return attack_range_[type]; }   /* ds:159C */
    uint8_t missile(uint8_t type) const { return missile_[type]; }             /* ds:15CC  kind of missile it throws */
    uint8_t companion(uint8_t type) const { return companion_[type]; }         /* ds:16D4  the kind that may join its group */
    uint8_t ambusher(unsigned n) const { return ambushers_[n & 7]; }           /* ds:1734  who ambushes a resting party */

    /* "Sea Serpent" (ds:1856) and, for a group met outdoors, "SEA SERPENTS" (ds:18B6). Out of
     * range gives Strings::NONE. */
    Strings::Id name_id(unsigned type) const { return type < COUNT ? names_[type] : Strings::NONE; }
    Strings::Id group_name_id(unsigned type) const { return type < COUNT ? group_names_[type] : Strings::NONE; }
    const char *name(unsigned type) const { return strings[name_id(type)]; }
    const char *group_name(unsigned type) const { return strings[group_name_id(type)]; }

    Monsters(const Monsters &) = delete;
    Monsters &operator=(const Monsters &) = delete;

private:
    Monsters();

    static uint8_t stats_[256][8], attack_range_[256], missile_[256], companion_[256], ambushers_[8];
    static uint16_t flags_[256];
    static Strings::Id names_[COUNT], group_names_[COUNT];
};

/* The one Monsters object (Monsters::instance()). */
extern Monsters &monsters;

#endif
