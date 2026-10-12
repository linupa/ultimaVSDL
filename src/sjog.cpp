/* sjog.cpp - Search, Jimmy, Open and Get (SJOG.OVL). See sjog.h. */
#include "combat.h"
#include "commands.h"
#include "dungeon.h"
#include "gamedata.h"
#include "gametime.h"
#include "gfx.h"
#include "items.h"
#include "map.h"
#include "misc.h"
#include "npc.h"
#include "objects.h"
#include "party.h"
#include "sjog.h"
#include "status.h"
#include "strings.h"
#include "text.h"
#include "town.h"
#include "view.h"
#include "world.h"


int find_free_object_slot(void)
{
    for (int i = 0x1F; i >= 0; i--)
        if (map.objects[i].tile == 0) return i;
    return 0;
}

/* A chest (tile 1) on the square, else 0x20. */
static int chest_at(unsigned y, unsigned x)
{
    int i;
    for (i = 1; i < 0x20; i++) {
        MapObject *o = &map.objects[i];
        if (o->x == x && o->y == y && (map.in_scene() || o->z == map.z) && o->tile == 1) break;
    }
    return i;
}

void print_found_item(unsigned kind)
{
    Strings::Id text;
    switch (kind) {
    case 1: text = Strings::a_chest_850E; break;      /* "a chest!" */
    case 2: text = Strings::a_sack_of_gold_8518; break;      /* "a sack of gold!" */
    case 3: text = Strings::a_potion_852A; break;      /* "a potion!" */
    case 4: text = Strings::a_scroll_8536; break;      /* "a scroll!" */
    case 5: text = Strings::a_weapon_8542; break;      /* "a weapon!" */
    case 6: text = Strings::a_shield_854E; break;      /* "a shield!" */
    case 7: text = Strings::a_ring_of_keys_855A; break;      /* "a ring of keys!" */
    case 8: text = Strings::a_gem_856C; break;      /* "a gem!" */
    case 9: text = Strings::a_helm_8574; break;      /* "a helm!" */
    case 0x0A: text = Strings::a_ring_857E; break;   /* "a ring!" */
    case 0x0B: text = Strings::some_armour_8588; break;   /* "some armour!" */
    case 0x0C: text = Strings::an_amulet_8596; break;   /* "an amulet!" */
    case 0x0D: text = Strings::some_torches_85A2; break;   /* "some torches!" */
    case 0x0F: text = Strings::some_food_85B2; break;   /* "some food!" */
    case 0x19: text = Strings::a_strange_rock_85BE; break;   /* "a strange rock!" */
    case 0x1E: text = Strings::a_rotting_body_85D0; break;   /* "a rotting body!" */
    case 0x1F: text = Strings::a_moldy_corpse_85E2; break;   /* "a moldy corpse!" */
    default: text = Strings::Nothing_of_note_85F4; break;     /* "Nothing of note!" */
    }
    print_text(strings[text]);
}

/* 1 time in 8 the body turns into food (1 in 4) or gold, 1-3 of it. Otherwise it is gone:
 * 1 in 32 gives the plague (poison), else nothing, worms, guts or a bloody pulp. */
void search_body(int member, int object)
{
    if (random_between(7, 0) == 0) {
        uint8_t kind;
        if (random_between(3, 0) == 0) {
            print_text(strings[Strings::food_863A]);   /* "food!\n" */
            kind = 0x0F;
        } else {
            print_text(strings[Strings::gold_8642]);   /* "gold!\n" */
            kind = 2;
        }
        map.objects[object].tile = map.objects[object].tile2 = kind;
        map.objects[object].hull = (uint8_t)random_between(3, 1);
        view.rebuild = true;
        world_tick();
        return;
    }
    set_object_entry(object, 0, 0, 0, 0, 0, 0);
    if (random_between(0x1F, 0) == 0x13) {
        print_text(strings[Strings::Plague_8606]);   /* "Plague!\n" */
        sound_noise(500, 3000, 0x28);
        party[member].status = 'P';
        status.dirty = true;
        return;
    }
    static const Strings::Id texts[4] = {Strings::nothing_8610, Strings::worms_861A, Strings::guts_8622, Strings::a_bloody_pulp_862A};   /* nothing! worms! guts! a bloody pulp! */
    print_text(strings[texts[random_between(random_between(3, 0), 0)]]);
}

