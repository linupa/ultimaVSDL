/* combat.cpp - combat core, COMBAT.OVL and the combat half of SJOG.OVL. See combat.h. */
#include <string.h>
#include "cmds.h"
#include "combat.h"
#include "commands.h"
#include "comsubs.h"
#include "dungeon.h"
#include "endings.h"
#include "game.h"
#include "gamedata.h"
#include "gametime.h"
#include "items.h"
#include "map.h"
#include "magic.h"
#include "gfx.h"
#include "input.h"
#include "misc.h"
#include "monsters.h"
#include "objects.h"
#include "party.h"
#include "picture.h"
#include "savegame.h"
#include "sjog.h"
#include "sound.h"
#include "stats.h"
#include "status.h"
#include "strings.h"
#include "text.h"
#include "textbuf.h"
#include "tiles.h"
#include "view.h"
#include "world.h"

#define C(slot) (&combat.combatants[(slot) & 0x1F])

Combat::Combat()
{
    SaveGame &save = SaveGame::instance();   /* not `savegame`: this runs before main, in no set order */
    save.add(0x5882, minute_ctr, "combat.minute_ctr");
    save.add(0x588F, cast_circle, "combat.cast_circle");
    save.add(0x5890, magic_attack, "combat.magic_attack");
    save.add(0x5894, prev_location, "combat.prev_location");
    save.add(0x5899, aim_x, "combat.aim_x");
    save.add(0x589A, aim_y, "combat.aim_y");
    save.add(0x589D, attack_item, "combat.attack_item");
    save.add(0x589E, turn, "combat.turn");
    save.add(0x589F, redraw, "combat.redraw");
    save.add(0x58A0, exit_dir, "combat.exit_dir");
    save.add(0x58A1, flags, "combat.flags");
    save.add(0x58A2, event, "combat.event");
    save.add(0x58A3, victory, "combat.victory");
    save.add(0x58A8, last_attacker, "combat.last_attacker");

    Data &data = Data::instance();   /* not a global: this runs before main, in no set order */
    data.add(0x2188, missile_shapes, "combat.missile_shapes", Data::SHARED);
}

Combat &Combat::instance()
{
    static Combat the_combat;
    return the_combat;
}

Combat &combat = Combat::instance();

int8_t Combat::missile_shapes[846];   /* ds:2188 */
#define OBJ(c) (&map.objects[(c)->object & 0x1F])
#define LMAP(y, x) view.local_map[(((y) & 0xFF) * 0x20 + ((x) & 0xFF)) & 0x3FF]

/* ---- the main program's part ---- */

int Combat::combatant_side(int slot)
{
    Combatant *c = C(slot);
    if (c->flags & 0x20) return 0;
    if (!(c->flags & 0x80)) return (c->flags & 1) == 0;
    /* a member whose name has 'j' as its fifth letter (Saduj) is always an enemy */
    if (c->kind == 0 || party[c->kind & 0x0F].name[4] != 'j') return c->flags & 1;
    return 1;
}

void Combat::flash_combatant(unsigned slot)
{
    Combatant *c = NULL;
    if (map.in_scene()) {
        c = C(slot);
        slot = c->object;
    }
    draw_tile_at(map.objects[slot & 0x1F].y, map.objects[slot & 0x1F].x, 0);
    if (!map.in_scene() || !(c->flags & 0x80)) {
        sound_noise(2000, 3000, 10);
    } else {
        highlight_member_row(c->kind);
        sound_noise(500, 3000, 0x28);
        highlight_member_row(c->kind);
    }
    world_tick();
}

void Combat::victory_fanfare(void)
{
    for (int i = 3; i != 0; i--) sound_sweep(6, 300, 0x2A30, 1, 0x11F8);
    sound_sweep(3, 300, 0x5460, 1, 0x17D4);
}

int Combat::in_combat_bounds(int y, int x) { return !(x < 0 || x > 10 || y < 0 || y > 10); }

/* The original sums a table value for each of the six equipment slots, but under a test that is
 * never true (unsigned item > 0xFFFF), so only the Protection spell counts. In Doom (0x28)
 * without item 0x0F as body armour it is 0. Armour proper is party[].armour (calc_damage). */
int Combat::get_armour_bonus(int member)
{
    if (combat.prev_location == 0x28 && party[member].body != 0x0F) return 0;
    return party.active_spell == 0x50 ? 3 : 0;
}

/* Takes the item out of whichever slot holds it; 1 if it was worn. */
int Combat::unequip_item(unsigned item, int member)
{
    PartyMember *m = &party[member & 0x0F];
    int found = 1;
    if (item == m->helm) m->helm = 0xFF;
    else if (item == m->body) m->body = 0xFF;
    else if (item == m->left_hand) m->left_hand = 0xFF;
    else if (item == m->right_hand) m->right_hand = 0xFF;
    else if (item == m->ring) {
        m->ring = 0xFF;
        if (item == 0x2A && map.in_scene() && combat.turn < 0x20) C(combat.turn)->flags &= 0xEF;   /* visible again */
    } else if (item == m->amulet) m->amulet = 0xFF;
    else found = 0;
    get_armour_bonus(member);
    return found;
}

/* Monsters go in slots 6 and up, the party from 0. An object (kind 2: a field, a chest) gets no
 * slot and the object index is returned instead. When a slot number is reused, objects that
 * remembered it as their last target forget it. */
