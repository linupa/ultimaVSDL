/* cmds.cpp - command handlers from CMDS.OVL. See cmds.h. */
#include <string.h>
#include "cmds.h"
#include "combat.h"
#include "comsubs.h"
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
#include "monsters.h"
#include "npc.h"
#include "objects.h"
#include "outdoor.h"
#include "party.h"
#include "picture.h"
#include "sound.h"
#include "spells.h"
#include "status.h"
#include "strings.h"
#include "text.h"
#include "town.h"
#include "view.h"
#include "world.h"

#define MASK(row, col) view.mask[(row) * 0x20 + (col)]

/* The camp: everyone but the guard sleeps (a bard on guard plays first), then time passes 5
 * minutes a step until the waking hour. After the first hour each step has 1 chance in 64 of an
 * ambush by one of eight monster types (ds:1734); the party is woken only if a guard was posted.
 * Returns 1 if a fight follows. Six hours or more undisturbed, not within 14 hours of the last
 * rest, heal 1-63 hit points and restore magic points for everyone but the guard and the
 * poisoned; then, 1 time in 4 (not in a dungeon), the apparition comes. */
int camp_rest(int hours, unsigned guard, unsigned flags)
{
    uint8_t last_hour = game_time.hour, active = party.active_player;
    int bard = -1, monster = -1, poisoned[8] = {0};
    party.active_spell_turns = 0;
    party.active_spell = 0;
    party.active_player = 0xFF;
    if (flags & 2) dungeon.place_combatants(0, DHERE);
    else combat.clear_combat_tables();
    combat.turn = 0xFF;
    int wake = hours + game_time.hour;
    if (wake > 0x17) wake -= 0x18;
    int g = (int16_t)guard;
    if (g > 5 || g < -1) g = -1;
    for (unsigned m = 0; m < party.size(); m++) {
        unsigned slot;
        for (slot = 0; slot < 0x20; slot++)
            if (combat.combatants[slot].kind == m) break;
        char st = party[m].status;
        if (st == 'P') poisoned[m & 7]++;
        if (st != 'G' && st != 'P' && st != 'S') continue;
        if ((int)m != g && slot < 0x20) combat.sleep_unless_poisoned(slot);
        if (party[m].cls == 'B' && (int)m == g && sound.enabled != 0) bard = (int)slot;
    }
    world_tick();
    if (bard != -1) {   /* tile 0x5F: playing */
        MapObject *o = &map.objects[combat.combatants[bard & 0x1F].object & 0x1F];
        uint8_t tile = o->tile, tile2 = o->tile2;
        o->tile = o->tile2 = 0x5F;
        sound.tune_pos = 1;
        wait_turns(0x34);
        o->tile = tile;
        o->tile2 = tile2;
        if (combat.combatants[bard & 0x1F].kind != g) combat.sleep_unless_poisoned((unsigned)bard);
        world_tick();
    }
    print_text(strings[Strings::Zzzzzz_41D4]);   /* "Zzzzzz...\n\n" */
    Combatant *gc = NULL;
    uint8_t guard_tile = 0;
    if (g != -1) {
        for (int i = 0; i < 0x20; i++)
            if (combat.combatants[i].kind == g) {
                gc = &combat.combatants[i];
                break;
            }
        if (gc) guard_tile = map.objects[gc->object & 0x1F].tile;
    }
    while (wake != game_time.hour && !game_quit()) {
        if (game_time.hour > 0x17) game_time.hour -= 0x18;
        world_tick();
        regen_ring_tick();
        draw_status_bar();
        if (last_hour != game_time.hour && random_between(0x3F, 0) == 0) {
            set_random_seed((uint16_t)time_seed());
            monster = monsters.ambusher((unsigned)random_between(7, 0));
            print_text(strings[Strings::Ambushed_41E0]);   /* "Ambushed!\n\n" */
            if (g >= 0)
                for (int slot = 0; slot < 6; slot++) {
                    unsigned m = combat.combatants[slot].kind;
                    if (m >= party.size()) continue;
                    combat.wake_combatant(slot);
                    party[m].status = poisoned[m & 7] ? 'P' : 'G';
                }
            draw_status_bar();
            if (flags & 2) dungeon.place_combatants(1, DHERE);
            else combat.place_monster_group((unsigned)monster, flags);
            break;
        }
        last_hour = game_time.hour;
        pass_time(5);
        delay_ticks(1);
        if (combat.prev_location < 0x21) {
            map.set(Map::Id(combat.prev_location));
            draw_sky_bar();
            map.set(Map::SCENE);
        }
        if (gc && random_between(3, 0) == 2) {   /* the guard paces */
            Square pace = {gc->x, gc->y};
            switch (random_between(3, 0)) {
            case 0: pace.y--; break;
            case 1: pace.y++; break;
            case 2: pace.x++; break;
            case 3: pace.x--; break;
            }
            if (combat.in_combat_bounds((int16_t)pace.y, (int16_t)pace.x) && combat.combat_tile_free(pace.y, pace.x, guard_tile)) {
                MapObject *o = &map.objects[gc->object & 0x1F];
                gc->x = o->x = (uint8_t)pace.x;
                gc->y = o->y = (uint8_t)pace.y;
                world_tick();
            }
        }
    }
    if (monster >= 0) return 1;
    if (game_time.rest_cooldown != 0 || hours < 6) {
        print_text(strings[Strings::No_effect_41FB]);   /* "No effect...\n" */
    } else {
        print_text(strings[Strings::Party_rested_41EC]);   /* "Party rested!\n" */
        for (unsigned m = 0; m < party.size(); m++) {
            PartyMember *pm = &party[m];
            if (poisoned[m & 7] != 0 || pm->dead() || (int)m == g) continue;
            pm->hp = (uint16_t)(pm->hp + random_between(0x3F, 1));
            if ((int16_t)pm->max_hp < (int16_t)pm->hp) pm->hp = pm->max_hp;
            if (pm->cls == 'A' || pm->cls == 'M') pm->mp = pm->intel;
            else if (pm->cls == 'B') pm->mp = pm->intel >> 1;
        }
        if (!(flags & 0x82) && random_between(99, 0) < 0x19) {
            game_time.apparition_month = game_time.month;
            apparition_level_up();
        }
        game_time.rest_cooldown = 0x0E;
    }
    for (unsigned m = 0; m < party.size(); m++)
        if (party[m].asleep()) party[m].status = 'G';
    party.active_player = active;
    draw_status_bar();
    return 0;
}

