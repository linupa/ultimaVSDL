/* town.cpp - NPC placement, lamps, horses, stairs and movement in towns (TOWN.OVL). */
#include <string.h>
#include "combat.h"
#include "commands.h"
#include "dungeon.h"
#include "endings.h"
#include "game.h"
#include "gamedata.h"
#include "gametime.h"
#include "gfx.h"
#include "input.h"
#include "map.h"
#include "misc.h"
#include "npc.h"
#include "objects.h"
#include "party.h"
#include "savegame.h"
#include "shrine.h"
#include "sound.h"
#include "status.h"
#include "strings.h"
#include "talk.h"
#include "text.h"
#include "town.h"
#include "view.h"
#include "world.h"

static void add_table_runs(Data &data);

Town::Town()
{
    SaveGame &save = SaveGame::instance();   /* not `savegame`: this runs before main, in no set order */
    save.add(0x58EE, lamp_x, "town.lamp_x");
    save.add(0x590E, lamp_y, "town.lamp_y");
    save.add(0x592E, lamp_tile, "town.lamp_tile");
    save.add(0x594E, lamp_count, "town.lamp_count");
    save.add(0x5953, stored_ship_x, "town.stored_ship_x");
    save.add(0x5954, stored_ship_y, "town.stored_ship_y");
    save.add(0x5957, drunk_turns, "town.drunk_turns");
    save.add(0x5958, shadowlord_here, "town.shadowlord_here");
    save.add(0x6605, stored_ship, "town.stored_ship");

    Data &data = Data::instance();   /* not a global: this runs before main, in no set order */
    add_table_runs(data);
    data.add(0x275A, secret_tune, "town.secret_tune");
}

Town &Town::instance()
{
    static Town the_town;
    return the_town;
}

Town &town = Town::instance();

uint8_t Town::secret_tune[13];   /* ds:275A */

/* The read-only tables of the settlements and their shops, taken from DATA.OVL in six runs and
 * read by their old ds offsets (the Table names), as in Dungeon. An offset outside the runs
 * gives 0. */
static uint8_t tables_21CA[0x280];
static uint8_t tables_2742[0x18];
static uint8_t tables_27DC[0x8];
static uint8_t tables_28C2[0x84];
static uint8_t tables_3A32[0x374];
static uint8_t tables_4C1E[0x26A];

static const struct { unsigned start, size; const uint8_t *data; } table_runs[] = {
    {0x21CA, sizeof tables_21CA, tables_21CA},
    {0x2742, sizeof tables_2742, tables_2742},
    {0x27DC, sizeof tables_27DC, tables_27DC},
    {0x28C2, sizeof tables_28C2, tables_28C2},
    {0x3A32, sizeof tables_3A32, tables_3A32},
    {0x4C1E, sizeof tables_4C1E, tables_4C1E},
};

/* The runs come from DATA.OVL when the game starts (gamedata.h); called by the constructor. */
static void add_table_runs(Data &data)
{
    data.add(0x21CA, tables_21CA, "town.tables_21CA");
    data.add(0x2742, tables_2742, "town.tables_2742");
    data.add(0x27DC, tables_27DC, "town.tables_27DC", Data::SHARED);
    data.add(0x28C2, tables_28C2, "town.tables_28C2", Data::SHARED);
    data.add(0x3A32, tables_3A32, "town.tables_3A32", Data::SHARED);
    data.add(0x4C1E, tables_4C1E, "town.tables_4C1E", Data::SHARED);
}

uint8_t Town::table_byte(unsigned ds_offset)
{
    for (const auto &run : table_runs)
        if (ds_offset - run.start < run.size) return run.data[ds_offset - run.start];
    return 0;
}

uint16_t Town::table_word(unsigned ds_offset)
{
    return (uint16_t)(table_byte(ds_offset) | table_byte(ds_offset + 1) << 8);
}

/* bit n of a per-location 32-bit table at ds offset base */
int Town::location_bit(uint16_t base, int npc)
{
    uint32_t bits = (uint32_t)table_word(base + map.id() * 4u) | (uint32_t)table_word(base + map.id() * 4u + 2) << 16;
    return (bits >> (npc & 31)) & 1;
}

/* Only people (type 0x0E and >= 0x40) can be dead (npc_dead_bits, set by mark_npc_dead). */
int Town::npc_is_dead(int npc)
{
    if (npcs.type[npc] == 0x0E || npcs.type[npc] > 0x3F)
        return (npcs.dead_bits[map.id()] >> (npc & 31)) & 1;
    return 0;
}