/* The searcher's roll (1-30) against (30 - INT [+ trap level]) / 2. A failed roll gives the
 * wrong answer: "no trap" on a trapped chest, "a trap" on a safe one. */
void search_chest_trap(int member, int object)
{
    uint8_t hull = map.objects[object].hull, intel = party[member].intel;
    uint16_t v = (hull & 0x80) ? (uint16_t)((hull & 0x7F) - intel + 0x1E) : (uint16_t)(0x1E - intel);
    int ok = (int16_t)(v >> 1) <= random_between(0x1E, 1);
    int trapped = (hull & 0x80) != 0;
    unsigned level = hull & 0x7F;
    if ((ok && !trapped) || (!ok && trapped)) print_text(strings[Strings::no_trap_864A]);   /* "no trap!\n" */
    else if (ok && level < 10) print_text(strings[Strings::a_simple_trap_8654]);                  /* "a simple trap!\n" */
    else if (ok && level > 0x14) print_text(strings[Strings::a_complex_trap_8664]);                /* "a complex trap!\n" */
    else print_text(strings[Strings::a_trap_8676]);                                        /* "a trap!\n" */
}

/* A moonstone buried here that isn't already lying on the square becomes an object (tile 0x19,
 * hull = its number). */
int search_moonstone(unsigned z, unsigned y, unsigned x)
{
    for (int i = 7; i >= 0; i--) {
        if (map.moonstone_x[i] != x || map.moonstone_y[i] != y || map.moonstone_z[i] != z || map.moonstone_loc[i] != map.id()) continue;
        uint16_t found;
        if (find_object_at((uint8_t)z, (uint8_t)y, (uint8_t)x, &found) == 0x19 && map.objects[found & 0x1F].hull == i) continue;
        int slot = alloc_object_slot();
        set_object_entry(slot, (uint8_t)i, (uint8_t)z, (uint8_t)y, (uint8_t)x, 0x19, 0x19);
        print_text(strings[Strings::a_strange_rock_8680]);   /* "a strange rock!\n" */
        view.rebuild = true;
        return 1;
    }
    return 0;
}

/* Three spots in Britannia (x at ds:3E66, y at ds:3E6A, reagent at ds:3E6E, its name at ds:3E72;
 * from DATA.OVL) give 2-15 mandrake or nightshade when searched between midnight and one, once a
 * day each. */
static uint8_t reagent_spot_x[4], reagent_spot_y[4], reagent_spot_kind[4];
static Strings::Id reagent_spot_name[3];
[[maybe_unused]] static const bool reagent_spots_added = (Data::instance().add(0x3E66, reagent_spot_x, "sjog.reagent_spot_x"),
                                                          Data::instance().add(0x3E6A, reagent_spot_y, "sjog.reagent_spot_y"),
                                                          Data::instance().add(0x3E6E, reagent_spot_kind, "sjog.reagent_spot_kind"),
                                                          Data::instance().add(0x3E72, reagent_spot_name, "sjog.reagent_spot_name"), true);

int search_reagent_spot(unsigned y, unsigned x)
{
    for (int i = 0; i < 3; i++) {
        if (reagent_spot_x[i] != x || reagent_spot_y[i] != y || game_time.hour != 0 || game_time.reagent_spot_day[i] == game_time.day) continue;
        game_time.reagent_spot_day[i] = game_time.day;
        uint8_t reagent = reagent_spot_kind[i];
        int n = random_between(0x0F, 2);
        party.reagents[reagent & 7] = (uint8_t)(party.reagents[reagent & 7] + n);
        if (party.reagents[reagent & 7] > 99) party.reagents[reagent & 7] = 99;
        print_number(' ', n < 10 ? 1 : 2, n);
        print_text(strings[Strings::sprigs_of_86BE]);   /* " sprigs of\n" */
        print_text(strings[reagent_spot_name[i]]);
        print_text(strings[Strings::NL_86CA]);   /* "\n" */
        return 1;
    }
    return 0;
}

/* 113 hidden things, six parallel tables of 0x72 bytes: kind ds:3E78, amount/id 3EEA, location
 * 3F5C, level 3FCE, x 4040, y 40B2. Each is found once (search_found_bits), except: 13 (a key)
 * comes back whenever the party has no keys, 14 once a day, 15 whenever the party has none of
 * item 0x27, as long as nothing lies there already. The thing appears as an object to Get.
 * (The tables come from DATA.OVL.) */