/* Esc in combat: once the battle is won (and not in a dungeon room) everything is swept off the
 * map at once, which ends it. Returns 1 if refused. */
int combat_escape(void)
{
    print_text(strings[Strings::Escape_4574]);   /* "Escape" */
    int members = 0;
    for (int i = 0; i < 0x20; i++)
        if ((combat.combatants[i].flags & 0xA0) == 0x80) {
            members = 1;
            break;
        }
    if (members) {
        if (combat.flags & 0x80) {
            print_text(strings[Strings::Not_here_457B]);   /* "-Not here!\n" */
            status.dirty = true;
            return 1;
        }
        if (combat.victory == 0) {
            print_text(strings[Strings::Not_yet_4587]);   /* "-Not yet!\n" */
            status.dirty = true;
            return 1;
        }
    }
    put_char('!');
    for (int i = 0; i < 0x20; i++)
        if (combat.combatants[i].flags != 0) {
            combat.combat_remove(-1 - i);
            world_tick();
        }
    for (int i = 0; i < 0x20; i++)
        if (map.objects[i].tile != 0) {
            combat.combat_remove(i + 1);
            world_tick();
        }
    sound_slide(0x28, 1, 2000, 0x4B0);
    status.dirty = true;
    return 0;
}

/* The town carries on for 16 turns first (an attacker cancels the rest). Then everyone sleeps,
 * 10 minutes at a time, until the hour comes round or someone turns up at the bed. The waking
 * hour past midnight is hour + n - 23, one more than asked: kept as it is. */
void cmd_hole_up_bed(void)
{
    print_text(strings[Strings::For_how_many_hours_4209]);   /* "For how many hours? " */
    uint8_t key;
    do {
        key = (uint8_t)get_command_key();
        if (game_quit()) return;
    } while (key != ' ' && (key < '0' || key > '9'));
    if (key == ' ' || key == '0') return;
    put_char(key);
    put_char('\n');
    unsigned wake = (unsigned)(game_time.hour + key - '0');
    if (wake > 0x17) wake -= 0x17;
    uint8_t active = party.active_player;
    for (int i = 0; i < 0x10; i++) {
        npcs.update_npcs(game_time.hour);
        world_tick();
        active = party.active_player;
        if (npcs.event == 'a') return;
    }
    for (unsigned i = 0; i < party.size(); i++)
        if (party[i].good()) party[i].status = 'S';
    draw_status_bar();
    print_text(strings[Strings::Zzzzzzz_421E]);   /* "Zzzzzzz...\n" */
    set_draw_color(0);
    fill_rect(0xB7, 0xB7, 8, 8);
    uint8_t prev = game_time.hour;
    while (wake != game_time.hour && !game_quit()) {
        pass_time(10);
        if (prev != game_time.hour && (game_time.hour == 0x14 || game_time.hour == 5)) town.update_lamps();
        prev = game_time.hour;
        draw_sky_bar();
        per_turn_status();
        draw_status_bar();
        town.place_npcs();
        if (find_object_at(map.z, map.y, map.x) != 0) {
            wake = 0xFFFF;
            break;
        }
        delay_ticks(1);
    }
    if (wake == 0xFFFF) print_text(strings[Strings::Thrown_out_of_bed_422A]);   /* "Thrown out of bed!\n" */
    for (unsigned i = 0; i < party.size(); i++)
        if (party[i].asleep()) party[i].status = 'G';
    map.x++;
    view.rebuild = true;
    map.objects[0].x++;
    party.active_player = active;
    draw_status_bar();
    world_tick();
}

int board_check_on_foot(void)
{
    if (party.transport == 0x1C || party.transport == 0x1D) return 1;
    print_text(strings[Strings::On_foot_423E]);   /* "\nOn foot\n" */
    return 0;
}

/* A ship can be boarded on foot, from a carpet or from a skiff. */
int board_check_ship_ok(void)
{
    uint8_t t = party.transport;
    if (t < 0x14 || (t > 0x15 && (t < 0x1C || (t > 0x1D && (t < 0x28 || t > 0x2B))))) {
        print_text(strings[Strings::On_foot_4248]);   /* "\nOn foot\n" */
        return 0;
    }
    return 1;
}

int is_land_adjacent(void)
{
    return is_land_tile(5, 4) == 1 || is_land_tile(5, 6) == 1 || is_land_tile(6, 5) == 1 || is_land_tile(4, 5) == 1;
}

/* A square showing an object counts if the object is a carpet, a party, a ship with furled
 * sails, a horse or a skiff. */
