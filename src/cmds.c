/* cmds.c - command handlers from CMDS.OVL. See cmds.h. */
#include <string.h>
#include "cmds.h"
#include "combat.h"
#include "comsubs.h"
#include "commands.h"
#include "data.h"
#include "dungeon.h"
#include "endings.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "map.h"
#include "misc.h"
#include "npc.h"
#include "objects.h"
#include "outdoor.h"
#include "picture.h"
#include "status.h"
#include "text.h"
#include "town.h"
#include "view.h"
#include "world.h"

#define S(off) ds_str(off)
#define MASK(row, col) dg.view_mask[(row) * 0x20 + (col)]

/* The camp: everyone but the guard sleeps (a bard on guard plays first), then time passes 5
 * minutes a step until the waking hour. After the first hour each step has 1 chance in 64 of an
 * ambush by one of eight monster types (ds:1734); the party is woken only if a guard was posted.
 * Returns 1 if a fight follows. Six hours or more undisturbed, not within 14 hours of the last
 * rest, heal 1-63 hit points and restore magic points for everyone but the guard and the
 * poisoned; then, 1 time in 4 (not in a dungeon), the apparition comes. */
int camp_rest(int hours, unsigned guard, unsigned flags)
{
    uint8_t last_hour = dg.hour, active = dg.active_player;
    int bard = -1, monster = -1, poisoned[8] = {0};
    dg.active_spell_turns = 0;
    dg.active_spell = 0;
    dg.active_player = 0xFF;
    if (flags & 2) place_combatants(0, DHERE);
    else clear_combat_tables();
    dg.combat_turn = 0xFF;
    int wake = hours + dg.hour;
    if (wake > 0x17) wake -= 0x18;
    int g = (int16_t)guard;
    if (g > 5 || g < -1) g = -1;
    for (unsigned m = 0; m < dg.party_size; m++) {
        unsigned slot;
        for (slot = 0; slot < 0x20; slot++)
            if (dg.combatants[slot].kind == m) break;
        char st = dg.party[m].status;
        if (st == 'P') poisoned[m & 7]++;
        if (st != 'G' && st != 'P' && st != 'S') continue;
        if ((int)m != g && slot < 0x20) sleep_unless_poisoned(slot);
        if (dg.party[m].cls == 'B' && (int)m == g && dg.sound_enabled != 0) bard = (int)slot;
    }
    world_tick();
    if (bard != -1) {   /* tile 0x5F: playing */
        MapObject *o = &dg.objects[dg.combatants[bard & 0x1F].object & 0x1F];
        uint8_t tile = o->tile, tile2 = o->tile2;
        o->tile = o->tile2 = 0x5F;
        dg.tune_pos = 1;
        wait_turns(0x34);
        o->tile = tile;
        o->tile2 = tile2;
        if (dg.combatants[bard & 0x1F].kind != g) sleep_unless_poisoned((unsigned)bard);
        world_tick();
    }
    print_text(S(0x41D4));   /* "Zzzzzz...\n\n" */
    Combatant *gc = NULL;
    uint8_t guard_tile = 0;
    if (g != -1) {
        for (int i = 0; i < 0x20; i++)
            if (dg.combatants[i].kind == g) {
                gc = &dg.combatants[i];
                break;
            }
        if (gc) guard_tile = dg.objects[gc->object & 0x1F].tile;
    }
    while (wake != dg.hour && !game_quit()) {
        if (dg.hour > 0x17) dg.hour -= 0x18;
        world_tick();
        regen_ring_tick();
        draw_status_bar();
        if (last_hour != dg.hour && random_between(0x3F, 0) == 0) {
            set_random_seed((uint16_t)time_seed());
            monster = ds_u8((uint16_t)(0x1734 + random_between(7, 0)));
            print_text(S(0x41E0));   /* "Ambushed!\n\n" */
            if (g >= 0)
                for (int slot = 0; slot < 6; slot++) {
                    unsigned m = dg.combatants[slot].kind;
                    if (m >= dg.party_size) continue;
                    wake_combatant(slot);
                    dg.party[m].status = poisoned[m & 7] ? 'P' : 'G';
                }
            draw_status_bar();
            if (flags & 2) place_combatants(1, DHERE);
            else place_monster_group((unsigned)monster, flags);
            break;
        }
        last_hour = dg.hour;
        pass_time(5);
        delay_ticks(1);
        if (dg.prev_location < 0x21) {
            dg.location = dg.prev_location;
            draw_sky_bar();
            dg.location = 0xFF;
        }
        if (gc && random_between(3, 0) == 2) {   /* the guard paces */
            dg.dir_x = gc->x;
            dg.dir_y = gc->y;
            switch (random_between(3, 0)) {
            case 0: dg.dir_y--; break;
            case 1: dg.dir_y++; break;
            case 2: dg.dir_x++; break;
            case 3: dg.dir_x--; break;
            }
            if (in_combat_bounds((int16_t)dg.dir_y, (int16_t)dg.dir_x) && combat_tile_free(dg.dir_y, dg.dir_x, guard_tile)) {
                MapObject *o = &dg.objects[gc->object & 0x1F];
                gc->x = o->x = (uint8_t)dg.dir_x;
                gc->y = o->y = (uint8_t)dg.dir_y;
                world_tick();
            }
        }
    }
    if (monster >= 0) return 1;
    if (dg.rest_cooldown != 0 || hours < 6) {
        print_text(S(0x41FB));   /* "No effect...\n" */
    } else {
        print_text(S(0x41EC));   /* "Party rested!\n" */
        for (unsigned m = 0; m < dg.party_size; m++) {
            PartyMember *pm = &dg.party[m];
            if (poisoned[m & 7] != 0 || pm->status == 'D' || (int)m == g) continue;
            pm->hp = (uint16_t)(pm->hp + random_between(0x3F, 1));
            if ((int16_t)pm->max_hp < (int16_t)pm->hp) pm->hp = pm->max_hp;
            if (pm->cls == 'A' || pm->cls == 'M') pm->mp = pm->intel;
            else if (pm->cls == 'B') pm->mp = pm->intel >> 1;
        }
        if (!(flags & 0x82) && random_between(99, 0) < 0x19) {
            ds_set_u8(0x588D, dg.month);
            apparition_level_up();
        }
        dg.rest_cooldown = 0x0E;
    }
    for (unsigned m = 0; m < dg.party_size; m++)
        if (dg.party[m].status == 'S') dg.party[m].status = 'G';
    dg.active_player = active;
    draw_status_bar();
    return 0;
}

