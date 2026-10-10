/* combat.c - combat core, COMBAT.OVL and the combat half of SJOG.OVL. See combat.h. */
#include <string.h>
#include "cmds.h"
#include "combat.h"
#include "commands.h"
#include "comsubs.h"
#include "data.h"
#include "dungeon.h"
#include "endings.h"
#include "game.h"
#include "magic.h"
#include "gfx.h"
#include "input.h"
#include "map.h"
#include "misc.h"
#include "objects.h"
#include "picture.h"
#include "sjog.h"
#include "stats.h"
#include "status.h"
#include "text.h"
#include "tiles.h"
#include "view.h"
#include "world.h"

#define S(off) ds_str(off)
#define C(slot) (&dg.combatants[(slot) & 0x1F])
#define OBJ(c) (&dg.objects[(c)->object & 0x1F])
#define LMAP(y, x) dg.local_map[(((y) & 0xFF) * 0x20 + ((x) & 0xFF)) & 0x3FF]

/* ---- the main program's part ---- */

int combatant_side(int slot)
{
    Combatant *c = C(slot);
    if (c->flags & 0x20) return 0;
    if (!(c->flags & 0x80)) return (c->flags & 1) == 0;
    /* a member whose name has 'j' as its fifth letter (Saduj) is always an enemy */
    if (c->kind == 0 || dg.party[c->kind & 0x0F].name[4] != 'j') return c->flags & 1;
    return 1;
}

void flash_combatant(unsigned slot)
{
    Combatant *c = NULL;
    if (dg.location > 0x7F) {
        c = C(slot);
        slot = c->object;
    }
    draw_tile_at(dg.objects[slot & 0x1F].y, dg.objects[slot & 0x1F].x, 0);
    if (dg.location < 0x80 || !(c->flags & 0x80)) {
        sound_noise(2000, 3000, 10);
    } else {
        highlight_member_row(c->kind);
        sound_noise(500, 3000, 0x28);
        highlight_member_row(c->kind);
    }
    world_tick();
}

void victory_fanfare(void)
{
    for (int i = 3; i != 0; i--) sound_sweep(6, 300, 0x2A30, 1, 0x11F8);
    sound_sweep(3, 300, 0x5460, 1, 0x17D4);
}

int in_combat_bounds(int y, int x) { return !(x < 0 || x > 10 || y < 0 || y > 10); }

/* The original sums a table value for each of the six equipment slots, but under a test that is
 * never true (unsigned item > 0xFFFF), so only the Protection spell counts. In Doom (0x28)
 * without item 0x0F as body armour it is 0. Armour proper is party[].armour (calc_damage). */
int get_armour_bonus(int member)
{
    if (dg.prev_location == 0x28 && dg.party[member].body != 0x0F) return 0;
    return dg.active_spell == 0x50 ? 3 : 0;
}

/* Takes the item out of whichever slot holds it; 1 if it was worn. */
int unequip_item(unsigned item, int member)
{
    PartyMember *m = &dg.party[member & 0x0F];
    int found = 1;
    if (item == m->helm) m->helm = 0xFF;
    else if (item == m->body) m->body = 0xFF;
    else if (item == m->left_hand) m->left_hand = 0xFF;
    else if (item == m->right_hand) m->right_hand = 0xFF;
    else if (item == m->ring) {
        m->ring = 0xFF;
        if (item == 0x2A && dg.location > 0x7F && dg.combat_turn < 0x20) C(dg.combat_turn)->flags &= 0xEF;   /* visible again */
    } else if (item == m->amulet) m->amulet = 0xFF;
    else found = 0;
    get_armour_bonus(member);
    return found;
}

/* Monsters go in slots 6 and up, the party from 0. An object (kind 2: a field, a chest) gets no
 * slot and the object index is returned instead. When a slot number is reused, objects that
 * remembered it as their last target forget it. */
int add_combatant(uint8_t z, uint8_t y, uint8_t x, int kind, int type)
{
    int slot = -1;
    Combatant *c = NULL;
    if (kind != 2) {
        for (int i = kind == 0 ? 6 : 0; i < 0x20; i++) {
            if (dg.combatants[i].flags != 0) continue;
            c = &dg.combatants[i];
            if (kind == 1 && dg.party[type].status != 'D') {
                c->speed = dg.party[type].dex;
                c->timer = (uint8_t)(0x24 - c->speed);
                c->flags = 0x80;
                if (dg.party[type].status != 'G' && dg.party[type].status != 'P') c->flags |= 8;
            }
            if (kind == 0) {
                c->hp = MONSTER_STAT(type, 5);
                c->speed = (uint8_t)(random_upto(7) - 4 + MONSTER_STAT(type, 1));
                if (c->speed > 0x1E) c->speed = MONSTER_STAT(type, 1);
                c->timer = (uint8_t)(0x24 - c->speed);
                c->flags = (type == 8 || type == 9) ? 0x20 : 0x40;
            }
            c->kind = (uint8_t)type;
            c->x = x;
            c->y = y;
            slot = i;
            break;
        }
    }
    if (slot >= 0 || kind == 2) {
        int o;
        for (o = 0; o < 0x20; o++) {
            MapObject *ob = &dg.objects[o];
            if (ob->tile != 0) continue;
            if (kind == 0) ob->tile = ob->tile2 = (uint8_t)(type * 4 + 0x40);
            if (kind == 2) {
                ob->tile = ob->tile2 = (uint8_t)type;
                slot = o;
            }
            ob->x = x;
            ob->y = y;
            ob->z = z;
            ob->skiffs = 0xFF;
            ob->hull = kind == 0 ? c->hp : (uint8_t)type;
            if (kind != 2) c->object = (uint8_t)o;
            break;
        }
        if (kind != 2 && o == 0x20) slot = -1;
    }
    if (slot >= 0)
        for (int i = 0; i < 0x20; i++)
            if (OBJ(&dg.combatants[i])->skiffs == slot) OBJ(&dg.combatants[i])->skiffs = 0xFF;
    return slot;
}

/* A ring of invisibility (0x2A) hides its wearer (tile 0x1D); a ring of regeneration (0x2C) heals. */
void apply_ring_effects(int slot)
{
    Combatant *c = C(slot);
    if (!(c->flags & 0x80) || (c->flags & 0x28)) return;
    if (dg.party[c->kind & 0x0F].ring == 0x2A) {
        OBJ(c)->tile2 = 0x1D;
        c->flags |= 0x10;
    } else if (dg.party[c->kind & 0x0F].ring == 0x2C) {
        regen_ring_tick();
    }
}

void wake_combatant(int slot)
{
    Combatant *c = C(slot);
    if (c->flags & 8) {
        if (!(c->flags & 0x80)) {
            OBJ(c)->flags = 0;
        } else {
            dg.party[c->kind & 0x0F].status = 'G';
            OBJ(c)->tile2 = (c->flags & 0x10) ? 0x1D : OBJ(c)->tile;
        }
        c->flags &= 0xF7;
    }
    draw_status_bar();
}

void sleep_unless_poisoned(unsigned slot)
{
    Combatant *c = C(slot);
    if (!(c->flags & 0x80) || dg.party[c->kind & 0x0F].status != 'P') sleep_combatant(slot);
}

void sleep_combatant(unsigned slot)
{
    Combatant *c = C(slot);
    if (!(c->flags & 0x80)) {
        c->flags |= 8;
        dg.combat_event = 4;
        OBJ(c)->flags = 0xFF;   /* stops its animation */
    } else {
        PartyMember *m = &dg.party[c->kind & 0x0F];
        if (m->status == 'D') return;
        m->status = 'S';
        c->flags |= 8;
        OBJ(c)->tile2 = 0x1E;
        if (slot == dg.active_player) dg.active_player = 0xFF;
        dg.combat_event = 4;
        draw_status_bar();
        if (dg.combat_flags & 4) return;
    }
    world_tick();
}