int is_land_tile(int x, int y)
{
    if (view.mask[x + y * 0x20] == 0) {
        uint8_t o = view.tiles[x + y * 0x10], k = o & 0xFC;
        return o == 0x1B || k == 0x1C || k == 0x24 || k == 0x10 || k == 0x28;
    }
    return tile_passable(view.mask[x + y * 0x20], 0x1C);
}

/* The object the party stands on becomes its transport. A horse that belongs to someone with
 * something to say can't be taken. Boarding a ship stows the carpet or skiff the party came on. */
int cmd_board(void)
{
    if (map.is_dungeon()) {
        print_text(strings[Strings::Not_here_4252]);   /* "\nNot here!\n" */
        return 1;
    }
    uint16_t slot;
    uint8_t tile = (uint8_t)find_object_at(map.z, map.y, map.x, &slot);
    int index = (int)slot;
    if ((tile & 0xFE) == 0x10) {
        int npc;
        if (!map.is_outdoors() && (npc = town.npc_for_object(index)) != -1 && npcs.state[npc].dialogue != 0) {
            print_text(strings[Strings::Nay_425E]);   /* "\"Nay!\"\n" */
            return 1;
        }
        if (!board_check_on_foot()) return 1;
        print_text(strings[Strings::horse_4266]);   /* "horse\n" */
        party.transport = (uint8_t)(tile + 2);
    } else if (tile == 0x1B) {
        if (!board_check_on_foot()) return 1;
        print_text(strings[Strings::carpet_426D]);   /* "carpet\n" */
        party.transport = 0x14;
    } else if ((tile & 0xFC) == 0x28) {
        if (!board_check_on_foot()) return 1;
        print_text(strings[Strings::skiff_4275]);   /* "skiff\n" */
        party.transport = tile;
    } else if ((tile & 0xFC) == 0x24) {
        if (!board_check_ship_ok()) return 1;
        print_text(strings[Strings::Ship_427C]);   /* "Ship\n" */
        uint8_t hull = map.objects[index].hull;
        if (hull < 10) print_text(strings[Strings::DANGER_SHIP_BADLY_4282]);   /* "\nDANGER: SHIP BADLY DAMAGED!\n" */
        unsigned skiffs = map.objects[index].skiffs;
        if ((party.transport & 0xFE) == 0x14) party.magic_carpets++;
        if ((party.transport & 0xFC) == 0x28) skiffs++;
        map.objects[0].hull = hull;
        if (skiffs == 0) print_text(strings[Strings::WARNING_NO_SKIFFS_ON_42A0]);   /* "\nWARNING: NO SKIFFS ON BOARD!\n" */
        map.objects[0].skiffs = (uint8_t)skiffs;
        status.dirty = true;
        party.transport = tile;
    } else {
        print_text(strings[Strings::What_42BF]);   /* "What?\n" */
        return 0;
    }
    set_object_entry(index, 0, 0, 0, 0, 0, 0);
    view.rebuild = true;
    return 1;
}

/* A ship fires only abeam. The shot reaches 3 squares; a hit takes 1-20 off the target's `hull`
 * and removes it when that goes below zero (monsters have 0, pirate ships 100). */
void fire_ship_cannons(void)
{
    Direction dir = {0, 0};
    if (party.transport < 0x20 || party.transport > 0x27) {
        print_text(strings[Strings::What_42C6]);   /* "What?\n" */
        return;
    }
    if (ask_direction(&dir) == 0) return;
    int dx = (int16_t)dir.dx, dy = (int16_t)dir.dy;
    if (!((dx != 0 || (party.transport & 1) != 0) && (dx == 0 || (party.transport & 1) == 0))) {
        print_text(strings[Strings::Fire_broadsides_only_42CD]);   /* "Fire broadsides only!\n" */
        return;
    }
    unsigned x = map.x, y = map.y;
    uint16_t slot = 0;
    sound_slide(300, 5, 200, 1000);
    for (int n = 0;; ) {
        x += (unsigned)dx;
        y += (unsigned)dy;
        int t = find_object_at(map.z, (uint8_t)y, (uint8_t)x, &slot);
        if (is_active_monster_tile(t) && (t & 0xFC) != 0xEC) break;
        if (++n > 2) {
            combat.animate_missile(1, dy * 3 + 5, dx * 3 + 5, 5, 5);
            return;
        }
    }
    int index = (int)slot;
    MapObject *o = &map.objects[index];
    if (combat.animate_missile(1, o->y - map.y + 5, o->x - map.x + 5, 5, 5) == 0) return;
    world_tick();
    flash_square((int)y, (int)x);
    o->hull = (uint8_t)(o->hull - random_between(0x14, 1));
    if (o->hull < 0x80) return;
    set_object_entry(index, 0, 0, 0, 0, 0, 0);
    view.rebuild = true;
}

/* In a town: a cannon (tiles 0xB4-0xB7, facing in the low bits) next to the party fires 4
 * squares along its facing. It destroys doors and kills whoever it hits (karma -5); the town is
 * alerted in any case. */