/* People only: not guards (0x70-0x73), not monsters (>= 0x80) except 0xB4-0xB7. */
void Town::mark_npc_dead(int npc)
{
    if (npc < 0 || npc >= 0x20) return;
    uint8_t kind = npcs.type[npc] & 0xFC;
    if ((kind != 0x70 && kind < 0x80) || kind == 0xB4) npcs.dead_bits[map.id()] |= (uint32_t)1 << npc;
}

/* Guards and low types attack in place (6), others chase (7); the change times are cleared, so
 * the schedule stays on entry 0. */
void Town::npc_make_hostile(int npc)
{
    uint8_t a = npcs.type[npc] < 0x2F ? 6 : 7;
    memset(npcs.schedules[npc].times, 0, 4);
    memset(npcs.schedules[npc].activity, a, 3);
}

/* People (0x40-0x73) who have a schedule, or are hostile (0xFE), become "Don't hurt me!" (0xFD)
 * and keep away (activity 3). */
void Town::npc_make_fleeing(int npc)
{
    int has_times = 0;
    for (int i = 0; i < 4; i++) has_times |= npcs.schedules[npc].times[i] != 0;
    if (npcs.type[npc] < 0x74 && npcs.type[npc] > 0x3F && (npcs.state[npc].dialogue == 0xFE || has_times)) {
        npcs.state[npc].dialogue = 0xFD;
        memset(npcs.schedules[npc].activity, 3, 3);
    }
}

/* Guards (0x70), daemons (0xD8) and type 0xFC turn hostile; others flee half the time. */
void Town::alert_town(void)
{
    for (int npc = 0; npc < 0x20; npc++) {
        if (npcs.state[npc].state == 0) continue;
        uint8_t t = npcs.type[npc];
        if (t == 0xFC || t == 0xD8 || t == 0x70) npc_make_hostile(npc);
        else if (random_between(0xFF, 0) < 0x80) npc_make_fleeing(npc);
    }
}

void Town::npc_begone(int npc)
{
    print_text(strings[Strings::Begone_vermin_278A]);   /* "Begone, vermin!" */
    damage_party();
    npc_make_fleeing(npc);
}

void Town::remove_npc(int npc)
{
    NpcState *s = &npcs.state[npc];
    MapObject *o = &map.objects[s->object];
    s->dialogue = s->z = s->y = s->x = s->state = 0;
    memset(o, 0, sizeof *o);
    memset(npcs.schedules[npc].activity, 0, 3);
    npcs.type[npc] = 0;
    view.rebuild = true;
}

int Town::npc_for_object(int object)
{
    for (int i = 0; i < 32; i++)
        if (npcs.state[i].object == object && npcs.type[i] != 0 && npcs.state[i].state != 0) return i;
    return -1;
}

/* Tiles under 0x87 toggle by XOR 0xDD; the lamps found at load (0x48/0x49) go to tile 3 at hour 20
 * and back at hour 5, unless the party stands on one. */
void Town::update_lamps(void)
{
    for (unsigned x = 0; x < 0x20; x++)
        for (unsigned y = 0; y < 0x20; y++)
            if (*map.get_map_tile_ptr(y, x) == 0x87) *map.get_map_tile_ptr(y + 1, x) ^= 0xDD;
    if ((*map.get_map_tile_ptr(map.y, map.x) & 0xFE) != 0x48)
        for (unsigned i = 0; i < town.lamp_count; i++)
            *map.get_map_tile_ptr(town.lamp_y[i], town.lamp_x[i]) = game_time.hour == 5 ? town.lamp_tile[i] : 3;
    view.rebuild = true;
}

/* Stairs 0xC4-0xC7 face a direction: walking in that facing goes up, the opposite way down. */
void Town::take_stairs(int tile, unsigned facing)
{
    if ((tile & 0xFC) != 0xC4) return;
    world_tick();
    if ((unsigned)(tile - 0xC4) == facing) {
        map.z++;
        print_text(strings[Strings::Up_265A]);   /* "Up!" */
    } else if ((unsigned)(tile - 0xC4) == (facing ^ 2)) {
        map.z--;
        print_text(strings[Strings::Down_265F]);   /* "Down!" */
    } else {
        return;
    }
    map.load_town_level(1);
}

/* Horses (0x10), carpets (0x14) and boats turn to face the way they move; dir 0 N 1 E 2 S 3 W. */
void Town::set_vehicle_facing(int dir)
{
    uint8_t kind = party.transport & 0xFC;
    if (kind == 0x10) {
        print_text(strings[Strings::Ride_2666]);   /* "Ride " */
        if (dir == 1) party.transport = 0x12;
        else if (dir == 3) party.transport = 0x13;
    } else if (kind == 0x14) {
        print_text(strings[Strings::Fly_266C]);   /* "Fly " */
        if (dir == 1) party.transport = 0x14;
        else if (dir == 3) party.transport = 0x15;
    } else if (kind == 0x20 || kind == 0x24 || kind == 0x28) {
        if (kind == 0x28) print_text(strings[Strings::Row_2671]);   /* "Row " */
        party.transport = (uint8_t)(dir + kind);
    }
}