enum { HIDDEN = 0x72 };
static uint8_t hidden_kind[HIDDEN], hidden_amount[HIDDEN], hidden_place[HIDDEN], hidden_level[HIDDEN], hidden_x[HIDDEN], hidden_y[HIDDEN];
[[maybe_unused]] static const bool hidden_added = (Data::instance().add(0x3E78, hidden_kind, "sjog.hidden_kind"),
                                                   Data::instance().add(0x3EEA, hidden_amount, "sjog.hidden_amount"),
                                                   Data::instance().add(0x3F5C, hidden_place, "sjog.hidden_place"),
                                                   Data::instance().add(0x3FCE, hidden_level, "sjog.hidden_level"),
                                                   Data::instance().add(0x4040, hidden_x, "sjog.hidden_x"),
                                                   Data::instance().add(0x40B2, hidden_y, "sjog.hidden_y"), true);
void search_hidden_item(unsigned y, unsigned x)
{
    int found = 0, i;
    for (i = 0; i <= 0x70; i++) {
        if (hidden_place[i] != map.id() || hidden_level[i] != map.z || hidden_x[i] != x || hidden_y[i] != y) continue;
        if ((i != 0x0D || party.keys != 0 || find_object_at(map.z, (uint8_t)y, (uint8_t)x) != 0) &&
            (i != 0x0E || game_time.day == game_time.hidden_item_day) &&
            (i != 0x0F || party.inventory[0x27] != 0 || find_object_at(map.z, (uint8_t)y, (uint8_t)x) != 0)) {
            uint8_t bit = (uint8_t)(1 << (i & 7));
            if ((bit & map.search_found_bits[i >> 3]) != 0 || (i > 0x0C && i < 0x10)) continue;
            map.search_found_bits[i >> 3] |= bit;
        }
        found = 1;
        break;
    }
    if (i == 0x0E) game_time.hidden_item_day = game_time.day;
    if (!found) {
        print_text(strings[Strings::nothing_of_note_86CC]);   /* "nothing of note.\n" */
        return;
    }
    int slot = alloc_object_slot();
    uint8_t kind = hidden_kind[i];
    set_object_entry(slot, hidden_amount[i], hidden_level[i], hidden_y[i], hidden_x[i], kind, kind);
    view.rebuild = true;
    print_found_item(kind);
}

void cmd_search(void)
{
    Direction dir = {0, 0};
    if (map.is_dungeon()) {
        dungeon.dungeon_search();
        return;
    }
    if (ask_direction(&dir) == 0) return;
    unsigned x = (uint16_t)(map.x + dir.dx), y = (uint16_t)(map.y + dir.dy);
    int member = ask_which_member();
    if (member == -1) return;
    uint8_t t = *map.get_map_tile_ptr(y, x);
    int chest = chest_at(y, x);
    if (chest < 0x20) {
        print_text(strings[Strings::Thou_dost_find_892C]);   /* "\nThou dost find\n" */
        search_chest_trap(member, chest);
        return;
    }
    uint16_t slot;
    if (object_tile_at(map.z, y, x, &slot) == 0x1F) {
        print_text(strings[Strings::Thou_dost_find_893E]);   /* "\nThou dost find\n" */
        search_body(member, (int)slot);
        return;
    }
    Strings::Id where;
    switch (t) {
    case 0x2B: where = Strings::In_the_stump_t_8950; break;              /* "\nIn the stump\nt" */
    case 0x4F: where = Strings::In_the_wall_t_8A24; break;              /* "\nIn the wall\nt" */
    case 0x5A: where = Strings::On_the_shelf_t_8960; break;              /* "\nOn the shelf\nt" */
    case 0x5C: case 0x5D: where = Strings::In_the_bookshelf_t_8970; break;   /* "\nIn the bookshelf\nt" */
    case 0xA1: where = Strings::Near_the_well_t_8984; break;              /* "\nNear the well\nt" */
    case 0xA5: where = Strings::In_the_desk_t_8996; break;              /* "\nIn the desk\nt" */
    case 0xA6: where = Strings::In_the_barrel_t_89A6; break;              /* "\nIn the barrel\nt" */
    case 0xA8: where = Strings::In_the_vanity_t_89B8; break;              /* "\nIn the vanity\nt" */
    case 0xAB: case 0xAC: where = Strings::Under_the_bed_t_89CA; break;   /* "\nUnder the bed\nt" */
    case 0xAD: where = Strings::In_the_dresser_t_89DC; break;              /* "\nIn the dresser\nt" */
    case 0xAF: where = Strings::In_the_trunk_t_89EE; break;              /* "\nIn the trunk\nt" */
    case 0xB2: where = Strings::In_the_brazier_t_8A12; break;              /* "\nIn the brazier\nt" */
    case 0xBC: where = Strings::In_the_fireplace_t_89FE; break;              /* "\nIn the fireplace\nt" */
    default: where = Strings::T_8A34; break;                /* "\nT" */
    }
    print_text(strings[where]);
    print_text(strings[Strings::hou_dost_find_8A38]);   /* "hou dost find\n" */
    if (t == 0x4E) {         /* a secret door becomes a door (locked above ground) */
        print_text(strings[Strings::a_hidden_door_8A48]);   /* "a hidden door!\n" */
        *map.get_map_tile_ptr(y, x) = map.z < 0x80 ? 0xB9 : 0xB8;
        view.rebuild = true;
    } else if ((t == 0xDC || search_moonstone(map.z, y, x) == 0) && search_reagent_spot(y, x) == 0) {
        search_hidden_item(y, x);
    }
}

