/* outdoor.cpp - Britannia and the Underworld (MAINOUT.OVL, rest of OUTSUBS.OVL). See outdoor.h. */
#include <stdlib.h>
#include "commands.h"
#include "combat.h"
#include "comsubs.h"
#include "dungeon.h"
#include "endings.h"
#include "game.h"
#include "gamedata.h"
#include "gametime.h"
#include "gfx.h"
#include "input.h"
#include "map.h"
#include "misc.h"
#include "objects.h"
#include "outdoor.h"
#include "party.h"
#include "shrine.h"
#include "picture.h"
#include "sound.h"
#include "status.h"
#include "strings.h"
#include "text.h"
#include "textbuf.h"
#include "town.h"
#include "view.h"
#include "world.h"

Outdoor &Outdoor::instance()
{
    static Outdoor the_outdoor;
    return the_outdoor;
}

Outdoor &outdoor = Outdoor::instance();

Outdoor::Outdoor()
{
    Data &data = Data::instance();   /* not a global: this runs before main, in no set order */
    data.add(0x29F6, wind_dx_, "outdoor.wind_dx");
    data.add(0x29FA, wind_dy_, "outdoor.wind_dy");
    data.add(0x2BC0, land_monsters_, "outdoor.land_monsters");
    data.add(0x2BCC, under_land_monsters_, "outdoor.under_land_monsters");
    data.add(0x2BD4, sea_monsters_, "outdoor.sea_monsters");
    data.add(0x2BDA, under_sea_monsters_, "outdoor.under_sea_monsters");
    data.add(0x2BDC, land_chances_, "outdoor.land_chances");
    data.add(0x2BE8, under_land_chances_, "outdoor.under_land_chances");
    data.add(0x2BF0, sea_chances_, "outdoor.sea_chances");
    data.add(0x2BF6, under_sea_chances_, "outdoor.under_sea_chances");
    data.add(0x2BF8, pirate_wait_, "outdoor.pirate_wait");
    data.add(0x2C18, chase_weight_, "outdoor.chase_weight");
    data.add(0x3A06, shadowlord_x_, "outdoor.shadowlord_x");
    data.add(0x3A0A, shadowlord_y_, "outdoor.shadowlord_y");
    data.add(0x3A0E, shadowlord_hull_, "outdoor.shadowlord_hull");
    data.add(0x3A26, apparition_tune_, "outdoor.apparition_tune");
}

#define MASK(row, col) view.mask[(row) * 0x20 + (col)]

void Outdoor::wait_if_object1_near(void)
{
    if (map.objects[1].tile != 0 && map.objects[1].z == map.z &&
        abs(map.x - map.objects[1].x) < 6 && abs(map.y - map.objects[1].y) < 6)
        wait_turns(1);
}

/* Horses and carpets only have east and west pictures. A ship turns first: unless it already
 * heads that way the move is used up by the turn (returns 1). With sails up and no wind it
 * doesn't move either. */
int Outdoor::outdoor_set_facing(int dir)
{
    uint8_t kind = party.transport & 0xFC;
    if (kind == 0x10) {
        print_text(strings[Strings::Ride_2946]);   /* "Ride " */
        if (dir == 1) party.transport = 0x12;
        else if (dir == 3) party.transport = 0x13;
        return 0;
    }
    if (kind == 0x14) {
        print_text(strings[Strings::Fly_294C]);   /* "Fly " */
        if (dir == 1) party.transport = 0x14;
        else if (dir == 3) party.transport = 0x15;
        return 0;
    }
    if (kind != 0x20 && kind != 0x24) {
        if (kind != 0x28) return 0;
        print_text(strings[Strings::Row_2951]);   /* "Row " */
        party.transport = (uint8_t)(dir + (party.transport & 0xFC));
        return 0;
    }
    uint8_t heading = (uint8_t)(dir + (party.transport & 0xFC));
    if (party.transport == heading) {
        if (heading > 0x23) return 0;
        if (map.wind_direction != 0) return 0;
        return 1;
    }
    party.transport = heading;
    print_text(strings[Strings::Head_2956]);   /* "Head " */
    static const Strings::Id names[4] = {Strings::North_295C, Strings::East_296A, Strings::South_2963, Strings::West_2970};   /* North East South West */
    if (dir >= 0 && dir <= 3) print_text(strings[names[dir]]);
    party.need_prompt = 1;
    if (map.objects[0].hull < 0x32) print_text(strings[Strings::Hull_weak_2976]);   /* "Hull weak!\n" */
    return 1;
}

/* An object on the square blocks, except: on foot or mounted, a ship with furled sails, a skiff,
 * a carpet (0x1B) or a horse (0x10/0x11) can be walked onto (to Board); a skiff can come alongside
 * a ship with furled sails. The terrain is taken from the view (0 where an object is drawn).
 * Under sail a blocked move is a collision. */
int Outdoor::outdoor_can_move(int dy, int dx)
{
    if ((party.transport & 0xFC) == 0x24) print_text(strings[Strings::Rowing_2982]);   /* "Rowing!\n" */
    int ok = 1;
    int obj = find_object_at(map.z, (uint8_t)(dy + map.y), (uint8_t)(dx + map.x));
    if (obj != 0) {
        ok = 0;
        if (party.transport < 0x30 && party.transport > 0x1F) {
            if (party.transport > 0x27 && obj > 0x23 && obj < 0x28) ok = 1;
        } else if ((obj > 0x23 && obj < 0x2C) || obj == 0x1B || (obj & 0xFE) == 0x10) {
            ok = 1;
        }
    }
    int tile = MASK(5 + dy, 5 + dx);
    if (ok && tile_passable(tile, party.transport)) return 1;
    if (party.sail_direction == 0) {
        if (party.transport > 0x1F && (obj & 0xFC) == 0xEC) return 0;   /* a whirlpool */
        print_text(strings[Strings::Blocked_29AE]);   /* "Blocked!\n" */
        if (tile == 0x2F) {      /* a cactus */
            print_text(strings[Strings::OUCH_29B8]);   /* "OUCH!\n" */
            damage_party();
        } else {
            beep(200, 0xA5);
        }
        flush_keyboard();
        return 0;
    }
    if (tile == 3) print_text(strings[Strings::BREAKING_UP_298B]);          /* "BREAKING UP!\n" (shoals) */
    else if (tile != 0x47) print_text(strings[Strings::COLLISION_2999]);  /* "COLLISION!\n" */
    if (tile == 0x47) {                            /* a pier */
        print_text(strings[Strings::Docked_29A5]);   /* "Docked!\n" */
        party.transport += 4;       /* sails furled */
    } else {
        sound_noise(300, 2000, 100);
        damage_vehicle();
    }
    party.need_prompt = 1;
    party.sail_direction = 0;
    return 0;
}