int Town::town_move(int dir)
{
    int dx = 0, dy = 0, at_edge = 0, facing = 0;
    const char *text = NULL;
    if (party.transport == 0x1C || (party.transport & 0xFE) == 0x12) step_sound();
    switch (dir) {
    case 1: dx = -1; at_edge = map.x == 0; facing = 3; text = strings[Strings::West_268A]; break;   /* West */
    case 2: dx = 1; at_edge = map.x > 0x1E; facing = 1; text = strings[Strings::East_2684]; break;  /* East */
    case 3: dy = -1; at_edge = map.y == 0; facing = 0; text = strings[Strings::North_2676]; break;   /* North */
    case 4: dy = 1; at_edge = map.y > 0x1E; facing = 2; text = strings[Strings::South_267D]; break;  /* South */
    }
    if (text) {
        set_vehicle_facing(facing);
        print_text(text);
    }
    int free = 1;
    uint8_t tile = view.mask[0xA5 + dx + dy * 0x20];   /* the square next to the party, as seen */
    int obj = find_object_at(map.z, (uint8_t)(map.y + dy), (uint8_t)(map.x + dx));
    if (obj != 0) {
        free = 0;
        if (party.transport >= 0x20 && party.transport < 0x30) {   /* boats may push into other boats */
            if (!(party.transport < 0x28 || obj < 0x24 || obj > 0x27)) free = 1;
        } else if ((obj >= 0x24 && obj <= 0x2B) || obj == 0x1B || (obj & 0xFE) == 0x10 || obj == 0x1E || obj == 0x1F) {
            free = 1;   /* boarding: ships, skiffs, horses, carpets */
        }
    }
    if (free && tile_passable(tile, party.transport)) {
        if (!at_edge) {
            map.x = (uint8_t)(map.x + dx);
            map.y = (uint8_t)(map.y + dy);
            view.rebuild = true;
            take_stairs(tile, (unsigned)facing);
            return 0;
        }
        print_text(strings[Strings::Dost_thou_wish_to_leave_2690]);   /* "Dost thou wish to leave? " */
        int key;
        do {
            key = get_command_key();
        } while (key != 'Y' && key != 'N' && key != 0x1B);
        if (key == 'Y') {
            print_text(strings[Strings::Yes_Exit_to_26AB]);   /* "Yes\n\nExit to\n" */
            if (map == Map::ARARAT) {
                print_text(strings[Strings::Underworld_26B9]);   /* "Underworld!" */
                map.z = 0xFF;
            } else {
                print_text(strings[Strings::Britannia_26C6]);   /* "Britannia!" */
                map.z = 0;
            }
            unsigned loc = map.id();
            map.set(Map::BRITANNIA);
            map.x = map.exit_x((uint8_t)loc);
            map.y = map.exit_y((uint8_t)loc);
            map.open_door_tile = 0;
            return at_edge;
        }
        print_text(strings[Strings::No_26D2]);   /* "No" */
    } else {
        print_text(strings[Strings::Blocked_26D6]);   /* "Blocked!" */
        beep(200, 0xA5);
        flush_keyboard();
    }
    return 0;
}

int Town::is_hitching_tile(unsigned y, unsigned x)
{
    uint8_t t = *map.get_map_tile_ptr(y, x);
    return t == 0xA2 || t == 0x43;
}

/* Horses not next to a hitching post move one square at random half the time. */
void Town::wander_horses(void)
{
    for (int i = 0; i < 0x20; i++) {
        MapObject *o = &map.objects[i];
        uint8_t tile = o->tile;
        if ((tile & 0xFE) != 0x10 || o->z != map.z || random_between(1, 0) != 0) continue;
        int x = o->x, y = o->y;
        if (is_hitching_tile((unsigned)(y + 1), (unsigned)x) || is_hitching_tile((unsigned)y, (unsigned)(x + 1)) ||
            is_hitching_tile((unsigned)(y - 1), (unsigned)x) || is_hitching_tile((unsigned)y, (unsigned)(x - 1)))
            continue;
        if (random_between(1, 0) == 0) {
            y += random_between(1, 0) * 2 - 1;
        } else {
            int d = random_between(1, 0) * 2 - 1;
            x += d;
            tile = d < 1 ? 0x11 : 0x10;   /* face the way it moves */
        }
        if (x < 0x20 && y < 0x20 && x >= 0 && y >= 0 &&
            tile_passable(*map.get_map_tile_ptr((unsigned)y, (unsigned)x), 0x10) &&
            object_tile_at(map.z, (unsigned)y, (unsigned)x) == 0) {
            o->tile2 = tile;
            o->tile = tile;
            o->x = (uint8_t)x;
            o->y = (uint8_t)y;
            view.rebuild = true;
        }
    }
}