/* Disarms a trapped chest: the roll (1-30) must beat (trap level - DEX + 30) / 2. Otherwise,
 * and always on a chest that isn't trapped, the key breaks. */
void jimmy_chest(int object)
{
    int member = ask_which_member();
    if (member == -1) return;
    uint8_t hull = map.objects[object].hull;
    if (hull >= 0x80) {
        unsigned need = (((unsigned)(hull & 0x7F) - party[member].dex + 0x1E) >> 1) & 0xFF;
        if (need < (unsigned)random_between(0x1E, 1)) {
            print_text(strings[Strings::Success_8A64]);   /* "Success!\n" */
            map.objects[object].hull &= 0x7F;
            return;
        }
        print_text(strings[Strings::Key_broke_8A6E]);   /* "Key broke!\n" */
    } else {
        print_text(strings[Strings::Key_broke_8A58]);   /* "Key broke!\n" */
    }
    sound_slide(0x32, 1, 2000, 800);
    party.keys--;
}

/* Locked doors (0xB9, 0xBB) open on a DEX roll; magically locked ones (0x97, 0x98) always break
 * the key. A prisoner in the stocks or manacles (0x84, 0x85) is freed: karma +2, the NPC stops
 * talking, wanders (activity 5) and is marked as gone for good. Else a chest. */
void cmd_jimmy(void)
{
    Direction dir = {0, 0};
    if (map.is_dungeon()) {
        dungeon.dungeon_jimmy();
        return;
    }
    if (party.keys == 0) {
        print_text(strings[Strings::No_Keys_8AD0]);   /* "No Keys!\n" */
        return;
    }
    if (ask_direction(&dir) == 0) return;
    unsigned x = (uint16_t)(map.x + dir.dx), y = (uint16_t)(map.y + dir.dy);
    uint8_t t = *map.get_map_tile_ptr(y, x);
    if (t == 0x97 || t == 0x98) {
        print_text(strings[Strings::Key_broke_8AF2]);   /* "Key broke!\n" */
        party.keys--;
        return;
    }
    if (t == 0x84 || t == 0x85) {
        /* In a scene the original does not look for the person at all, and goes on with what
         * ask_direction left in dir_x as the object. */
        uint16_t slot = dir.dx;
        if (!map.in_scene() && find_object_at(map.z, (uint8_t)y, (uint8_t)x, &slot) == 0) {
            print_text(strings[Strings::No_one_is_there_8AFE]);   /* "No one is there!\n" */
            return;
        }
        int object = (int)slot;
        int member = ask_which_member();
        if (member == -1) return;
        if (party[member].dex <= (unsigned)random_between(0x1D, 0)) {
            print_text(strings[Strings::Key_broke_8B10]);   /* "Key broke!\n" */
            party.keys--;
            return;
        }
        if (map.id() >= 0x7F) {   /* in combat the manacles just open */
            *map.get_map_tile_ptr(y, x) = 0x44;
            view.rebuild = true;
            print_text(strings[Strings::Unlocked_8B48]);   /* "Unlocked\n" */
            return;
        }
        int npc = town.npc_for_object(object);
        if (npc == -1) {
            print_text(strings[Strings::Couldn_t_find_this_npc_8B1C]);   /* "Couldn't find this npc\n\n" */
            return;
        }
        if (npcs.state[npc].dialogue != 0) npcs.state[npc].dialogue = 0;
        if (town.npc_is_dead(npc) == 0) {
            npcs.schedules[npc].activity[0] = 5;
            npcs.schedules[npc].activity[1] = 5;
            npcs.schedules[npc].activity[2] = 5;
            print_text(strings[Strings::I_thank_thee_8B36]);   /* "\n\"I thank thee!\"\n" */
            add_byte_capped(99, 2, &party.karma);
        }
        town.mark_npc_dead(npc);
        return;
    }
    if (t == 0xB9 || t == 0xBB) {
        int member = ask_which_member();
        if (member == -1) return;
        if (party[member].dex <= (unsigned)random_between(0x1D, 0)) {
            print_text(strings[Strings::Key_broke_8ADA]);   /* "Key broke!\n" */
            party.keys--;
            return;
        }
        *map.get_map_tile_ptr(y, x) = (uint8_t)(t - 1);
        view.rebuild = true;
        print_text(strings[Strings::Unlocked_8AE6]);   /* "Unlocked!\n" */
        return;
    }
    int chest = chest_at(y, x);
    if (chest < 0x20) jimmy_chest(chest);
    else print_text(strings[Strings::No_lock_8B52]);   /* "No lock!\n" */
}