/* Swamp, scrub and hills cost a monster turn and 2 minutes more; forest and mountains two turns
 * and 4 minutes. The message shows only if no monster acted meanwhile. */
void Outdoor::terrain_slowdown(void)
{
    uint8_t t = *map.get_map_tile_ptr(map.y, map.x);
    int slow;
    if (t == 5) slow = 0;
    else if (t == 0x1E || t == 0x1F) slow = 1;
    else if (t < 4 || t > 0x0F) slow = 0;
    else slow = t > 8 ? 2 : 1;
    if (slow == 1) {
        int acted = outdoor_monsters_turn();
        wait_if_object1_near();
        if (acted == 0) print_text(strings[Strings::Slow_progress_29BF]);   /* "Slow progress!\n" */
        pass_time(2);
    } else if (slow == 2) {
        int acted = outdoor_monsters_turn();
        wait_if_object1_near();
        acted += outdoor_monsters_turn();
        wait_if_object1_near();
        if (acted == 0) print_text(strings[Strings::Very_slow_29CF]);   /* "Very slow!\n" */
        pass_time(4);
    }
}

/* `fast` is outdoor_idle_check's flag: set while the party stands on an unseen (0xFF) square, when
 * the move is only announced. */
int Outdoor::outdoor_move(int fast, unsigned key)
{
    if ((party.transport & 0xFC) == 0x20) {   /* under sail: the key sets the course */
        if (key != party.sail_direction) {
            party.sail_direction = (uint8_t)key;
            party.sail_counter = 0;
        }
        party.need_prompt = 0;
    }
    if (party.transport == 0x1C || (party.transport & 0xFE) == 0x12) step_sound();
    static const int8_t dxs[5] = {0, -1, 1, 0, 0}, dys[5] = {0, 0, 0, -1, 1};
    static const int8_t facing[5] = {0, 3, 1, 0, 2};
    static const Strings::Id names[5] = {Strings::NONE, Strings::West_29EF, Strings::East_29E9, Strings::North_29DB, Strings::South_29E2};   /* West East North South */
    int dx = 0, dy = 0;
    if (key >= 1 && key <= 4) {
        dx = dxs[key];
        dy = dys[key];
        int r = outdoor_set_facing(facing[key]);
        if (r != 0) return r;
        if (party.sail_direction == 0) print_text(strings[names[key]]);
    }
    int moved = outdoor_can_move(dy, dx);
    if (fast == 0 && moved != 0) {
        if ((party.transport & 0xFE) == 0x12) step_sound();
        map.outdoor_step(dy, dx);
        terrain_slowdown();
    }
    return moved;
}

/* Under sail the ship moves by itself until another key is pressed. The tables at ds:29F5 /
 * ds:29F9 give the square the wind blows towards; the ship waits 1 turn per move running before
 * the wind, 2 beating against it and none with the wind abeam. With the HMS Cape plans aboard a
 * turn takes 1 minute instead of 2 and the monsters move only every other turn. */
unsigned Outdoor::outdoor_read_key(void)
{
    unsigned key = 0;
    world_tick();
    status.refresh();
    if ((MASK(6, 5) & 0xFC) == 0xD4) {   /* a waterfall just south */
        waterfall_fall();
        return 0;
    }
    if (party.need_prompt != 0) {
        put_char('\n');
        draw_label_left();
    }
    party.need_prompt = 1;
    while (party.sail_direction != 0) {
        key = to_upper((uint8_t)wait_key_cursor());
        if (key != 0 && key != party.sail_direction) break;
        key = party.sail_direction;
        if (map.wind_direction != 0) {
            int dx = key == 1 ? -1 : key == 2 ? 1 : 0, dy = key == 3 ? -1 : key == 4 ? 1 : 0;
            unsigned wait = 1;
            if (wind_dx_[(map.wind_direction - 1) & 3] != dx) wait = 2;
            if (wind_dy_[(map.wind_direction - 1) & 3] != dy) wait++;
            if (wait % 3 <= party.sail_counter) {
                party.sail_counter = 0;
                break;
            }
        }
        int minutes = 2;
        if (party.hms_cape_plans >= 0x80) {
            cape_speed_toggle = cape_speed_toggle == 0;
            minutes = 1;
        }
        pass_time(minutes);
        if (cape_speed_toggle == 0 || party.hms_cape_plans < 0x80) outdoor_monsters_turn();
        wait_turns(1);
        party.sail_counter++;
        if (game_quit()) return 0;
    }
    if (party.sail_direction == 0) key = (unsigned)get_command_key();
    return key;
}

int Outdoor::cmd_attack_outdoor(void)
{
    Direction dir = {0, 0};
    print_text(strings[Strings::Attack_29FE]);   /* "Attack-" */
    if (*map.get_map_tile_ptr(map.y, map.x) < 4 && ((party.transport & 0xFC) == 0x28 || (party.transport & 0xFE) == 0x14)) {
        print_text(strings[Strings::On_foot_2A06]);   /* "On foot!\n": not from a skiff or carpet over water */
    } else if (ask_direction(&dir) != 0) {
        uint16_t slot;
        unsigned t = (unsigned)find_object_at(map.z, (uint8_t)(map.y + dir.dy), (uint8_t)(map.x + dir.dx), &slot) & 0xFC;
        if (t == 0x2C || (t != 0xB4 && t != 0xE8 && t > 0x3F)) combat.combat_with_object((int)slot);
        else print_text(strings[Strings::Nothing_to_attack_2A10]);   /* "Nothing to attack!\n" */
    }
    return 0;
}

