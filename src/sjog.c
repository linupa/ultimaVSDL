/* sjog.c - Search, Jimmy, Open and Get (SJOG.OVL). See sjog.h. */
#include "commands.h"
#include "data.h"
#include "dungeon.h"
#include "gfx.h"
#include "map.h"
#include "misc.h"
#include "objects.h"
#include "sjog.h"
#include "text.h"
#include "town.h"
#include "view.h"
#include "world.h"

#define S(off) ds_str(off)

int find_free_object_slot(void)
{
    for (int i = 0x1F; i >= 0; i--)
        if (dg.objects[i].tile == 0) return i;
    return 0;
}

/* A chest (tile 1) on the square, else 0x20. */
static int chest_at(unsigned y, unsigned x)
{
    int i;
    for (i = 1; i < 0x20; i++) {
        MapObject *o = &dg.objects[i];
        if (o->x == x && o->y == y && (dg.location > 0x7F || o->z == dg.z_level) && o->tile == 1) break;
    }
    return i;
}

void print_found_item(unsigned kind)
{
    uint16_t text;
    switch (kind) {
    case 1: text = 0x850E; break;      /* "a chest!" */
    case 2: text = 0x8518; break;      /* "a sack of gold!" */
    case 3: text = 0x852A; break;      /* "a potion!" */
    case 4: text = 0x8536; break;      /* "a scroll!" */
    case 5: text = 0x8542; break;      /* "a weapon!" */
    case 6: text = 0x854E; break;      /* "a shield!" */
    case 7: text = 0x855A; break;      /* "a ring of keys!" */
    case 8: text = 0x856C; break;      /* "a gem!" */
    case 9: text = 0x8574; break;      /* "a helm!" */
    case 0x0A: text = 0x857E; break;   /* "a ring!" */
    case 0x0B: text = 0x8588; break;   /* "some armour!" */
    case 0x0C: text = 0x8596; break;   /* "an amulet!" */
    case 0x0D: text = 0x85A2; break;   /* "some torches!" */
    case 0x0F: text = 0x85B2; break;   /* "some food!" */
    case 0x19: text = 0x85BE; break;   /* "a strange rock!" */
    case 0x1E: text = 0x85D0; break;   /* "a rotting body!" */
    case 0x1F: text = 0x85E2; break;   /* "a moldy corpse!" */
    default: text = 0x85F4; break;     /* "Nothing of note!" */
    }
    print_text(S(text));
}

/* 1 time in 8 the body turns into food (1 in 4) or gold, 1-3 of it. Otherwise it is gone:
 * 1 in 32 gives the plague (poison), else nothing, worms, guts or a bloody pulp. */
void search_body(int member, int object)
{
    if (random_between(7, 0) == 0) {
        uint8_t kind;
        if (random_between(3, 0) == 0) {
            print_text(S(0x863A));   /* "food!\n" */
            kind = 0x0F;
        } else {
            print_text(S(0x8642));   /* "gold!\n" */
            kind = 2;
        }
        dg.objects[object].tile = dg.objects[object].tile2 = kind;
        dg.objects[object].hull = (uint8_t)random_between(3, 1);
        dg.redraw_flags |= 2;
        world_tick();
        return;
    }
    set_object_entry(object, 0, 0, 0, 0, 0, 0);
    if (random_between(0x1F, 0) == 0x13) {
        print_text(S(0x8606));   /* "Plague!\n" */
        sound_noise(500, 3000, 0x28);
        dg.party[member].status = 'P';
        dg.status_dirty = 1;
        return;
    }
    static const uint16_t texts[4] = {0x8610, 0x861A, 0x8622, 0x862A};   /* nothing! worms! guts! a bloody pulp! */
    print_text(S(texts[random_between(random_between(3, 0), 0)]));
}

/* The searcher's roll (1-30) against (30 - INT [+ trap level]) / 2. A failed roll gives the
 * wrong answer: "no trap" on a trapped chest, "a trap" on a safe one. */