/* A chest (1) holds 1 to `level` things and a sack of gold (2) 1 to 3 x level; potions and
 * scrolls get a type 0-7. */
void spawn_chest_item(int *found, int level, uint8_t z, uint8_t y, uint8_t x, int amount, int kind)
{
    if (kind == 3 || kind == 4) amount = (uint8_t)(amount - 1);
    if (kind == 1) amount = (uint8_t)random_between(level, 1);
    else if (kind == 2) amount = (uint8_t)random_between(level * 3, 1);
    int slot = find_free_object_slot();
    if (slot == 0) return;
    set_object_entry(slot, (uint8_t)amount, z, y, x, (uint8_t)kind, (uint8_t)kind);
    if (!map.in_scene()) {
        map.objects[slot].skiffs = 0;
        map.objects[slot].z = z;
    } else {
        map.objects[slot].skiffs = 0x20;
    }
    view.rebuild = true;
    world_tick();
    if (*found == 0) {
        print_text(strings[Strings::Found_8B5C]);   /* "Found:\n" */
        *found = 1;
    }
    print_found_item((unsigned)kind);
}

/* The eight kinds of thing a chest can hold (ds:4124: chest, gold, potion, scroll, keys, gems,
 * torches, food), the lowest chest level each appears at (ds:412C), and the most of each
 * (ds:4134), from DATA.OVL. */
static uint8_t chest_item_kind[8], chest_item_min_level[8], chest_item_max_count[8];
[[maybe_unused]] static const bool chest_items_added = (Data::instance().add(0x4124, chest_item_kind, "sjog.chest_item_kind"),
                                                        Data::instance().add(0x412C, chest_item_min_level, "sjog.chest_item_min_level"),
                                                        Data::instance().add(0x4134, chest_item_max_count, "sjog.chest_item_max_count"), true);

/* Eight kinds (chest_item_kind): each appears if
 * its minimum level is no more than the chest's level and than a 1-30 roll. */
void chest_roll_items(int *found, unsigned z, unsigned y, unsigned x, unsigned level)
{
    for (int i = 7; i >= 0; i--) {
        if (chest_item_min_level[i] > level) continue;
        if (chest_item_min_level[i] > (unsigned)random_between(0x1E, 1)) continue;
        int amount = chest_item_max_count[i] == 1 ? 1 : random_between(chest_item_max_count[i], 1);
        spawn_chest_item(found, (int)level, (uint8_t)z, (uint8_t)y, (uint8_t)x, amount, chest_item_kind[i]);
    }
}

/* level / 2 + 1 tries: a random item id 0-47 appears on the same two tests against its level at
 * ds:416C (0xFF = never); its kind (weapon, shield, helm...) is at ds:413C. (From DATA.OVL.) */
static uint8_t equipment_kind[48], equipment_level[48];
[[maybe_unused]] static const bool equipment_added = (Data::instance().add(0x413C, equipment_kind, "sjog.equipment_kind"),
                                                      Data::instance().add(0x416C, equipment_level, "sjog.equipment_level"), true);