void cmd_fire(void)
{
    if (map.is_dungeon()) {
        print_text(strings[Strings::What_42E4]);   /* "What?\n" */
        return;
    }
    if (map.is_outdoors()) {
        fire_ship_cannons();
        return;
    }
    restore_open_door(map.open_door_y, map.open_door_x, map.open_door_tile);
    int cdx, cdy;
    uint8_t cannon;
    if ((MASK(4, 5) & 0xFC) == 0xB4) { cdx = 0; cdy = -1; cannon = MASK(4, 5); }
    else if ((MASK(5, 6) & 0xFC) == 0xB4) { cdx = 1; cdy = 0; cannon = MASK(5, 6); }
    else if ((MASK(6, 5) & 0xFC) == 0xB4) { cdx = 0; cdy = 1; cannon = MASK(6, 5); }
    else if ((MASK(5, 4) & 0xFC) == 0xB4) { cdx = -1; cdy = 0; cannon = MASK(5, 4); }
    else {
        print_text(strings[Strings::What_42EB]);   /* "What?\n" */
        return;
    }
    unsigned x = (unsigned)(cdx + map.x), y = (unsigned)(cdy + map.y);
    int from_x = cdx + 5, from_y = cdy + 5;
    static const int8_t fdx[4] = {0, 1, 0, -1}, fdy[4] = {-1, 0, 1, 0};
    int dx = fdx[cannon & 3], dy = fdy[cannon & 3];
    print_text(strings[Strings::BOOOM_42F2]);   /* "BOOOM!\n" */
    sound_slide(300, 5, 200, 1000);
    town.alert_town();
    int door = 0, hit = 0, target = 0, vx = from_x, vy = from_y;
    for (int range = 5; !door && !hit && --range > 0; ) {
        x += (unsigned)dx;
        y += (unsigned)dy;
        vx += dx;
        vy += dy;
        uint16_t slot;
        uint8_t t = object_tile_at(map.z, y, x, &slot);
        if (t == 0) {
            uint8_t m = *map.get_map_tile_ptr(y, x);
            if (m > 0x96 && (m < 0x9A || (m > 0xB7 && m < 0xBC))) door = 1;
        } else if ((t > 0x1B && (t & 0xF8) != 0x78) || t == 0x10 || t == 0x11) {   /* (the original also tests (t & 0xFC) != 0x2F, always true) */
            hit = 1;
            target = (int)slot;
        }
    }
    combat.animate_missile(1, vy, vx, from_y, from_x);
    if (door || hit) flash_square((int)y, (int)x);
    if (door) {
        print_text(strings[Strings::Door_destroyed_42FA]);   /* "Door destroyed!\n" */
        *map.get_map_tile_ptr(y, x) = 0x44;
        view.rebuild = true;
        map.open_door_tile = 0;
    }
    if (hit && target != 0) {
        set_object_entry(target, 0, 0, 0, 0, 0, 0);
        view.rebuild = true;
        sub_byte_floor0(5, &party.karma);
        int npc = town.npc_for_object(target);
        if (npc == -1) return;
        town.mark_npc_dead(npc);
        town.remove_npc(npc);
    }
    if (hit && target == 0) damage_party();   /* object 0 is the party itself */
}

void cmd_ignite_torch(void)
{
    if (party.torches == 0) {
        print_text(strings[Strings::None_owned_430B]);   /* "None owned!\n" */
        return;
    }
    party.torches--;
    if (!map.is_dungeon()) party.torch_light = 0xF0;
    else add_byte_capped(0xFF, (uint8_t)(random_between(0x0F, 0) + 0x70), &party.torch_light);
}

void cmd_new_order(void)
{
    print_text(strings[Strings::Swap_4318]);   /* "\n\nSwap " */
    int a = ask_member_number();
    if (a == -1) {
        print_text(strings[Strings::nobody_4320]);   /* "nobody!\n" */
        return;
    }
    print_text(party[a].name);
    if (a == 0) {
        print_text(strings[Strings::NL_NL_4329]);   /* "\n\n" */
        print_text(party[0].name);
        print_text(strings[Strings::must_lead_432C]);   /* " must lead!\n" */
        return;
    }
    print_text(strings[Strings::with_4339]);   /* "\nwith " */
    int b = ask_member_number();
    if (b == -1) {
        print_text(strings[Strings::nobody_4340]);   /* "nobody!\n" */
        return;
    }
    print_text(party[b].name);
    if (b == 0) {
        print_text(strings[Strings::NL_NL_4349]);   /* "\n\n" */
        print_text(party[0].name);
        print_text(strings[Strings::must_lead_434C]);   /* " must lead!\n" */
        return;
    }
    print_text(strings[Strings::Bang_NL_4359]);   /* "!\n" */
    PartyMember tmp = party[a];
    party[a] = party[b];
    party[b] = tmp;
    status.dirty = true;
}

/* The transport is left on the square as an object. From a ship with furled sails and no land
 * alongside the party takes a skiff (or, with none, a carpet). The first test can never be
 * true (location < 0x20 && > 0x29), so X works everywhere. */