/* Empties the objects and combatants and puts the living party on the map's start squares, as
 * mages (0x40), bards (0x44), fighters (0x48) or the Avatar (0x4C). Each ring of invisibility or
 * regeneration worn has 1 chance in 16 to vanish. A 0xDC in the middle of the map (dungeon
 * rooms) becomes a chest of level 3 x dungeon level + 7. */
void clear_combat_tables(void)
{
    for (int i = 0; i < 0x20; i++) {
        MapObject *o = &dg.objects[i];
        o->flags = o->x = o->tile2 = o->tile = o->hull = o->z = o->y = 0;
        memset(&dg.combatants[i], 0, sizeof dg.combatants[i]);
    }
    for (unsigned m = 0; m < dg.party_size; m++) {
        PartyMember *pm = &dg.party[m];
        if (pm->status == 'D') continue;
        if (pm->ring == 0x2A || pm->ring == 0x2C) {
            uint8_t ring = pm->ring;
            if (random_between(0x0F, 0) == 0x0B) {
                print_text(S(0xA422));   /* "A ring has vanished!\n" */
                sound_slide(0x28, 1, 2000, 0x4B0);
                unequip_item(ring, (int)m);
            }
        }
        int slot = add_combatant(dg.z_level, dg.combat_party_start_y[m], dg.combat_party_start_x[m], 1, (int)m);
        if (slot < 0) continue;   /* can't happen: the tables were just emptied */
        MapObject *o = OBJ(C(slot));
        o->skiffs = 0xFF;
        switch (pm->cls) {
        case 'A': o->tile = 0x4C; break;
        case 'B': case 'S': case 'T': o->tile = 0x44; break;
        case 'D': case 'M': o->tile = 0x40; break;
        case 'F': case 'P': case 'R': o->tile = 0x48; break;
        }
        o->tile2 = o->tile;
        if (pm->status == 'S') sleep_combatant((unsigned)slot);
        else apply_ring_effects(slot);
    }
    if (dg.local_map[0xA5] == 0xDC) {
        int o = add_combatant(dg.z_level, 5, 5, 2, 1);
        dg.objects[o & 0x1F].hull = (uint8_t)(dg.z_level * 3 + 7);
        dg.local_map[0xA5] = dg.corridor_floor_tile;
    }
}

/* monster + 0x100 (the attacked object's `hull` was 0x80 or more) means the type's full group
 * even in a town, where otherwise one fights alone (except type 0x0C). The group is 1 to the
 * type's size (byte 6), rolled twice while no_ambush is set; sizes 1, 8 and 16 are exact. Among
 * the first quarter 1 in 9 is the type's companion (ds:16D4). The start squares come from the
 * combat map, shuffled for an ambush (flags 4). The original reads past its 16 start squares for
 * bigger groups; the port stops at 16. */
void place_monster_group(unsigned monster, unsigned flags)
{
    int alone = (int)monster < 0x100;
    if (!alone) monster -= 0x100;
    dg.combat_exit = 0;
    if (!(flags & 4)) clear_combat_tables();
    int order[16];
    for (int i = 0; i < 0x10; i++) order[i] = i;
    if (flags & 4)
        for (int i = 0; i < 0x10; i++) {
            int j = random_between(0x0F, 0), t = order[i];
            order[i] = order[j];
            order[j] = t;
        }
    print_text(S(0xA438));   /* "*** CONFLICT ***\n" */
    int count;
    if (dg.prev_location == 0 || dg.prev_location > 0x20 || monster == 0x0C || !alone) count = MONSTER_STAT(monster, 6);
    else count = 1;
    if (count != 8 && count != 0x10 && count != 1) {
        count = random_between(count, 1);
        if (dg.no_ambush != 0) count = random_between(count, 1);
        world_tick();
        if (count > 0x19) count = 0x1A;
    }
    add_combatant(dg.z_level, dg.combat_monster_start2[order[0]], dg.combat_monster_start[order[0]], 0, (int)monster);
    for (int i = 1; i < count && i < 0x10; i++) {
        unsigned type = monster;
        if (i < count / 4 + 1 && random_upto(8) == 0) type = ds_u8((uint16_t)(0x16D4 + monster));
        add_combatant(dg.z_level, dg.combat_monster_start2[order[i]], dg.combat_monster_start[order[i]], 0, (int)type);
    }
}

/* Map n of BRIT.CBT: eleven 32-byte rows. The columns right of the 11 x 11 map hold the start
 * squares: the party's x at 0x6B and y at 0x71 (row 3), the monsters' x at 0xCB (row 6) and y at
 * 0xEB (row 7), 16 each. */
void load_combat_map(int map)
{
    load_file_part((uint32_t)map * 0x160, 0x160, dg.local_map, S(0xA3F0));   /* "BRIT.CBT" */
    memcpy(dg.combat_party_start_x, dg.local_map + 0x6B, 6);
    memcpy(dg.combat_party_start_y, dg.local_map + 0x71, 6);
    memcpy(dg.combat_monster_start, dg.local_map + 0xCB, 16);
    memcpy(dg.combat_monster_start2, dg.local_map + 0xEB, 16);
}

/* flags 0: a fight with the map object `object`; 4: a camp (arg = hours, object = the guard);
 * 2: a dungeon corridor (the map was built by setup_corridor_combat). The map's objects are put
 * aside for the battle. */
void run_combat(int arg, int object, unsigned flags)
{
    uint8_t x = dg.party_x, y = dg.party_y, z = dg.z_level, active = dg.active_player;
    dg.combat_flags = (uint8_t)flags;
    dg.prev_location = dg.location;
    dg.location = 0xFF;
    memcpy(dg.objects_backup, dg.objects, 0x100);
    int fight = 1;
    if (flags == 0) {
        MapObject *o = &dg.objects[object & 0x1F];
        unsigned monster = (o->tile & 0xFC) == 0x2C ? 1 : (unsigned)(o->tile - 0x40) >> 2;
        if (o->hull > 0x7F) monster += 0x100;
        place_monster_group(monster, 1);
    } else if (flags & 4) {
        fight = camp_rest(arg, (unsigned)object, flags);
    } else if (flags & 2) {
        place_combatants(2, 0);
    } else {
        fight = 0;
    }
    if (fight) {
        dg.active_player = 0xFF;
        dg.combat_turn = 0xFF;
        dg.combat_victory = 0;
        combat_main_loop();
    }
    if (dg.on_ladder != 0) {
        dg.on_ladder = 0;
        ladder_tiles_patch(1);
    }
    dg.location = dg.prev_location;
    dg.redraw_flags = 1;
    dg.z_level = z;
    dg.party_x = x;
    dg.party_y = y;
    draw_status_bar();
    /* with no active player the original tests "member 255", bytes far past the party table */
    char st = (char)ds_u8((uint16_t)(offsetof(struct DGroup, party) + active * 0x20u + offsetof(PartyMember, status)));
    dg.active_player = (st == 'D' || st == 'S') ? 0xFF : active;
    memcpy(dg.objects, dg.objects_backup, 0x100);
}

/* The combat map comes from the terrain under the enemy: 1 swamp, 2 grass, 3 scrub and desert,
 * 4 forest, 5 hills, 6 mountains, 7 bridges and such, 8 floors and anything in a town, 10 a
 * Shadowlord, 11-14 aboard ship (against sea monsters, from the shore, against land, against
 * pirates), 12 pirates from land, 15 on water. */