int Combat::add_combatant(uint8_t z, uint8_t y, uint8_t x, int kind, int type)
{
    int slot = -1;
    Combatant *c = NULL;
    if (kind != 2) {
        for (int i = kind == 0 ? 6 : 0; i < 0x20; i++) {
            if (combat.combatants[i].flags != 0) continue;
            c = &combat.combatants[i];
            if (kind == 1 && !party[type].dead()) {
                c->speed = party[type].dex;
                c->timer = (uint8_t)(0x24 - c->speed);
                c->flags = 0x80;
                if (!party[type].good() && !party[type].poisoned()) c->flags |= 8;
            }
            if (kind == 0) {
                c->hp = monsters.hp(type);
                c->speed = (uint8_t)(random_upto(7) - 4 + monsters.speed(type));
                if (c->speed > 0x1E) c->speed = monsters.speed(type);
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
            MapObject *ob = &map.objects[o];
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
            if (OBJ(&combat.combatants[i])->skiffs == slot) OBJ(&combat.combatants[i])->skiffs = 0xFF;
    return slot;
}

/* A ring of invisibility (0x2A) hides its wearer (tile 0x1D); a ring of regeneration (0x2C) heals. */
void Combat::apply_ring_effects(int slot)
{
    Combatant *c = C(slot);
    if (!(c->flags & 0x80) || (c->flags & 0x28)) return;
    if (party[c->kind & 0x0F].ring == 0x2A) {
        OBJ(c)->tile2 = 0x1D;
        c->flags |= 0x10;
    } else if (party[c->kind & 0x0F].ring == 0x2C) {
        regen_ring_tick();
    }
}

void Combat::wake_combatant(int slot)
{
    Combatant *c = C(slot);
    if (c->flags & 8) {
        if (!(c->flags & 0x80)) {
            OBJ(c)->flags = 0;
        } else {
            party[c->kind & 0x0F].status = 'G';
            OBJ(c)->tile2 = (c->flags & 0x10) ? 0x1D : OBJ(c)->tile;
        }
        c->flags &= 0xF7;
    }
    draw_status_bar();
}

void Combat::sleep_unless_poisoned(unsigned slot)
{
    Combatant *c = C(slot);
    if (!(c->flags & 0x80) || !party[c->kind & 0x0F].poisoned()) sleep_combatant(slot);
}

void Combat::sleep_combatant(unsigned slot)
{
    Combatant *c = C(slot);
    if (!(c->flags & 0x80)) {
        c->flags |= 8;
        combat.event = 4;
        OBJ(c)->flags = 0xFF;   /* stops its animation */
    } else {
        PartyMember *m = &party[c->kind & 0x0F];
        if (m->dead()) return;
        m->status = 'S';
        c->flags |= 8;
        OBJ(c)->tile2 = 0x1E;
        if (slot == party.active_player) party.active_player = 0xFF;
        combat.event = 4;
        draw_status_bar();
        if (combat.flags & 4) return;
    }
    world_tick();
}

/* Empties the objects and combatants and puts the living party on the map's start squares, as
 * mages (0x40), bards (0x44), fighters (0x48) or the Avatar (0x4C). Each ring of invisibility or
 * regeneration worn has 1 chance in 16 to vanish. A 0xDC in the middle of the map (dungeon
 * rooms) becomes a chest of level 3 x dungeon level + 7. */
void Combat::clear_combat_tables(void)
{
    for (int i = 0; i < 0x20; i++) {
        MapObject *o = &map.objects[i];
        o->flags = o->x = o->tile2 = o->tile = o->hull = o->z = o->y = 0;
        memset(&combat.combatants[i], 0, sizeof combat.combatants[i]);
    }
    for (unsigned m = 0; m < party.size(); m++) {
        PartyMember *pm = &party[m];
        if (pm->dead()) continue;
        if (pm->ring == 0x2A || pm->ring == 0x2C) {
            uint8_t ring = pm->ring;
            if (random_between(0x0F, 0) == 0x0B) {
                print_text(strings[Strings::A_ring_has_vanished_A422]);   /* "A ring has vanished!\n" */
                sound_slide(0x28, 1, 2000, 0x4B0);
                unequip_item(ring, (int)m);
            }
        }
        int slot = add_combatant(map.z, combat.party_start_y[m], combat.party_start_x[m], 1, (int)m);
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
        if (pm->asleep()) sleep_combatant((unsigned)slot);
        else apply_ring_effects(slot);
    }
    if (view.local_map[0xA5] == 0xDC) {
        int o = add_combatant(map.z, 5, 5, 2, 1);
        map.objects[o & 0x1F].hull = (uint8_t)(map.z * 3 + 7);
        view.local_map[0xA5] = dungeon.corridor_floor_tile;
    }
}

/* monster + 0x100 (the attacked object's `hull` was 0x80 or more) means the type's full group
 * even in a town, where otherwise one fights alone (except type 0x0C). The group is 1 to the
 * type's size (byte 6), rolled twice while no_ambush is set; sizes 1, 8 and 16 are exact. Among
 * the first quarter 1 in 9 is the type's companion (ds:16D4). The start squares come from the
 * combat map, shuffled for an ambush (flags 4). The original reads past its 16 start squares for
 * bigger groups; the port stops at 16. */
void Combat::place_monster_group(unsigned monster, unsigned flags)
{
    int alone = (int)monster < 0x100;
    if (!alone) monster -= 0x100;
    combat.exit_dir = 0;
    if (!(flags & 4)) clear_combat_tables();
    int order[16];
    for (int i = 0; i < 0x10; i++) order[i] = i;
    if (flags & 4)
        for (int i = 0; i < 0x10; i++) {
            int j = random_between(0x0F, 0), t = order[i];
            order[i] = order[j];
            order[j] = t;
        }
    print_text(strings[Strings::CONFLICT_A438]);   /* "*** CONFLICT ***\n" */
    int count;
    if (combat.prev_location == 0 || combat.prev_location > 0x20 || monster == 0x0C || !alone) count = monsters.group_size(monster);
    else count = 1;
    if (count != 8 && count != 0x10 && count != 1) {
        count = random_between(count, 1);
        if (game_time.no_ambush != 0) count = random_between(count, 1);
        world_tick();
        if (count > 0x19) count = 0x1A;
    }
    add_combatant(map.z, combat.monster_start_y[order[0]], combat.monster_start_x[order[0]], 0, (int)monster);
    for (int i = 1; i < count && i < 0x10; i++) {
        unsigned type = monster;
        if (i < count / 4 + 1 && random_upto(8) == 0) type = monsters.companion((uint8_t)monster);
        add_combatant(map.z, combat.monster_start_y[order[i]], combat.monster_start_x[order[i]], 0, (int)type);
    }
}

/* Map n of BRIT.CBT: eleven 32-byte rows. The columns right of the 11 x 11 map hold the start
 * squares: the party's x at 0x6B and y at 0x71 (row 3), the monsters' x at 0xCB (row 6) and y at
 * 0xEB (row 7), 16 each. */
void Combat::load_combat_map(int map_no)
{
    load_file_part((uint32_t)map_no * 0x160, 0x160, view.local_map, strings[Strings::BRIT_CBT_A3F0]);   /* "BRIT.CBT" */
    memcpy(combat.party_start_x, view.local_map + 0x6B, 6);
    memcpy(combat.party_start_y, view.local_map + 0x71, 6);
    memcpy(combat.monster_start_x, view.local_map + 0xCB, 16);
    memcpy(combat.monster_start_y, view.local_map + 0xEB, 16);
}

/* flags 0: a fight with the map object `object`; 4: a camp (arg = hours, object = the guard);
 * 2: a dungeon corridor (the map was built by setup_corridor_combat). The map's objects are put
 * aside for the battle. */
void Combat::run_combat(int arg, int object, unsigned flags)
{
    uint8_t x = map.x, y = map.y, z = map.z, active = party.active_player;
    combat.flags = (uint8_t)flags;
    combat.prev_location = map.id();
    map.set(Map::SCENE);
    memcpy(map.objects_backup, map.objects.bytes(), 0x100);
    int fight = 1;
    if (flags == 0) {
        MapObject *o = &map.objects[object & 0x1F];
        unsigned monster = (o->tile & 0xFC) == 0x2C ? 1 : (unsigned)(o->tile - 0x40) >> 2;
        if (o->hull > 0x7F) monster += 0x100;
        place_monster_group(monster, 1);
    } else if (flags & 4) {
        fight = camp_rest(arg, (unsigned)object, flags);
    } else if (flags & 2) {
        dungeon.place_combatants(2, 0);
    } else {
        fight = 0;
    }
    if (fight) {
        party.active_player = 0xFF;
        combat.turn = 0xFF;
        combat.victory = 0;
        combat_main_loop();
    }
    if (combat.on_ladder != 0) {
        combat.on_ladder = 0;
        ladder_tiles_patch(1);
    }
    map.set(Map::Id(combat.prev_location));
    view.rebuild = true;
    map.z = z;
    map.x = x;
    map.y = y;
    draw_status_bar();
    /* with no active player the original tests "member 255", bytes far past the party table;
     * here that is Party's spare entry, which is neither dead nor asleep */
    char st = party[active].status;
    party.active_player = (st == 'D' || st == 'S') ? 0xFF : active;
    memcpy(map.objects.bytes(), map.objects_backup, 0x100);
}

/* The combat map comes from the terrain under the enemy: 1 swamp, 2 grass, 3 scrub and desert,
 * 4 forest, 5 hills, 6 mountains, 7 bridges and such, 8 floors and anything in a town, 10 a
 * Shadowlord, 11-14 aboard ship (against sea monsters, from the shore, against land, against
 * pirates), 12 pirates from land, 15 on water. */
void Combat::combat_with_object(int object)
{
    MapObject *o = &map.objects[object & 0x1F];
    unsigned kind = o->tile & 0xFC;
    put_char('\n');
    put_char(0xFC);
    print_text(kind < 0x40 ? strings[Strings::PIRATES_A3FA] : monsters.group_name((unsigned)(kind - 0x40) >> 2));   /* "PIRATES" or the type's name */
    put_char(0xFB);
    print_text(strings[Strings::NL_NL_A402]);   /* "\n\n" */
    uint8_t t = *map.get_map_tile_ptr(o->y, o->x);
    int water = t < 4 || ((t & 0xFE) != 0x6A && (t & 0xF0) == 0x60);
    int other = map.is_outdoors() ? 2 : 8, map_no;
    if (kind == 0xFC) {
        map_no = 10;
        if (party.lb_sceptre != 0) {
            print_text(strings[Strings::The_Sceptre_is_reclaimed_A406]);   /* "The Sceptre is reclaimed!\n" */
            sound_sweep(1, 1, -0x218, 1, 0xFD2);
            party.lb_sceptre = 0;
        }
    } else {
        if ((kind & 0xF0) == 0x80) water = 1;   /* sea monsters */
        if ((party.transport & 0xF8) == 0x20) map_no = kind == 0x2C ? 0x0E : water ? 0x0B : 0x0D;
        else if (kind == 0x2C) map_no = 0x0C;
        else if (water) map_no = 0x0F;
        else if (t == 4) map_no = 1;
        else if (t == 5) map_no = 2;
        else if (t == 6 || t == 8) map_no = 3;
        else if (t == 7 || t == 0x1E || t == 0x1F) map_no = 4;
        else if (t == 9 || t == 0x0A) map_no = 5;
        else if (t >= 0x0B && t <= 0x0F) map_no = 6;
        else if (t == 0x1D || t == 0x48 || t == 0x49 || t == 0x6A || t == 0x6B) map_no = 7;
        else if (t == 0x44) map_no = 8;
        else map_no = other;
    }
    load_combat_map(map_no);
    run_combat(0, object, 0);
    combat_cleanup(object);
    update_light_map();
    pass_time(0);
}

void Combat::camp_ambush_combat(int hours, int guard)
{
    load_combat_map(0);
    run_combat(hours, guard, 4);
    update_light_map();
}

/* ---- COMBAT.OVL ---- */

/* Off the map counts as free. Fields (0xE8-0xEA), bodies and invisible things don't block;
 * the energy field 0xEB does. */
int Combat::combat_tile_free(unsigned y, unsigned x, uint8_t mover)
{
    int ix = (int16_t)x, iy = (int16_t)y;
    if (ix >= 0x0B || iy >= 0x0B || ix < 0 || iy < 0) return 1;
    uint8_t t = *map.get_map_tile_ptr(y, x);
    if (!tile_passable(t, mover) || t == 0xFF) return 0;
    for (int i = 0; i < 0x20; i++) {
        MapObject *o = &map.objects[i];
        Combatant *c = &combat.combatants[i];
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
int Combat::monster_ranged_attack(int force_miss, int target, int attacker)
{
    unsigned type = C(attacker)->kind;
    uint8_t missile = monsters.missile(type);
    if (!(type == 0x1A || random_between(0xFF, 0) < 0x80)) return 0;
    if ((monsters.flags(type) & 0x8000) && (party.active_spell == 0x4E || party.active_spell == 0x1C)) return 0;
    sound_slide(0x96, 5, 400, 0x2EE);
    int hit = force_miss ? 0 : roll_to_hit(0, -(int)combat.cast_circle, attacker, target);
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
int Combat::monster_attack(int slot)
{
    Combatant *c = C(slot);
    unsigned type = c->kind;
    int force_miss = 0;
    int target = find_nearest_enemy(slot);
    if (target < 0) return 0;
    if (c->flags & 1) {
        if (combatant_distance(target, slot) != 1) return 0;
        combat.attack_item = 0x21;
        melee_attack(0x21, (unsigned)target, slot);
        return 1;
    }
    Combatant *tc = C(target);
    if ((tc->flags & 0x80) && party[tc->kind & 0x0F].amulet == 0x2D && (monsters.flags(type) & 0x8000))
        force_miss = random_between(0xFF, 0) < 0x80;
    if ((unsigned)combatant_distance(target, slot) > monsters.attack_range(type)) return 0;
    if (type == 0x1A) OBJ(c)->flags = 0x20;
    if (combatant_distance(target, slot) != 1) return monster_ranged_attack(force_miss, target, slot);
    if (type == 0x2D) OBJ(c)->flags = 0x20;
    sound_slide(0x96, 5, 400, 0x2EE);
    combat.last_attacker[target & 0x1F] = (uint8_t)slot;
    if (roll_to_hit(0, -(int)combat.cast_circle, slot, target) == 0) return 1;
    if ((monsters.flags(type) & 2) && random_between(3, 0) != 0 && party.food != 0) {
        print_text(strings[Strings::A_6D74]);   /* "\nA " */
        print_combatant_name(slot);
        print_text(strings[Strings::stole_some_food_6D78]);   /* " stole some food!\n" */
        sub_word_floor0(5, &party.food);
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
void Combat::monster_turn(void)
{
    int slot = combat.turn;
    Combatant *c = C(slot);
    combat.attack_item = 0;
    if (party.active_spell == 0x54) return;
    if (party.active_spell == 0x51 && random_between(1, 0) == 0) return;
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
    if (combat.event & 0x10) {
        put_char('\n');
        sound_slide(0x28, 1, 2000, 0x4B0);
        print_combatant_name(slot);
        print_text(strings[Strings::escapes_6D8C]);   /* " escapes!\n" */
        combat_remove(-1 - slot);
        if (c->kind == 0x2F) combat_check_pass_out();   /* (the flags were just cleared, so only the type counts) */
    }
    world_tick();
}

int Combat::combat_item_command(int which, const char *prompt)
{
    print_text(prompt);
    if (!(C(combat.turn)->flags & 0x80)) {
        print_text(strings[Strings::Can_t_6D98]);   /* "Can't!\n" */
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
int Combat::append_weapon_name(int count, int item)
{
    if (item == 0xFF || items.damage(item) == 0) return 0;
    char *buf = (char *)text_buffer.data;
    if (count != 0) strcat(buf, strings[Strings::Comma_SP_6DA0]);   /* ", " */
    strcat(buf, items.name(item));
    return 1;
}

/* A turn of anyone on the party's side: a member, or a monster that changed sides. With an
 * active player set, everyone else's turn is skipped. A member holding item 0x23 is possessed
 * and acts as a monster. Keys that aren't a turn ("What?", a blocked move, the sound toggle) ask
 * again; commands that fail print the name line again first. */
void Combat::party_member_turn(void)
{
    int slot = combat.turn;
    Combatant *c = C(slot);
    unsigned member = c->kind, key = 0;
    map.x = c->x;
    map.y = c->y;
    if (party.active_player != 0xFF && (!(c->flags & 0x80) || member != party.active_player)) {
        combat_end_of_turn();
        return;
    }
    PartyMember *pm = &party[member & 0x0F];
    if ((c->flags & 0x80) && (pm->left_hand == 0x23 || pm->right_hand == 0x23)) {
        c->flags |= 1;
        party.active_player = 0xFF;
        status.dirty = true;
        monster_turn();
    } else {
        int again, weapons = 0;
        if (c->flags & 0x80) highlight_member_row((int)c->kind);
        status.refresh();
        do {
            put_char('\n');
            print_combatant_name(slot);
            text_buffer.data[0] = 0;
            if (c->flags & 0x80) {
                print_text(strings[Strings::armed_with_6DA4]);   /* ", armed with " */
                weapons = append_weapon_name(0, pm->helm);
                weapons += append_weapon_name(weapons, pm->left_hand);
                weapons += append_weapon_name(weapons, pm->right_hand);
                if (weapons == 0) strcat((char *)text_buffer.data, strings[Strings::bare_hands_6DB2]);   /* "bare hands" */
            }
            strcat((char *)text_buffer.data, strings[Strings::Colon_6DBE]);   /* ":" */
            print_text((char *)text_buffer.data);
            again = 0;
            for (int done = 0; !done; ) {
                put_char('\n');
                draw_label_left();
                done = 1;
                if (c->flags & 4) {
                    print_text(strings[Strings::ARGH_6DC0]);   /* "ARGH!\n" */
                    sound_noise(500, 3000, 0x28);
                    check_regurgitate(slot);
                    continue;
                }
                if (c->flags & 8) {
                    if (random_between(0xFF, 0) < 0x10) wake_combatant(slot);
                    print_text(strings[Strings::Zzzzz_6DC8]);   /* "Zzzzz...\n" */
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
                    print_text(strings[Strings::Sound_6DE4]);   /* "Sound " */
                    print_text(strings[sound.enabled == 0 ? Strings::On_6DF2 : Strings::Off_6DEC]);   /* "On\n" / "Off\n" */
                    sound.enabled = sound.enabled == 0;
                    done = 0;
                    break;
                case 0x1B: again = combat_escape(); break;
                case ' ': print_text(strings[Strings::Pass_6E60]); break;   /* "Pass\n" */
                case '0':
                    party.active_player = 0xFF;
                    print_text(strings[Strings::Set_active_plr_None_6E66]);   /* "Set active plr:\nNone!\n" */
                    draw_status_bar();
                    break;
                case '1': case '2': case '3': case '4': case '5': case '6':
                    if (combat_set_active_player(key - '1') == 0) again = 1;
                    break;
                case 'A': cmd_attack_combat(weapons, slot); break;
                case 'B': again = combat_cant_do(1, strings[Strings::Board_6E7E]); break;   /* "Board" */
                case 'C':
                    print_text(strings[Strings::Cast_6DF6]);   /* "Cast...\n" */
                    again = 1;
                    if (!(c->flags & 0x80)) {
                        print_text(strings[Strings::Can_t_6E0C]);   /* "Can't!\n" */
                    } else if (check_interference(slot) == 0) {
                        combat.magic_attack = 1;
                        combat.cast_circle = 1;
                        again = 0;
                        if (party.active_spell == 0x4E || (party.lb_crown == 0 && combat.prev_location == 0x12)) {
                            print_text(strings[Strings::Absorbed_6E00]);   /* "Absorbed!\n" (Negate Magic, or Blackthorn's castle without the Crown) */
                            sound_sweep(2, 1000, 28000, 1, 0x2648);
                        } else {
                            cmd_cast();
                        }
                    }
                    break;
                case 'D': print_text(strings[Strings::D_What_6E84]); again = 1; break;        /* "D-What?\n" */
                case 'E': again = combat_cant_do(2, strings[Strings::Enter_6E8E]); break;    /* "Enter" */
                case 'F': again = combat_cant_do(2, strings[Strings::Fire_6E94]); break;    /* "Fire" */
                case 'G': again = combat_item_command(0, strings[Strings::Get_6E14]); break;   /* "Get-" */
                case 'H': again = combat_cant_do(2, strings[Strings::Hole_up_6E9A]); break;    /* "Hole up" */
                case 'I': again = combat_cant_do(2, strings[Strings::Ignite_torch_6EA2]); break;    /* "Ignite torch" */
                case 'J': again = combat_item_command(1, strings[Strings::Jimmy_6E1A]); break;   /* "Jimmy-" */
                case 'K':
                    if (combat_klimb() == 0) done = 0;
                    break;
                case 'L': again = combat_cant_do(2, strings[Strings::Look_6EB0]); break;    /* "Look" */
                case 'M': again = combat_cant_do(2, strings[Strings::Mix_6EB6]); break;    /* "Mix" */
                case 'N': again = combat_cant_do(2, strings[Strings::New_order_6EBA]); break;    /* "New order" */
                case 'O': again = combat_item_command(2, strings[Strings::Open_6E22]); break;   /* "Open-" */
                case 'P':
                    print_text(strings[Strings::Push_6E28]);   /* "Push-" */
                    cmd_push();
                    break;
                case 'Q': again = combat_cant_do(2, strings[Strings::Quit_6EC4]); break;    /* "Quit" */
                case 'R': again = combat_item_command(3, strings[Strings::Ready_6E2E]); break;   /* "Ready...\n\n" */
                case 'S': again = combat_item_command(4, strings[Strings::Search_6E3A]); break;   /* "Search-" */
                case 'T': again = combat_cant_do(3, strings[Strings::Talk_6ECA]); break;    /* "Talk" */
                case 'U': again = combat_item_command(5, strings[Strings::Use_item_6E42]); break;   /* "Use item\n\n" */
                case 'V': again = combat_cant_do(2, strings[Strings::View_6ED0]); break;    /* "View" */
                case 'W': print_text(strings[Strings::W_What_6ED6]); again = 1; break;        /* "W-What?\n" */
                case 'X': again = combat_cant_do(1, strings[Strings::X_it_6EE0]); break;    /* "X-it" */
                case 'Y':
                    print_text(strings[Strings::Yell_6E4E]);   /* "Yell " */
                    cmd_yell();
                    break;
                case 'Z':
                    print_text(strings[Strings::Z_stats_6E54]);   /* "Z-stats...\n" */
                    cmd_zstats();
                    break;
                case 0xFC:
                    print_text(strings[Strings::Buffer_O_6DD2]);   /* "Buffer O" */
                    input.typeahead_on = input.typeahead_on == 0;
                    print_text(strings[input.typeahead_on == 0 ? Strings::n_6DE0 : Strings::ff_6DDC]);
                    done = 0;
                    break;
                default:
                    print_text(strings[Strings::What_6EE6]);   /* "What?\n" */
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
int Combat::combat_main_loop(void)
{
    int result = 1, over = 0;
    combat.exit_dir = 0;
    world_tick();
    draw_status_bar();
    flush_keyboard();
    combat.victory = count_combat_sides().others == 0;
    while (!over) {
        for (combat.turn = 0; combat.turn < 0x20; combat.turn++) {
            if (game_quit()) {
                over = 1;
                break;
            }
            map.open_door_tile = 0;
            Combatant *c = C(combat.turn);
            if ((c->flags & 0xC0) == 0 || (c->flags & 0x20)) continue;
            if ((c->flags & 0x80) && party[c->kind & 0x0F].dead()) {   /* died outside a blow: poison, a trap */
                c->flags |= 0x20;
                flash_combatant(combat.turn);
                apply_damage(99, combat.turn);
                continue;
            }
            if ((LMAP(c->y, c->x) & 0xFE) == 0x84) continue;
            if (--c->timer != 0) continue;
            c->timer = (uint8_t)(0x24 - c->speed);
            view.aim_mode = 0;
            combat.event = 0;
            combat.cast_circle = 0;
            combat.magic_attack = 0;
            combat.attack_item = 0;
            if (++combat.minute_ctr == 10) {
                combat.minute_ctr = 0;
                pass_time(1);
            }
            combat.redraw = 1;
            if (combatant_side(combat.turn) == 0) party_member_turn();
            else monster_turn();
            combat.last_attacker[combat.turn & 0x1F] = 0xFF;
            apply_tile_hazard(combat.turn);
            Sides sides = count_combat_sides();
            if (sides.party == 0) {
                if (sides.others == 0) {
                    result = 0;
                } else {
                    if (combat_check_pass_out() != -1) continue;   /* a possessed member comes to: not over yet */
                    world_tick();
                    if (combat.victory == 0) {
                        print_text(strings[Strings::BATTLE_IS_LOST_6EEE]);   /* "\nBATTLE IS LOST!" */
                        result = 1;
                    }
                }
                over = 1;
                break;
            }
            if (sides.others == 0 && combat.victory == 0) {
                print_text(strings[Strings::VICTORY_6F00]);   /* "\nVICTORY!\n" */
                combat.victory = 1;
                victory_fanfare();
                flush_keyboard();
            }
        }
    }
    flush_keyboard();
    map.beam_frame = 0xFF;
    return result;
}

/* The nearest living fighter of the other side that isn't swallowed and can be seen (invisible
 * ones only in Doom, or by type 0x2F). Under the 'C' effect a failed intelligence roll makes
 * the monster take its own side for the enemy. With nobody to fight the monsters all turn to
 * flee. *toward gets the step in its direction (away, when fleeing). */
int Combat::find_nearest_enemy(int slot, Step *toward)
{
    Combatant *c = C(slot);
    int party_seen = -1, best = -1, best_dist = 99;
    unsigned tx = 5, ty = 5;   /* (uninitialised in the original when only a passed-out member is found) */
    int side = combatant_side(slot);
    if (party.active_spell == 0x43 && get_combatant_stat(-1, slot) < random_stat_roll()) side = 0;
    for (int i = 0x1F; i >= 0; i--) {
        Combatant *o = C(i);
        if (i == slot || o->flags == 0 || (o->flags & 0x20) || side == combatant_side(i)) continue;
        if (!(combat.prev_location == 0x28 || c->kind == 0x2F || !(o->flags & 0x10)) || (o->flags & 4)) continue;
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
    Step step = {0, 0};
    if (tx < c->x) step.dx = 0xFFFF;
    if (c->x < tx) step.dx = 1;
    if (ty < c->y) step.dy = 0xFFFF;
    if (c->y < ty) step.dy = 1;
    if (c->flags & 2) {
        step.dx = (uint16_t)-step.dx;
        step.dy = (uint16_t)-step.dy;
    }
    if (toward) *toward = step;
    return best == slot ? -1 : best;
}

/* Types 0x1A and 0x1B never move. Teleporters (flag 0x2000) jump to a random free square 3
 * times in 4. Others step towards the enemy along one axis, picked at random, the other if
 * that is blocked, else up to four random tries. Only a fleeing monster can step off the map. */
int Combat::monster_move(int slot)
{
    Combatant *c = C(slot);
    int moved = 0, r = 0;
    Square pos = {0, 0};
    if (c->kind == 0x1B || c->kind == 0x1A) return 0;
    if (!(c->flags & 0x80) && (monsters.flags(c->kind) & 0x2000) && party.active_spell != 0x4E && party.active_spell != 0x1C &&
        (combat_is_surrounded(slot) || random_upto(3) != 3) && random_combat_pos(&pos)) {
        MapObject *o = OBJ(c);
        if (combat_tile_free(pos.y, pos.x, o->tile)) {
            c->x = o->x = (uint8_t)pos.x;
            c->y = o->y = (uint8_t)pos.y;
            print_text(monsters.name(c->kind));
            print_text(strings[Strings::teleports_6F0C]);   /* " teleports!\n" */
            moved = 1;
        }
    }
    if (moved || combat_is_surrounded(slot)) return moved;
    Step step = {0, 0};
    find_nearest_enemy(slot, &step);
    if (random_upto(0xFF) < 0x80 || combat_is_blocked(slot, c->y, (uint16_t)(c->x + step.dx))) {
        if (!combat_is_blocked(slot, (uint16_t)(c->y + step.dy), c->x)) {
            step.dx = 0;
            r = 0x3E6;
        }
    } else {
        step.dy = 0;
        r = 999;
    }
    if (r < 0x3DE) {
        uint8_t y = c->y, x = c->x;
        moved = 4;   /* the try counter shares the result variable */
        do {
            r = random_upto(3);
            if (r == 0) { step.dy = 1; step.dx = 0; }
            else if (r == 1) { step.dx = 1; step.dy = 0; }
            else if (r == 2) { step.dy = 0xFFFF; step.dx = 0; }
            else if (r == 3) { step.dx = 0xFFFF; step.dy = 0; }
            if (!combat_is_blocked(slot, (uint16_t)(y + step.dy), (uint16_t)(x + step.dx))) r = 0x3DF;
            if (r > 0x3DE) break;
            step.dy = step.dx = 0;
        } while (--moved > 0);
    }
    if (r != 0) {
        MapObject *o = OBJ(c);
        c->x = (uint8_t)(c->x + step.dx);
        o->x = c->x;
        c->y = (uint8_t)(c->y + step.dy);
        o->y = c->y;
        moved = 1;
        if (!in_combat_bounds(c->y, c->x)) combat.event = 0x10;
    }
    return moved;
}

/* Dungeon rooms: stepping on one of eight trigger squares changes up to two squares of the map
 * (the lists are in the columns right of the map). */
int Combat::combat_room_trigger(unsigned y, unsigned x)
{
    int fired = 0;
    if (!(combat.flags & 0x80) && !(combat.flags & 2)) return 0;
    uint8_t *m = view.local_map;
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

int Combat::random_combat_pos(Square *pos)
{
    pos->x = (uint16_t)random_upto(0x0F);
    pos->y = (uint16_t)random_upto(0x0F);
    return pos->x < 0x0B && pos->y < 0x0B;
}

void Combat::combat_remove(int which)
{
    int object;
    if (which < 0) {
        Combatant *c = C(-1 - which);
        object = c->object;
        c->object = c->speed = c->hp = c->y = c->x = c->timer = c->flags = 0;
    } else {
        object = which - 1;
    }
    MapObject *o = &map.objects[object & 0x1F];
    o->x = o->tile2 = o->tile = o->hull = o->z = o->y = 0;
}

/* A monster does its type's damage; a weapon 1 to its value (item_damage), the glass sword
 * (0x27) 99 and then it shatters, item 0x28 nothing, bare hands 1. The defender's armour takes
 * off 1 to its value, so the result can be negative. */
int Combat::calc_damage(int defender, int attacker)
{
    int damage;
    if (C(attacker)->flags & 0x40) {
        damage = monsters.damage(C(attacker)->kind);
    } else if (combat.attack_item == 0x27) {
        print_text(strings[Strings::Thy_sword_hath_shattered_6F1A]);   /* "Thy sword hath shattered!\n" */
        unequip_item(0x27, attacker);   /* the slot is passed as the member */
        damage = 99;
    } else if (combat.attack_item == 0x28) {
        damage = 0;
    } else if (combat.attack_item == 0xFF) {
        damage = 1;
    } else {
        damage = items.damage(combat.attack_item);
        if (damage > 1 && damage != 99) damage = random_between(damage, 1);
    }
    if (damage == 99) return 99;
    uint8_t armour = (C(defender)->flags & 0x40) ? monsters.armour(C(defender)->kind) : party[C(defender)->kind & 0x0F].armour;
    if (armour != 0) damage -= random_between(armour, 1);
    return damage;
}

uint8_t Combat::get_combatant_speed(int slot)
{
    Combatant *c = C(slot);
    if ((party.active_spell == 0x54 && (c->flags & 0x40)) || c->kind == 0x1A || (c->flags & 8)) return 1;
    return c->speed;
}

/* which: -1 intelligence, -2 speed (dexterity), -3 strength, -4 armour bonus. A weapon id
 * (party) picks strength for missile kind 8, else speed; 0 (monsters) picks strength for types
 * flagged 0x80, else speed. Other values return garbage in the original, 0 here. */
int Combat::get_combatant_stat(int which, int slot)
{
    Combatant *c = C(slot);
    int monster = (c->flags & 0x40) != 0;
    if (!monster) {
        if (which >= 1) which = items.missile((uint8_t)which) == 8 ? -3 : -2;
    } else if (which == 0) {
        which = (monsters.flags(c->kind) & 0x80) ? -3 : -2;
    }
    PartyMember *m = &party[c->kind & 0x0F];
    switch (which) {
    case -4: return monster ? monsters.armour(c->kind) : get_armour_bonus(c->kind);
    case -3: return monster ? monsters.strength(c->kind) : m->str;
    case -2: return get_combatant_speed(slot);
    case -1: return monster ? monsters.intelligence(c->kind) : m->intel;
    }
    return 0;
}

/* A hit if (defender's stat - attacker's stat + 30) / 2 <= a 1-30 roll. With a weapon the
 * defender's speed stands against the attacker's weapon stat; with a spell, intelligence against
 * intelligence. Items 0x27, 0x23, 0x28 and spells 0x2A-0x31, 0x33 always hit. */
int Combat::roll_to_hit(int weapon, int magic, int attacker, int defender)
{
    (void)magic;
    int a_stat, d_stat;
    if (combat.cast_circle == 0) {
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
int Combat::apply_damage(unsigned damage, int slot)
{
    Combatant *c = C(slot);
    int xp = 0;
    if ((int16_t)damage < 1) {
        combat.event = 0x20;
        damage = 0;
    }
    if (c->flags & 0x80) {
        PartyMember *m = &party[c->kind & 0x0F];
        m->hp = (uint16_t)(m->hp - damage);
        if ((int16_t)m->hp < 1 || damage == 99) {
            m->hp = 0;
            c->flags |= 0x20;
            m->status = 'D';
            OBJ(c)->tile = OBJ(c)->tile2 = 0x1E;
            if (c->kind == party.active_player) party.active_player = 0xFF;
        }
        draw_status_bar();
        return 0;
    }
    unsigned type = c->kind, mf = monsters.flags(type);
    if ((mf & 0x20) && combat.magic_attack == 0) damage /= 2;
    if (mf & 8) damage = 0;
    c->hp = c->hp < damage ? 0 : (uint8_t)(c->hp - damage);
    if (c->hp == 0 || damage == 99) {
        xp = (monsters.hp(type) >> 2) + 1;
        uint8_t treasure = monsters.treasure(type), x = c->x, y = c->y;
        MapObject *o = OBJ(c);
        c->flags = 0x20;
        c->hp = 0;
        if ((mf & 0x1001) == 0) {
            if (type == 0x1C) {
                o->tile = o->tile2 = 0x1F;
                add_combatant(map.z, y, x, 0, 0x1F);
                world_tick();
                return xp;
            }
            if (type == 0x1E) {
                *map.get_map_tile_ptr(y, x) = 0x4C;
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
            print_text(monsters.name(type));
            print_text(strings[Strings::vanishes_6F36]);   /* " vanishes!" */
            combat.event = 2;
            o->tile = o->tile2 = 0x16;
            dissolve_tile(c->y, c->x, LMAP(c->y, c->x));
            combat_remove(-1 - slot);
            combat_check_pass_out();
            return xp;
        }
        combat_remove(-1 - slot);
    } else if (mf & 0x10) {
        for (int tries = 0; tries < 8; tries++) {
            Square near = random_near(c->y, c->x);
            unsigned nx = near.x, ny = near.y;
            if (!combat_tile_free(ny, nx, OBJ(c)->tile)) continue;
            int n = add_combatant(map.z, (uint8_t)ny, (uint8_t)nx, 0, (OBJ(c)->tile - 0x40) >> 2);
            if (n < 0) continue;
            C(n)->hp = c->hp;
            print_text(monsters.name(type));
            print_text(strings[Strings::divides_6F42]);   /* " divides!\n" */
            return 0;
        }
    }
    return xp;
}

/* A healthy member is poisoned; anyone else takes 0-20. */
void Combat::apply_poison_hit(int attacker, int defender)
{
    Combatant *d = C(defender);
    if ((d->flags & 0x80) && party[d->kind & 0x0F].good()) {
        party[d->kind & 0x0F].status = 'P';
        print_text(party[d->kind & 0x0F].name);
        print_text(strings[Strings::is_poisoned_6F4E]);   /* " is poisoned!\n" */
        combat.event = 8;
        status.dirty = true;
        return;
    }
    int xp = apply_damage((unsigned)random_upto(0x14), defender);
    if (attacker >= 0 && (C(attacker)->flags & 0x80)) add_word_capped(9999, (uint16_t)xp, &party[C(attacker)->kind & 0x0F].xp);
}

/* Monsters flagged 0x204 poison 3 times in 4 instead of wounding; type 0x1C puts its victim
 * to sleep; magic 0x34 is sleep and 0x33 poison. Experience goes to the member who struck. */
void Combat::resolve_hit(int attacker, int defender)
{
    Combatant *a = C(attacker), *d = C(defender);
    int monster = !(a->flags & 0x80);
    if (monster && (monsters.flags(a->kind) & 0x204) && random_between(3, 0) != 0) {
        apply_poison_hit(attacker, defender);
        return;
    }
    if (monster && a->kind == 0x1C && !(d->flags & 8)) {
        sleep_combatant((unsigned)defender);
        return;
    }
    if (combat.magic_attack != 0 && combat.attack_item == 0x34) {
        sleep_combatant((unsigned)defender);
        return;
    }
    if (combat.magic_attack != 0 && combat.attack_item == 0x33) {
        apply_poison_hit(attacker, defender);
        return;
    }
    int damage = calc_damage(defender, attacker);
    if (damage < 0 && (d->flags & 0x80)) {
        combat.event = 0x20;
        return;
    }
    int xp = apply_damage((unsigned)damage, defender);
    if (a->flags & 0x80) add_word_capped(9999, (uint16_t)xp, &party[a->kind & 0x0F].xp);
}

/* With q = a quarter of the type's hit points: below q the monster flees (1), below 2q it
 * flees 4 times in 256 (2), below 3q (3), else (4). */
int Combat::monster_morale(int slot)
{
    Combatant *c = C(slot);
    int level, flee = 0;
    if (c->flags & 0x80) return 0;   /* (the original returns a pointer here) */
    unsigned q = monsters.hp(c->kind) >> 2;
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
void Combat::apply_tile_hazard(unsigned slot)
{
    Combatant *c = C(slot);
    int hazard = 0;
    uint8_t t = LMAP(c->y, c->x);
    if (t == 0x8F || t == 0xBC) hazard = 100;
    if (t == 4) hazard = 0x32;
    if (hazard == 0)
        for (unsigned i = 0; i < 0x20 && hazard == 0; i++) {
            MapObject *o = &map.objects[i];
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
        status.dirty = true;
    } else if (hazard == 0x96) {
        sleep_combatant(slot);
    }
}

void Combat::check_regurgitate(int slot)
{
    Combatant *c = C(slot);
    if ((unsigned)random_stat_roll() >= c->speed) return;
    print_text((c->flags & 0x80) ? party[c->kind & 0x0F].name : monsters.name(c->kind));
    print_text(strings[Strings::regurgitated_6F5E]);   /* " regurgitated!\n" */
    sound_noise(600, 7000, 1);
    c->flags &= 0xFB;
    OBJ(c)->tile2 = OBJ(c)->tile;
}

/* ---- the combat half of SJOG.OVL ---- */

int Combat::count_members_where(unsigned item)
{
    int n = 0;
    for (unsigned m = 0; m < party.size(); m++)
        if (unequip_item(item, (int)m)) n++;
    return n;
}

Combat::Sides Combat::count_combat_sides(void)
{
    Sides sides = {0, 0};
    for (int i = 0; i < 0x20; i++) {
        if (combat.combatants[i].flags == 0 || (combat.combatants[i].flags & 0x20)) continue;
        if (combatant_side(i) == 0) sides.party++;
        else sides.others++;
    }
    return sides;
}

/* exit_dir: 1-4 the side of the map (W E N S), 5 up, 6 down. In a dungeon room everyone must
 * use the same exit. Aboard ship nobody leaves. */
int Combat::combat_leave_map(unsigned exit_dir, int slot)
{
    if ((party.transport & 0xF8) == 0x20) {
        print_text(strings[Strings::Stay_with_ship_8E76]);   /* "\nStay with ship!\n" */
        return 0;
    }
    if (C(slot)->flags & 0x80) {
        if (combat.exit_dir == 0) {
            combat.exit_dir = (uint8_t)exit_dir;
        } else if (exit_dir != combat.exit_dir && (combat.flags & 0x80)) {
            print_text(strings[Strings::All_must_use_the_same_8E88]);   /* "\nAll must use the same exit!\n" */
            beep(200, 0xA5);
            return 0;
        }
    }
    combat.redraw = 1;
    print_text(strings[count_combat_sides().others == 0 ? Strings::Leave_8EA6 : Strings::Escape_8EAE]);   /* "Leave!\n" / "Escape!\n" */
    sound_slide(0x28, 1, 2000, 0x4B0);
    party.active_player = 0xFF;
    combat_remove(-1 - slot);
    draw_status_bar();
    world_tick();
    return 1;
}

int Combat::combat_move_member(int dir, int slot)
{
    Combatant *c = C(slot);
    MapObject *o = OBJ(c);
    int dx = 0, dy = 0;
    if (dir == 1) { dx = -1; print_text(strings[Strings::West_8ECE]); }        /* "West\n" */
    else if (dir == 2) { dx = 1; print_text(strings[Strings::East_8EC8]); }    /* "East\n" */
    else if (dir == 3) { dy = -1; print_text(strings[Strings::North_8EB8]); }   /* "North\n" */
    else if (dir == 4) { dy = 1; print_text(strings[Strings::South_8EC0]); }    /* "South\n" */
    int x = c->x + dx, y = c->y + dy;
    if (!(x < 0x0B && y < 0x0B && x > -1 && y > -1)) return combat_leave_map((unsigned)dir, slot);
    if (!combat_tile_free((unsigned)y, (unsigned)x, o->tile)) {
        print_text(strings[Strings::Blocked_8ED4]);   /* "Blocked!\n" */
        beep(200, 0xA5);
        flush_keyboard();
        return 0;
    }
    c->x = o->x = (uint8_t)x;
    c->y = o->y = (uint8_t)y;
    step_sound();
    if (combat.flags & 0x82) combat_room_trigger((unsigned)y, (unsigned)x);
    return 1;
}

/* Ladders leave the map (up 5, down 6; a grate goes down in dungeon rooms); otherwise a
 * direction is asked and a fence (0x4C) with nothing on it can be climbed onto. */
int Combat::combat_klimb(void)
{
    Direction dir = {0, 0};
    print_text(strings[Strings::Klimb_8EDE]);   /* "Klimb-" */
    uint8_t t = *map.get_map_tile_ptr(map.y, map.x);
    if ((combat.flags & 2) && t == 0xC8 && combat.on_ladder != 0) {
        print_text(strings[Strings::U_D_8EE6]);   /* "U/D-" */
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
        print_text(strings[Strings::Up_8EEC]);   /* "Up!\n" */
        return combat_leave_map(5, combat.turn);
    }
    if ((t == 0x86 && (combat.flags & 0x80)) || t == 0xC9) {
        print_text(strings[Strings::Down_8EF2]);   /* "Down!\n" */
        return combat_leave_map(6, combat.turn);
    }
    if (ask_direction(&dir) != 0) {
        unsigned x = (uint16_t)(map.x + dir.dx), y = (uint16_t)(map.y + dir.dy);
        if (*map.get_map_tile_ptr(y, x) != 0x4C || object_tile_at(0, y, x) != 0) {
            print_text(strings[Strings::What_8EFA]);   /* "What?\n" */
            return 0;
        }
        Combatant *c = C(combat.turn);
        c->x = OBJ(c)->x = (uint8_t)x;
        c->y = OBJ(c)->y = (uint8_t)y;
        combat_room_trigger(y, x);
    }
    return 1;
}

/* In the last battle: whoever stands in row 2 on one of the 0x3C tiles is taken (exit 'M'). */
void Combat::check_absorbed(void)
{
    Combatant *c = C(combat.turn);
    if (c->flags == 0 || (c->flags & 0x20) || c->y != 2 || (view.tiles[0x10 + (c->x & 0x0F)] & 0xFC) != 0x3C) return;
    combat.exit_dir = 0x4D;
    put_char('\n');
    print_combatant_name(combat.turn);
    print_text(strings[Strings::is_absorbed_8F02]);   /* " is absorbed!\n" */
    sound_slide(0x28, 1, 2000, 0x4B0);
    party.active_player = 0xFF;
    draw_status_bar();
    combat_remove(-1 - combat.turn);
    world_tick();
}

int Combat::combat_cant_do(int kind, const char *prompt)
{
    print_text(prompt);
    if (kind == 1) print_text(strings[Strings::what_8F12]);        /* " what?" */
    else if (kind == 2) print_text(strings[Strings::Not_here_8F1A]);   /* "-Not here" */
    else if (kind == 3) print_text(strings[Strings::Funny_no_response_8F24]);   /* "-Funny, no response!" */
    put_char('\n');
    beep(0x96, 0xDC);
    beep(0x96, 0x96);
    return 1;
}

int Combat::combat_set_active_player(unsigned member)
{
    print_text(strings[Strings::Set_active_plr_8F3A]);   /* "Set active plr:\n" */
    for (int slot = 0; slot < 0x20; slot++) {
        Combatant *c = C(slot);
        if (!(c->flags & 0x80) || combatant_side(slot) != 0 || c->kind != member) continue;
        if (c->flags & 0x2C) break;
        party.active_player = (uint8_t)member;
        print_combatant_name(slot);
        put_char('\n');
        draw_status_bar();
        return 1;
    }
    print_text(strings[Strings::Invalid_8F4C]);   /* "Invalid!\n" */
    return 0;
}

void Combat::combat_end_of_turn(void)
{
    apply_ring_effects(combat.turn);
    decay_combat_fields();
    if (party.active_spell_turns != 0 && party.active_spell_turns != 0xFF && --party.active_spell_turns == 0) {
        party.active_spell = 0;
        status.dirty = true;
    }
}

/* After a fight with a map object: the object is gone whatever happened - except a pirate ship
 * that was beaten, which becomes a ship to board (tile - 8, hull 99, 2 skiffs). */
void Combat::combat_cleanup(int object)
{
    if (combat.exit_dir == 0x4D) endings.rescue_lord_british();
    memcpy(map.objects.bytes(), map.objects_backup, 0x100);
    if (combat.flags & 0x82) return;
    if (object < 0x20) {
        MapObject *o = &map.objects[object & 0x1F];
        if (combat.victory == 0 || (o->tile & 0xFC) != 0x2C) {
            o->z = o->y = o->x = o->tile2 = o->tile = 0;
        } else {
            o->tile -= 8;
            o->tile2 -= 8;
            o->hull = 99;
            o->skiffs = 2;
        }
    }
    put_char('\n');
    map.set(Map::Id(combat.prev_location));
}

int Combat::combat_is_blocked(int slot, unsigned y, unsigned x)
{
    int ix = (int16_t)x, iy = (int16_t)y;
    if (ix < 0 || ix > 10 || iy < 0 || iy > 10) return (C(slot)->flags & 2) ? 0 : 1;
    return 1 - combat_tile_free(y, x, OBJ(C(slot))->tile);
}

int Combat::combat_is_surrounded(int slot)
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
int Combat::combat_check_pass_out(void)
{
    for (int slot = 0; slot < 6; slot++) {
        Combatant *c = C(slot);
        if (!(c->flags & 1) || !(c->flags & 0x80)) continue;
        c->flags &= 0xFE;
        print_text(party[c->kind & 0x0F].name);
        print_text(strings[Strings::passes_out_8F56]);   /* " passes out!" */
        sound_sweep(2, 1000, 30000, 1, 0xC1C);
        unequip_item(0x23, c->kind);
        sleep_combatant((unsigned)slot);
        return slot;
    }
    return -1;
}