void search_chest_trap(int member, int object)
{
    uint8_t hull = dg.objects[object].hull, intel = dg.party[member].intel;
    uint16_t v = (hull & 0x80) ? (uint16_t)((hull & 0x7F) - intel + 0x1E) : (uint16_t)(0x1E - intel);
    int ok = (int16_t)(v >> 1) <= random_between(0x1E, 1);
    int trapped = (hull & 0x80) != 0;
    unsigned level = hull & 0x7F;
    if ((ok && !trapped) || (!ok && trapped)) print_text(S(0x864A));   /* "no trap!\n" */
    else if (ok && level < 10) print_text(S(0x8654));                  /* "a simple trap!\n" */
    else if (ok && level > 0x14) print_text(S(0x8664));                /* "a complex trap!\n" */
    else print_text(S(0x8676));                                        /* "a trap!\n" */
}

/* A moonstone buried here that isn't already lying on the square becomes an object (tile 0x19,
 * hull = its number). */
int search_moonstone(unsigned z, unsigned y, unsigned x)
{
    for (int i = 7; i >= 0; i--) {
        if (dg.moonstone_x[i] != x || dg.moonstone_y[i] != y || dg.moonstone_z[i] != z || dg.moonstone_loc[i] != dg.location) continue;
        if (find_object_at((uint8_t)z, (uint8_t)y, (uint8_t)x) == 0x19 && dg.objects[dg.dir_x & 0x1F].hull == i) continue;
        int slot = alloc_object_slot();
        set_object_entry(slot, (uint8_t)i, (uint8_t)z, (uint8_t)y, (uint8_t)x, 0x19, 0x19);
        print_text(S(0x8680));   /* "a strange rock!\n" */
        dg.redraw_flags |= 2;
        return 1;
    }
    return 0;
}

/* Three spots in Britannia (x at ds:3E66, y at ds:3E6A, reagent at ds:3E6E) give 2-15 mandrake or
 * nightshade when searched between midnight and one, once a day each. */
int search_reagent_spot(unsigned y, unsigned x)
{
    for (int i = 0; i < 3; i++) {
        if (ds_u8((uint16_t)(0x3E66 + i)) != x || ds_u8((uint16_t)(0x3E6A + i)) != y || dg.hour != 0 || dg.reagent_spot_day[i] == dg.day) continue;
        dg.reagent_spot_day[i] = dg.day;
        uint8_t reagent = ds_u8((uint16_t)(0x3E6E + i));
        int n = random_between(0x0F, 2);
        dg.reagents[reagent & 7] = (uint8_t)(dg.reagents[reagent & 7] + n);
        if (dg.reagents[reagent & 7] > 99) dg.reagents[reagent & 7] = 99;
        print_number(' ', n < 10 ? 1 : 2, n);
        print_text(S(0x86BE));   /* " sprigs of\n" */
        print_text(S(ds_u16((uint16_t)(0x3E72 + i * 2))));
        print_text(S(0x86CA));   /* "\n" */
        return 1;
    }
    return 0;
}

/* 113 hidden things, six parallel tables of 0x72 bytes: kind ds:3E78, amount/id 3EEA, location
 * 3F5C, level 3FCE, x 4040, y 40B2. Each is found once (search_found_bits), except: 13 (a key)
 * comes back whenever the party has no keys, 14 once a day, 15 whenever the party has none of
 * item 0x27, as long as nothing lies there already. The thing appears as an object to Get. */
void search_hidden_item(unsigned y, unsigned x)
{
    int found = 0, i;
    for (i = 0; i <= 0x70; i++) {
        if (ds_u8((uint16_t)(0x3F5C + i)) != dg.location || ds_u8((uint16_t)(0x3FCE + i)) != dg.z_level ||
            ds_u8((uint16_t)(0x4040 + i)) != x || ds_u8((uint16_t)(0x40B2 + i)) != y) continue;
        if ((i != 0x0D || dg.keys != 0 || find_object_at(dg.z_level, (uint8_t)y, (uint8_t)x) != 0) &&
            (i != 0x0E || dg.day == dg.hidden_item_day) &&
            (i != 0x0F || dg.inventory[0x27] != 0 || find_object_at(dg.z_level, (uint8_t)y, (uint8_t)x) != 0)) {
            uint8_t bit = (uint8_t)(1 << (i & 7));
            if ((bit & dg.search_found_bits[i >> 3]) != 0 || (i > 0x0C && i < 0x10)) continue;
            dg.search_found_bits[i >> 3] |= bit;
        }
        found = 1;
        break;
    }
    if (i == 0x0E) dg.hidden_item_day = dg.day;
    if (!found) {
        print_text(S(0x86CC));   /* "nothing of note.\n" */
        return;
    }
    int slot = alloc_object_slot();
    uint8_t kind = ds_u8((uint16_t)(0x3E78 + i));
    set_object_entry(slot, ds_u8((uint16_t)(0x3EEA + i)), ds_u8((uint16_t)(0x3FCE + i)), ds_u8((uint16_t)(0x40B2 + i)),
                     ds_u8((uint16_t)(0x4040 + i)), kind, kind);
    dg.redraw_flags |= 2;
    print_found_item(kind);
}