void chest_roll_equipment(int *found, uint8_t z, uint8_t y, uint8_t x, unsigned level)
{
    for (int n = (int)level / 2; n >= 0; n--) {
        int item = random_between(0x2F, 0);
        uint8_t min = equipment_level[item];
        if (min > level) continue;
        if (min > (unsigned)random_between(0x1E, 1)) continue;
        spawn_chest_item(found, (int)level, z, y, x, item, equipment_kind[item]);
    }
}

/* Opening a chest in a town costs 2 karma. The contents are rolled only now, from the chest's
 * level, and appear as objects on the chest's square. */
void open_chest(unsigned z, unsigned y, unsigned x)
{
    int found = 0, i;
    for (i = 1; i < 0x20; i++) {
        MapObject *o = &map.objects[i];
        if (o->x != x || o->y != y || !(map.in_scene() || o->z == z)) continue;
        if (o->tile == 1) break;
        if (o->tile == 0x0E) {
            print_text(strings[Strings::Can_t_8B64]);   /* "Can't!\n" (the sandalwood box) */
            return;
        }
    }
    if (i == 0x20) {
        print_text(strings[Strings::Nothing_to_open_8B6C]);   /* "Nothing to open!\n" */
        return;
    }
    int member = ask_which_member();
    if (member == -1) return;
    uint8_t level = map.objects[i].hull;
    set_object_entry(i, 0, 0, 0, 0, 0, 0);
    view.rebuild = true;
    if (map.is_town()) sub_byte_floor0(2, &party.karma);
    if (level > 0x7F) {
        level &= 0x7F;
        print_text(strings[Strings::Trapped_8B7E]);   /* "Trapped!\n" */
        spring_trap((unsigned)member);
        if (map.in_scene() && member >= 0 && party[member].dead()) {   /* killed in combat */
            for (int c = 0; c < 0x20; c++) {
                Combatant *cb = &combat.combatants[c];
                if ((cb->flags & 0x80) && cb->kind == member) {
                    cb->flags |= 0x20;
                    map.objects[cb->object & 0x1F].tile = map.objects[cb->object & 0x1F].tile2 = 0x1E;
                    break;
                }
            }
            if (member == party.active_player) party.active_player = 0xFF;
            world_tick();
        }
    }
    chest_roll_items(&found, z, y, x, level);
    chest_roll_equipment(&found, (uint8_t)z, (uint8_t)y, (uint8_t)x, level);
    if (found == 0) print_text(strings[Strings::Chest_empty_8B88]);   /* "Chest empty!\n" */
}

/* Doors 0xB8 and 0xBA open for 4 turns; 0xB9 and 0xBB (and the magic doors 0x97, 0x98) are
 * locked; a portcullis (0x99) is too heavy; an open chest tile is 0xAF. Anything else: a chest. */
void cmd_open(void)
{
    Direction dir = {0, 0};
    if (map.is_dungeon()) {
        dungeon.dungeon_open_chest();
        return;
    }
    restore_open_door(map.open_door_y, map.open_door_x, map.open_door_tile);
    if (ask_direction(&dir) == 0) return;
    unsigned x = (uint16_t)(map.x + dir.dx), y = (uint16_t)(map.y + dir.dy);
    uint8_t *p = map.get_map_tile_ptr(y, x), t = *p;
    if (t == 0xAF) {
        print_text(strings[Strings::It_s_open_8BBE]);   /* "It's open!\n" */
    } else if (t == 0x99) {
        print_text(strings[Strings::Too_heavy_8BCA]);   /* "Too heavy!\n" */
    } else if (t == 0xB9 || t == 0xBB || t == 0x97 || t == 0x98) {
        print_text(strings[Strings::Locked_8BD6]);   /* "Locked!\n" */
    } else if (t == 0xB8 || t == 0xBA) {
        map.open_door_timer = 4;
        map.open_door_x = (uint8_t)x;
        map.open_door_y = (uint8_t)y;
        map.open_door_tile = t;
        *p = 0x44;
        view.rebuild = true;
        print_text(strings[Strings::Opened_8BE0]);   /* "Opened!\n" */
    } else {
        open_chest(map.z, y, x);
    }
}