/* The dungeon entrances are entries 0x21-0x28 of the town exit tables. Doom (0x28) is guarded by
 * the Shadowlords while any is left. Britannia's objects are saved and the dungeon's 8 levels
 * (0x200 bytes) loaded from DUNGEON.DAT. From Britannia the party starts on level 1 at (1, 1)
 * facing east; from the Underworld (and never in Doom) on level 8 at (7, 7) facing west. */
int Outdoor::enter_dungeon(const char *prompt)
{
    print_text(prompt);
    int i;
    for (i = 0x20; i < 0x28; i++)
        if (map.exit_x((uint8_t)(1 + i)) == map.x && map.exit_y((uint8_t)(1 + i)) == map.y) break;
    if (i >= 0x28) {
        print_text(strings[Strings::What_dungeon_2A5F]);   /* "\nWhat dungeon?\n" */
        return 1;
    }
    if (party.transport != 0x1C) {
        print_text(strings[Strings::On_foot_2A24]);   /* "\nOn foot!\n" */
        return 1;                /* (the original returns the string's address) */
    }
    if (i == 0x27) {
        if ((map.shadowlord_locations[0] & map.shadowlord_locations[1] & map.shadowlord_locations[2]) < 0x80) {
            print_text(strings[Strings::Attacked_at_entrance_2A2F]);   /* "\nAttacked at entrance!\n" */
            int slot = alloc_object_slot();
            map.objects[slot].tile = 0xFC;
            combat.combat_with_object(slot);
            return slot;
        }
    } else {
        print_text(strings[Strings::NL_NL_2A47]);   /* "\n\n" */
        put_char(0xFC);
        print_text(map.name((unsigned)i + 1));
        put_char(0xFB);
    }
    put_char('\n');
    save_file_part(0x100, map.objects.bytes(), objects_file_name());
    load_file_part((uint32_t)(i - 0x20) * 0x200, 0x200, dungeon.grid, strings[Strings::DUNGEON_DAT_2A53]);   /* "DUNGEON.DAT" */
    map.set(Map::Id((i + 1)));
    if (map.z == 0 || map == Map::DOOM) {
        map.z = 0;
        map.x = map.y = 1;
        dungeon.facing = 1;
        dungeon.entry_dir = 5;
    } else {
        map.z = 7;
        dungeon.facing = 3;
        dungeon.entry_dir = 4;
        map.x = map.y = 7;
    }
    return 1;
}

int Outdoor::cmd_enter(void)
{
    print_text(strings[Strings::Enter_2A6F]);   /* "Enter " */
    const char *what;
    switch (*map.get_map_tile_ptr(map.y, map.x)) {
    case 0x10: what = strings[Strings::hut_2A85]; break;   /* "hut" */
    case 0x11:
        print_text(strings[Strings::the_Shrine_of_the_Codex_2A89]);            /* "the Shrine of the Codex!\n" */
        visit_shrine();
        return 1;
    case 0x12: what = strings[Strings::keep_2AA3]; break;   /* "keep" */
    case 0x13: what = strings[Strings::village_2AA8]; break;   /* "village" */
    case 0x14: what = strings[Strings::towne_2AB0]; break;   /* "towne" */
    case 0x15: what = strings[Strings::castle_2AB6]; break;   /* "castle" */
    case 0x16: return enter_dungeon(strings[Strings::cave_2ABD]);   /* "cave" */
    case 0x17: return enter_dungeon(strings[Strings::mine_2AC2]);   /* "mine" */
    case 0x18: return enter_dungeon(strings[Strings::dungeon_2AC7]);   /* "dungeon" */
    case 0x19: {
        print_text(strings[Strings::the_shrine_of_2A76]);            /* "the shrine of\n" */
        int i;
        for (i = 0; i < 8; i++)
            if (map.shrine_x((unsigned)i) == map.x && map.shrine_y((unsigned)i) == map.y) break;
        print_text(map.virtue_name((unsigned)(i & 7)));   /* the original reads past the table if none matches */
        put_char('\n');
        visit_shrine();
        return 1;
    }
    case 0x1A:
        print_text(strings[Strings::ruins_2ACF]);            /* "ruins" */
        return 1;
    case 0x1B: what = strings[Strings::lighthouse_2AD5]; break;   /* "lighthouse" */
    case 0x39: what = strings[Strings::the_palace_of_Blackthorn_2AE0]; break;   /* "the palace of Blackthorn!" */
    case 0x3E: what = strings[Strings::the_Castle_of_Lord_2AFA]; break;   /* "the Castle of Lord British!" */
    default:
        print_text(strings[Strings::What_2B16]);            /* "What?\n" */
        return 0;
    }
    return enter_town_from_map(what);
}

/* On a square the party cannot see (0xFF) and without the 0x0E effect the view goes dark and
 * moves are only announced (the flag goes to outdoor_move). */
int Outdoor::outdoor_idle_check(int ticked)
{
    if (*map.get_map_tile_ptr(map.y, map.x) == 0xFF && party.active_spell != 0x0E) {
        view.light_radius = 0;
        if (ticked == 0) {
            world_tick();
            ticked = 1;
        }
    } else {
        ticked = 0;
        pass_time(0);
    }
    return ticked;
}

void Outdoor::random_earthquake(void)
{
    if (map.z != 0 && random_between(0xFF, 0) == 0x69) {
        print_text(strings[Strings::EARTHQUAKE_2B1D]);   /* "EARTHQUAKE!\n" */
        screen_shake();
        damage_party();
    }
}