void cmd_search(void)
{
    if (dg.location > 0x20 && dg.location < 0x29) {
        dungeon_search();
        return;
    }
    if (ask_direction() == 0) return;
    unsigned x = (uint16_t)(dg.party_x + dg.dir_x), y = (uint16_t)(dg.party_y + dg.dir_y);
    int member = ask_which_member();
    if (member == -1) return;
    uint8_t t = *get_map_tile_ptr(y, x);
    int chest = chest_at(y, x);
    if (chest < 0x20) {
        print_text(S(0x892C));   /* "\nThou dost find\n" */
        search_chest_trap(member, chest);
        return;
    }
    if (object_tile_at(dg.z_level, y, x) == 0x1F) {
        print_text(S(0x893E));   /* "\nThou dost find\n" */
        search_body(member, (int)dg.dir_x);
        return;
    }
    uint16_t where;
    switch (t) {
    case 0x2B: where = 0x8950; break;              /* "\nIn the stump\nt" */
    case 0x4F: where = 0x8A24; break;              /* "\nIn the wall\nt" */
    case 0x5A: where = 0x8960; break;              /* "\nOn the shelf\nt" */
    case 0x5C: case 0x5D: where = 0x8970; break;   /* "\nIn the bookshelf\nt" */
    case 0xA1: where = 0x8984; break;              /* "\nNear the well\nt" */
    case 0xA5: where = 0x8996; break;              /* "\nIn the desk\nt" */
    case 0xA6: where = 0x89A6; break;              /* "\nIn the barrel\nt" */
    case 0xA8: where = 0x89B8; break;              /* "\nIn the vanity\nt" */
    case 0xAB: case 0xAC: where = 0x89CA; break;   /* "\nUnder the bed\nt" */
    case 0xAD: where = 0x89DC; break;              /* "\nIn the dresser\nt" */
    case 0xAF: where = 0x89EE; break;              /* "\nIn the trunk\nt" */
    case 0xB2: where = 0x8A12; break;              /* "\nIn the brazier\nt" */
    case 0xBC: where = 0x89FE; break;              /* "\nIn the fireplace\nt" */
    default: where = 0x8A34; break;                /* "\nT" */
    }
    print_text(S(where));
    print_text(S(0x8A38));   /* "hou dost find\n" */
    if (t == 0x4E) {         /* a secret door becomes a door (locked above ground) */
        print_text(S(0x8A48));   /* "a hidden door!\n" */
        *get_map_tile_ptr(y, x) = dg.z_level < 0x80 ? 0xB9 : 0xB8;
        dg.redraw_flags |= 2;
    } else if ((t == 0xDC || search_moonstone(dg.z_level, y, x) == 0) && search_reagent_spot(y, x) == 0) {
        search_hidden_item(y, x);
    }
}

/* Disarms a trapped chest: the roll (1-30) must beat (trap level - DEX + 30) / 2. Otherwise,
 * and always on a chest that isn't trapped, the key breaks. */
void jimmy_chest(int object)
{
    int member = ask_which_member();
    if (member == -1) return;
    uint8_t hull = dg.objects[object].hull;
    if (hull >= 0x80) {
        unsigned need = (((unsigned)(hull & 0x7F) - dg.party[member].dex + 0x1E) >> 1) & 0xFF;
        if (need < (unsigned)random_between(0x1E, 1)) {
            print_text(S(0x8A64));   /* "Success!\n" */
            dg.objects[object].hull &= 0x7F;
            return;
        }
        print_text(S(0x8A6E));   /* "Key broke!\n" */
    } else {
        print_text(S(0x8A58));   /* "Key broke!\n" */
    }
    sound_slide(0x32, 1, 2000, 800);
    dg.keys--;
}