void combat_with_object(int object)
{
    MapObject *o = &dg.objects[object & 0x1F];
    unsigned kind = o->tile & 0xFC;
    put_char('\n');
    put_char(0xFC);
    print_text(kind < 0x40 ? S(0xA3FA) : S(ds_u16((uint16_t)(0x18B6 + ((kind - 0x40) >> 2) * 2))));   /* "PIRATES" or the type's name */
    put_char(0xFB);
    print_text(S(0xA402));   /* "\n\n" */
    uint8_t t = *get_map_tile_ptr(o->y, o->x);
    int water = t < 4 || ((t & 0xFE) != 0x6A && (t & 0xF0) == 0x60);
    int other = dg.location == 0 ? 2 : 8, map;
    if (kind == 0xFC) {
        map = 10;
        if (dg.lb_sceptre != 0) {
            print_text(S(0xA406));   /* "The Sceptre is reclaimed!\n" */
            sound_sweep(1, 1, -0x218, 1, 0xFD2);
            dg.lb_sceptre = 0;
        }
    } else {
        if ((kind & 0xF0) == 0x80) water = 1;   /* sea monsters */
        if ((dg.transport & 0xF8) == 0x20) map = kind == 0x2C ? 0x0E : water ? 0x0B : 0x0D;
        else if (kind == 0x2C) map = 0x0C;
        else if (water) map = 0x0F;
        else if (t == 4) map = 1;
        else if (t == 5) map = 2;
        else if (t == 6 || t == 8) map = 3;
        else if (t == 7 || t == 0x1E || t == 0x1F) map = 4;
        else if (t == 9 || t == 0x0A) map = 5;
        else if (t >= 0x0B && t <= 0x0F) map = 6;
        else if (t == 0x1D || t == 0x48 || t == 0x49 || t == 0x6A || t == 0x6B) map = 7;
        else if (t == 0x44) map = 8;
        else map = other;
    }
    load_combat_map(map);
    run_combat(0, object, 0);
    combat_cleanup(object);
    update_light_map();
    pass_time(0);
}

void camp_ambush_combat(int hours, int guard)
{
    load_combat_map(0);
    run_combat(hours, guard, 4);
    update_light_map();
}

/* ---- COMBAT.OVL ---- */

/* Off the map counts as free. Fields (0xE8-0xEA), bodies and invisible things don't block;
 * the energy field 0xEB does. */
int combat_tile_free(unsigned y, unsigned x, uint8_t mover)
{
    int ix = (int16_t)x, iy = (int16_t)y;
    if (ix >= 0x0B || iy >= 0x0B || ix < 0 || iy < 0) return 1;
    uint8_t t = *get_map_tile_ptr(y, x);
    if (!tile_passable(t, mover) || t == 0xFF) return 0;
    for (int i = 0; i < 0x20; i++) {
        MapObject *o = &dg.objects[i];
        Combatant *c = &dg.combatants[i];
        if (o->x == x && o->y == y) {
            if (o->tile == 0xEB) return 0;
            if ((o->tile & 0xFC) != 0xE8 && o->tile != 0x1E && o->tile != 0x1F && o->tile != 0 && o->tile2 != 0) return 0;
        } else if (c->x == x && c->y == y && (c->flags & 0x24) == 0 && c->flags != 0) {
            return 0;
        }
    }
    return 1;
}

/* Half the time (always for type 0x1A). Monsters flagged 0x8000 can't under Negate Magic. */
int monster_ranged_attack(int force_miss, int target, int attacker)
{
    unsigned type = C(attacker)->kind;
    uint8_t missile = ds_u8((uint16_t)(offsetof(struct DGroup, monster_missile) + type));
    if (!(type == 0x1A || random_between(0xFF, 0) < 0x80)) return 0;
    if ((MONSTER_FLAGS(type) & 0x8000) && (dg.active_spell == 0x4E || dg.active_spell == 0x1C)) return 0;
    sound_slide(0x96, 5, 400, 0x2EE);
    int hit = force_miss ? 0 : roll_to_hit(0, -(int)dg.cast_circle, attacker, target);
    int t = fire_missile(missile, hit, C(target)->y, C(target)->x, attacker);
    if (t >= 0) {
        flash_combatant((unsigned)t);
        put_char('\n');
        resolve_hit(attacker, t);
        report_hit(attacker, t);
    }
    return 1;
}

/* 1 if the monster used its turn. A magic attacker's shot at a wearer of amulet 0x2D misses
 * half the time. Thieves (flag 2) steal 5 food 3 times in 4 instead of wounding. A fighter that
 * changed sides (flags 1) strikes with weapon 0x21. */
int monster_attack(int slot)
{
    Combatant *c = C(slot);
    unsigned type = c->kind;
    int force_miss = 0;
    int target = find_nearest_enemy(slot);
    if (target < 0) return 0;
    if (c->flags & 1) {
        if (combatant_distance(target, slot) != 1) return 0;
        dg.attack_item = 0x21;
        melee_attack(0x21, (unsigned)target, slot);
        return 1;
    }
    Combatant *tc = C(target);
    if ((tc->flags & 0x80) && dg.party[tc->kind & 0x0F].amulet == 0x2D && (MONSTER_FLAGS(type) & 0x8000))
        force_miss = random_between(0xFF, 0) < 0x80;
    if ((unsigned)combatant_distance(target, slot) > ds_u8((uint16_t)(offsetof(struct DGroup, monster_attack_range) + type))) return 0;
    if (type == 0x1A) OBJ(c)->flags = 0x20;
    if (combatant_distance(target, slot) != 1) return monster_ranged_attack(force_miss, target, slot);
    if (type == 0x2D) OBJ(c)->flags = 0x20;
    sound_slide(0x96, 5, 400, 0x2EE);
    dg.last_attacker[target & 0x1F] = (uint8_t)slot;
    if (roll_to_hit(0, -(int)dg.cast_circle, slot, target) == 0) return 1;
    if ((MONSTER_FLAGS(type) & 2) && random_between(3, 0) != 0 && dg.food != 0) {
        print_text(S(0x6D74));   /* "\nA " */
        print_combatant_name(slot);
        print_text(S(0x6D78));   /* " stole some food!\n" */
        sub_word_floor0(5, &dg.food);
        sound_slide(0x32, 1, 2000, 800);
        draw_status_bar();
        return 1;
    }
    flash_combatant((unsigned)target);
    put_char('\n');
    resolve_hit(slot, target);
    report_hit(slot, target);
    return 1;
}

/* Nothing under Time Stop, half the turns under Quickness, nothing while swallowed; a sleeper
 * wakes 1 time in 17. Otherwise: a special power, else an attack, else a move. A fleeing monster
 * (flags 2) regains a hit point 1 turn in 4 and checks its morale again; one that steps off the
 * map escapes. */
void monster_turn(void)
{
    int slot = dg.combat_turn;
    Combatant *c = C(slot);
    dg.attack_item = 0;
    if (dg.active_spell == 0x54) return;
    if (dg.active_spell == 0x51 && random_between(1, 0) == 0) return;
    if (c->flags & 4) return;
    if (c->flags & 8) {
        if (random_between(0x10, 0) == 0x10) wake_combatant(slot);
        return;
    }
    if (c->kind == 0x2D) OBJ(c)->flags = 0;
    if (!(c->flags & 2)) {
        if (monster_special(slot) != 0) return;
        if (monster_attack(slot) != 0) return;
    } else {
        if (!(c->flags & 0x80) && random_between(3, 0) == 3) c->hp++;
        monster_morale(slot);
    }
    if (monster_move(slot) == 0) {
        if (c->flags & 2) monster_attack(slot);
        return;
    }
    if (dg.combat_event & 0x10) {
        put_char('\n');
        sound_slide(0x28, 1, 2000, 0x4B0);
        print_combatant_name(slot);
        print_text(S(0x6D8C));   /* " escapes!\n" */
        combat_remove(-1 - slot);
        if (c->kind == 0x2F) combat_check_pass_out();   /* (the flags were just cleared, so only the type counts) */
    }
    world_tick();
}