void Outdoor::outdoor_main_loop(void)
{
    int ticked = 0;
    for (;;) {
        int leave = 0, result = 1;
        ticked = outdoor_idle_check(ticked);
        int state = check_party_state();
        if (state == 1) {
            put_char('\n');
            draw_label_left();
            print_text(strings[Strings::Zzzzzz_2B2A]);   /* "Zzzzzz...\n" */
            wait_turns(1);           /* port: the original only redraws; this keeps it from spinning */
        } else if (state == -1) {
            save_file_part(0x100, map.objects.bytes(), objects_file_name());
            endings.party_death();
            leave = 1;
            result = 0;
        } else {
            if (check_moongate() != 0) visit_shrine();
            if (!map.is_outdoors()) return;
            unsigned key = outdoor_read_key() & 0xFF;
            if (game_quit()) return;
            if (key > 0x1F) {
                result = (key < '0' || key > '9') ? dispatch_command((int)key) : cmd_set_active_player((int)key);
            } else {
                switch (key) {
                case 0:
                    result = 0;
                    break;
                case 1: case 2: case 3: case 4:
                    result = outdoor_move(ticked, key);
                    break;
                case 5:   /* Ctrl-E */
                    print_text(strings[Strings::Exit_to_DOS_2B3E]);   /* "Exit to DOS? " */
                    if ((get_command_key() & 0xFF) != 'Y') {
                        print_text(strings[Strings::N_2B4C]);   /* "N\n" */
                    } else {
                        exit_game(0);
                        return;
                    }
                    break;
                case 0x0B:   /* Ctrl-K */
                    print_number(' ', 1, party.karma);
                    put_char('\n');
                    result = 0;
                    break;
                case 0x13:   /* Ctrl-S */
                    print_text(strings[Strings::Sound_2B54]);   /* "Sound " */
                    print_text(strings[sound.enabled == 0 ? Strings::On_2B60 : Strings::Off_2B5B]);   /* "On\n" / "Off\n" */
                    sound.enabled = sound.enabled == 0;
                    break;
                case 0x16:   /* Ctrl-V */
                    print_text(strings[Strings::N1_16_2B4F]);   /* "1.16" */
                    put_char('\n');
                    result = 0;
                    break;
                default:
                    print_text(strings[Strings::What_2B64]);   /* "What?\n" */
                    break;
                }
            }
        }
        if (game_quit()) return;
        if ((party.transport & 0xFC) != 0x20) party.sail_direction = 0;
        if (!map.is_outdoors()) return;
        if (leave) return;
        if (result == 0) continue;
        pass_time(2);
        uint8_t t = *map.get_map_tile_ptr(map.y, map.x);
        if ((t & 0xFE) == 0x6A) {
            troll_bridge();
        } else if (t == 4 && party.transport == 0x1C) {
            swamp_poison();
            wait_turns(1);
        } else if (t == 0x8F) {
            lava_burn();
        } else if (map.x == 0xE9 && map.y == 0xEB && map.z == 0 && map.is_outdoors()) {
            /* the passage to the Shrine of the Codex */
            print_text(strings[Strings::NL_Quote_2B6B]);   /* "\n\"" */
            if (party.sacred_quest == 0) {
                print_text(strings[Strings::Thou_art_not_upon_a_2B7E]);   /* "Thou art not upon a Sacred Quest!\n" */
                print_text(strings[Strings::Passage_denied_2BA1]);   /* "Passage denied!\"\n" */
                map.y++;
            } else {
                print_text(strings[Strings::Pass_Seeker_2B6E]);   /* "Pass, Seeker!\"\n" */
            }
        }
        random_earthquake();
        per_turn_status();
        if (t == 1 && ((party.transport & 0xFC) == 0x28 || (party.transport & 0xFE) == 0x14)) {
            print_text(strings[Strings::Rough_seas_2BB3]);   /* "Rough seas!\n": a skiff or carpet on deep water */
            flash_square(map.y, map.x);
            damage_vehicle();
        }
        if ((t & 0xFC) == 0xD4) waterfall_fall();
        outdoor_monsters_turn();
    }
}

/* The ship the party left at a town's entrance becomes an object again. */
void Outdoor::outdoor_start(void)
{
    map.enter_outdoors();
    if (town.stored_ship > 0x3F) {
        int slot = alloc_object_slot();
        MapObject *o = &map.objects[slot];
        o->skiffs = town.stored_ship & 0x3F;
        o->x = town.stored_ship_x;
        o->y = town.stored_ship_y;
        o->z = 0;
        o->tile = o->tile2 = town.stored_ship < 0x80 ? 0x29 : 0x25;
        o->hull = 99;
        town.stored_ship = 0;
    }
    outdoor_main_loop();
}

/* Compared with a 1-30 roll every turn: 1 on open land, 2 in swamp, forest and mountains, 0 on
 * tiles 0x20-0x26; 3 more by night (the test is hour > 31 or < 5, so only after midnight); 3
 * underground. */
int Outdoor::encounter_chance(void)
{
    if (map.z >= 0x80) return 3;
    uint8_t t = *map.get_map_tile_ptr(map.y, map.x);
    int chance;
    if (t >= 0x20 && t <= 0x26) chance = 0;
    else if (t == 4 || (t > 8 && t < 0x10)) chance = 2;
    else chance = 1;
    if (game_time.hour > 0x1F || game_time.hour < 5) chance += 3;
    return chance;
}

int Outdoor::weighted_random_index(const uint8_t *weights)
{
    unsigned roll = (unsigned)random_between(0xFF, 0);
    int i = 0;
    for (; weights[i] <= roll; i++) roll -= weights[i];
    return i;
}

/* The wandering monsters by terrain and their chances (see outdoor.h). The chances add up to
 * 256, so the roll always lands in the table. */