/* Locked doors (0xB9, 0xBB) open on a DEX roll; magically locked ones (0x97, 0x98) always break
 * the key. A prisoner in the stocks or manacles (0x84, 0x85) is freed: karma +2, the NPC stops
 * talking, wanders (activity 5) and is marked as gone for good. Else a chest. */
void cmd_jimmy(void)
{
    if (dg.location > 0x20 && dg.location < 0x29) {
        dungeon_jimmy();
        return;
    }
    if (dg.keys == 0) {
        print_text(S(0x8AD0));   /* "No Keys!\n" */
        return;
    }
    if (ask_direction() == 0) return;
    unsigned x = (uint16_t)(dg.party_x + dg.dir_x), y = (uint16_t)(dg.party_y + dg.dir_y);
    uint8_t t = *get_map_tile_ptr(y, x);
    if (t == 0x97 || t == 0x98) {
        print_text(S(0x8AF2));   /* "Key broke!\n" */
        dg.keys--;
        return;
    }
    if (t == 0x84 || t == 0x85) {
        if (dg.location < 0x80 && find_object_at(dg.z_level, (uint8_t)y, (uint8_t)x) == 0) {
            print_text(S(0x8AFE));   /* "No one is there!\n" */
            return;
        }
        int object = (int)dg.dir_x;
        int member = ask_which_member();
        if (member == -1) return;
        if (dg.party[member].dex <= (unsigned)random_between(0x1D, 0)) {
            print_text(S(0x8B10));   /* "Key broke!\n" */
            dg.keys--;
            return;
        }
        if (dg.location >= 0x7F) {   /* in combat the manacles just open */
            *get_map_tile_ptr(y, x) = 0x44;
            dg.redraw_flags |= 2;
            print_text(S(0x8B48));   /* "Unlocked\n" */
            return;
        }
        int npc = npc_for_object(object);
        if (npc == -1) {
            print_text(S(0x8B1C));   /* "Couldn't find this npc\n\n" */
            return;
        }
        if (dg.npc_state[npc].dialogue != 0) dg.npc_state[npc].dialogue = 0;
        if (npc_is_dead(npc) == 0) {
            dg.npc_schedules[npc].activity[0] = 5;
            dg.npc_schedules[npc].activity[1] = 5;
            dg.npc_schedules[npc].activity[2] = 5;
            print_text(S(0x8B36));   /* "\n\"I thank thee!\"\n" */
            add_byte_capped(99, 2, &dg.karma);
        }
        mark_npc_dead(npc);
        return;
    }
    if (t == 0xB9 || t == 0xBB) {
        int member = ask_which_member();
        if (member == -1) return;
        if (dg.party[member].dex <= (unsigned)random_between(0x1D, 0)) {
            print_text(S(0x8ADA));   /* "Key broke!\n" */
            dg.keys--;
            return;
        }
        *get_map_tile_ptr(y, x) = (uint8_t)(t - 1);
        dg.redraw_flags |= 2;
        print_text(S(0x8AE6));   /* "Unlocked!\n" */
        return;
    }
    int chest = chest_at(y, x);
    if (chest < 0x20) jimmy_chest(chest);
    else print_text(S(0x8B52));   /* "No lock!\n" */
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
    if (dg.location < 0x80) {
        dg.objects[slot].skiffs = 0;
        dg.objects[slot].z = z;
    } else {
        dg.objects[slot].skiffs = 0x20;
    }
    dg.redraw_flags |= 2;
    world_tick();
    if (*found == 0) {
        print_text(S(0x8B5C));   /* "Found:\n" */
        *found = 1;
    }
    print_found_item((unsigned)kind);
}

/* Eight kinds (ds:4124: chest, gold, potion, scroll, keys, gems, torches, food): each appears if
 * its minimum level is no more than the chest's level and than a 1-30 roll. */
void chest_roll_items(int *found, unsigned z, unsigned y, unsigned x, unsigned level)
{
    for (int i = 7; i >= 0; i--) {
        if (dg.chest_item_min_level[i] > level) continue;
        if (dg.chest_item_min_level[i] > (unsigned)random_between(0x1E, 1)) continue;
        int amount = dg.chest_item_max_count[i] == 1 ? 1 : random_between(dg.chest_item_max_count[i], 1);
        spawn_chest_item(found, (int)level, (uint8_t)z, (uint8_t)y, (uint8_t)x, amount, ds_u8((uint16_t)(0x4124 + i)));
    }
}