int combat_item_command(int which, const char *prompt)
{
    print_text(prompt);
    if (!(C(dg.combat_turn)->flags & 0x80)) {
        print_text(S(0x6D98));   /* "Can't!\n" */
        return 1;
    }
    switch (which) {
    case 0: cmd_get(); break;
    case 1: cmd_jimmy(); break;
    case 2: cmd_open(); break;
    case 3: cmd_ready(); break;
    case 4: cmd_search(); break;
    case 5: cmd_use(); break;
    }
    return 0;
}

/* Adds the item's name to the line in text_buffer if it is a weapon. */
int append_weapon_name(int count, int item)
{
    if (item == 0xFF || ds_u8((uint16_t)(offsetof(struct DGroup, item_damage) + item)) == 0) return 0;
    char *buf = (char *)dg.text_buffer;
    if (count != 0) strcat(buf, S(0x6DA0));   /* ", " */
    strcat(buf, S(ds_u16((uint16_t)(offsetof(struct DGroup, item_names) + item * 2))));
    return 1;
}

/* A turn of anyone on the party's side: a member, or a monster that changed sides. With an
 * active player set, everyone else's turn is skipped. A member holding item 0x23 is possessed
 * and acts as a monster. Keys that aren't a turn ("What?", a blocked move, the sound toggle) ask
 * again; commands that fail print the name line again first. */
void party_member_turn(void)
{
    int slot = dg.combat_turn;
    Combatant *c = C(slot);
    unsigned member = c->kind, key = 0;
    dg.party_x = c->x;
    dg.party_y = c->y;
    if (dg.active_player != 0xFF && (!(c->flags & 0x80) || member != dg.active_player)) {
        combat_end_of_turn();
        return;
    }
    PartyMember *pm = &dg.party[member & 0x0F];
    if ((c->flags & 0x80) && (pm->left_hand == 0x23 || pm->right_hand == 0x23)) {
        c->flags |= 1;
        dg.active_player = 0xFF;
        dg.status_dirty = 1;
        monster_turn();
    } else {
        int again, weapons = 0;
        if (c->flags & 0x80) highlight_member_row((int)c->kind);
        if (dg.status_dirty != 0) {
            draw_status_bar();
            dg.status_dirty = 0;
        }
        do {
            put_char('\n');
            print_combatant_name(slot);
            dg.text_buffer[0] = 0;
            if (c->flags & 0x80) {
                print_text(S(0x6DA4));   /* ", armed with " */
                weapons = append_weapon_name(0, pm->helm);
                weapons += append_weapon_name(weapons, pm->left_hand);
                weapons += append_weapon_name(weapons, pm->right_hand);
                if (weapons == 0) strcat((char *)dg.text_buffer, S(0x6DB2));   /* "bare hands" */
            }
            strcat((char *)dg.text_buffer, S(0x6DBE));   /* ":" */
            print_text((char *)dg.text_buffer);
            again = 0;
            for (int done = 0; !done; ) {
                put_char('\n');
                draw_label_left();
                done = 1;
                if (c->flags & 4) {
                    print_text(S(0x6DC0));   /* "ARGH!\n" */
                    sound_noise(500, 3000, 0x28);
                    check_regurgitate(slot);
                    continue;
                }
                if (c->flags & 8) {
                    if (random_between(0xFF, 0) < 0x10) wake_combatant(slot);
                    print_text(S(0x6DC8));   /* "Zzzzz...\n" */
                    continue;
                }
                key = (unsigned)get_command_key() & 0xFF;
                if (game_quit()) return;
                again = 0;
                switch (key) {
                case 1: case 2: case 3: case 4:
                    if (combat_move_member((int)key, slot) == 0) done = 0;
                    break;
                case 0x13:
                    print_text(S(0x6DE4));   /* "Sound " */
                    print_text(S(dg.sound_enabled == 0 ? 0x6DF2 : 0x6DEC));   /* "On\n" / "Off\n" */
                    dg.sound_enabled = dg.sound_enabled == 0;
                    done = 0;
                    break;
                case 0x1B: again = combat_escape(); break;
                case ' ': print_text(S(0x6E60)); break;   /* "Pass\n" */
                case '0':
                    dg.active_player = 0xFF;
                    print_text(S(0x6E66));   /* "Set active plr:\nNone!\n" */
                    draw_status_bar();
                    break;
                case '1': case '2': case '3': case '4': case '5': case '6':
                    if (combat_set_active_player(key - '1') == 0) again = 1;
                    break;
                case 'A': cmd_attack_combat(weapons, slot); break;
                case 'B': again = combat_cant_do(1, S(0x6E7E)); break;   /* "Board" */
                case 'C':
                    print_text(S(0x6DF6));   /* "Cast...\n" */
                    again = 1;
                    if (!(c->flags & 0x80)) {
                        print_text(S(0x6E0C));   /* "Can't!\n" */
                    } else if (check_interference(slot) == 0) {
                        dg.magic_attack = 1;
                        dg.cast_circle = 1;
                        again = 0;
                        if (dg.active_spell == 0x4E || (dg.lb_crown == 0 && dg.prev_location == 0x12)) {
                            print_text(S(0x6E00));   /* "Absorbed!\n" (Negate Magic, or Blackthorn's castle without the Crown) */
                            sound_sweep(2, 1000, 28000, 1, 0x2648);
                        } else {
                            cmd_cast();
                        }
                    }
                    break;
                case 'D': print_text(S(0x6E84)); again = 1; break;        /* "D-What?\n" */
                case 'E': again = combat_cant_do(2, S(0x6E8E)); break;    /* "Enter" */
                case 'F': again = combat_cant_do(2, S(0x6E94)); break;    /* "Fire" */
                case 'G': again = combat_item_command(0, S(0x6E14)); break;   /* "Get-" */
                case 'H': again = combat_cant_do(2, S(0x6E9A)); break;    /* "Hole up" */
                case 'I': again = combat_cant_do(2, S(0x6EA2)); break;    /* "Ignite torch" */
                case 'J': again = combat_item_command(1, S(0x6E1A)); break;   /* "Jimmy-" */
                case 'K':
                    if (combat_klimb() == 0) done = 0;
                    break;
                case 'L': again = combat_cant_do(2, S(0x6EB0)); break;    /* "Look" */
                case 'M': again = combat_cant_do(2, S(0x6EB6)); break;    /* "Mix" */
                case 'N': again = combat_cant_do(2, S(0x6EBA)); break;    /* "New order" */
                case 'O': again = combat_item_command(2, S(0x6E22)); break;   /* "Open-" */
                case 'P':
                    print_text(S(0x6E28));   /* "Push-" */
                    cmd_push();
                    break;
                case 'Q': again = combat_cant_do(2, S(0x6EC4)); break;    /* "Quit" */
                case 'R': again = combat_item_command(3, S(0x6E2E)); break;   /* "Ready...\n\n" */
                case 'S': again = combat_item_command(4, S(0x6E3A)); break;   /* "Search-" */
                case 'T': again = combat_cant_do(3, S(0x6ECA)); break;    /* "Talk" */
                case 'U': again = combat_item_command(5, S(0x6E42)); break;   /* "Use item\n\n" */
                case 'V': again = combat_cant_do(2, S(0x6ED0)); break;    /* "View" */
                case 'W': print_text(S(0x6ED6)); again = 1; break;        /* "W-What?\n" */
                case 'X': again = combat_cant_do(1, S(0x6EE0)); break;    /* "X-it" */
                case 'Y':
                    print_text(S(0x6E4E));   /* "Yell " */
                    cmd_yell();
                    break;
                case 'Z':
                    print_text(S(0x6E54));   /* "Z-stats...\n" */
                    cmd_zstats();
                    break;
                case 0xFC:
                    print_text(S(0x6DD2));   /* "Buffer O" */
                    dg.typeahead_on = dg.typeahead_on == 0;
                    print_text(S(dg.typeahead_on == 0 ? 0x6DE0 : 0x6DDC));
                    done = 0;
                    break;
                default:
                    print_text(S(0x6EE6));   /* "What?\n" */
                    done = 0;
                    break;
                }
            }
        } while (again);
        if (c->flags & 0x80) highlight_member_row((int)c->kind);
    }
    if (key < '0' || key > '6') {
        combat_end_of_turn();
        world_tick();
        check_absorbed();
    }
}