void cmd_exit(void)
{
    if (map.id() < 0x20 && map.id() > 0x29) {
        print_text(strings[Strings::Not_here_435C]);   /* "\nNot here!\n" */
        return;
    }
    uint8_t tile = *map.get_map_tile_ptr(map.y, map.x);
    uint8_t kind = party.transport & 0xFC, left, skiffs = 0;   /* skiffs is uninitialised in the original */
    if (kind == 0x10) {
        print_text(strings[Strings::horse_43A4]);   /* "horse!\n" */
        left = (uint8_t)(party.transport - 2);
        party.transport = 0x1C;
    } else if (kind == 0x14) {
        if (!is_land_adjacent() && !tile_passable(tile, 0x1C)) {
            print_text(strings[is_land_adjacent() == 0 ? Strings::No_land_nearby_4386 : Strings::Not_here_4398]);   /* "\nNo land nearby!\n" / "\nNot here!\n" */
            return;
        }
        print_text(strings[Strings::carpet_437D]);   /* "carpet!\n" */
        left = 0x1B;
        party.transport = 0x1C;
    } else if (kind == 0x1C) {
        print_text(strings[Strings::what_4368]);   /* "what?\n" */
        return;
    } else if (kind == 0x20) {
        print_text(strings[Strings::Under_sail_436F]);   /* "\nUnder sail!\n" */
        return;
    } else if (kind == 0x24) {
        print_text(strings[Strings::ship_43D2]);   /* "ship!\n" */
        int land = is_land_adjacent();
        left = party.transport;
        skiffs = map.objects[0].skiffs;
        if (land) {
            party.transport = 0x1C;
        } else if (skiffs != 0) {
            party.transport += 4;
            skiffs--;
        } else if (party.magic_carpets != 0) {
            party.magic_carpets--;
            party.transport = 0x14;
        } else {
            print_text(strings[Strings::No_skiffs_on_board_43D9]);   /* "\nNo skiffs on board!\n" */
            return;
        }
    } else if (kind == 0x28) {
        if (!is_land_adjacent()) {
            print_text(strings[Strings::No_land_nearby_43AC]);   /* "\nNo land nearby!\n" */
            return;
        }
        if ((tile & 0xFE) == 0x6A) {
            print_text(strings[Strings::Not_here_43BE]);   /* "\nNot here!\n" (under a bridge) */
            return;
        }
        print_text(strings[Strings::skiff_43CA]);   /* "skiff!\n" */
        left = party.transport;
        party.transport = 0x1C;
    } else {
        return;   /* no other transport exists; the original would leave a garbage object */
    }
    int slot = alloc_object_slot();
    set_object_entry(slot, map.objects[0].hull, map.z, map.y, map.x, left, left);
    map.objects[slot].skiffs = skiffs;
    draw_status_bar();
}

/* The Shadowlords' names, which yelled summon them (ds:444A, from DATA.OVL). */
static Strings::Id shadowlord_names[3];
[[maybe_unused]] static const bool shadowlord_names_added = (Data::instance().add(0x444A, shadowlord_names, "cmds.shadowlord_names"), true);

/* At the three flames (locations 0x1E-0x20), yelling a Shadowlord's name (FAULINEI, ASTAROTH,
 * NOSFENTOR) while he is still about brings him two squares north of the party, as an NPC that
 * stands still. */
void yell_summon_shadowlord(const char *word)
{
    if (map != Map::LYCAEUM && map != Map::EMPATH_ABBEY && map != Map::SERPENTS_HOLD) {
        print_text(strings[Strings::No_effect_443C]);   /* "\nNo effect!\n" */
        return;
    }
    int who;
    for (who = 0; who < 3; who++)
        if (str_find_nocase(word, strings[shadowlord_names[who]]) > -1) break;
    if (who == 3 || map.y <= 1 || map.shadowlord_locations[who] == 0xFF) {
        print_text(strings[Strings::No_effect_440B]);   /* "\nNo effect!\n" */
        return;
    }
    for (int i = 0; i < 0x20; i++)
        if (map.objects[i].tile == 0xFC) {
            print_text(strings[Strings::No_effect_4418]);   /* "\nNo effect!\n" */
            return;
        }
    party.summoned_shadowlord = (uint8_t)who;
    int obj = alloc_object_slot();
    set_object_entry(obj, 0, map.z, (uint8_t)(map.y - 2), map.x, 0xFC, 0xFC);
    int npc = 0x1F;
    for (int i = 0x1F; i >= 0; i--)
        if (npcs.type[i] == 0) {
            npc = i;
            break;
        }
    NpcState *st = &npcs.state[npc];
    NpcSchedule *sc = &npcs.schedules[npc];
    st->state = 1;
    st->object = (uint16_t)obj;
    st->x = map.x;
    st->y = (uint16_t)(map.y - 2);
    st->z = map.z;
    memset(sc->times, 0, 4);
    for (int i = 0; i < 3; i++) {
        sc->activity[i] = 6;
        sc->x[i] = map.x;
        sc->y[i] = (uint8_t)(map.y - 2);
        sc->z[i] = map.z;
    }
    npcs.type[npc] = 0xFC;
    print_text(strings[Strings::A_shadowlord_appears_4425]);   /* "\nA shadowlord appears\n" */
    sound_sweep(2, 2000, 30000, 1, 0x28A0);
    map.objects[obj].tile = map.objects[obj].tile2 = 0x16;
    dissolve_tile(3, 5, 0x1FC);
    map.objects[obj].tile = map.objects[obj].tile2 = 0xFC;
}

/* A word of power yelled beside a ruined shrine (tile 0x1A): the virtue and three times the
 * mantra, all correct for this shrine, restore it. */
void meditate_restore_shrine(unsigned y, unsigned x, int shrine)
{
    char buf[32];
    print_text(strings[Strings::Upon_what_virtue_dost_4450]);   /* "\nUpon what virtue\ndost thou\nmeditate?\n\n:" */
    read_line(0x0F, buf);
    int ok = str_find_nocase(buf, map.virtue_name((unsigned)shrine)) != -1;
    for (int i = 3; i != 0; i--) {
        print_text(strings[Strings::Mantra_4479]);   /* "\nMantra:" */
        read_line(0x0F, buf);
        if (str_find_nocase(buf, map.mantra((unsigned)shrine)) == -1) ok = 0;
    }
    if (ok && map.shrine_x((unsigned)shrine) == x && map.shrine_y((unsigned)shrine) == y) {
        map.shrine_state[shrine] &= 0x7F;
        print_text(strings[Strings::The_Shrine_is_restored_4482]);   /* "\n\nThe Shrine is\nrestored!\n" */
        screen_shake();
        *map.get_map_tile_ptr(y, x) = 0x19;
        view.rebuild = true;
    } else {
        put_char('\n');
    }
}