/* level / 2 + 1 tries: a random item id 0-47 appears on the same two tests against its level at
 * ds:416C (0xFF = never); its kind (weapon, shield, helm...) is at ds:413C. */
void chest_roll_equipment(int *found, uint8_t z, uint8_t y, uint8_t x, unsigned level)
{
    for (int n = (int)level / 2; n >= 0; n--) {
        int item = random_between(0x2F, 0);
        uint8_t min = ds_u8((uint16_t)(0x416C + item));
        if (min > level) continue;
        if (min > (unsigned)random_between(0x1E, 1)) continue;
        spawn_chest_item(found, (int)level, z, y, x, item, ds_u8((uint16_t)(0x413C + item)));
    }
}

/* Opening a chest in a town costs 2 karma. The contents are rolled only now, from the chest's
 * level, and appear as objects on the chest's square. */
void open_chest(unsigned z, unsigned y, unsigned x)
{
    int found = 0, i;
    for (i = 1; i < 0x20; i++) {
        MapObject *o = &dg.objects[i];
        if (o->x != x || o->y != y || !(dg.location > 0x7F || o->z == z)) continue;
        if (o->tile == 1) break;
        if (o->tile == 0x0E) {
            print_text(S(0x8B64));   /* "Can't!\n" (the sandalwood box) */
            return;
        }
    }
    if (i == 0x20) {
        print_text(S(0x8B6C));   /* "Nothing to open!\n" */
        return;
    }
    int member = ask_which_member();
    if (member == -1) return;
    uint8_t level = dg.objects[i].hull;
    set_object_entry(i, 0, 0, 0, 0, 0, 0);
    dg.redraw_flags |= 2;
    if (dg.location != 0 && dg.location < 0x21) sub_byte_floor0(2, &dg.karma);
    if (level > 0x7F) {
        level &= 0x7F;
        print_text(S(0x8B7E));   /* "Trapped!\n" */
        spring_trap((unsigned)member);
        if (dg.location > 0x7F && member >= 0 && dg.party[member].status == 'D') {   /* killed in combat */
            for (int c = 0; c < 0x20; c++) {
                Combatant *cb = &dg.combatants[c];
                if ((cb->flags & 0x80) && cb->kind == member) {
                    cb->flags |= 0x20;
                    dg.objects[cb->object & 0x1F].tile = dg.objects[cb->object & 0x1F].tile2 = 0x1E;
                    break;
                }
            }
            if (member == dg.active_player) dg.active_player = 0xFF;
            world_tick();
        }
    }
    chest_roll_items(&found, z, y, x, level);
    chest_roll_equipment(&found, (uint8_t)z, (uint8_t)y, (uint8_t)x, level);
    if (found == 0) print_text(S(0x8B88));   /* "Chest empty!\n" */
}

/* Doors 0xB8 and 0xBA open for 4 turns; 0xB9 and 0xBB (and the magic doors 0x97, 0x98) are
 * locked; a portcullis (0x99) is too heavy; an open chest tile is 0xAF. Anything else: a chest. */
void cmd_open(void)
{
    if (dg.location > 0x20 && dg.location < 0x29) {
        dungeon_open_chest();
        return;
    }
    restore_open_door(dg.open_door_y, dg.open_door_x, dg.open_door_tile);
    if (ask_direction() == 0) return;
    unsigned x = (uint16_t)(dg.party_x + dg.dir_x), y = (uint16_t)(dg.party_y + dg.dir_y);
    uint8_t *p = get_map_tile_ptr(y, x), t = *p;
    if (t == 0xAF) {
        print_text(S(0x8BBE));   /* "It's open!\n" */
    } else if (t == 0x99) {
        print_text(S(0x8BCA));   /* "Too heavy!\n" */
    } else if (t == 0xB9 || t == 0xBB || t == 0x97 || t == 0x98) {
        print_text(S(0x8BD6));   /* "Locked!\n" */
    } else if (t == 0xB8 || t == 0xBA) {
        dg.open_door_timer = 4;
        dg.open_door_x = (uint8_t)x;
        dg.open_door_y = (uint8_t)y;
        dg.open_door_tile = t;
        *p = 0x44;
        dg.redraw_flags = 1;
        print_text(S(0x8BE0));   /* "Opened!\n" */
    } else {
        open_chest(dg.z_level, y, x);
    }
}