void Town::place_npcs(void)
{
    for (int i = 1; i < 0x20; i++) {
        set_object_entry(i, 0, 0, 0, 0, 0, 0);
        npcs.state[i].object = 0;
    }
    for (int i = 1; i < 0x20; i++) {
        if (npcs.type[i] == 0) continue;
        uint8_t slot = npcs.current_schedule_slot(game_time.hour, i);
        NpcSchedule *sc = &npcs.schedules[i];
        place_npc(sc->z[slot], sc->y[slot], sc->x[slot], i);
        npcs.state[i].state = 1;
        npcs.state[i].sched_slot = slot;
        npcs.path_pos[i] = 0xFFFF;
    }
}

/* Gives the NPC a map object while it is on the party's floor. The object's hull byte is 0x1E for
 * type 1, else 0xFF if the NPC's bit is set in the per-location table at ds:28C2 (meaning not
 * known yet), else 0. */
void Town::place_npc(unsigned z, unsigned y, unsigned x, int npc)
{
    if (npcs.type[npc] == 0) return;
    NpcState *s = &npcs.state[npc];
    if (z != map.z && s->object != 0) {
        map.objects[s->object].tile = 0;
        s->object = 0;
    }
    if (z == map.z && s->object == 0) {
        int slot = alloc_object_slot();
        uint8_t hull = npcs.type[npc] == 1 ? 0x1E : location_bit(NPC_HIDDEN_BITS, npc) ? 0xFF : 0;
        set_object_entry(slot, hull, (uint8_t)z, (uint8_t)y, (uint8_t)x, npcs.type[npc], npcs.type[npc]);
        map.objects[slot].flags = 0;
        s->object = (uint16_t)slot;
    }
    if (z == map.z && s->object != 0) {
        MapObject *o = &map.objects[s->object];
        o->x = (uint8_t)x;
        o->y = (uint8_t)y;
        o->z = (uint8_t)z;
    }
    s->x = (uint16_t)x;
    s->y = (uint16_t)y;
    s->z = (uint16_t)z;
    s->state = 1;
}

/* With a Shadowlord in town most of the flowers and crops (tiles 0x2D, 0x2E) are withered (0x2C,
 * 0x2B). The pattern is seeded by the day so it stays the same all day. */
void Town::shadowlord_blight(void)
{
    if (town.shadowlord_here == 0xFF) return;
    set_random_seed(game_time.day);
    for (int i = 0; i < 0x400; i++) {
        if (map.buffer[i] == 0x2D) {
            if (random_between(7, 0) != 0) map.buffer[i] = 0x2C;
        } else if (map.buffer[i] == 0x2E && random_between(7, 0) != 0) {
            map.buffer[i] = 0x2B;
        }
    }
    view.rebuild = true;
    set_random_seed((uint16_t)time_seed());
}

/* A Shadowlord in this town (not when the party arrives at row 4, i.e. in a cell) gets the highest
 * free NPC slot: an object 0xFC at column 15, at the row from ds:13A5[location], standing still. */
void Town::check_shadowlord_arrival(void)
{
    town.shadowlord_here = 0xFF;
    if (map.y != 4)
        for (int i = 0; i < 3; i++)
            if (map.shadowlord_locations[i] == map.id()) {
                town.shadowlord_here = (uint8_t)i;
                break;
            }
    if (town.shadowlord_here == 0xFF) return;
    shadowlord_blight();
    for (int i = 0; i < 0x20; i++)
        if (map.objects[i].tile == 0xFC) return;
    int obj = alloc_object_slot();
    int npc = 0x1F;
    for (int i = 0x1F; i >= 0; i--)
        if (npcs.type[i] == 0) {
            npc = i;
            break;
        }
    MapObject *o = &map.objects[obj];
    NpcState *st = &npcs.state[npc];
    NpcSchedule *sc = &npcs.schedules[npc];
    st->state = 1;
    st->object = (uint16_t)obj;
    o->x = 0x0F;
    st->x = 0x0F;
    o->y = map.shadowlord_row();
    st->y = o->y;
    o->skiffs = 0;
    o->z = 0;
    o->hull = 0;
    o->flags = 0;
    st->z = 0;
    o->tile = o->tile2 = 0xFC;
    memset(sc->times, 0, 4);
    for (int i = 0; i < 3; i++) {
        sc->activity[i] = 6;
        sc->x[i] = 0x0F;
        sc->y[i] = o->y;
        sc->z[i] = 0;
    }
    npcs.type[npc] = 0xFC;
}