uint8_t Outdoor::pick_monster_for_terrain(int tile)
{
    if (tile < 4 || (tile > 0x5F && tile < 0x70) || (tile > 0xD3 && tile < 0xD8) || (tile > 0xE3 && tile < 0xE8)) {
        if (random_between(0x40, 0) < 0x10) {
            if (map.z > 0x7F) return under_sea_monsters_[weighted_random_index(under_sea_chances_) & 1];
            if (tile == 1 && random_between(7, 0) == 7) return 0xEC;   /* a whirlpool */
            return sea_monsters_[weighted_random_index(sea_chances_) % 6];
        }
    } else if (tile == 7) {
        if (random_between(3, 0) == 0) return 0xE0;
    } else {
        if (tile == 4 && map.z == 0xFF) return 0xF8;
        if (tile != 0x0C && tile != 0x0D && (tile < 0x10 || ((uint8_t)tile & 0xFC) == 0x30)) {
            if (map.z < 0x80) return land_monsters_[weighted_random_index(land_chances_) % 12];
            return under_land_monsters_[weighted_random_index(under_land_chances_) & 7];
        }
    }
    return 0;
}

/* A square of the loaded map window at least 7 away from the party on both axes. */
void Outdoor::random_spawn_pos(unsigned *y, unsigned *x)
{
    for (;;) {
        *x = (uint8_t)(random_between(0x1F, 0) + map.origin_x);
        *y = (uint8_t)(random_between(0x1F, 0) + map.origin_y);
        int ax = abs((int)*x - map.x), ay = abs((int)*y - map.y);
        if (ax >= 7 && ay >= 7 && ax <= 0xF9 && ay <= 0xF9) return;
    }
}

void Outdoor::spawn_random_monster(void)
{
    int tries;
    unsigned x = 0, y = 0;
    uint8_t tile = 0;
    for (tries = 0; tries < 0x80; tries++) {
        random_spawn_pos(&y, &x);
        tile = pick_monster_for_terrain(*map.get_map_tile_ptr(y, x));
        if (tile != 0 && (tile != 0x2C || (*map.get_map_tile_ptr(y, x) & 0xF0) != 0x60)) break;   /* no pirates on rivers */
    }
    if (tries == 0x80) return;
    int slot = alloc_object_slot();
    set_object_entry(slot, 0, map.z, (uint8_t)y, (uint8_t)x, tile, tile);
    if (tile == 0x2C) map.objects[slot].hull = 100;
}

/* Pirate ships and monsters 0x80 and up, except 0xB4-0xB7 and 0xE8-0xEB. */
int Outdoor::is_active_monster_tile(int tile)
{
    if ((tile < 0x2C || tile > 0x2F) && (tile < 0x80 || (tile > 0xB3 && tile < 0xB8) || (tile > 0xE7 && tile < 0xEC))) return 0;
    return 1;
}

void Outdoor::damage_vehicle(void)
{
    if ((party.transport & 0xF8) != 0x20) {
        damage_party();
        return;
    }
    unsigned damage = (unsigned)random_between(0x1E, 1);
    if (damage < map.objects[0].hull) {
        map.objects[0].hull = (uint8_t)(map.objects[0].hull - damage);
        draw_status_bar();
        return;
    }
    print_text(strings[Strings::Ship_sunk_6ADA]);   /* "Ship sunk!\n" */
    uint8_t skiffs = map.objects[0].skiffs;
    if (skiffs == 0 && party.magic_carpets == 0) {
        party.transport = 0;
        draw_status_bar();
        world_tick();
        sound_slide(0x1E78, 0x28, 0x96, 0x294);
        print_text(strings[Strings::DROWNING_6AF6]);   /* "DROWNING!!!\n" */
        while (check_party_state() != -1 && !game_quit()) {
            flash_square(map.y, map.x);
            damage_party();
        }
    } else {
        print_text(strings[Strings::Abandon_ship_6AE6]);   /* "Abandon ship!\n" */
        if (skiffs == 0) {
            party.magic_carpets--;
            party.transport = (uint8_t)(random_between(1, 0) + 0x14);
        } else {
            party.transport = (party.transport & 3) + 0x28;
        }
        status.dirty = true;
    }
}

/* The pirate ship turns broadside on (a random side) and fires. */
void Outdoor::pirate_broadside(int dist_y, int dist_x, int object)
{
    MapObject *o = &map.objects[object];
    if (dist_x == 0 && (o->tile2 == 0x2C || o->tile2 == 0x2E)) o->tile2 = (uint8_t)((random_between(3, 0) & 2) + 0x2D);
    if (dist_y == 0 && (o->tile2 == 0x2D || o->tile2 == 0x2F)) o->tile2 = (uint8_t)((random_between(3, 0) & 2) + 0x2C);
    world_tick();
    sound_slide(100, 5, 300, 0x514);
    if (combat.animate_missile(1, 5, 5, (uint8_t)(o->y - map.y + 5), (uint8_t)(o->x - map.x + 5)) != 0) {
        world_tick();
        flash_square(map.y, map.x);
        damage_vehicle();
    }
}

/* A whirlpool takes a ship to the Underworld at (0x22, 0x12); other monsters attack. Over water
 * in a skiff or on a carpet there is no fight, only damage. Sea serpents' kin 0xE0 just damage. */
void Outdoor::monster_contact(int object)
{
    MapObject *o = &map.objects[object];
    if ((o->tile & 0xFC) == 0xEC) {
        if (party.transport != 0x1C) {
            o->tile = o->tile2 = 0;
            print_text(strings[Strings::WHIRLPOOL_6B04]);   /* "\nWHIRLPOOL!\n" */
            uint8_t transport = party.transport;
            party.transport = 0xEC;
            world_tick();
            sound_slide(0x1E78, 0x28, 0x96, 0x294);
            party.transport = transport;
            damage_vehicle();
            map.z = 0xFF;
            map.x = 0x22;
            map.y = 0x12;
            map.enter_outdoors();
            return;
        }
    } else if ((o->tile & 0xFC) != 0xE0) {
        world_tick();
        print_text(strings[Strings::Attacked_6B12]);   /* "\nAttacked!\n" */
        if (*map.get_map_tile_ptr(map.y, map.x) > 3 || ((party.transport & 0xFE) != 0x14 && (party.transport & 0xFC) != 0x28)) {
            combat.combat_with_object(object);
            return;
        }
    }
    damage_vehicle();
}