void get_item(int slot, unsigned amount, int kind)
{
    switch (kind) {
    case 1:
        print_text(S(0x8C3E));   /* "Open it first!\n" */
        return;
    case 2:
        print_number(' ', 1, (int)amount);
        print_text(S(0x8CF0));   /* " gold!\n" */
        add_word_capped(9999, (uint16_t)amount, &dg.gold);
        break;
    case 3:
        print_text(S(0x8CF8));   /* "A " */
        print_text(S(ds_u16((uint16_t)(0x419C + amount * 2))));   /* blue yellow red green orange purple black white */
        print_text(S(0x8CFC));   /* " potion!\n" */
        if (++dg.potions[amount & 7] == 100) dg.potions[amount & 7] = 99;
        break;
    case 4:
        if (amount == 0xFF) {
            print_text(S(0x8CC2));   /* "The plans for the HMS Cape!\n" */
            dg.hms_cape_plans = 0xFF;
        } else {
            print_text(S(0x8CE0));   /* "A scroll: " */
            select_font(1);
            print_text(S(ds_u16((uint16_t)(0x41AC + (amount & 7) * 2))));   /* VL RH IS IA IQW KXC IMC AT, in runes */
            select_font(0);
            print_text(S(0x8CEC));   /* "!\n" */
            if (++dg.scrolls[amount & 7] == 100) dg.scrolls[amount & 7] = 99;
        }
        break;
    case 7:
        if ((int)amount < 0x80) {
            print_number(' ', 1, (int)amount);
            print_text(S(0x8CB4));   /* " key" */
            add_byte_capped(99, (uint8_t)amount, &dg.keys);
        } else {
            amount &= 0x7F;
            print_number(' ', 1, (int)amount);
            print_text(S(0x8CAA));   /* " odd key" */
            add_byte_capped(99, (uint8_t)amount, &dg.odd_keys);
        }
        print_text(S(amount == 1 ? 0x8CBA : 0x8CBE));   /* "!\n" / "s!\n" */
        break;
    case 8:
        print_number(' ', 1, (int)amount);
        print_text(S(0x8C9C));   /* " gem" */
        print_text(S(amount == 1 ? 0x8CA2 : 0x8CA6));
        add_byte_capped(99, (uint8_t)amount, &dg.gems);
        break;
    case 5: case 6: case 9: case 0x0A: case 0x0B: case 0x0C:   /* equipment: amount is the item id */
        if (amount == 0x1B || amount == 0x1D) {                /* arrows and quarrels come in fives */
            add_byte_capped(99, 5, &dg.inventory[amount]);
        } else if (++dg.inventory[amount % 48] == 100) {
            dg.inventory[amount % 48] = 99;
        }
        print_text(S(ds_u16((uint16_t)(offsetof(struct DGroup, item_names) + amount * 2))));
        print_text(S(0x8D06));   /* "!\n" */
        break;
    case 0x0D:
        print_number(' ', 1, (int)amount);
        print_text(S(0x8C8A));   /* " torch" */
        print_text(S(amount == 1 ? 0x8C92 : 0x8C96));   /* "!\n" / "es!\n" */
        add_byte_capped(99, (uint8_t)amount, &dg.torches);
        break;
    case 0x0E:
        print_text(S(0x8C76));   /* "A sandalwood box!\n" */
        dg.sandalwood_box = 0xFF;
        dg.npc_dead_bits[0x11] |= 0x80000000u;   /* it won't be there again */
        break;
    case 0x0F:
        print_number(' ', 1, (int)amount);
        print_text(S(0x8C6E));   /* " food!\n" */
        add_word_capped(9999, (uint16_t)amount, &dg.food);
        break;
    case 0x19:
        print_text(S(0x8C4E));   /* "A moonstone!\n" */
        dg.moonstone_loc[amount & 7] = 0xFF;
        break;
    case 0x1B:
        print_text(S(0x8C5C));   /* "A magic carpet!\n" */
        if (++dg.magic_carpets == 100) dg.magic_carpets = 99;
        if (dg.location == 0x11) remove_npc(0x16);   /* the one in Lord British's castle */
        break;
    case 0xB4: {
        unsigned shard = amount & 3;
        dg.shards[shard % 3] = 0xFF;
        print_text(S(0x8D0A));   /* "The Shard of\n" */
        print_text(S(shard == 0 ? 0x8D18 : shard == 1 ? 0x8D24 : 0x8D2E));   /* Falsehood! Hatred! Cowardice! */
        break;
    }
    case 0xB5: {
        dg.lb_crown = 0xFF;
        print_text(S(0x8D3A));   /* "The Crown of Lord British!\n" */
        int npc = npc_for_object(slot);
        if (npc >= 0) {          /* the original doesn't check */
            mark_npc_dead(npc);
            remove_npc(npc);
        }
        break;
    }
    case 0xB6:
        dg.lb_sceptre = 0xFF;
        print_text(S(0x8D56));   /* "The Sceptre of Lord British!\n" */
        break;
    case 0xB7:
        dg.lb_amulet = 0xFF;
        print_text(S(0x8D74));   /* "The Amulet of Lord British!\n" */
        break;
    default:
        print_text(S(0x8D92));   /* "Nothing to get!\n" */
        return;
    }
    if (slot < 0x20) set_object_entry(slot, 0, 0, 0, 0, 0, 0);
    dg.status_dirty = 1;
    dg.redraw_flags |= 2;
}