void Town::fight_npc(int npc)
{
    mark_npc_dead(npc);
    combat.combat_with_object(npcs.state[npc].object);
    remove_npc(npc);
    map.load_town_level(0);
    check_shadowlord_arrival();
}

/* Only NPCs with a schedule can be swayed, half the time. The type test reads npc_type[4]
 * whatever the NPC (the loop counter is left in the index register): a bug in the original. */
int Town::npc_swayed_by_shadowlord(int npc)
{
    int swayed = 0;
    for (int i = 0; i < 4; i++)
        if (npcs.schedules[npc].times[i] != 0) swayed = 1;
    if (npcs.type[4] < 0x40 || npcs.type[4] > 0x73) swayed = 0;
    if (random_between(1, 0) != 0) swayed = 0;
    return swayed;
}

/* Hatred (1) turns townspeople hostile (dialogue 0xFE), Cowardice (2) makes them flee (0xFD).
 * Falsehood (0) acts in the shops. */
void Town::apply_shadowlord_influence(void)
{
    if (town.shadowlord_here != 1 && town.shadowlord_here != 2) return;
    for (int i = 0; i < 0x20; i++) {
        if (!npc_swayed_by_shadowlord(i)) continue;
        if (town.shadowlord_here == 1) {
            npc_make_hostile(i);
            npcs.state[i].dialogue = 0xFE;
        } else {
            npc_make_fleeing(i);
            npcs.state[i].dialogue = 0xFD;
        }
    }
}

void Town::print_shadowlord_air(int shadowlord)
{
    print_text(strings[Strings::An_air_of_27B8]);   /* "An air of " */
    print_text(strings[Strings::Id(table_word(SHADOWLORD_AIR + shadowlord * 2))]);
    print_text(strings[Strings::doth_surround_thee_27C4]);   /* " doth surround thee..." */
    sound_sweep(1, 2000, -0x15A0, 1, 0x19C8);
}

void Town::enter_town(int fresh)
{
    view.rebuild = true;
    town.just_entered = 1;
    if (fresh) {
        for (int i = 1; i < 0x20; i++) map.objects[i].tile = 0;
        town.drunk_turns = 0;
        view.world_tick_enabled = 1;
        npcs.load_town_npcs();
        npcs.init_npc_states(game_time.hour);
    }
    town.shadowlord_here = 0xFF;
    map.load_town_level(fresh);
    check_shadowlord_arrival();
    for (int i = 0; i < 0x20; i++)
        if (npc_is_dead(i)) remove_npc(i);
    if (map == Map::STONEGATE && party.lb_sceptre != 0) remove_npc(9);
    draw_status_bar();
    if (check_party_state() < 0) return;
    world_tick();
    if (map == Map::STONEGATE) {   /* Stonegate: every Shadowlord still about */
        for (int i = 2; i >= 0; i--)
            if (map.shadowlord_locations[i] < 0x80) print_shadowlord_air(i);
    } else if (town.shadowlord_here != 0xFF) {
        print_shadowlord_air(town.shadowlord_here);
        apply_shadowlord_influence();
    }
}

/* In Blackthorn's castle capture means the interrogation. Elsewhere: going quietly puts the
 * party in the gaol at Yew (location 4) at 8:00 without its keys; refusing alerts the town
 * (returns 1: the guard attacks). */
int Town::arrest_party(void)
{
    if (map == Map::PALACE_OF_BLACKTHORN) {
        if (check_party_state() < 0) return 0;
        endings.captured_by_blackthorn();
    } else {
        print_text(strings[Strings::Thou_art_under_arrest_27E2]);   /* "\"Thou art under arrest!\"" */
        print_text(strings[Strings::Wilt_thou_come_quietly_27FE]);   /* "\"Wilt thou come quietly?\"" */
        char c;
        do {
            c = (char)get_command_key();
            if (game_quit()) return 0;
        } while (c != 'N' && c != 'Y');
        if (c != 'Y') {
            print_text(strings[Strings::No_Then_defend_thyself_285E]);   /* "No! Then defend thyself, rogue!" */
            alert_town();
            return 1;
        }
        print_text(strings[Strings::Yes_The_guard_strikes_281B]);   /* "Yes The guard strikes thee unconscious!" */
        set_draw_color(0);
        print_text(strings[Strings::Thou_dost_awaken_to_2845]);   /* "Thou dost awaken to..." */
        map.set(Map::YEW);
        map.x = 0x19;
        map.y = 4;
        view.rebuild = true;
        while (game_time.hour != 8) pass_time(0x14);
        party.keys = 0;
        map.z = 0;
    }
    enter_town(1);
    return 0;
}