/* Each pass every living combatant's timer drops by one; at zero it acts and the timer restarts
 * at 36 - speed. Ten turns make a minute. Whoever stands in stocks or manacles (0x84, 0x85)
 * never acts. The battle ends when nobody is left on the party's side: 0 if the other side is
 * gone too (the party walked off), else 1 ("BATTLE IS LOST!" unless it was won before). */
int combat_main_loop(void)
{
    int result = 1, over = 0;
    dg.combat_exit = 0;
    world_tick();
    draw_status_bar();
    flush_keyboard();
    count_combat_sides();
    dg.combat_victory = dg.dir_x == 0;
    while (!over) {
        for (dg.combat_turn = 0; dg.combat_turn < 0x20; dg.combat_turn++) {
            if (game_quit()) {
                over = 1;
                break;
            }
            dg.open_door_tile = 0;
            Combatant *c = C(dg.combat_turn);
            if ((c->flags & 0xC0) == 0 || (c->flags & 0x20)) continue;
            if ((c->flags & 0x80) && dg.party[c->kind & 0x0F].status == 'D') {   /* died outside a blow: poison, a trap */
                c->flags |= 0x20;
                flash_combatant(dg.combat_turn);
                apply_damage(99, dg.combat_turn);
                continue;
            }
            if ((LMAP(c->y, c->x) & 0xFE) == 0x84) continue;
            if (--c->timer != 0) continue;
            c->timer = (uint8_t)(0x24 - c->speed);
            dg.aim_mode = 0;
            dg.combat_event = 0;
            dg.cast_circle = 0;
            dg.magic_attack = 0;
            dg.attack_item = 0;
            if (++dg.combat_minute_ctr == 10) {
                dg.combat_minute_ctr = 0;
                pass_time(1);
            }
            dg.combat_redraw = 1;
            if (combatant_side(dg.combat_turn) == 0) party_member_turn();
            else monster_turn();
            dg.last_attacker[dg.combat_turn & 0x1F] = 0xFF;
            apply_tile_hazard(dg.combat_turn);
            count_combat_sides();
            if (dg.dir_y == 0) {
                if (dg.dir_x == 0) {
                    result = 0;
                } else {
                    if (combat_check_pass_out() != -1) continue;   /* a possessed member comes to: not over yet */
                    world_tick();
                    if (dg.combat_victory == 0) {
                        print_text(S(0x6EEE));   /* "\nBATTLE IS LOST!" */
                        result = 1;
                    }
                }
                over = 1;
                break;
            }
            if (dg.dir_x == 0 && dg.combat_victory == 0) {
                print_text(S(0x6F00));   /* "\nVICTORY!\n" */
                dg.combat_victory = 1;
                victory_fanfare();
                flush_keyboard();
            }
        }
    }
    flush_keyboard();
    dg.beam_frame = 0xFF;
    return result;
}

/* The nearest living fighter of the other side that isn't swallowed and can be seen (invisible
 * ones only in Doom, or by type 0x2F). Under the 'C' effect a failed intelligence roll makes
 * the monster take its own side for the enemy. With nobody to fight the monsters all turn to
 * flee. dir_x / dir_y get the direction (away, when fleeing). */
int find_nearest_enemy(int slot)
{
    Combatant *c = C(slot);
    int party_seen = -1, best = -1, best_dist = 99;
    unsigned tx = 5, ty = 5;   /* (uninitialised in the original when only a passed-out member is found) */
    int side = combatant_side(slot);
    if (dg.active_spell == 0x43 && get_combatant_stat(-1, slot) < random_stat_roll()) side = 0;
    for (int i = 0x1F; i >= 0; i--) {
        Combatant *o = C(i);
        if (i == slot || o->flags == 0 || (o->flags & 0x20) || side == combatant_side(i)) continue;
        if (!(dg.prev_location == 0x28 || c->kind == 0x2F || !(o->flags & 0x10)) || (o->flags & 4)) continue;
        if (i < 5) party_seen++;
        int d = distance(o->y, o->x, c->y, c->x);
        if (d < best_dist) {
            tx = o->x;
            ty = o->y;
            best = i;
            best_dist = d;
        }
    }
    if (party_seen == -1 && best == -1) best = combat_check_pass_out();
    if (best == -1) {
        for (int i = 0x1F; i > 5; i--)
            if (C(i)->flags & 0x40) {
                C(i)->hp = 1;
                C(i)->flags |= 2;
            }
        tx = ty = 5;
    }
    dg.dir_x = dg.dir_y = 0;
    if (tx < c->x) dg.dir_x = 0xFFFF;
    if (c->x < tx) dg.dir_x = 1;
    if (ty < c->y) dg.dir_y = 0xFFFF;
    if (c->y < ty) dg.dir_y = 1;
    if (c->flags & 2) {
        dg.dir_x = (uint16_t)-dg.dir_x;
        dg.dir_y = (uint16_t)-dg.dir_y;
    }
    return best == slot ? -1 : best;
}

/* Types 0x1A and 0x1B never move. Teleporters (flag 0x2000) jump to a random free square 3
 * times in 4. Others step towards the enemy along one axis, picked at random, the other if
 * that is blocked, else up to four random tries. Only a fleeing monster can step off the map. */
int monster_move(int slot)
{
    Combatant *c = C(slot);
    int moved = 0, r = 0;
    if (c->kind == 0x1B || c->kind == 0x1A) return 0;
    if (!(c->flags & 0x80) && (MONSTER_FLAGS(c->kind) & 0x2000) && dg.active_spell != 0x4E && dg.active_spell != 0x1C &&
        (combat_is_surrounded(slot) || random_upto(3) != 3) && random_combat_pos()) {
        MapObject *o = OBJ(c);
        if (combat_tile_free(dg.dir_y, dg.dir_x, o->tile)) {
            c->x = o->x = (uint8_t)dg.dir_x;
            c->y = o->y = (uint8_t)dg.dir_y;
            print_text(MONSTER_NAME(c->kind));
            print_text(S(0x6F0C));   /* " teleports!\n" */
            moved = 1;
        }
    }
    if (moved || combat_is_surrounded(slot)) return moved;
    find_nearest_enemy(slot);
    if (random_upto(0xFF) < 0x80 || combat_is_blocked(slot, c->y, (uint16_t)(c->x + dg.dir_x))) {
        if (!combat_is_blocked(slot, (uint16_t)(c->y + dg.dir_y), c->x)) {
            dg.dir_x = 0;
            r = 0x3E6;
        }
    } else {
        dg.dir_y = 0;
        r = 999;
    }
    if (r < 0x3DE) {
        uint8_t y = c->y, x = c->x;
        moved = 4;   /* the try counter shares the result variable */
        do {
            r = random_upto(3);
            if (r == 0) { dg.dir_y = 1; dg.dir_x = 0; }
            else if (r == 1) { dg.dir_x = 1; dg.dir_y = 0; }
            else if (r == 2) { dg.dir_y = 0xFFFF; dg.dir_x = 0; }
            else if (r == 3) { dg.dir_x = 0xFFFF; dg.dir_y = 0; }
            if (!combat_is_blocked(slot, (uint16_t)(y + dg.dir_y), (uint16_t)(x + dg.dir_x))) r = 0x3DF;
            if (r > 0x3DE) break;
            dg.dir_y = dg.dir_x = 0;
        } while (--moved > 0);
    }
    if (r != 0) {
        MapObject *o = OBJ(c);
        c->x = (uint8_t)(c->x + dg.dir_x);
        o->x = c->x;
        c->y = (uint8_t)(c->y + dg.dir_y);
        o->y = c->y;
        moved = 1;
        if (!in_combat_bounds(c->y, c->x)) dg.combat_event = 0x10;
    }
    return moved;
}