static int wrap_distance(uint8_t a, uint8_t b)
{
    int d = abs((int)a - (int)b);
    return d > 0x7F ? 0x100 - d : d;
}

/* 1 if the monster did something instead of moving: contact when next to the party; tiles 0x88
 * and 0xDC shoot from up to 3 squares (1 in 8); a pirate ship in line within 3 fires. */
int Outdoor::monster_act(int object)
{
    MapObject *o = &map.objects[object];
    uint8_t tile = o->tile;
    int ax = wrap_distance(o->x, map.x), ay = wrap_distance(o->y, map.y);
    if ((ax == 1 && ay == 0) || (ax == 0 && ay == 1)) {
        monster_contact(object);
        return 1;
    }
    if (tile == 0x88 || tile == 0xDC) {
        if (ax < 4 && ay < 4 && random_between(7, 0) == 0) {
            world_tick();
            sound_slide(100, 5, 300, 0x514);
            if (combat.animate_missile(3, 5, 5, (uint8_t)(o->y - map.y + 5), (uint8_t)(o->x - map.x + 5)) != 0) {
                flash_square(map.y, map.x);
                damage_vehicle();
            }
            return 1;
        }
    } else if ((tile & 0xFC) == 0x2C && ((ax == 0 && ay < 4) || (ay == 0 && ax < 4))) {
        print_text(strings[Strings::BOOOM_6B1E]);   /* "* BOOOM! *\n\n" */
        pirate_broadside(ay, ax, object);
        return 1;
    }
    return 0;
}

int Outdoor::monster_can_step(unsigned y, unsigned x, int object)
{
    return tile_passable(*map.get_map_tile_ptr(y, x), map.objects[object].tile) && object_tile_at(map.z, y, x) == 0;
}

/* One position is remembered for all monsters: the square the last one to move came from. */
int Outdoor::not_last_position(unsigned y, unsigned x)
{
    return !(x == last_monster_x && y == last_monster_y);
}

/* 11 x 6 table at ds:2C18 by distance from the party (only read by the Shadowlord). */
uint8_t Outdoor::shadowlord_chase_weight(int object)
{
    int ax = wrap_distance(map.objects[object].x, map.x), ay = wrap_distance(map.objects[object].y, map.y);
    if (ax < 6 && ay < 6) return chase_weight_[ax + ay * 0x0B];
    return 0;
}

/* Pirate ships turn to face the step. Other monsters, except 0xDC, 0x94, 0xD8 and 0xF0, are held
 * up half the time by swamp, scrub and hills and two times in three by forest and mountains. A
 * monster stepping into a moongate is gone. */
void Outdoor::monster_step(int dy, int dx, int object)
{
    MapObject *o = &map.objects[object];
    uint8_t tile = o->tile;
    unsigned x = (unsigned)(o->x + dx), y = (unsigned)(o->y + dy);
    uint8_t ground = *map.get_map_tile_ptr(y, x);
    if ((tile & 0xFC) == 0x2C) {
        uint8_t heading = 0;   /* the original leaves it unset if no case matches */
        if (dx == 0 && dy == -1) heading = 0;
        else if (dx == 1 && dy == 0) heading = 1;
        else if (dx == 0 && dy == 1) heading = 2;
        else if (dx == -1 && dy == 0) heading = 3;
        o->tile = o->tile2 = (uint8_t)(heading + 0x2C);
    } else if (tile != 0xDC && tile != 0x94 && tile != 0xD8 && tile != 0xF0) {
        switch (ground) {
        case 4: case 6: case 7: case 8: case 0x1E: case 0x1F:
            if (random_between(1, 0) == 0) return;
            break;
        case 9: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F:
            if (random_between(2, 0) != 2) return;
            break;
        }
    }
    last_monster_x = o->x;
    last_monster_y = o->y;
    o->x = (uint8_t)x;
    o->y = (uint8_t)y;
    view.rebuild = true;
    if (*map.get_map_tile_ptr(y, x) == 0xDC) o->tile = o->tile2 = 0;
}

void Outdoor::monster_random_walk(int object)
{
    unsigned x = map.objects[object].x, y = map.objects[object].y;
    switch (random_between(3, 0)) {
    case 0: if (monster_can_step(y - 1, x, object)) monster_step(-1, 0, object); break;
    case 1: if (monster_can_step(y, x + 1, object)) monster_step(0, 1, object); break;
    case 2: if (monster_can_step(y + 1, x, object)) monster_step(1, 0, object); break;
    case 3: if (monster_can_step(y, x - 1, object)) monster_step(0, -1, object); break;
    }
}

/* One step towards the party, along a random axis first, then the other; never back to the
 * remembered square; else a random step. The first argument isn't used. */
void Outdoor::monster_chase(int chase, int object)
{
    (void)chase;
    unsigned x = map.objects[object].x, y = map.objects[object].y;
    int ddx = (int8_t)(map.objects[object].x - map.x), ddy = (int8_t)(map.objects[object].y - map.y);
    int sx = ddx == 0 ? 0 : ddx > 0 ? -1 : 1, sy = ddy == 0 ? 0 : ddy > 0 ? -1 : 1;
#define TRY_X (sx != 0 && monster_can_step(y, x + (unsigned)sx, object) && not_last_position(y, x + (unsigned)sx))
#define TRY_Y (sy != 0 && monster_can_step(y + (unsigned)sy, x, object) && not_last_position(y + (unsigned)sy, x))
    if (random_between(1, 0) == 1) {
        if (TRY_X) monster_step(0, sx, object);
        else if (TRY_Y) monster_step(sy, 0, object);
        else monster_random_walk(object);
    } else {
        if (TRY_Y) monster_step(sy, 0, object);
        else if (TRY_X) monster_step(0, sx, object);
        else monster_random_walk(object);
    }
#undef TRY_X
#undef TRY_Y
}