/* An NPC reached the party (npc_event_slot), or a conversation ended badly (mode 1). An attacker
 * ('a') who was turned by the Shadowlord of Hatred only shoves; a guard (tile 0x70) arrests;
 * anyone else fights. Otherwise the NPC talks first (mode 0) and a bad end means arrest. */
void Town::npc_encounter(int mode)
{
    int fight = 0, npc = npcs.event_slot;
    int obj = npcs.state[npc].object;
    world_tick();
    if (npcs.event == 'a') {
        if (npcs.state[npc].dialogue == 0xFE) {
            put_char('\n');
            npc_begone(npc);
        } else if (map.objects[obj].tile != 0x70) {
            fight = 1;
        } else {
            fight = arrest_party();
        }
    } else if (mode == 0) {
        if (npcs.state[npc].dialogue != 0 && conversation.talk_to_npc(npc) != 0) fight = arrest_party();
    } else {
        fight = arrest_party();
    }
    if (fight) {
        npc = npcs.event_slot;
        if (map.objects[npcs.state[npc].object].tile < 0x40) {
            remove_npc(npc);
        } else {
            print_text(strings[Strings::Attacked_2881]);   /* "Attacked!" */
            fight_npc(npc);
        }
    }
}

int Town::cmd_attack_town(void)
{
    Direction dir = {0, 0};
    print_text(strings[Strings::Attack_26E0]);   /* "Attack-" */
    if (*map.get_map_tile_ptr(map.y, map.x) < 4 && party.transport != 0x1C) {
        print_text(strings[Strings::On_foot_26E8]);   /* "On foot!\n" */
        return 0;
    }
    if (ask_direction(&dir) == 0) return 1;
    unsigned x = (unsigned)(map.x + dir.dx) & 0xFFFF, y = (unsigned)(map.y + dir.dy) & 0xFFFF;
    if (*map.get_map_tile_ptr(y, x) == 0x9D) {   /* a mirror */
        *map.get_map_tile_ptr(y, x) = 0x9F;
        print_text(strings[Strings::Broken_26F2]);   /* "Broken!\n" */
        for (int f = 2000; f < 20000; f += 1000) sound_noise(f, 0x78, 0x28);
        view.rebuild = true;
        return 1;
    }
    uint16_t slot;
    int tile = find_object_at(map.z, (uint8_t)y, (uint8_t)x, &slot);
    int nothing = 1, npc = -1;
    if (tile != 0) {
        npc = npc_for_object((int)slot);
        if (tile > 0x3F && (tile < 0xE8 || tile > 0xEF) && (tile & 0xFC) != 0xB4) nothing = 0;
    }
    if (nothing) {
        print_text(strings[Strings::Nothing_to_attack_26FB]);   /* "Nothing to attack!\n" */
        return 1;
    }
    if (tile < 0x80) {   /* a person: karma -5 and the town turns on the party */
        sub_byte_floor0(5, &party.karma);
        alert_town();
    } else if ((tile & 0xFC) == 0xD8) {
        alert_town();
    }
    uint8_t ground = *map.get_map_tile_ptr(y, x);
    if (ground < 0x84 || (ground > 0x85 && ground != 0x9F && ground != 0xAB)) {
        if (npc < 0) return 1;
        mark_npc_dead(npc);
        fight_npc(npc);
        return 1;
    }
    /* someone in the stocks, behind the broken mirror or in bed can't fight back */
    if (tile == 0x78) {
        print_text(strings[Strings::Missed_270F]);   /* "Missed!\n" */
        return 1;
    }
    print_text(strings[Strings::Murdered_2718]);   /* "Murdered!\n" */
    sub_byte_floor0(5, &party.karma);
    flash_square((int)y, (int)x);
    if (npc < 0) return 1;
    mark_npc_dead(npc);
    remove_npc(npc);
    return 1;
}

/* On a ladder (0xC8 up, 0xC9 down) or a grate (0x86, down) the party climbs at once; otherwise
 * a direction is asked and fences and such (0x4C, 0xCA, 0xCB) can be climbed over. */