/* The eight words (FALLAX ... VERAMOCOR) seal or unseal a dungeon: the entrance must be next to
 * the party (west, south, east, north are tried) and must be dungeon n's own, showing its open
 * tile (seal_tiles) or the seal (0xDF). */
void yell_word_of_power(const char *word)
{
    for (int n = 0; n < 8; n++) {
        if (str_find_nocase(word, map.word_of_power((unsigned)n)) < 0) continue;
        print_text(strings[Strings::A_word_of_power_is_44D7]);   /* "\nA word of power is uttered\n" */
        screen_shake();
        static const int8_t dxs[4] = {-1, 0, 1, 0}, dys[4] = {0, 1, 0, -1};
        int d;
        uint8_t t = 0;
        for (d = 0; d < 4; d++) {
            t = MASK(5 + dys[d], 5 + dxs[d]);
            if (t == map.seal_tile((unsigned)n) || t == 0xDF || t == 0x1A) break;
        }
        if (d == 4) break;
        unsigned x = (unsigned)(map.x + dxs[d]), y = (unsigned)(map.y + dys[d]);
        if (t == 0x1A) {
            meditate_restore_shrine(y, x, n);
            return;
        }
        if (x == map.exit_x((uint8_t)(0x21 + n)) && y == map.exit_y((uint8_t)(0x21 + n))) {
            map.dungeon_seals[n] ^= 0x80;
            *map.get_map_tile_ptr(y, x) ^= map.seal_tile((unsigned)n) ^ 0xDF;
            view.rebuild = true;
        }
        return;
    }
    print_text(strings[Strings::No_effect_44F4]);   /* "\nNo effect!\n" */
}

/* On a ship Yell furls or hoists the sails. The original returns a pointer, never zero. */
int cmd_yell(void)
{
    char buf[40];
    if ((party.transport & 0xF8) == 0x20 && !map.in_scene()) {
        if ((party.transport & 0xFC) == 0x20) {
            print_text(strings[Strings::FURL_451A]);   /* "FURL!\n" */
            party.transport += 4;
        } else {
            print_text(strings[Strings::HOIST_4521]);   /* "HOIST!\n" */
            party.transport -= 4;
        }
        return 1;
    }
    print_text(strings[Strings::what_4529]);   /* "what?\n:" */
    read_line(0x1E, buf);
    if (buf[0] == '\0') {
        print_text(strings[Strings::Nothing_4531]);   /* "Nothing\n" */
        return 1;
    }
    put_char('\n');
    if (map.is_outdoors()) yell_word_of_power(buf);
    else if (!map.on_tile_map()) print_text(strings[Strings::No_effect_453A]);   /* "\nNo effect!\n" */
    else yell_summon_shadowlord(buf);
    return 1;
}

/* 0x5B, 0x90-0x93 (chairs), 0xA5, 0xA6 (desk, barrel), 0xA8, 0xA9, 0xAD-0xAF, 0xB4-0xB7 (cannons). */
int is_pushable_tile(uint8_t tile)
{
    if (tile < 0xA7) return !(tile < 0xA5 && tile != 0x5B && (tile < 0x90 || tile > 0x93));
    if (tile < 0xA8) return 0;
    if (tile > 0xA9) {
        if (tile < 0xAD) return 0;
        if (tile > 0xAF && (tile < 0xB4 || tile > 0xB7)) return 0;
    }
    return 1;
}

/* Chairs and cannons turn to face the way they were pushed (the opposite when pulled). */
unsigned rotate_pushed_tile(int pulled, unsigned tile, int dy, int dx)
{
    if (dx == 1 && dy == 0) tile += 1;
    if (dx == 0 && dy == 1) tile += 2;
    if (dx == -1 && dy == 0) tile += 3;
    if (pulled) tile ^= 2;
    return tile;
}

void push_object(int dy, int dx, unsigned to_y, unsigned to_x, unsigned from_y, unsigned from_x, uint8_t dest_tile, uint8_t tile)
{
    print_text(strings[Strings::Pushed_4547]);   /* "Pushed!\n" */
    *map.get_map_tile_ptr(to_y, to_x) = tile;
    *map.get_map_tile_ptr(from_y, from_x) = dest_tile;
    unsigned k = tile & 0xFC;
    if (k == 0x90 || k == 0xB4) *map.get_map_tile_ptr(to_y, to_x) = (uint8_t)rotate_pushed_tile(0, k, dy, dx);
}

void pull_object(int dy, int dx, unsigned to_y, unsigned to_x, unsigned from_y, unsigned from_x, uint8_t dest_tile, uint8_t tile)
{
    print_text(strings[Strings::Pulled_4550]);   /* "Pulled!\n" */
    *map.get_map_tile_ptr(from_y, from_x) = dest_tile;
    *map.get_map_tile_ptr(to_y, to_x) = tile;
    unsigned k = dest_tile & 0xFC;
    if (k == 0x90 || k == 0xB4) *map.get_map_tile_ptr(from_y, from_x) = (uint8_t)rotate_pushed_tile(1, k, dy, dx);
}

/* The thing moves a square on if the floor beyond is plain (0x44; for a cannon 0x45). If not,
 * and the pusher stands on such floor, the two change places ("Pulled!"). Either way the pusher
 * steps into the thing's square. In combat the pusher is the fighter whose turn it is. */