/* Esc in combat: once the battle is won (and not in a dungeon room) everything is swept off the
 * map at once, which ends it. Returns 1 if refused. */
int combat_escape(void)
{
    print_text(S(0x4574));   /* "Escape" */
    int members = 0;
    for (int i = 0; i < 0x20; i++)
        if ((dg.combatants[i].flags & 0xA0) == 0x80) {
            members = 1;
            break;
        }
    if (members) {
        if (dg.combat_flags & 0x80) {
            print_text(S(0x457B));   /* "-Not here!\n" */
            dg.status_dirty = 1;
            return 1;
        }
        if (dg.combat_victory == 0) {
            print_text(S(0x4587));   /* "-Not yet!\n" */
            dg.status_dirty = 1;
            return 1;
        }
    }
    put_char('!');
    for (int i = 0; i < 0x20; i++)
        if (dg.combatants[i].flags != 0) {
            combat_remove(-1 - i);
            world_tick();
        }
    for (int i = 0; i < 0x20; i++)
        if (dg.objects[i].tile != 0) {
            combat_remove(i + 1);
            world_tick();
        }
    sound_slide(0x28, 1, 2000, 0x4B0);
    dg.status_dirty = 1;
    return 0;
}

/* The town carries on for 16 turns first (an attacker cancels the rest). Then everyone sleeps,
 * 10 minutes at a time, until the hour comes round or someone turns up at the bed. The waking
 * hour past midnight is hour + n - 23, one more than asked: kept as it is. */
void cmd_hole_up_bed(void)
{
    print_text(S(0x4209));   /* "For how many hours? " */
    uint8_t key;
    do {
        key = (uint8_t)get_command_key();
        if (game_quit()) return;
    } while (key != ' ' && (key < '0' || key > '9'));
    if (key == ' ' || key == '0') return;
    put_char(key);
    put_char('\n');
    unsigned wake = (unsigned)(dg.hour + key - '0');
    if (wake > 0x17) wake -= 0x17;
    uint8_t active = dg.active_player;
    for (int i = 0; i < 0x10; i++) {
        update_npcs(dg.hour);
        world_tick();
        active = dg.active_player;
        if (dg.npc_event == 'a') return;
    }
    for (unsigned i = 0; i < dg.party_size; i++)
        if (dg.party[i].status == 'G') dg.party[i].status = 'S';
    draw_status_bar();
    print_text(S(0x421E));   /* "Zzzzzzz...\n" */
    set_draw_color(0);
    fill_rect(0xB7, 0xB7, 8, 8);
    uint8_t prev = dg.hour;
    while (wake != dg.hour && !game_quit()) {
        pass_time(10);
        if (prev != dg.hour && (dg.hour == 0x14 || dg.hour == 5)) update_lamps();
        prev = dg.hour;
        draw_sky_bar();
        per_turn_status();
        draw_status_bar();
        place_npcs();
        if (find_object_at(dg.z_level, dg.party_y, dg.party_x) != 0) {
            wake = 0xFFFF;
            break;
        }
        delay_ticks(1);
    }
    if (wake == 0xFFFF) print_text(S(0x422A));   /* "Thrown out of bed!\n" */
    for (unsigned i = 0; i < dg.party_size; i++)
        if (dg.party[i].status == 'S') dg.party[i].status = 'G';
    dg.party_x++;
    dg.redraw_flags = 1;
    dg.objects[0].x++;
    dg.active_player = active;
    draw_status_bar();
    world_tick();
}