/* Dungeon rooms: stepping on one of eight trigger squares changes up to two squares of the map
 * (the lists are in the columns right of the map). */
int combat_room_trigger(unsigned y, unsigned x)
{
    int fired = 0;
    if (!(dg.combat_flags & 0x80) && !(dg.combat_flags & 2)) return 0;
    uint8_t *m = dg.local_map;
    for (int i = 0; i < 8; i++) {
        if (m[0x10B + i] != x || m[0x113 + i] != y) continue;
        m[0x113 + i] = m[0x10B + i] = 0xFF;
        if (m[0x12B + i] < 0x0B && m[0x133 + i] < 0x0B) m[m[0x133 + i] * 0x20 + m[0x12B + i]] = m[0x0B + i];
        if (m[0x14B + i] < 0x0B && m[0x153 + i] < 0x0B) m[m[0x153 + i] * 0x20 + m[0x14B + i]] = m[0x0B + i];
        fired = 1;
    }
    if (fired) world_tick();
    return fired;
}

int random_combat_pos(void)
{
    dg.dir_x = (uint16_t)random_upto(0x0F);
    dg.dir_y = (uint16_t)random_upto(0x0F);
    return dg.dir_x < 0x0B && dg.dir_y < 0x0B;
}

void combat_remove(int which)
{
    int object;
    if (which < 0) {
        Combatant *c = C(-1 - which);
        object = c->object;
        c->object = c->speed = c->hp = c->y = c->x = c->timer = c->flags = 0;
    } else {
        object = which - 1;
    }
    MapObject *o = &dg.objects[object & 0x1F];
    o->x = o->tile2 = o->tile = o->hull = o->z = o->y = 0;
}

/* A monster does its type's damage; a weapon 1 to its value (item_damage), the glass sword
 * (0x27) 99 and then it shatters, item 0x28 nothing, bare hands 1. The defender's armour takes
 * off 1 to its value, so the result can be negative. */
int calc_damage(int defender, int attacker)
{
    int damage;
    if (C(attacker)->flags & 0x40) {
        damage = MONSTER_STAT(C(attacker)->kind, 4);
    } else if (dg.attack_item == 0x27) {
        print_text(S(0x6F1A));   /* "Thy sword hath shattered!\n" */
        unequip_item(0x27, attacker);   /* the slot is passed as the member */
        damage = 99;
    } else if (dg.attack_item == 0x28) {
        damage = 0;
    } else if (dg.attack_item == 0xFF) {
        damage = 1;
    } else {
        damage = ds_u8((uint16_t)(offsetof(struct DGroup, item_damage) + dg.attack_item));
        if (damage > 1 && damage != 99) damage = random_between(damage, 1);
    }
    if (damage == 99) return 99;
    uint8_t armour = (C(defender)->flags & 0x40) ? MONSTER_STAT(C(defender)->kind, 3) : dg.party[C(defender)->kind & 0x0F].armour;
    if (armour != 0) damage -= random_between(armour, 1);
    return damage;
}

uint8_t get_combatant_speed(int slot)
{
    Combatant *c = C(slot);
    if ((dg.active_spell == 0x54 && (c->flags & 0x40)) || c->kind == 0x1A || (c->flags & 8)) return 1;
    return c->speed;
}

/* which: -1 intelligence, -2 speed (dexterity), -3 strength, -4 armour bonus. A weapon id
 * (party) picks strength for missile kind 8, else speed; 0 (monsters) picks strength for types
 * flagged 0x80, else speed. Other values return garbage in the original, 0 here. */
int get_combatant_stat(int which, int slot)
{
    Combatant *c = C(slot);
    int monster = (c->flags & 0x40) != 0;
    if (!monster) {
        if (which >= 1) which = ds_u8((uint16_t)(offsetof(struct DGroup, item_missile) + which)) == 8 ? -3 : -2;
    } else if (which == 0) {
        which = (MONSTER_FLAGS(c->kind) & 0x80) ? -3 : -2;
    }
    PartyMember *m = &dg.party[c->kind & 0x0F];
    switch (which) {
    case -4: return monster ? MONSTER_STAT(c->kind, 3) : get_armour_bonus(c->kind);
    case -3: return monster ? MONSTER_STAT(c->kind, 0) : m->str;
    case -2: return get_combatant_speed(slot);
    case -1: return monster ? MONSTER_STAT(c->kind, 2) : m->intel;
    }
    return 0;
}

/* A hit if (defender's stat - attacker's stat + 30) / 2 <= a 1-30 roll. With a weapon the
 * defender's speed stands against the attacker's weapon stat; with a spell, intelligence against
 * intelligence. Items 0x27, 0x23, 0x28 and spells 0x2A-0x31, 0x33 always hit. */
int roll_to_hit(int weapon, int magic, int attacker, int defender)
{
    (void)magic;
    int a_stat, d_stat;
    if (dg.cast_circle == 0) {
        if (weapon == 0x27 || weapon == 0x23 || weapon == 0x28) return 1;
        d_stat = -2;
        a_stat = weapon;
    } else {
        if (!((weapon < 0x2A || weapon > 0x31) && weapon != 0x33)) return 1;
        a_stat = d_stat = -1;
    }
    int dv = get_combatant_stat(d_stat, defender), av = get_combatant_stat(a_stat, attacker);
    return (dv - av + 0x1E) / 2 <= random_stat_roll();
}

/* Monster flags here: 0x20 half damage from weapons, 8 immune, 0x10 divides when wounded,
 * 0x1000 vanishes, 1 leaves nothing. A dead monster otherwise leaves a body (0x1F) or, if its
 * treasure level beats a 1-30 roll, a chest of that level, trapped on a second such roll -
 * unless it died on water. Type 0x1C leaves a body and a new monster 0x1F; type 0x1E a
 * fence-like tile 0x4C. Returns the experience: hit points / 4 + 1. */