int Town::cmd_klimb_town(void)
{
    Direction dir = {0, 0};
    print_text(strings[Strings::Klimb_2723]);   /* "Klimb-" */
    if ((party.transport & 0xFE) == 0x12) {
        print_text(strings[Strings::On_foot_272A]);   /* "-On foot!\n" */
        return 0;
    }
    uint8_t t = *map.get_map_tile_ptr(map.y, map.x);
    if (t == 0x86 || t == 0xC9 || t == 0xC8) {
        dungeon.facing = t == 0xC8 ? 0 : 2;
        take_stairs(0xC4, dungeon.facing);
        return 1;
    }
    if (ask_direction(&dir) != 0) {
        t = *map.get_map_tile_ptr((unsigned)(map.y + dir.dy) & 0xFFFF, (unsigned)(map.x + dir.dx) & 0xFFFF);
        if (t != 0x4C && t != 0xCA && t != 0xCB) {
            print_text(strings[Strings::What_2735]);   /* "What?\n" */
            return 0;
        }
        map.x = (uint8_t)(map.x + dir.dx);
        map.y = (uint8_t)(map.y + dir.dy);
        view.rebuild = true;
        world_tick();
    }
    return 1;
}

/* While drunk, half the keys are replaced by a random direction ("Hic!") and the town is
 * alerted. */
unsigned Town::town_read_key(int redraw)
{
    if (redraw != 0) {
        world_tick();
        status.refresh();
        put_char('\n');
        draw_label_left();
    }
    unsigned key = (unsigned)get_command_key();
    if (town.drunk_turns != 0 && random_between(1, 0) != 0) {
        alert_town();
        town.drunk_turns--;
        print_text(strings[Strings::Hic_273C]);   /* "Hic!\n" */
        key = table_byte(DRUNK_KEY + random_between(3, 0));   /* 3 4 2 1 */
    }
    return key;
}

/* Digits set the active player, except facing a harpsichord (0x8D to the south), where they
 * play notes: the 13 notes of secret_tune (Stones) open the secret panel in Lord British's
 * castle (location 0x11, level 2). Returns 3: no turn passes and the prompt isn't redrawn. */
int Town::town_number_key(int key)
{
    if (view.mask[6 * 0x20 + 5] != 0x8D) return cmd_set_active_player(key);
    unsigned note = (unsigned)(key - '0');
    if (sound.enabled != 0) sound_sweep(-4, 20000, 4000, 1, table_word(NOTE_PITCH + note * 2));
    if (Town::secret_tune[town.tune_progress] == note) {
        if (++town.tune_progress == 0x0D) {
            town.tune_progress = 0;
            if (map == Map::LORD_BRITISHS_CASTLE && map.z == 2) {
                map.buffer[0x1B1] ^= 0x0B;
                screen_shake();
                view.rebuild = true;
            }
        }
    } else if (town.tune_progress == 10 && note == 8) {
        town.tune_progress = 3;
    } else if (town.tune_progress == 11 && note == 7) {
        town.tune_progress = 2;
    } else if (note == Town::secret_tune[0]) {
        town.tune_progress = 1;
    } else {
        town.tune_progress = 0;
    }
    return 3;
}

/* Sleepers wake 1 time in 16. A trapdoor (0x8C, not on a carpet) drops the party a level; in
 * Stonegate (0x1D) it drops them into lava and everyone dies. Swamp poisons, fire and lava burn. */
void Town::town_tile_effects(void)
{
    for (unsigned i = 0; i < party.size(); i++)
        if (party[i].asleep() && random_between(0x0F, 0) == 0x0F) party[i].status = 'G';
    int again;
    do {
        again = 0;
        uint8_t t = *map.get_map_tile_ptr(map.y, map.x);
        if (t == 0x8C && (party.transport & 0xFE) != 0x14) {
            print_text(strings[Strings::A_TRAPDOOR_2768]);   /* "A TRAPDOOR!\n" */
            uint8_t transport = party.transport;
            party.transport = 0;
            world_tick();
            damage_party();
            party.transport = transport;
            if (map == Map::STONEGATE) {
                set_draw_color(0);
                fill_rect(0xB7, 0xB7, 8, 8);
                for (int f = 1000; f > 0xFA; f--) {   /* a long falling whistle */
                    speaker_on((unsigned)f);
                    delay_loop(0x28, 1);
                    if (game_quit()) break;
                }
                speaker_off();
                memset(map.buffer, 0x8F, 0x400);
                view.rebuild = true;
                for (int i = 0; i < 0x20; i++) map.objects[i].tile = 0;   /* the original clears 0x100 bytes, 1 apart */
                for (unsigned i = 0; i < party.size(); i++) {
                    party[i].hp = 0;
                    party[i].status = 'D';
                    sound_noise(500, 3000, 0x28);
                    draw_status_bar();
                }
            } else {
                map.z--;
                map.load_town_level(1);
                again = 1;
            }
        } else if (t == 4 && party.transport == 0x1C) {
            for (unsigned i = 0; i < party.size(); i++) {
                PartyMember *m = &party[i];
                if (!m->dead() && !m->poisoned() && m->dex < (unsigned)random_between(0x1D, 0)) {
                    print_text(strings[Strings::Poisoned_2775]);   /* "Poisoned!\n" */
                    m->status = 'P';
                    draw_status_bar();
                }
            }
        } else if (t == 0xBC || t == 0x8F) {
            world_tick();
            print_text(strings[Strings::Burning_2780]);   /* "Burning!\n" */
            damage_party();
        }
    } while (again);
    per_turn_status();
}