void cmd_push(void)
{
    Direction dir = {0, 0};
    restore_open_door(map.open_door_y, map.open_door_x, map.open_door_tile);
    if (ask_direction(&dir) == 0) return;
    int dx = (int16_t)dir.dx, dy = (int16_t)dir.dy;
    uint8_t party_x = map.x, party_y = map.y;
    int in_combat = map.in_scene();
    if (in_combat) {
        map.x = combat.combatants[combat.turn & 0x1F].x;
        map.y = combat.combatants[combat.turn & 0x1F].y;
    }
    unsigned x = (uint16_t)(dir.dx + map.x), y = (uint16_t)(dir.dy + map.y);
    if (in_combat && combat.combat_room_trigger(y, x) != 0) return;   /* (the party position stays the fighter's, as in the original) */
    uint8_t tile = *map.get_map_tile_ptr(y, x);
    if (find_object_at(map.z, (uint8_t)y, (uint8_t)x) != 0 || !is_pushable_tile(tile)) {
        print_text(strings[Strings::Won_t_budge_4559]);   /* "Won't budge!\n" */
        return;
    }
    uint8_t floor = (tile & 0xFC) == 0xB4 ? 0x45 : 0x44;
    unsigned x2 = (uint16_t)(x + (unsigned)dx), y2 = (uint16_t)(y + (unsigned)dy);
    uint8_t beyond = *map.get_map_tile_ptr(y2, x2);
    uint8_t here = *map.get_map_tile_ptr(map.y, map.x);
    if (find_object_at(map.z, (uint8_t)y2, (uint8_t)x2) == 0 && beyond == floor) {
        push_object(dy, dx, y2, x2, y, x, beyond, tile);
    } else if (here == floor) {
        pull_object(dy, dx, y, x, map.y, map.x, tile, here);
    } else {
        print_text(strings[Strings::Won_t_budge_4567]);   /* "Won't budge\n" */
        return;
    }
    map.x = (uint8_t)(map.x + dx);
    map.y = (uint8_t)(map.y + dy);
    view.rebuild = true;
    if (in_combat) {
        Combatant *c = &combat.combatants[combat.turn & 0x1F];
        MapObject *o = &map.objects[c->kind & 0x1F];   /* the original indexes the objects by `kind`, not `object` */
        c->x = (uint8_t)(c->x + dx);
        c->y = (uint8_t)(c->y + dy);
        o->x = (uint8_t)(o->x + dx);
        o->y = (uint8_t)(o->y + dy);
        map.x = party_x;
        map.y = party_y;
        world_tick();
    }
}

/* The reagents owned are listed in the status window; arrows move the bar, Return or Space
 * marks one, M mixes, Esc gives up. */
unsigned mix_select_reagents(void)
{
    uint8_t list[8];
    int n = 0;
    for (int i = 0; i < 8; i++)
        if (party.reagents[i] != 0) list[n++] = (uint8_t)i;
    select_text_window(1);
    define_text_window(9, 0x26, 1, 0x18, 1);
    put_char(0xFF);
    define_text_window(9, 0x27, 1, 0x18, 1);
    clear_side_panel();
    draw_panel_title(strings[Strings::Reagents_8F64]);   /* "Reagents:" */
    for (int i = 0; i != n; i++) {
        put_char('\n');
        put_char(' ');
        print_number('0', 2, party.reagents[list[i]]);
        put_char(' ');
        print_text(spells.reagent_name(list[i]));
    }
    int cur = 0, done = 0;
    unsigned mask = 0;
    highlight_member_row(1);
    while (!done) {
        select_text_window(2);
        int key = get_command_key();
        if (game_quit()) key = 0x1B;
        if (key == 4 || key == 2) {
            if (cur < n - 1) {
                highlight_member_row(cur + 1);
                cur++;
                highlight_member_row(cur + 1);
            }
        } else if (key == 1 || key == 3) {
            if (cur > 0) {
                highlight_member_row(cur + 1);
                cur--;
                highlight_member_row(cur + 1);
            }
        } else if (key == 0x0D || key == ' ') {
            unsigned bit = 0x80u >> (list[cur] & 0x1F);
            mask ^= bit;
            select_text_window(1);
            set_cursor(cur + 1, 3);
            put_char(0xFD);
            put_char((bit & mask) == 0 ? ' ' : 0x0F);
            put_char(0xFD);
        } else if (key == 0x1B) {
            mask = 0xFFFF;
            done = 1;
            put_char('\n');
        } else if (key == 'M') {
            done = 1;
            print_text(strings[Strings::NL_NL_8F6E]);   /* "\n\n" */
        }
    }
    highlight_member_row(cur + 1);
    select_text_window(2);
    return mask;
}

unsigned mix_ask_amount(unsigned reagent_mask)
{
    for (;;) {
        int ok = 1;
        print_text(strings[Strings::How_much_8F72]);   /* "How much? " */
        unsigned n = (unsigned)read_number(2);
        if (n != 0)
            for (int i = 0; i < 8; i++)
                if ((reagent_mask & (0x80u >> i)) != 0 && party.reagents[i] < n) {
                    print_text(strings[Strings::Insufficient_reagents_8F7E]);   /* "Insufficient reagents!\n\n" */
                    ok = 0;
                    break;
                }
        if (ok || game_quit()) return n;
    }
}

/* The reagents are used up whatever the spell; if they aren't exactly the spell's recipe
 * (spell_recipes) the mixture goes off like a trap in the first able member's face. */