void get_item(int slot, unsigned amount, int kind)
{
    switch (kind) {
    case 1:
        print_text(strings[Strings::Open_it_first_8C3E]);   /* "Open it first!\n" */
        return;
    case 2:
        print_number(' ', 1, (int)amount);
        print_text(strings[Strings::gold_8CF0]);   /* " gold!\n" */
        add_word_capped(9999, (uint16_t)amount, &party.gold);
        break;
    case 3:
        print_text(strings[Strings::A_8CF8]);   /* "A " */
        print_text(items.potion_colour((unsigned)amount));   /* blue yellow red green orange purple black white */
        print_text(strings[Strings::potion_8CFC]);   /* " potion!\n" */
        if (++party.potions[amount & 7] == 100) party.potions[amount & 7] = 99;
        break;
    case 4:
        if (amount == 0xFF) {
            print_text(strings[Strings::The_plans_for_the_HMS_8CC2]);   /* "The plans for the HMS Cape!\n" */
            party.hms_cape_plans = 0xFF;
        } else {
            print_text(strings[Strings::A_scroll_8CE0]);   /* "A scroll: " */
            select_font(1);
            print_text(items.scroll_letters((unsigned)amount));   /* VL RH IS IA IQW KXC IMC AT, in runes */
            select_font(0);
            print_text(strings[Strings::Bang_NL_8CEC]);   /* "!\n" */
            if (++party.scrolls[amount & 7] == 100) party.scrolls[amount & 7] = 99;
        }
        break;
    case 7:
        if ((int)amount < 0x80) {
            print_number(' ', 1, (int)amount);
            print_text(strings[Strings::key_8CB4]);   /* " key" */
            add_byte_capped(99, (uint8_t)amount, &party.keys);
        } else {
            amount &= 0x7F;
            print_number(' ', 1, (int)amount);
            print_text(strings[Strings::odd_key_8CAA]);   /* " odd key" */
            add_byte_capped(99, (uint8_t)amount, &party.odd_keys);
        }
        print_text(strings[amount == 1 ? Strings::Bang_NL_8CBA : Strings::s_8CBE]);   /* "!\n" / "s!\n" */
        break;
    case 8:
        print_number(' ', 1, (int)amount);
        print_text(strings[Strings::gem_8C9C]);   /* " gem" */
        print_text(strings[amount == 1 ? Strings::Bang_NL_8CA2 : Strings::s_8CA6]);
        add_byte_capped(99, (uint8_t)amount, &party.gems);
        break;
    case 5: case 6: case 9: case 0x0A: case 0x0B: case 0x0C:   /* equipment: amount is the item id */
        if (amount == 0x1B || amount == 0x1D) {                /* arrows and quarrels come in fives */
            add_byte_capped(99, 5, &party.inventory[amount]);
        } else if (++party.inventory[amount % 48] == 100) {
            party.inventory[amount % 48] = 99;
        }
        print_text(items.name(amount));
        print_text(strings[Strings::Bang_NL_8D06]);   /* "!\n" */
        break;
    case 0x0D:
        print_number(' ', 1, (int)amount);
        print_text(strings[Strings::torch_8C8A]);   /* " torch" */
        print_text(strings[amount == 1 ? Strings::Bang_NL_8C92 : Strings::es_8C96]);   /* "!\n" / "es!\n" */
        add_byte_capped(99, (uint8_t)amount, &party.torches);
        break;
    case 0x0E:
        print_text(strings[Strings::A_sandalwood_box_8C76]);   /* "A sandalwood box!\n" */
        party.sandalwood_box = 0xFF;
        npcs.dead_bits[0x11] |= 0x80000000u;   /* it won't be there again */
        break;
    case 0x0F:
        print_number(' ', 1, (int)amount);
        print_text(strings[Strings::food_8C6E]);   /* " food!\n" */
        add_word_capped(9999, (uint16_t)amount, &party.food);
        break;
    case 0x19:
        print_text(strings[Strings::A_moonstone_8C4E]);   /* "A moonstone!\n" */
        map.moonstone_loc[amount & 7] = 0xFF;
        break;
    case 0x1B:
        print_text(strings[Strings::A_magic_carpet_8C5C]);   /* "A magic carpet!\n" */
        if (++party.magic_carpets == 100) party.magic_carpets = 99;
        if (map == Map::LORD_BRITISHS_CASTLE) town.remove_npc(0x16);   /* the one in Lord British's castle */
        break;
    case 0xB4: {
        unsigned shard = amount & 3;
        party.shards[shard % 3] = 0xFF;
        print_text(strings[Strings::The_Shard_of_8D0A]);   /* "The Shard of\n" */
        print_text(strings[shard == 0 ? Strings::Falsehood_8D18 : shard == 1 ? Strings::Hatred_8D24 : Strings::Cowardice_8D2E]);   /* Falsehood! Hatred! Cowardice! */
        break;
    }
    case 0xB5: {
        party.lb_crown = 0xFF;
        print_text(strings[Strings::The_Crown_of_Lord_8D3A]);   /* "The Crown of Lord British!\n" */
        int npc = town.npc_for_object(slot);
        if (npc >= 0) {          /* the original doesn't check */
            town.mark_npc_dead(npc);
            town.remove_npc(npc);
        }
        break;
    }
    case 0xB6:
        party.lb_sceptre = 0xFF;
        print_text(strings[Strings::The_Sceptre_of_Lord_8D56]);   /* "The Sceptre of Lord British!\n" */
        break;
    case 0xB7:
        party.lb_amulet = 0xFF;
        print_text(strings[Strings::The_Amulet_of_Lord_8D74]);   /* "The Amulet of Lord British!\n" */
        break;
    default:
        print_text(strings[Strings::Nothing_to_get_8D92]);   /* "Nothing to get!\n" */
        return;
    }
    if (slot < 0x20) set_object_entry(slot, 0, 0, 0, 0, 0, 0);
    status.dirty = true;
    view.rebuild = true;
}