/* Whirlpools move every other turn, half the time at random. The Shadowlord counts its turns
 * near the party in `hull`. Pirate ships don't move in a calm; the word table at ds:2BF8 (by
 * heading and wind) says how many turns they wait per move (4 = none), counted in `skiffs`. */
void Outdoor::monster_move_outdoor(int object)
{
    MapObject *o = &map.objects[object];
    uint8_t tile = o->tile;
    int chase = 1;
    if ((tile & 0xFC) == 0xEC) {
        o->hull ^= 1;
        if (o->hull == 0) return;
        if (random_between(1, 0) == 0) {
            monster_random_walk(object);
            return;
        }
    } else if (tile == 0xFC) {
        if (shadowlord_chase_weight(object) != 0 && o->hull++ < 0x14) chase = 0;
    } else if ((tile & 0xFC) == 0x2C) {
        if (map.wind_direction == 0) return;
        unsigned wait = pirate_wait_[tile - 0x2C][(map.wind_direction - 1) & 3];
        if (wait != 4 && ++o->skiffs > wait) {
            o->skiffs = 0;
            return;
        }
    }
    monster_chase(chase, object);
}

/* Nothing moves under Time Stop; under Quickness, or while the party rides a horse or carpet,
 * only every other call. Once a monster has acted the others stop moving for this turn (they can
 * still act). Monsters outside the loaded map window are removed. */
int Outdoor::outdoor_monsters_turn(void)
{
    int acted = 0;
    if (party.active_spell == 0x54 ||
        (party.active_spell == 0x51 && (quickness_toggle ^= 1, quickness_toggle != 0)) ||
        (((party.transport & 0xFE) == 0x12 || (party.transport & 0xFE) == 0x14) && (riding_toggle ^= 1, riding_toggle != 0)))
        return 0;
    if (random_between(0x1E, 1) < encounter_chance()) spawn_random_monster();
    for (int i = 0x1F; i > 0; i--) {
        if (!is_active_monster_tile(map.objects[i].tile)) continue;
        acted += monster_act(i);
        if (acted == 0) monster_move_outdoor(i);
        if (!map.is_outdoors() || game_quit()) return acted;   /* port: a fight can end somewhere else */
    }
    for (int i = 0x1F; i > 0; i--) {
        MapObject *o = &map.objects[i];
        if (is_active_monster_tile(o->tile) &&
            ((uint8_t)(o->x - map.origin_x) > 0x1F || (uint8_t)(o->y - map.origin_y) > 0x1F))
            set_object_entry(i, 0, 0, 0, 0, 0, 0);
    }
    return acted;
}

/* The toll is 99 - 3 x the strength of the first able member. Refusing, or not having the
 * gold, means a fight with trolls (0xE4). */
void Outdoor::troll_toll(void)
{
    print_text(strings[Strings::Caught_The_trolls_demand_6B2C]);   /* "Caught!\n\nThe trolls demand a " */
    int toll = 99 - party[party.first_able()].str * 3;
    print_number(' ', 2, toll);
    print_text(strings[Strings::gp_toll_Dost_thou_pay_6B4A]);   /* " gp toll!\n\nDost thou pay?" */
    uint8_t c;
    do {
        c = (uint8_t)get_command_key();
        if (game_quit()) return;
    } while (c != 'Y' && c != 'N');
    put_char(c);
    put_char('\n');
    if (c == 'Y') {
        party.gold = (uint16_t)(party.gold - toll);
        status.dirty = true;
        if ((int16_t)party.gold >= 0) return;
        party.gold = (uint16_t)(party.gold + toll);
    }
    int slot = alloc_object_slot();
    set_object_entry(slot, 0, 0, map.y, map.x, 0, 0xE4);
    combat.combat_with_object(slot);
}

/* On foot on a bridge, 1 time in 8: each member sneaks across; a 1-30 roll above a member's
 * dexterity and the trolls catch the party. */
void Outdoor::troll_bridge(void)
{
    if (random_between(7, 0) != 0 || party.transport != 0x1C) return;
    world_tick();
    print_text(strings[Strings::Thou_spieth_trolls_under_6B64]);   /* "\nThou spieth trolls under the bridge!\n\n" */
    wait_turns(10);
    for (unsigned i = 0; i < party.size(); i++) {
        PartyMember *m = &party[i];
        if (m->dead() || m->asleep()) continue;
        print_text(m->name);
        print_text(strings[Strings::sneaks_across_6B8C]);   /* " sneaks across" */
        for (int n = 3; n != 0; n--) {
            wait_turns(5);
            put_char('.');
        }
        print_text(strings[Strings::NL_NL_6B9C]);   /* "\n\n" */
        if (m->dex < (unsigned)random_between(0x1E, 1)) {
            troll_toll();
            return;
        }
    }
    print_text(strings[Strings::Trolls_evaded_6BA0]);   /* "Trolls evaded!\n" */
}

/* Two squares south over the falls; each member whose dexterity fails a 1-30 roll loses 1 hp.
 * The falls at (0x36, 0x8A) lead to the Underworld. */
void Outdoor::waterfall_fall(void)
{
    print_text(strings[Strings::F_A_L_L_S_39B5]);   /* "F-A-L-L-S!!!\n" */
    map.outdoor_step(1, 0);
    wait_turns(1);
    map.outdoor_step(1, 0);
    sound_slide(300, 1, 800, 0x9C4);
    uint8_t transport = party.transport;
    party.transport = 0;
    wait_turns(1);
    for (unsigned i = 0; i < party.size(); i++)
        if (!party[i].dead() && party[i].dex <= (unsigned)random_stat_roll()) damage_member(1, (int)i);
    wait_turns(2);
    party.transport = transport;
    if (map.x == 0x36 && map.y == 0x8A) {
        print_text(strings[Strings::Falling_into_underworld_39C3]);   /* "Falling into underworld!!\n" */
        map.z = 0xFF;
        save_file_part(0x100, map.objects.bytes(), strings[Strings::BRIT_OOL_39DE]);   /* BRIT.OOL */
        load_file_part(0, 0x100, map.objects.bytes(), strings[Strings::UNDER_OOL_39E7]);      /* UNDER.OOL */
        map.enter_outdoors();
    }
}