void cmd_mix_reagents(void)
{
    int total = 0;
    for (int i = 0; i < 8; i++) total += party.reagents[i];
    if (total == 0) {
        print_text(strings[Strings::No_reagents_owned_8F98]);   /* "No reagents owned!\n" */
        return;
    }
    print_text(strings[Strings::For_what_spell_8FAC]);   /* "For what spell?\n:" */
    int spell = select_spell();
    if (spell == -1) {
        print_text(strings[Strings::None_8FBE]);   /* "\nNone!\n" */
    } else {
        put_char('\n');
        put_char(0x1B);
        put_char(',');
        put_char(0x1A);
        put_char(',');
        put_char(0x18);
        put_char(',');
        put_char(0x19);
        print_text(strings[Strings::to_move_RETURN_selects_8FC6]);   /* " to move,\nRETURN selects.\nType M to mix:" */
        unsigned mask = mix_select_reagents();
        int amount;
        if ((int16_t)mask >= 0 && (amount = (int16_t)mix_ask_amount(mask)) >= 1) {
            if (mask == 0) {
                print_text(strings[Strings::Nothing_to_mix_9004]);   /* "\nNothing to mix!\n" */
            } else {
                print_text(strings[Strings::Mixing_8FF0]);   /* "Mixing...\n" */
                if (map.on_tile_map()) wait_turns(10);
                else delay_ticks(10);
                for (int i = 0; i < 8; i++)
                    if ((0x80u >> i) & mask) party.reagents[i] = (uint8_t)(party.reagents[i] - amount);
                if (spell < 0 || spells.recipe((unsigned)spell) != mask) {
                    put_char('\n');
                    spring_trap(party.first_able());
                } else {
                    print_text(strings[Strings::Done_8FFC]);   /* "\nDone!\n" */
                    party.mixed_spells[spell] = (uint8_t)(party.mixed_spells[spell] + amount);
                    if (party.mixed_spells[spell] > 99) party.mixed_spells[spell] = 99;
                }
            }
        }
    }
    draw_panel_top_bar();
    draw_panel_bottom_bar();
    draw_status_bar();
}

/* With a grapple, on foot, the party can climb onto low mountains (0x0C); each member whose
 * dexterity fails a 1-30 roll falls for 1-5. */
void cmd_klimb_grapple(void)
{
    Direction dir = {0, 0};
    if (party.grapples == 0) {
        print_text(strings[Strings::With_what_9016]);   /* "With what?\n" */
        return;
    }
    if (party.transport != 0x1C) {
        print_text(strings[Strings::On_foot_9022]);   /* "On foot!\n" */
        return;
    }
    if (ask_direction(&dir) == 0) return;
    int dx = (int16_t)dir.dx, dy = (int16_t)dir.dy;
    uint8_t t = *map.get_map_tile_ptr((uint16_t)(map.y + dir.dy), (uint16_t)(map.x + dir.dx));
    if (t == 0x0D) {
        print_text(strings[Strings::Impassable_902C]);   /* "Impassable!\n" */
    } else if (t == 0x0C) {
        for (unsigned i = 0; i < party.size(); i++)
            if (!party[i].dead() && party[i].dex < (unsigned)random_between(0x1E, 1)) {
                print_text(strings[Strings::Fell_904A]);   /* "Fell!\n" */
                damage_member(random_between(5, 1), (int)i);
            }
        map.outdoor_step(dy, dx);
    } else {
        print_text(strings[Strings::Not_climbable_903A]);   /* "Not climbable!\n" */
    }
}

/* A spell is typed as the first letters of its words (IN LOR = "IL"); each letter prints its
 * syllable (table ds:1B7A by letter). J and O have none. Up to four letters; Backspace takes one
 * back, Return or Space ends, Esc ends with nothing. The letters are sorted, repeats dropped, and
 * matched against the 48 entries of spell_menu_layout, which are stored sorted. */
int select_spell(void)
{
    uint8_t letters[4], cols[4];
    int n = 0, done = 0;
    while (!done) {
        unsigned key = to_upper((uint8_t)get_command_key()) & 0xFF;
        if (game_quit()) return -1;
        if (key == 'J' || key == 'O') continue;
        if (key >= 'A' && key <= 'Z' && n <= 3) {
            const char *syllable = spells.syllable(key);
            letters[n] = (uint8_t)key;
            if (get_cursor_col() + (int)strlen(syllable) > 0x0D) put_char('\n');
            cols[n] = get_cursor_col();
            print_text(syllable);
            put_char(' ');
            n++;
        } else if (key == 8) {
            if (n != 0) {
                int row = get_cursor_col() == 0 ? get_cursor_row() - 1 : get_cursor_row();
                set_cursor(row, cols[n - 1]);
                for (int len = (int)strlen(spells.syllable(letters[n - 1])); len > 0; len--) put_char(' ');
                set_cursor(get_cursor_row(), cols[n - 1]);
                n--;
            }
        } else if (key == 0x0D || key == ' ') {
            done = 1;
        } else if (key == 0x1B) {
            n = 0;
            done = 1;
        }
    }
    for (int sorted = 0; !sorted; ) {
        sorted = 1;
        for (int i = 0; i + 1 < n; i++)
            if (letters[i + 1] < letters[i]) {
                uint8_t t = letters[i];
                letters[i] = letters[i + 1];
                letters[i + 1] = t;
                sorted = 0;
            }
    }
    int spell = -1;
    for (int s = 0; s < 0x30 && spell < 0; s++) {
        const char *want = spells.letters((unsigned)s);
        int match = 1, k = 0;
        for (int i = 0; i < n; i++, k++) {
            if ((uint8_t)want[k] != letters[i]) {
                match = 0;
                break;
            }
            while (i < n - 1 && letters[i + 1] == letters[i]) i++;
        }
        if (match && want[k] != '\0') match = 0;
        if (match) spell = s;
    }
    if (n == 0) return -1;
    put_char('\n');
    return spell < 0 ? -2 : spell;
}