/* The end of a town turn: a minute passes, the lamps, the tile under the party, the open door,
 * then the townspeople. On a carpet or horse the town moves only every other turn (unless the
 * party passed); Time Stop freezes it and Quickness halves it. result: 1 a turn, 2 a talk that
 * ended in arrest. */
void Town::town_end_turn(int key, unsigned result)
{
    static int carpet_skip, quick_skip;
    if (map.is_outdoors() || result == 0) return;
    if (check_party_state() == -1) return;
    uint8_t hour = game_time.hour;
    pass_time(1);
    if (hour != game_time.hour && (game_time.hour == 0x14 || game_time.hour == 5)) update_lamps();
    town_tile_effects();
    if (map.open_door_tile != 0 && --map.open_door_timer == 0)
        restore_open_door(map.open_door_y, map.open_door_x, map.open_door_tile);
    map.objects[0].x = map.x;
    map.objects[0].y = map.y;
    map.objects[0].z = map.z;
    int go = party.transport < 0x12 || party.transport > 0x15 || key == ' ' || (carpet_skip = !carpet_skip, !carpet_skip);
    go = go && party.active_spell != 0x54 && (party.active_spell != 0x51 || (quick_skip = !quick_skip, !quick_skip));
    if (go) {
        wander_horses();
        if ((int)result < 2) npcs.update_npcs(game_time.hour);
        if (npcs.event_slot != 0 || result == 2) npc_encounter((int)result - 1);
    }
}

void Town::town_main_loop(void)
{
    for (;;) {
        unsigned result = 1, key = 0;
        int state = check_party_state();
        if (state == 1) {
            put_char('\n');
            draw_label_left();
            print_text(strings[Strings::Zzzzzz_288D]);   /* "Zzzzzz...\n" */
            wait_turns(1);                /* port: lets the frame show (the original just loops) */
        } else if (state == -1) {
            endings.party_death();
            result = 0;
        } else {
            if (town.just_entered == 0) {
                if (check_moongate() != 0) visit_shrine();
            } else {
                town.just_entered = 0;
            }
            if (map.is_outdoors()) return;
            int redraw = 1;
            do {
                key = town_read_key(redraw) & 0xFF;
                redraw = 0;
                if (game_quit()) return;
                if (key > 0x1F) {
                    result = (key < '0' || key > '9') ? (unsigned)dispatch_command((int)key) : (unsigned)town_number_key((int)key);
                    continue;
                }
                switch (key) {
                case 1: case 2: case 3: case 4:
                    result = town_move((int)key) == 0;
                    break;
                case 5:   /* Ctrl-E */
                    print_text(strings[Strings::Exit_to_DOS_2898]);   /* "Exit to DOS? " */
                    if ((get_command_key() & 0xFF) == 'Y') {
                        exit_game(0);
                        return;
                    }
                    print_text(strings[Strings::N_28A6]);   /* "N\n" */
                    break;
                case 0x0B:   /* Ctrl-K */
                    print_number(' ', 1, party.karma);
                    put_char('\n');
                    result = 0;
                    break;
                case 0x13:   /* Ctrl-S */
                    print_text(strings[Strings::Sound_28AE]);   /* "Sound " */
                    print_text(strings[sound.enabled == 0 ? Strings::On_28BA : Strings::Off_28B5]);   /* "On\n" / "Off\n" */
                    sound.enabled = sound.enabled == 0;
                    result = 0;
                    break;
                case 0x16:   /* Ctrl-V */
                    print_text(strings[Strings::N1_16_28A9]);   /* "1.16" */
                    put_char('\n');
                    result = 0;
                    break;
                default:
                    print_text(strings[Strings::What_28BE]);   /* "What?\n" */
                    result = 0;
                    break;
                }
            } while (result == 3);
        }
        if (game_quit()) return;
        if (map.is_outdoors()) return;
        town_end_turn((int)key, result);
        if (map.is_outdoors() || game_quit()) return;
    }
}