int apply_damage(unsigned damage, int slot)
{
    Combatant *c = C(slot);
    int xp = 0;
    if ((int16_t)damage < 1) {
        dg.combat_event = 0x20;
        damage = 0;
    }
    if (c->flags & 0x80) {
        PartyMember *m = &dg.party[c->kind & 0x0F];
        m->hp = (uint16_t)(m->hp - damage);
        if ((int16_t)m->hp < 1 || damage == 99) {
            m->hp = 0;
            c->flags |= 0x20;
            m->status = 'D';
            OBJ(c)->tile = OBJ(c)->tile2 = 0x1E;
            if (c->kind == dg.active_player) dg.active_player = 0xFF;
        }
        draw_status_bar();
        return 0;
    }
    unsigned type = c->kind, mf = MONSTER_FLAGS(type);
    if ((mf & 0x20) && dg.magic_attack == 0) damage /= 2;
    if (mf & 8) damage = 0;
    c->hp = c->hp < damage ? 0 : (uint8_t)(c->hp - damage);
    if (c->hp == 0 || damage == 99) {
        xp = (MONSTER_STAT(type, 5) >> 2) + 1;
        uint8_t treasure = MONSTER_STAT(type, 7), x = c->x, y = c->y;
        MapObject *o = OBJ(c);
        c->flags = 0x20;
        c->hp = 0;
        if ((mf & 0x1001) == 0) {
            if (type == 0x1C) {
                o->tile = o->tile2 = 0x1F;
                add_combatant(dg.z_level, y, x, 0, 0x1F);
                world_tick();
                return xp;
            }
            if (type == 0x1E) {
                *get_map_tile_ptr(y, x) = 0x4C;
            } else if (LMAP(y, x) != 0x87 && LMAP(y, x) > 3) {
                if (treasure < random_stat_roll()) {
                    o->tile = o->tile2 = 0x1F;
                    return xp;
                }
                o->tile = o->tile2 = 1;
                o->hull = treasure;
                if (treasure > random_stat_roll()) o->hull |= 0x80;
                return xp;
            }
        } else if (mf & 0x1000) {
            print_text(MONSTER_NAME(type));
            print_text(S(0x6F36));   /* " vanishes!" */
            dg.combat_event = 2;
            o->tile = o->tile2 = 0x16;
            dissolve_tile(c->y, c->x, LMAP(c->y, c->x));
            combat_remove(-1 - slot);
            combat_check_pass_out();
            return xp;
        }
        combat_remove(-1 - slot);
    } else if (mf & 0x10) {
        for (int tries = 0; tries < 8; tries++) {
            random_near(c->y, c->x);
            unsigned nx = dg.dir_x, ny = dg.dir_y;
            if (!combat_tile_free(ny, nx, OBJ(c)->tile)) continue;
            int n = add_combatant(dg.z_level, (uint8_t)ny, (uint8_t)nx, 0, (OBJ(c)->tile - 0x40) >> 2);
            if (n < 0) continue;
            C(n)->hp = c->hp;
            print_text(MONSTER_NAME(type));
            print_text(S(0x6F42));   /* " divides!\n" */
            return 0;
        }
    }
    return xp;
}

/* A healthy member is poisoned; anyone else takes 0-20. */
void apply_poison_hit(int attacker, int defender)
{
    Combatant *d = C(defender);
    if ((d->flags & 0x80) && dg.party[d->kind & 0x0F].status == 'G') {
        dg.party[d->kind & 0x0F].status = 'P';
        print_text(dg.party[d->kind & 0x0F].name);
        print_text(S(0x6F4E));   /* " is poisoned!\n" */
        dg.combat_event = 8;
        dg.status_dirty = 1;
        return;
    }
    int xp = apply_damage((unsigned)random_upto(0x14), defender);
    if (attacker >= 0 && (C(attacker)->flags & 0x80)) add_word_capped(9999, (uint16_t)xp, &dg.party[C(attacker)->kind & 0x0F].xp);
}

/* Monsters flagged 0x204 poison 3 times in 4 instead of wounding; type 0x1C puts its victim
 * to sleep; magic 0x34 is sleep and 0x33 poison. Experience goes to the member who struck. */
void resolve_hit(int attacker, int defender)
{
    Combatant *a = C(attacker), *d = C(defender);
    int monster = !(a->flags & 0x80);
    if (monster && (MONSTER_FLAGS(a->kind) & 0x204) && random_between(3, 0) != 0) {
        apply_poison_hit(attacker, defender);
        return;
    }
    if (monster && a->kind == 0x1C && !(d->flags & 8)) {
        sleep_combatant((unsigned)defender);
        return;
    }
    if (dg.magic_attack != 0 && dg.attack_item == 0x34) {
        sleep_combatant((unsigned)defender);
        return;
    }
    if (dg.magic_attack != 0 && dg.attack_item == 0x33) {
        apply_poison_hit(attacker, defender);
        return;
    }
    int damage = calc_damage(defender, attacker);
    if (damage < 0 && (d->flags & 0x80)) {
        dg.combat_event = 0x20;
        return;
    }
    int xp = apply_damage((unsigned)damage, defender);
    if (a->flags & 0x80) add_word_capped(9999, (uint16_t)xp, &dg.party[a->kind & 0x0F].xp);
}

/* With q = a quarter of the type's hit points: below q the monster flees (1), below 2q it
 * flees 4 times in 256 (2), below 3q (3), else (4). */
int monster_morale(int slot)
{
    Combatant *c = C(slot);
    int level, flee = 0;
    if (c->flags & 0x80) return 0;   /* (the original returns a pointer here) */
    unsigned q = MONSTER_STAT(c->kind, 5) >> 2;
    if (c->hp < q) {
        level = 1;
        flee = 1;
    } else if (c->hp < q * 2) {
        level = 2;
        flee = random_upto(0x100) >= 0xFC;
    } else {
        level = c->hp < q * 3 ? 3 : 4;
    }
    if (flee) c->flags |= 2;
    else c->flags &= 0xFD;
    return level;
}

/* After each turn: lava and fire (tiles 0x8F, 0xBC, field 0xEA) burn for 0-10, swamp and the
 * poison field (0xE8) poison anyone whose tile is below 0x80, the sleep field (0xE9) sleeps. */
void apply_tile_hazard(unsigned slot)
{
    Combatant *c = C(slot);
    int hazard = 0;
    uint8_t t = LMAP(c->y, c->x);
    if (t == 0x8F || t == 0xBC) hazard = 100;
    if (t == 4) hazard = 0x32;
    if (hazard == 0)
        for (unsigned i = 0; i < 0x20 && hazard == 0; i++) {
            MapObject *o = &dg.objects[i];
            if (i == c->object || c->x != o->x || c->y != o->y) continue;
            if (o->tile == 0xEA) hazard = 100;
            if (o->tile == 0xE8) hazard = 0x32;
            if (o->tile == 0xE9) hazard = 0x96;
        }
    if (hazard == 0x32) {
        if (OBJ(c)->tile < 0x80) {
            apply_poison_hit(-1, (int)slot);
            flash_combatant(slot);
        }
    } else if (hazard == 100) {
        flash_combatant(slot);
        apply_damage((unsigned)random_upto(10), (int)slot);
        report_hit(0xFF, (int)slot);
        dg.status_dirty = 1;
    } else if (hazard == 0x96) {
        sleep_combatant(slot);
    }
}

void check_regurgitate(int slot)
{
    Combatant *c = C(slot);
    if ((unsigned)random_stat_roll() >= c->speed) return;
    print_text((c->flags & 0x80) ? dg.party[c->kind & 0x0F].name : MONSTER_NAME(c->kind));
    print_text(S(0x6F5E));   /* " regurgitated!\n" */
    sound_noise(600, 7000, 1);
    c->flags &= 0xFB;
    OBJ(c)->tile2 = OBJ(c)->tile;
}

/* ---- the combat half of SJOG.OVL ---- */

int count_members_where(unsigned item)
{
    int n = 0;
    for (unsigned m = 0; m < dg.party_size; m++)
        if (unequip_item(item, (int)m)) n++;
    return n;
}

void count_combat_sides(void)
{
    dg.dir_y = dg.dir_x = 0;
    for (int i = 0; i < 0x20; i++) {
        if (dg.combatants[i].flags == 0 || (dg.combatants[i].flags & 0x20)) continue;
        if (combatant_side(i) == 0) dg.dir_y++;
        else dg.dir_x++;
    }
}

/* exit_dir: 1-4 the side of the map (W E N S), 5 up, 6 down. In a dungeon room everyone must
 * use the same exit. Aboard ship nobody leaves. */
int combat_leave_map(unsigned exit_dir, int slot)
{
    if ((dg.transport & 0xF8) == 0x20) {
        print_text(S(0x8E76));   /* "\nStay with ship!\n" */
        return 0;
    }
    if (C(slot)->flags & 0x80) {
        if (dg.combat_exit == 0) {
            dg.combat_exit = (uint8_t)exit_dir;
        } else if (exit_dir != dg.combat_exit && (dg.combat_flags & 0x80)) {
            print_text(S(0x8E88));   /* "\nAll must use the same exit!\n" */
            beep(200, 0xA5);
            return 0;
        }
    }
    dg.combat_redraw = 1;
    count_combat_sides();
    print_text(S(dg.dir_x == 0 ? 0x8EA6 : 0x8EAE));   /* "Leave!\n" / "Escape!\n" */
    sound_slide(0x28, 1, 2000, 0x4B0);
    dg.active_player = 0xFF;
    combat_remove(-1 - slot);
    draw_status_bar();
    world_tick();
    return 1;
}