/* Besides objects: crops (0x2D) can be picked and food taken from a plate within reach (0x9A
 * from the north, 0x9B from the south, 0x9C either), each 1 food for 1 karma; a wall torch
 * (0xB0, 0xB1) can be borrowed for 100 turns of light. */
void cmd_get(void)
{
    if (dg.location > 0x20 && dg.location < 0x29) {
        dungeon_get();
        return;
    }
    if (ask_direction() == 0) return;
    uint16_t dx = dg.dir_x, dy = dg.dir_y;
    unsigned x = (uint16_t)(dx + dg.party_x), y = (uint16_t)(dy + dg.party_y);
    print_text(S(0x8DE6));   /* "\n" */
    for (int i = 1; i < 0x20; i++) {
        MapObject *o = &dg.objects[i];
        if (o->x != x || o->y != y || !(dg.location > 0x7F || o->z == dg.z_level)) continue;
        if (o->tile < 0x10 || o->tile == 0x19 || o->tile == 0x1B || (o->tile & 0xFC) == 0xB4) {
            get_item(i, o->hull, o->tile);
            return;
        }
    }
    uint8_t *p = get_map_tile_ptr(y, x), t = *p;
    uint16_t text;
    int plate = 0;
    if (t == 0x9A) {
        if (dy != 1) {
            print_text(S(0x8E10));   /* "Can't reach plate!\n" */
            return;
        }
        *p = 0x95;
        text = 0x8E04;   /* "Mmmmm...!\n" */
    } else if (t == 0x9B) {
        if (dy != 0xFFFF) {
            print_text(S(0x8E30));   /* "Can't reach plate!\n" */
            return;
        }
        *p = 0x95;
        text = 0x8E24;
        plate = 1;
    } else if (t == 0x9C) {
        if (dx == 1 || dx == 0xFFFF) {
            print_text(S(0x8E44));   /* "Can't reach plate!\n" */
            return;
        }
        if (dy == 1) *p = 0x9B;
        if (dy == 0xFFFF) *p = 0x9A;
        text = 0x8E58;
        plate = 1;
    } else if (t == 0xB0 || t == 0xB1) {
        *p = 0x44;
        dg.redraw_flags = 1;
        if (dg.location < 0x80) update_light_map();
        dg.torch_light = 100;
        print_text(S(0x8DE8));   /* "Borrowed!\n" */
        sound_slide(0x32, 1, 2000, 800);
        world_tick();
        return;
    } else if (t == 0x2D) {
        *p = 0x2C;
        text = 0x8DF4;   /* "Crops picked!\n" */
    } else {
        print_text(S(0x8E64));   /* "Nothing to get!\n" */
        return;
    }
    dg.redraw_flags |= 2;
    print_text(S(text));
    add_word_capped(9999, 1, &dg.food);
    if (!plate) dg.status_dirty = 1;   /* the status bar isn't refreshed for the other two plates */
    if (dg.karma != 0) dg.karma--;
}