/* In the Underworld objects 0x1C-0x1F are the Amulet of Lord British (until found) and the shard
 * of each Shadowlord still about. `hull` is the item id for Get. */
void Outdoor::place_underworld_specials(void)
{
    if (map.z == 0) return;
    if (party.lb_amulet == 0) {
        MapObject *o = &map.objects[0x1C];
        o->tile = o->tile2 = 0xB7;
        o->x = 0x69;
        o->y = 0xE1;
        o->z = 0xFF;
        o->hull = 0xF3;
    }
    for (int i = 0; i < 3; i++) {
        if (party.shards[i] != 0 || map.shadowlord_locations[i] >= 0x80) continue;
        MapObject *o = &map.objects[0x1D + i];
        o->tile = o->tile2 = 0xB4;
        o->x = shadowlord_x_[i];
        o->y = shadowlord_y_[i];
        o->z = 0xFF;
        o->hull = shadowlord_hull_[i];
    }
}

void Outdoor::lava_burn(void)
{
    world_tick();
    print_text(strings[Strings::Burning_3A11]);   /* "Burning!\n" */
    damage_party();
}

void Outdoor::swamp_poison(void)
{
    for (unsigned i = 0; i < party.size(); i++) {
        PartyMember *m = &party[i];
        if (m->dead() || m->poisoned()) continue;
        if (m->dex < (unsigned)random_between(0x1E, 1)) {
            m->status = 'P';
            print_text(strings[Strings::Poisoned_3A1B]);   /* "Poisoned!\n" */
        }
    }
}

/* In the camp: the old man appears at the centre, heals every living member and restores magic
 * points. Whoever has earned it gains a level - the level is 1 + the number of times experience
 * / 100 can be halved - with 30 hit points a level and one point of a random attribute (30 at
 * most). He then speaks one of the five karma texts of KARMA.DAT. */
void Outdoor::apparition_level_up(void)
{
    MapObject *man = &map.objects[10];
    print_text(strings[Strings::An_apparition_7750]);   /* "An apparition!\n" */
    sound_sweep(6, 0x9C4, 10000, 1, 0xA3C);
    for (uint16_t note : apparition_tune_) sound_sweep(0x0D, 200, 5000, 1, note);
    man->x = man->y = 5;
    man->tile = man->tile2 = 0x16;
    dissolve_tile(5, 5, 0x174);
    man->tile = man->tile2 = 0x74;
    for (unsigned m = 0; m < party.size(); m++) {
        PartyMember *pm = &party[m];
        if (!pm->dead()) {
            MapObject *o = man;   /* (the original keeps the previous member's object if none is found) */
            pm->hp = pm->max_hp;
            pm->status = 'G';
            for (int i = 0; i < 0x20; i++)
                if (combat.combatants[i].kind == m) {
                    o = &map.objects[combat.combatants[i].object & 0x1F];
                    break;
                }
            o->tile = o->tile2 = party.class_tile((unsigned)str_index((uint8_t)pm->cls, strings[Strings::AMBFDTPRS_7760]));   /* "AMBFDTPRS" */
            man->flags = 0;
            wait_turns(1);
            sound_sweep(0x0D, 200, 5000, 1, 0x157C);
            set_draw_color(gfx.color_text);
            xor_fill_rect(0xB7, 0xB7, 8, 8);
            sound_sweep(1, 0x9C4, -0x15A0, 1, 0x157C);
            for (int i = 3; i != 0; i--) {
                man->flags = 1;
                wait_turns(1);
            }
            int level = 1;
            for (int n = (int16_t)pm->xp / 100; n > 0; n >>= 1) level++;
            if (pm->level != level) {
                pm->level = (uint8_t)level;
                pm->max_hp = pm->hp = (uint16_t)(level * 0x1E);
                print_text(strings[Strings::Hail_776A]);   /* "\n\"Hail, " */
                print_text(pm->name);
                print_text(strings[Strings::For_thy_valiant_deeds_I_7774]);   /* "!\nFor thy valiant deeds, I shall reward thee!\n" */
                print_text(strings[Strings::Thou_art_now_level_77A4]);   /* "Thou art now level " */
                print_number(' ', 1, level);
                print_text(strings[Strings::and_77B8]);   /* ", and\n" */
                switch (random_between(3, 1)) {
                case 1: print_text(strings[Strings::stronger_77C0]); add_byte_capped(0x1E, 1, &pm->str); break;     /* "stronger!" */
                case 2: print_text(strings[Strings::quicker_77CA]); add_byte_capped(0x1E, 1, &pm->dex); break;     /* "quicker!" */
                case 3: print_text(strings[Strings::wiser_77D4]); add_byte_capped(0x1E, 1, &pm->intel); break;   /* "wiser!" */
                }
                print_text(strings[Strings::Quote_SP_77DC]);   /* "\" " */
                get_command_key();
                put_char('\n');
            }
        }
        if (pm->cls == 'A' || pm->cls == 'M') pm->mp = pm->intel;
        else if (pm->cls == 'B') pm->mp = pm->intel >> 1;
        draw_status_bar();
    }
    print_text(strings[Strings::NL_Quote_77E0]);   /* "\n\"" */
    unsigned k = party.karma / 0x14;
    load_file_part(k < 4 ? party.karma_text(k) : 0x29F, 2000, text_buffer.data, strings[Strings::KARMA_DAT_77E4]);   /* KARMA.DAT */
    print_text((char *)text_buffer.data);
    put_char('"');
    get_command_key();
    print_text(strings[Strings::The_strangely_familiar_77F8]);   /* "\n\nThe strangely familiar old man vanishes...\n" */
    man->tile = man->tile2 = 0x16;
    dissolve_tile(5, 5, view.local_map[0xA5]);
    man->tile = man->tile2 = 0;
    wait_turns(1);
    pass_time(0);
}