int combat_move_member(int dir, int slot)
{
    Combatant *c = C(slot);
    MapObject *o = OBJ(c);
    int dx = 0, dy = 0;
    if (dir == 1) { dx = -1; print_text(S(0x8ECE)); }        /* "West\n" */
    else if (dir == 2) { dx = 1; print_text(S(0x8EC8)); }    /* "East\n" */
    else if (dir == 3) { dy = -1; print_text(S(0x8EB8)); }   /* "North\n" */
    else if (dir == 4) { dy = 1; print_text(S(0x8EC0)); }    /* "South\n" */
    int x = c->x + dx, y = c->y + dy;
    if (!(x < 0x0B && y < 0x0B && x > -1 && y > -1)) return combat_leave_map((unsigned)dir, slot);
    if (!combat_tile_free((unsigned)y, (unsigned)x, o->tile)) {
        print_text(S(0x8ED4));   /* "Blocked!\n" */
        beep(200, 0xA5);
        flush_keyboard();
        return 0;
    }
    c->x = o->x = (uint8_t)x;
    c->y = o->y = (uint8_t)y;
    step_sound();
    if (dg.combat_flags & 0x82) combat_room_trigger((unsigned)y, (unsigned)x);
    return 1;
}

/* Ladders leave the map (up 5, down 6; a grate goes down in dungeon rooms); otherwise a
 * direction is asked and a fence (0x4C) with nothing on it can be climbed onto. */
int combat_klimb(void)
{
    print_text(S(0x8EDE));   /* "Klimb-" */
    uint8_t t = *get_map_tile_ptr(dg.party_y, dg.party_x);
    if ((dg.combat_flags & 2) && t == 0xC8 && dg.on_ladder != 0) {
        print_text(S(0x8EE6));   /* "U/D-" */
        for (;;) {
            int key = get_command_key();
            if (game_quit()) return 0;
            if (key == 3 || key == 'U') break;
            if (key == 4 || key == 'D') {
                t = 0xC9;
                break;
            }
        }
    }
    if (t == 0xC8) {
        print_text(S(0x8EEC));   /* "Up!\n" */
        return combat_leave_map(5, dg.combat_turn);
    }
    if ((t == 0x86 && (dg.combat_flags & 0x80)) || t == 0xC9) {
        print_text(S(0x8EF2));   /* "Down!\n" */
        return combat_leave_map(6, dg.combat_turn);
    }
    if (ask_direction() != 0) {
        unsigned x = (uint16_t)(dg.party_x + dg.dir_x), y = (uint16_t)(dg.party_y + dg.dir_y);
        if (*get_map_tile_ptr(y, x) != 0x4C || object_tile_at(0, y, x) != 0) {
            print_text(S(0x8EFA));   /* "What?\n" */
            return 0;
        }
        Combatant *c = C(dg.combat_turn);
        c->x = OBJ(c)->x = (uint8_t)x;
        c->y = OBJ(c)->y = (uint8_t)y;
        combat_room_trigger(y, x);
    }
    return 1;
}

/* In the last battle: whoever stands in row 2 on one of the 0x3C tiles is taken (exit 'M'). */
void check_absorbed(void)
{
    Combatant *c = C(dg.combat_turn);
    if (c->flags == 0 || (c->flags & 0x20) || c->y != 2 || (dg.view_tiles[0x10 + (c->x & 0x0F)] & 0xFC) != 0x3C) return;
    dg.combat_exit = 0x4D;
    put_char('\n');
    print_combatant_name(dg.combat_turn);
    print_text(S(0x8F02));   /* " is absorbed!\n" */
    sound_slide(0x28, 1, 2000, 0x4B0);
    dg.active_player = 0xFF;
    draw_status_bar();
    combat_remove(-1 - dg.combat_turn);
    world_tick();
}

int combat_cant_do(int kind, const char *prompt)
{
    print_text(prompt);
    if (kind == 1) print_text(S(0x8F12));        /* " what?" */
    else if (kind == 2) print_text(S(0x8F1A));   /* "-Not here" */
    else if (kind == 3) print_text(S(0x8F24));   /* "-Funny, no response!" */
    put_char('\n');
    beep(0x96, 0xDC);
    beep(0x96, 0x96);
    return 1;
}

int combat_set_active_player(unsigned member)
{
    print_text(S(0x8F3A));   /* "Set active plr:\n" */
    for (int slot = 0; slot < 0x20; slot++) {
        Combatant *c = C(slot);
        if (!(c->flags & 0x80) || combatant_side(slot) != 0 || c->kind != member) continue;
        if (c->flags & 0x2C) break;
        dg.active_player = (uint8_t)member;
        print_combatant_name(slot);
        put_char('\n');
        draw_status_bar();
        return 1;
    }
    print_text(S(0x8F4C));   /* "Invalid!\n" */
    return 0;
}

void combat_end_of_turn(void)
{
    apply_ring_effects(dg.combat_turn);
    decay_combat_fields();
    if (dg.active_spell_turns != 0 && dg.active_spell_turns != 0xFF && --dg.active_spell_turns == 0) {
        dg.active_spell = 0;
        dg.status_dirty = 1;
    }
}

/* After a fight with a map object: the object is gone whatever happened - except a pirate ship
 * that was beaten, which becomes a ship to board (tile - 8, hull 99, 2 skiffs). */
void combat_cleanup(int object)
{
    if (dg.combat_exit == 0x4D) rescue_lord_british();
    memcpy(dg.objects, dg.objects_backup, 0x100);
    if (dg.combat_flags & 0x82) return;
    if (object < 0x20) {
        MapObject *o = &dg.objects[object & 0x1F];
        if (dg.combat_victory == 0 || (o->tile & 0xFC) != 0x2C) {
            o->z = o->y = o->x = o->tile2 = o->tile = 0;
        } else {
            o->tile -= 8;
            o->tile2 -= 8;
            o->hull = 99;
            o->skiffs = 2;
        }
    }
    put_char('\n');
    dg.location = dg.prev_location;
}

int combat_is_blocked(int slot, unsigned y, unsigned x)
{
    int ix = (int16_t)x, iy = (int16_t)y;
    if (ix < 0 || ix > 10 || iy < 0 || iy > 10) return (C(slot)->flags & 2) ? 0 : 1;
    return 1 - combat_tile_free(y, x, OBJ(C(slot))->tile);
}

int combat_is_surrounded(int slot)
{
    unsigned x = C(slot)->x, y = C(slot)->y;
    int blocked = combat_is_blocked(slot, y + 1, x);
    blocked += combat_is_blocked(slot, y, x + 1);
    blocked += combat_is_blocked(slot, y, (uint16_t)(x - 1));
    blocked += combat_is_blocked(slot, (uint16_t)(y - 1), x);
    return blocked == 4;
}

/* A possessed member (flags 1) among slots 0-5 passes out: the possessing item 0x23 drops and
 * the member sleeps. Returns the slot, or -1 if there is none. */
int combat_check_pass_out(void)
{
    for (int slot = 0; slot < 6; slot++) {
        Combatant *c = C(slot);
        if (!(c->flags & 1) || !(c->flags & 0x80)) continue;
        c->flags &= 0xFE;
        print_text(dg.party[c->kind & 0x0F].name);
        print_text(S(0x8F56));   /* " passes out!" */
        sound_sweep(2, 1000, 30000, 1, 0xC1C);
        unequip_item(0x23, c->kind);
        sleep_combatant((unsigned)slot);
        return slot;
    }
    return -1;
}