int board_check_on_foot(void)
{
    if (dg.transport == 0x1C || dg.transport == 0x1D) return 1;
    print_text(S(0x423E));   /* "\nOn foot\n" */
    return 0;
}

/* A ship can be boarded on foot, from a carpet or from a skiff. */
int board_check_ship_ok(void)
{
    uint8_t t = dg.transport;
    if (t < 0x14 || (t > 0x15 && (t < 0x1C || (t > 0x1D && (t < 0x28 || t > 0x2B))))) {
        print_text(S(0x4248));   /* "\nOn foot\n" */
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
    if (dg.view_mask[x + y * 0x20] == 0) {
        uint8_t o = dg.view_tiles[x + y * 0x10], k = o & 0xFC;
        return o == 0x1B || k == 0x1C || k == 0x24 || k == 0x10 || k == 0x28;
    }
    return tile_passable(dg.view_mask[x + y * 0x20], 0x1C);
}

/* The object the party stands on becomes its transport. A horse that belongs to someone with
 * something to say can't be taken. Boarding a ship stows the carpet or skiff the party came on. */
int cmd_board(void)
{
    if (!(dg.location < 0x21 || dg.location > 0x28)) {
        print_text(S(0x4252));   /* "\nNot here!\n" */
        return 1;
    }
    uint8_t tile = (uint8_t)find_object_at(dg.z_level, dg.party_y, dg.party_x);
    int index = (int)dg.dir_x;
    if ((tile & 0xFE) == 0x10) {
        int npc;
        if (dg.location != 0 && (npc = npc_for_object(index)) != -1 && dg.npc_state[npc].dialogue != 0) {
            print_text(S(0x425E));   /* "\"Nay!\"\n" */
            return 1;
        }
        if (!board_check_on_foot()) return 1;
        print_text(S(0x4266));   /* "horse\n" */
        dg.transport = (uint8_t)(tile + 2);
    } else if (tile == 0x1B) {
        if (!board_check_on_foot()) return 1;
        print_text(S(0x426D));   /* "carpet\n" */
        dg.transport = 0x14;
    } else if ((tile & 0xFC) == 0x28) {
        if (!board_check_on_foot()) return 1;
        print_text(S(0x4275));   /* "skiff\n" */
        dg.transport = tile;
    } else if ((tile & 0xFC) == 0x24) {
        if (!board_check_ship_ok()) return 1;
        print_text(S(0x427C));   /* "Ship\n" */
        uint8_t hull = dg.objects[index].hull;
        if (hull < 10) print_text(S(0x4282));   /* "\nDANGER: SHIP BADLY DAMAGED!\n" */
        unsigned skiffs = dg.objects[index].skiffs;
        if ((dg.transport & 0xFE) == 0x14) dg.magic_carpets++;
        if ((dg.transport & 0xFC) == 0x28) skiffs++;
        dg.objects[0].hull = hull;
        if (skiffs == 0) print_text(S(0x42A0));   /* "\nWARNING: NO SKIFFS ON BOARD!\n" */
        dg.objects[0].skiffs = (uint8_t)skiffs;
        dg.status_dirty = 1;
        dg.transport = tile;
    } else {
        print_text(S(0x42BF));   /* "What?\n" */
        return 0;
    }
    set_object_entry(index, 0, 0, 0, 0, 0, 0);
    dg.redraw_flags |= 2;
    return 1;
}

/* A ship fires only abeam. The shot reaches 3 squares; a hit takes 1-20 off the target's `hull`
 * and removes it when that goes below zero (monsters have 0, pirate ships 100). */
void fire_ship_cannons(void)
{
    if (dg.transport < 0x20 || dg.transport > 0x27) {
        print_text(S(0x42C6));   /* "What?\n" */
        return;
    }
    if (ask_direction() == 0) return;
    int dx = (int16_t)dg.dir_x, dy = (int16_t)dg.dir_y;
    if (!((dx != 0 || (dg.transport & 1) != 0) && (dx == 0 || (dg.transport & 1) == 0))) {
        print_text(S(0x42CD));   /* "Fire broadsides only!\n" */
        return;
    }
    unsigned x = dg.party_x, y = dg.party_y;
    sound_slide(300, 5, 200, 1000);
    for (int n = 0;; ) {
        x += (unsigned)dx;
        y += (unsigned)dy;
        int t = find_object_at(dg.z_level, (uint8_t)y, (uint8_t)x);
        if (is_active_monster_tile(t) && (t & 0xFC) != 0xEC) break;
        if (++n > 2) {
            animate_missile(1, dy * 3 + 5, dx * 3 + 5, 5, 5);
            return;
        }
    }
    int index = (int)dg.dir_x;
    MapObject *o = &dg.objects[index];
    if (animate_missile(1, o->y - dg.party_y + 5, o->x - dg.party_x + 5, 5, 5) == 0) return;
    world_tick();
    flash_square((int)y, (int)x);
    o->hull = (uint8_t)(o->hull - random_between(0x14, 1));
    if (o->hull < 0x80) return;
    set_object_entry(index, 0, 0, 0, 0, 0, 0);
    dg.redraw_flags |= 2;
}

/* In a town: a cannon (tiles 0xB4-0xB7, facing in the low bits) next to the party fires 4
 * squares along its facing. It destroys doors and kills whoever it hits (karma -5); the town is
 * alerted in any case. */
void cmd_fire(void)
{
    if (dg.location > 0x20 && dg.location < 0x29) {
        print_text(S(0x42E4));   /* "What?\n" */
        return;
    }
    if (dg.location == 0) {
        fire_ship_cannons();
        return;
    }
    restore_open_door(dg.open_door_y, dg.open_door_x, dg.open_door_tile);
    int cdx, cdy;
    uint8_t cannon;
    if ((MASK(4, 5) & 0xFC) == 0xB4) { cdx = 0; cdy = -1; cannon = MASK(4, 5); }
    else if ((MASK(5, 6) & 0xFC) == 0xB4) { cdx = 1; cdy = 0; cannon = MASK(5, 6); }
    else if ((MASK(6, 5) & 0xFC) == 0xB4) { cdx = 0; cdy = 1; cannon = MASK(6, 5); }
    else if ((MASK(5, 4) & 0xFC) == 0xB4) { cdx = -1; cdy = 0; cannon = MASK(5, 4); }
    else {
        print_text(S(0x42EB));   /* "What?\n" */
        return;
    }
    unsigned x = (unsigned)(cdx + dg.party_x), y = (unsigned)(cdy + dg.party_y);
    int from_x = cdx + 5, from_y = cdy + 5;
    static const int8_t fdx[4] = {0, 1, 0, -1}, fdy[4] = {-1, 0, 1, 0};
    int dx = fdx[cannon & 3], dy = fdy[cannon & 3];
    print_text(S(0x42F2));   /* "BOOOM!\n" */
    sound_slide(300, 5, 200, 1000);
    alert_town();
    int door = 0, hit = 0, target = 0, vx = from_x, vy = from_y;
    for (int range = 5; !door && !hit && --range > 0; ) {
        x += (unsigned)dx;
        y += (unsigned)dy;
        vx += dx;
        vy += dy;
        uint8_t t = object_tile_at(dg.z_level, y, x);
        if (t == 0) {
            uint8_t m = *get_map_tile_ptr(y, x);
            if (m > 0x96 && (m < 0x9A || (m > 0xB7 && m < 0xBC))) door = 1;
        } else if ((t > 0x1B && (t & 0xF8) != 0x78) || t == 0x10 || t == 0x11) {   /* (the original also tests (t & 0xFC) != 0x2F, always true) */
            hit = 1;
            target = (int)dg.dir_x;
        }
    }
    animate_missile(1, vy, vx, from_y, from_x);
    if (door || hit) flash_square((int)y, (int)x);
    if (door) {
        print_text(S(0x42FA));   /* "Door destroyed!\n" */
        *get_map_tile_ptr(y, x) = 0x44;
        dg.redraw_flags = 1;
        dg.open_door_tile = 0;
    }
    if (hit && target != 0) {
        set_object_entry(target, 0, 0, 0, 0, 0, 0);
        dg.redraw_flags |= 2;
        sub_byte_floor0(5, &dg.karma);
        int npc = npc_for_object(target);
        if (npc == -1) return;
        mark_npc_dead(npc);
        remove_npc(npc);
    }
    if (hit && target == 0) damage_party();   /* object 0 is the party itself */
}

void cmd_ignite_torch(void)
{
    if (dg.torches == 0) {
        print_text(S(0x430B));   /* "None owned!\n" */
        return;
    }
    dg.torches--;
    if (dg.location < 0x21 || dg.location > 0x28) dg.torch_light = 0xF0;
    else add_byte_capped(0xFF, (uint8_t)(random_between(0x0F, 0) + 0x70), &dg.torch_light);
}

void cmd_new_order(void)
{
    print_text(S(0x4318));   /* "\n\nSwap " */
    int a = ask_member_number();
    if (a == -1) {
        print_text(S(0x4320));   /* "nobody!\n" */
        return;
    }
    print_text(dg.party[a].name);
    if (a == 0) {
        print_text(S(0x4329));   /* "\n\n" */
        print_text(dg.party[0].name);
        print_text(S(0x432C));   /* " must lead!\n" */
        return;
    }
    print_text(S(0x4339));   /* "\nwith " */
    int b = ask_member_number();
    if (b == -1) {
        print_text(S(0x4340));   /* "nobody!\n" */
        return;
    }
    print_text(dg.party[b].name);
    if (b == 0) {
        print_text(S(0x4349));   /* "\n\n" */
        print_text(dg.party[0].name);
        print_text(S(0x434C));   /* " must lead!\n" */
        return;
    }
    print_text(S(0x4359));   /* "!\n" */
    PartyMember tmp = dg.party[a];
    dg.party[a] = dg.party[b];
    dg.party[b] = tmp;
    dg.status_dirty = 1;
}

/* The transport is left on the square as an object. From a ship with furled sails and no land
 * alongside the party takes a skiff (or, with none, a carpet). The first test can never be
 * true (location < 0x20 && > 0x29), so X works everywhere. */
void cmd_exit(void)
{
    if (dg.location < 0x20 && dg.location > 0x29) {
        print_text(S(0x435C));   /* "\nNot here!\n" */
        return;
    }
    uint8_t tile = *get_map_tile_ptr(dg.party_y, dg.party_x);
    uint8_t kind = dg.transport & 0xFC, left, skiffs = 0;   /* skiffs is uninitialised in the original */
    if (kind == 0x10) {
        print_text(S(0x43A4));   /* "horse!\n" */
        left = (uint8_t)(dg.transport - 2);
        dg.transport = 0x1C;
    } else if (kind == 0x14) {
        if (!is_land_adjacent() && !tile_passable(tile, 0x1C)) {
            print_text(S(is_land_adjacent() == 0 ? 0x4386 : 0x4398));   /* "\nNo land nearby!\n" / "\nNot here!\n" */
            return;
        }
        print_text(S(0x437D));   /* "carpet!\n" */
        left = 0x1B;
        dg.transport = 0x1C;
    } else if (kind == 0x1C) {
        print_text(S(0x4368));   /* "what?\n" */
        return;
    } else if (kind == 0x20) {
        print_text(S(0x436F));   /* "\nUnder sail!\n" */
        return;
    } else if (kind == 0x24) {
        print_text(S(0x43D2));   /* "ship!\n" */
        int land = is_land_adjacent();
        left = dg.transport;
        skiffs = dg.objects[0].skiffs;
        if (land) {
            dg.transport = 0x1C;
        } else if (skiffs != 0) {
            dg.transport += 4;
            skiffs--;
        } else if (dg.magic_carpets != 0) {
            dg.magic_carpets--;
            dg.transport = 0x14;
        } else {
            print_text(S(0x43D9));   /* "\nNo skiffs on board!\n" */
            return;
        }
    } else if (kind == 0x28) {
        if (!is_land_adjacent()) {
            print_text(S(0x43AC));   /* "\nNo land nearby!\n" */
            return;
        }
        if ((tile & 0xFE) == 0x6A) {
            print_text(S(0x43BE));   /* "\nNot here!\n" (under a bridge) */
            return;
        }
        print_text(S(0x43CA));   /* "skiff!\n" */
        left = dg.transport;
        dg.transport = 0x1C;
    } else {
        return;   /* no other transport exists; the original would leave a garbage object */
    }
    int slot = alloc_object_slot();
    set_object_entry(slot, dg.objects[0].hull, dg.z_level, dg.party_y, dg.party_x, left, left);
    dg.objects[slot].skiffs = skiffs;
    draw_status_bar();
}

/* At the three flames (locations 0x1E-0x20), yelling a Shadowlord's name (FAULINEI, ASTAROTH,
 * NOSFENTOR) while he is still about brings him two squares north of the party, as an NPC that
 * stands still. */
void yell_summon_shadowlord(const char *word)
{
    if (dg.location != 0x1E && dg.location != 0x1F && dg.location != 0x20) {
        print_text(S(0x443C));   /* "\nNo effect!\n" */
        return;
    }
    int who;
    for (who = 0; who < 3; who++)
        if (str_find_nocase(word, S(ds_u16((uint16_t)(0x444A + who * 2)))) > -1) break;
    if (who == 3 || dg.party_y <= 1 || dg.shadowlord_locations[who] == 0xFF) {
        print_text(S(0x440B));   /* "\nNo effect!\n" */
        return;
    }
    for (int i = 0; i < 0x20; i++)
        if (dg.objects[i].tile == 0xFC) {
            print_text(S(0x4418));   /* "\nNo effect!\n" */
            return;
        }
    dg.summoned_shadowlord = (uint8_t)who;
    int obj = alloc_object_slot();
    set_object_entry(obj, 0, dg.z_level, (uint8_t)(dg.party_y - 2), dg.party_x, 0xFC, 0xFC);
    int npc = 0x1F;
    for (int i = 0x1F; i >= 0; i--)
        if (dg.npc_type[i] == 0) {
            npc = i;
            break;
        }
    NpcState *st = &dg.npc_state[npc];
    NpcSchedule *sc = &dg.npc_schedules[npc];
    st->state = 1;
    st->object = (uint16_t)obj;
    st->x = dg.party_x;
    st->y = (uint16_t)(dg.party_y - 2);
    st->z = dg.z_level;
    memset(sc->times, 0, 4);
    for (int i = 0; i < 3; i++) {
        sc->activity[i] = 6;
        sc->x[i] = dg.party_x;
        sc->y[i] = (uint8_t)(dg.party_y - 2);
        sc->z[i] = dg.z_level;
    }
    dg.npc_type[npc] = 0xFC;
    print_text(S(0x4425));   /* "\nA shadowlord appears\n" */
    sound_sweep(2, 2000, 30000, 1, 0x28A0);
    dg.objects[obj].tile = dg.objects[obj].tile2 = 0x16;
    dissolve_tile(3, 5, 0x1FC);
    dg.objects[obj].tile = dg.objects[obj].tile2 = 0xFC;
}

/* A word of power yelled beside a ruined shrine (tile 0x1A): the virtue and three times the
 * mantra, all correct for this shrine, restore it. */
void meditate_restore_shrine(unsigned y, unsigned x, int shrine)
{
    char buf[32];
    print_text(S(0x4450));   /* "\nUpon what virtue\ndost thou\nmeditate?\n\n:" */
    read_line(0x0F, buf);
    int ok = str_find_nocase(buf, S(dg.virtue_names[shrine])) != -1;
    for (int i = 3; i != 0; i--) {
        print_text(S(0x4479));   /* "\nMantra:" */
        read_line(0x0F, buf);
        if (str_find_nocase(buf, S(dg.mantras[shrine])) == -1) ok = 0;
    }
    if (ok && dg.shrine_x[shrine] == x && dg.shrine_y[shrine] == y) {
        dg.shrine_state[shrine] &= 0x7F;
        print_text(S(0x4482));   /* "\n\nThe Shrine is\nrestored!\n" */
        screen_shake();
        *get_map_tile_ptr(y, x) = 0x19;
        dg.redraw_flags |= 2;
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
        if (str_find_nocase(word, S(dg.words_of_power[n])) < 0) continue;
        print_text(S(0x44D7));   /* "\nA word of power is uttered\n" */
        screen_shake();
        static const int8_t dxs[4] = {-1, 0, 1, 0}, dys[4] = {0, 1, 0, -1};
        int d;
        uint8_t t = 0;
        for (d = 0; d < 4; d++) {
            t = MASK(5 + dys[d], 5 + dxs[d]);
            if (t == dg.seal_tiles[n] || t == 0xDF || t == 0x1A) break;
        }
        if (d == 4) break;
        unsigned x = (unsigned)(dg.party_x + dxs[d]), y = (unsigned)(dg.party_y + dys[d]);
        if (t == 0x1A) {
            meditate_restore_shrine(y, x, n);
            return;
        }
        if (x == dg.town_exit_x[0x21 + n] && y == dg.town_exit_y[0x21 + n]) {
            dg.dungeon_seals[n] ^= 0x80;
            *get_map_tile_ptr(y, x) ^= dg.seal_tiles[n] ^ 0xDF;
            dg.redraw_flags |= 2;
        }
        return;
    }
    print_text(S(0x44F4));   /* "\nNo effect!\n" */
}

/* On a ship Yell furls or hoists the sails. The original returns a pointer, never zero. */
int cmd_yell(void)
{
    char buf[40];
    if ((dg.transport & 0xF8) == 0x20 && dg.location < 0x80) {
        if ((dg.transport & 0xFC) == 0x20) {
            print_text(S(0x451A));   /* "FURL!\n" */
            dg.transport += 4;
        } else {
            print_text(S(0x4521));   /* "HOIST!\n" */
            dg.transport -= 4;
        }
        return 1;
    }
    print_text(S(0x4529));   /* "what?\n:" */
    read_line(0x1E, buf);
    if (buf[0] == '\0') {
        print_text(S(0x4531));   /* "Nothing\n" */
        return 1;
    }
    put_char('\n');
    if (dg.location == 0) yell_word_of_power(buf);
    else if (dg.location > 0x20) print_text(S(0x453A));   /* "\nNo effect!\n" */
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
    print_text(S(0x4547));   /* "Pushed!\n" */
    *get_map_tile_ptr(to_y, to_x) = tile;
    *get_map_tile_ptr(from_y, from_x) = dest_tile;
    unsigned k = tile & 0xFC;
    if (k == 0x90 || k == 0xB4) *get_map_tile_ptr(to_y, to_x) = (uint8_t)rotate_pushed_tile(0, k, dy, dx);
}

void pull_object(int dy, int dx, unsigned to_y, unsigned to_x, unsigned from_y, unsigned from_x, uint8_t dest_tile, uint8_t tile)
{
    print_text(S(0x4550));   /* "Pulled!\n" */
    *get_map_tile_ptr(from_y, from_x) = dest_tile;
    *get_map_tile_ptr(to_y, to_x) = tile;
    unsigned k = dest_tile & 0xFC;
    if (k == 0x90 || k == 0xB4) *get_map_tile_ptr(from_y, from_x) = (uint8_t)rotate_pushed_tile(1, k, dy, dx);
}

/* The thing moves a square on if the floor beyond is plain (0x44; for a cannon 0x45). If not,
 * and the pusher stands on such floor, the two change places ("Pulled!"). Either way the pusher
 * steps into the thing's square. In combat the pusher is the fighter whose turn it is. */
void cmd_push(void)
{
    restore_open_door(dg.open_door_y, dg.open_door_x, dg.open_door_tile);
    if (ask_direction() == 0) return;
    int dx = (int16_t)dg.dir_x, dy = (int16_t)dg.dir_y;
    uint8_t party_x = dg.party_x, party_y = dg.party_y;
    int combat = dg.location > 0x7F;
    if (combat) {
        dg.party_x = dg.combatants[dg.combat_turn & 0x1F].x;
        dg.party_y = dg.combatants[dg.combat_turn & 0x1F].y;
    }
    unsigned x = (uint16_t)(dg.dir_x + dg.party_x), y = (uint16_t)(dg.dir_y + dg.party_y);
    if (combat && combat_room_trigger(y, x) != 0) return;   /* (the party position stays the fighter's, as in the original) */
    uint8_t tile = *get_map_tile_ptr(y, x);
    if (find_object_at(dg.z_level, (uint8_t)y, (uint8_t)x) != 0 || !is_pushable_tile(tile)) {
        print_text(S(0x4559));   /* "Won't budge!\n" */
        return;
    }
    uint8_t floor = (tile & 0xFC) == 0xB4 ? 0x45 : 0x44;
    unsigned x2 = (uint16_t)(x + (unsigned)dx), y2 = (uint16_t)(y + (unsigned)dy);
    uint8_t beyond = *get_map_tile_ptr(y2, x2);
    uint8_t here = *get_map_tile_ptr(dg.party_y, dg.party_x);
    if (find_object_at(dg.z_level, (uint8_t)y2, (uint8_t)x2) == 0 && beyond == floor) {
        push_object(dy, dx, y2, x2, y, x, beyond, tile);
    } else if (here == floor) {
        pull_object(dy, dx, y, x, dg.party_y, dg.party_x, tile, here);
    } else {
        print_text(S(0x4567));   /* "Won't budge\n" */
        return;
    }
    dg.party_x = (uint8_t)(dg.party_x + dx);
    dg.party_y = (uint8_t)(dg.party_y + dy);
    dg.redraw_flags = 1;
    if (combat) {
        Combatant *c = &dg.combatants[dg.combat_turn & 0x1F];
        MapObject *o = &dg.objects[c->kind & 0x1F];   /* the original indexes the objects by `kind`, not `object` */
        c->x = (uint8_t)(c->x + dx);
        c->y = (uint8_t)(c->y + dy);
        o->x = (uint8_t)(o->x + dx);
        o->y = (uint8_t)(o->y + dy);
        dg.party_x = party_x;
        dg.party_y = party_y;
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
        if (dg.reagents[i] != 0) list[n++] = (uint8_t)i;
    select_text_window(1);
    define_text_window(9, 0x26, 1, 0x18, 1);
    put_char(0xFF);
    define_text_window(9, 0x27, 1, 0x18, 1);
    clear_side_panel();
    draw_panel_title(S(0x8F64));   /* "Reagents:" */
    for (int i = 0; i != n; i++) {
        put_char('\n');
        put_char(' ');
        print_number('0', 2, dg.reagents[list[i]]);
        put_char(' ');
        print_text(S(ds_u16((uint16_t)(0x19D2 + list[i] * 2))));
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
            print_text(S(0x8F6E));   /* "\n\n" */
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
        print_text(S(0x8F72));   /* "How much? " */
        unsigned n = (unsigned)read_number(2);
        if (n != 0)
            for (int i = 0; i < 8; i++)
                if ((reagent_mask & (0x80u >> i)) != 0 && dg.reagents[i] < n) {
                    print_text(S(0x8F7E));   /* "Insufficient reagents!\n\n" */
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
    for (int i = 0; i < 8; i++) total += dg.reagents[i];
    if (total == 0) {
        print_text(S(0x8F98));   /* "No reagents owned!\n" */
        return;
    }
    print_text(S(0x8FAC));   /* "For what spell?\n:" */
    int spell = select_spell();
    if (spell == -1) {
        print_text(S(0x8FBE));   /* "\nNone!\n" */
    } else {
        put_char('\n');
        put_char(0x1B);
        put_char(',');
        put_char(0x1A);
        put_char(',');
        put_char(0x18);
        put_char(',');
        put_char(0x19);
        print_text(S(0x8FC6));   /* " to move,\nRETURN selects.\nType M to mix:" */
        unsigned mask = mix_select_reagents();
        int amount;
        if ((int16_t)mask >= 0 && (amount = (int16_t)mix_ask_amount(mask)) >= 1) {
            if (mask == 0) {
                print_text(S(0x9004));   /* "\nNothing to mix!\n" */
            } else {
                print_text(S(0x8FF0));   /* "Mixing...\n" */
                if (dg.location < 0x21) wait_turns(10);
                else delay_ticks(10);
                for (int i = 0; i < 8; i++)
                    if ((0x80u >> i) & mask) dg.reagents[i] = (uint8_t)(dg.reagents[i] - amount);
                if (spell < 0 || dg.spell_recipes[spell] != mask) {
                    put_char('\n');
                    check_party_state();
                    spring_trap(dg.dir_x);
                } else {
                    print_text(S(0x8FFC));   /* "\nDone!\n" */
                    dg.spells[spell] = (uint8_t)(dg.spells[spell] + amount);
                    if (dg.spells[spell] > 99) dg.spells[spell] = 99;
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
    if (dg.grapples == 0) {
        print_text(S(0x9016));   /* "With what?\n" */
        return;
    }
    if (dg.transport != 0x1C) {
        print_text(S(0x9022));   /* "On foot!\n" */
        return;
    }
    if (ask_direction() == 0) return;
    int dx = (int16_t)dg.dir_x, dy = (int16_t)dg.dir_y;
    uint8_t t = *get_map_tile_ptr((uint16_t)(dg.party_y + dg.dir_y), (uint16_t)(dg.party_x + dg.dir_x));
    if (t == 0x0D) {
        print_text(S(0x902C));   /* "Impassable!\n" */
    } else if (t == 0x0C) {
        for (unsigned i = 0; i < dg.party_size; i++)
            if (dg.party[i].status != 'D' && dg.party[i].dex < (unsigned)random_between(0x1E, 1)) {
                print_text(S(0x904A));   /* "Fell!\n" */
                damage_member(random_between(5, 1), (int)i);
            }
        outdoor_step(dy, dx);
    } else {
        print_text(S(0x903A));   /* "Not climbable!\n" */
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
            const char *syllable = S(ds_u16((uint16_t)(0x1B7A + key * 2)));
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
                for (int len = (int)strlen(S(ds_u16((uint16_t)(0x1B7A + letters[n - 1] * 2)))); len > 0; len--) put_char(' ');
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
        const char *want = S(ds_u16((uint16_t)(offsetof(struct DGroup, spell_menu_layout) + s * 2)));
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