/* Besides objects: crops (0x2D) can be picked and food taken from a plate within reach (0x9A
 * from the north, 0x9B from the south, 0x9C either), each 1 food for 1 karma; a wall torch
 * (0xB0, 0xB1) can be borrowed for 100 turns of light. */
void cmd_get(void)
{
    Direction dir = {0, 0};
    if (map.is_dungeon()) {
        dungeon.dungeon_get();
        return;
    }
    if (ask_direction(&dir) == 0) return;
    uint16_t dx = dir.dx, dy = dir.dy;
    unsigned x = (uint16_t)(dx + map.x), y = (uint16_t)(dy + map.y);
    print_text(strings[Strings::NL_8DE6]);   /* "\n" */
    for (int i = 1; i < 0x20; i++) {
        MapObject *o = &map.objects[i];
        if (o->x != x || o->y != y || !(map.in_scene() || o->z == map.z)) continue;
        if (o->tile < 0x10 || o->tile == 0x19 || o->tile == 0x1B || (o->tile & 0xFC) == 0xB4) {
            get_item(i, o->hull, o->tile);
            return;
        }
    }
    uint8_t *p = map.get_map_tile_ptr(y, x), t = *p;
    Strings::Id text;
    int plate = 0;
    if (t == 0x9A) {
        if (dy != 1) {
            print_text(strings[Strings::Can_t_reach_plate_8E10]);   /* "Can't reach plate!\n" */
            return;
        }
        *p = 0x95;
        text = Strings::Mmmmm_8E04;   /* "Mmmmm...!\n" */
    } else if (t == 0x9B) {
        if (dy != 0xFFFF) {
            print_text(strings[Strings::Can_t_reach_plate_8E30]);   /* "Can't reach plate!\n" */
            return;
        }
        *p = 0x95;
        text = Strings::Mmmmm_8E24;
        plate = 1;
    } else if (t == 0x9C) {
        if (dx == 1 || dx == 0xFFFF) {
            print_text(strings[Strings::Can_t_reach_plate_8E44]);   /* "Can't reach plate!\n" */
            return;
        }
        if (dy == 1) *p = 0x9B;
        if (dy == 0xFFFF) *p = 0x9A;
        text = Strings::Mmmmm_8E58;
        plate = 1;
    } else if (t == 0xB0 || t == 0xB1) {
        *p = 0x44;
        view.rebuild = true;
        if (!map.in_scene()) update_light_map();
        party.torch_light = 100;
        print_text(strings[Strings::Borrowed_8DE8]);   /* "Borrowed!\n" */
        sound_slide(0x32, 1, 2000, 800);
        world_tick();
        return;
    } else if (t == 0x2D) {
        *p = 0x2C;
        text = Strings::Crops_picked_8DF4;   /* "Crops picked!\n" */
    } else {
        print_text(strings[Strings::Nothing_to_get_8E64]);   /* "Nothing to get!\n" */
        return;
    }
    view.rebuild = true;
    print_text(strings[text]);
    add_word_capped(9999, 1, &party.food);
    if (!plate) status.dirty = true;   /* the status bar isn't refreshed for the other two plates */
    if (party.karma != 0) party.karma--;
}
