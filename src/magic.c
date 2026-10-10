/* magic.c - Cast and Use (CAST.OVL, spell helpers of CAST2.OVL). See magic.h. */
#include <string.h>
#include "cmds.h"
#include "combat.h"
#include "commands.h"
#include "comsubs.h"
#include "data.h"
#include "dungeon.h"
#include "endings.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "look.h"
#include "magic.h"
#include "map.h"
#include "misc.h"
#include "objects.h"
#include "picture.h"
#include "stats.h"
#include "status.h"
#include "text.h"
#include "town.h"
#include "view.h"
#include "world.h"

#define S(off) ds_str(off)
#define C(slot) (&dg.combatants[(slot) & 0x1F])
#define OBJ(c) (&dg.objects[(c)->object & 0x1F])

static void spell_fizzle(void) { sound_slide(0x32, 1, 2000, 800); }

/* ---- CAST2.OVL helpers ---- */

/* fx 0-8: a noise rising with fx and two sweeps (tables at ds:4AF6-4B3C) while the view
 * flashes; 9: one short sweep. */
void spell_effect(int fx)
{
    if (fx >= 9) {
        sound_sweep(1, 300, -0x218, 1, 0x1180);
        return;
    }
    sound_noise(700, fx * 0x640 + 8000, 800);
    set_draw_color(dg.color_text);
    xor_fill_rect(0xB7, 0xB7, 8, 8);
    sound_sweep((int16_t)ds_u16((uint16_t)(0x4B2C + fx * 2)), ds_u16((uint16_t)(0x4B08 + fx * 2)), 0x2710 + fx * 4000, 1, ds_u16((uint16_t)(0x4AF6 + fx * 2)));
    sound_sweep(-(int16_t)ds_u16((uint16_t)(0x4B2C + fx * 2)), ds_u16((uint16_t)(0x4B1A + fx * 2)), 0x2710 + fx * 4000, 1, ds_u16((uint16_t)(0x4AF6 + fx * 2)));
    xor_fill_rect(0xB7, 0xB7, 8, 8);
}

int ask_target_member(void)
{
    print_text(S(0x94F4));   /* "On who: " */
    int member = ask_member_number();
    print_text(member < 0 ? S(0x94FE) : dg.party[member].name);   /* "None!" */
    if (get_cursor_col() != 0) put_char('\n');
    return member;
}

int ask_spell_direction(void)
{
    if (dg.location > 0x7F) {
        dg.dir_x = C(dg.combat_turn)->x;
        dg.dir_y = C(dg.combat_turn)->y;
    } else {
        dg.dir_x = dg.party_x;
        dg.dir_y = dg.party_y;
    }
    print_text(S(0x9504));   /* "Direction-" */
    for (;;) {
        int key = get_command_key();
        if (game_quit()) key = ' ';
        switch (key) {
        case 1: print_text(S(0x9526)); dg.dir_x--; return 1;   /* "West\n" */
        case 2: print_text(S(0x9518)); dg.dir_x++; return 2;   /* "East\n" */
        case 3: print_text(S(0x9510)); dg.dir_y--; return 3;   /* "North\n" */
        case 4: print_text(S(0x951E)); dg.dir_y++; return 4;   /* "South\n" */
        case ' ': print_text(S(0x952C)); return 0;             /* "Pass\n" */
        }
    }
}

int heal_member(int member)
{
    PartyMember *m = &dg.party[member & 0x0F];
    if (m->status == 'D') return 0;
    add_word_capped(m->max_hp, (uint16_t)random_stat_roll(), &m->hp);
    dg.status_dirty = 1;
    return 1;
}

/* The wind comes to blow towards `dir` (a direction key 1-4); 0 calms it, but only from the
 * scroll (show_fx 1). */
void change_wind(int show_fx, int dir)
{
    static const int8_t wind[5] = {0, 4, 3, 1, 2};
    if (dir == 0 && show_fx == 0) return;
    spell_effect(show_fx == 0 ? 2 : 1);
    if (dir >= 0 && dir <= 4) set_wind_direction(wind[dir]);
}

void party_view_effect(void)
{
    compute_view_mask(0x20, dg.party_y - dg.map_origin_y, dg.party_x - dg.map_origin_x, -1);
    for (int i = 0x14; i != 0; i--) {
        if (dg.active_spell != 0x54) animate_objects();
        update_view_objects();
        draw_view();
        delay_ticks(1);
    }
    world_tick();
}

/* A daemon (type 0x26) on a free random square. Cast (not from the scroll) it serves only if
 * the caster's intelligence beats a 1-30 roll: "Oops..." (-1) and it is an enemy. */
int summon_daemon(int from_scroll)
{
    spell_effect(from_scroll == 0 ? 8 : 5);
    int tries = 0;
    while (!random_combat_pos() || !combat_tile_free(dg.dir_y, dg.dir_x, 0xD8) || *get_map_tile_ptr(dg.dir_y, dg.dir_x) == 0xFF)
        if (++tries > 7) return 0;
    int n = add_combatant(dg.z_level, (uint8_t)dg.dir_y, (uint8_t)dg.dir_x, 0, 0x26);
    if (n < 0) return 0;
    sound_sweep(5, 500, 12000, 1, 0xAC8);
    MapObject *o = OBJ(C(n));
    o->tile = o->tile2 = 0x16;
    dissolve_tile(dg.dir_y, dg.dir_x, 0x1D8);
    o->tile = o->tile2 = 0xD8;
    if (from_scroll == 0 && get_combatant_stat(-1, dg.combat_turn) <= random_stat_roll()) {
        print_text(S(0x9532));   /* "Oops...\n" */
        return -1;
    }
    C(n)->flags |= 1;
    return 1;
}

/* In runes: latitude then longitude, each as two letters A-P (sixteenths, then units). */
void print_coordinates(void)
{
    select_font(1);
    put_char('\n');
    put_char((uint8_t)((dg.party_y >> 4) + 'A'));
    put_char('\'');
    put_char((uint8_t)((dg.party_y & 0x0F) + 'A'));
    print_text(S(0x9548));   /* "\", " */
    put_char((uint8_t)((dg.party_x >> 4) + 'A'));
    put_char('\'');
    put_char((uint8_t)((dg.party_x & 0x0F) + 'A'));
    put_char('"');
    select_font(0);
    put_char('\n');
}

/* Magically locked doors (0x97, 0x98) become plain doors (0xB8, 0xBA). */
int magic_unlock_door(void)
{
    if (ask_spell_direction() == 0) return -1;
    uint8_t *p = get_map_tile_ptr(dg.dir_y, dg.dir_x);
    if (*p == 0x97) *p = 0xB8;
    else if (*p == 0x98) *p = 0xBA;
    else return 0;
    dg.redraw_flags |= 2;
    return 1;
}

/* The dungeon square the party is on or faces (dungeon_map: 8 x 8 x 8, high nibble = kind). */
static uint8_t *dungeon_square(unsigned kind)
{
    uint8_t *p = &dg.dungeon_map[((dg.z_level & 7) * 0x40 + (dg.party_y & 7) * 8 + (dg.party_x & 7)) & 0x1FF];
    if ((*p & 0xF0) != kind)
        p = &dg.dungeon_map[((dg.z_level & 7) * 0x40 + ((dg.facing_dy[dg.facing & 3] + dg.party_y) & 7) * 8 + ((dg.facing_dx[dg.facing & 3] + dg.party_x) & 7)) & 0x1FF];
    return p;
}

/* In combat: a field object next to the caster. Elsewhere (dungeons): a field square (0x8n). */
int dispel_field(int show_fx)
{
    if (dg.location < 0x80) {
        if (show_fx) spell_effect(4);
        uint8_t *p = dungeon_square(0x80);
        if ((*p & 0xF0) != 0x80) return 0;
        *p &= 8;
        print_text(S(0x954C));   /* "Field destroyed!\n" */
        return -1;
    }
    if (ask_spell_direction() == 0) return -1;
    if (show_fx) spell_effect(4);
    for (int i = 0; i < 0x20; i++)
        if ((dg.objects[i].tile & 0xFC) == 0xE8 && dg.objects[i].x == dg.dir_x && dg.objects[i].y == dg.dir_y) {
            set_object_entry(i, 0, 0, 0, 0, 0, 0);
            return 1;
        }
    return 0;
}

void set_light(uint8_t turns) { dg.light_spell = turns; }

void start_timed_spell(int fx, uint8_t turns, uint8_t letter)
{
    dg.active_spell = letter;
    dg.active_spell_turns = turns;
    spell_effect(fx);
    draw_status_bar();
}

/* ---- CAST.OVL: the spells ---- */

int is_spell_immune_kind(int slot)
{
    uint8_t k = C(slot)->kind;
    return k == 0x2F || k == 0x0E || k == 0x0F;
}

/* Grav Por (0x30), Vas Flam (0x31), Xen Corp (0x32): aimed like a missile weapon. */
void cast_missile(int missile)
{
    dg.attack_item = (uint8_t)missile;
    attack_with(missile, dg.combat_turn);
}

/* In combat the field is thrown (attack items 0x35 fire, 0x33 poison, 0x34 sleep, 0x36
 * energy); in a dungeon it fills the empty square ahead (codes at ds:4596). */
int cast_field(int field)
{
    if (dg.location >= 0x80) {
        dg.attack_item = ds_u8((uint16_t)(0x4592 + field));
        attack_with(dg.attack_item, dg.combat_turn);
        return -1;
    }
    spell_effect(field == 3 ? 4 : 3);
    uint8_t *p = &dg.dungeon_map[((dg.z_level & 7) * 0x40 + ((dg.facing_dy[dg.facing & 3] + dg.party_y) & 7) * 8 + ((dg.facing_dx[dg.facing & 3] + dg.party_x) & 7)) & 0x1FF];
    if ((*p & 0xF7) != 0) return 0;
    *p = (*p & 8) | ds_u8((uint16_t)(0x4596 + field));
    return -1;
}

/* An Zu: wakes a sleeper. */
int spell_an_zu(void)
{
    int member = ask_target_member();
    if (member < 0) return -1;
    if (dg.party[member].status != 'S') return 0;
    dg.party[member].status = 'G';
    if (dg.location > 0x7F)
        for (int slot = 0; slot < 0x20; slot++)
            if ((C(slot)->flags & 0xE8) == 0x88 && C(slot)->kind == member) {
                wake_combatant(slot);
                break;
            }
    spell_effect(1);
    dg.status_dirty = 1;
    return -1;
}

/* An Nox: cures poison. */
int spell_an_nox(void)
{
    int member = ask_target_member();
    if (member < 0) return -1;
    if (dg.party[member].status != 'P') return 0;
    dg.party[member].status = 'G';
    spell_effect(1);
    dg.status_dirty = 1;
    return 1;
}

/* Mani: heals 1-30. */
int spell_mani(void)
{
    int member = ask_target_member();
    int r = member < 0 ? -1 : heal_member(member);
    if (r != 0) spell_effect(1);
    return r;
}

/* An Ylem: makes a piece of furniture vanish (the tiles Push can move, and mirrors). */
int spell_an_ylem(void)
{
    if (ask_spell_direction() == 0) return -1;
    spell_effect(1);
    uint8_t *p = get_map_tile_ptr(dg.dir_y, dg.dir_x), t = *p;
    if (!(t == 0x9D || t == 0x5B || (t > 0x8F && t < 0x94) || t == 0xA5 || t == 0xA6 || t == 0xA8 || t == 0xA9 || (t > 0xAC && t < 0xB0)))
        return 0;
    *p = 0x44;
    print_text(S(0x459A));   /* "POOF!\n" */
    dg.redraw_flags |= 2;
    world_tick();
    sound_slide(0x28, 1, 2000, 0x4B0);
    return -1;
}

/* An Sanct: unlocks a door (0xB9, 0xBB) or disarms a chest; in a dungeon opens the chest on or
 * ahead of the party. */
int spell_an_sanct(void)
{
    if (!(dg.location < 0x21 || dg.location > 0x7F)) {
        spell_effect(2);
        uint8_t *p = dungeon_square(0x40);
        if ((*p & 0xF0) != 0x40) return 0;
        if (*p & 1) print_text(S(0x45A1));   /* "Disarmed!\n" */
        *p = (*p & 8) | 0x70;
        print_text(S(0x45AC));   /* "Chest opened!\n" */
        return -1;
    }
    if (ask_spell_direction() == 0) return -1;
    uint8_t *p = get_map_tile_ptr(dg.dir_y, dg.dir_x);
    if (*p == 0xB9 || *p == 0xBB) {
        (*p)--;
        dg.redraw_flags |= 2;
        spell_effect(2);
        return 1;
    }
    for (int i = 0; i < 0x20; i++) {
        MapObject *o = &dg.objects[i];
        if (o->tile == 1 && o->x == dg.dir_x && o->y == dg.dir_y && (dg.location > 0x7F || o->z == dg.z_level)) {
            o->hull &= 0x7F;
            spell_effect(2);
            return 1;
        }
    }
    return 0;
}

/* An Xen Corp: monsters flagged 0x20 (the undead) that fail to resist turn and flee. */
void spell_an_xen_corp(void)
{
    spell_effect(2);
    for (int slot = 0; slot < 0x20; slot++) {
        Combatant *c = C(slot);
        if (is_spell_immune_kind(slot) || (c->flags & 0xC0) != 0x40 || !(MONSTER_FLAGS(c->kind) & 0x20)) continue;
        if (magic_resist_roll(0, slot, dg.combat_turn) == 0) {
            c->hp = 1;
            c->flags |= 2;
        }
    }
}

/* In Wis: the party's position. */
void spell_in_wis(void)
{
    spell_effect(2);
    print_coordinates();
}

/* Kal Xen: a rat (6 in 16), spider (5), bat (3) or python (2) appears on the party's side. */
int spell_kal_xen(void)
{
    spell_effect(2);
    int r = random_between(0x0F, 0);
    int type = r < 6 ? 0x14 : r < 0x0B ? 0x16 : r < 0x0E ? 0x15 : 0x22;
    uint8_t tile = (uint8_t)(type * 4 + 0x40);
    int tries = 0;
    while (!random_combat_pos() || !combat_tile_free(dg.dir_y, dg.dir_x, 0x90))
        if (++tries > 7) return 0;
    int n = add_combatant(dg.z_level, (uint8_t)dg.dir_y, (uint8_t)dg.dir_x, 0, type);
    if (n < 0) return 0;
    MapObject *o = OBJ(C(n));
    o->tile = o->tile2 = 0x16;
    dissolve_tile(dg.dir_y, dg.dir_x, type * 4 + 0x140);
    o->tile = o->tile2 = tile;
    C(n)->flags |= 1;
    return 1;
}

/* In Xen Mani: 1-3 food. */
int spell_in_xen_mani(void)
{
    spell_effect(2);
    add_word_capped(9999, (uint16_t)random_between(3, 1), &dg.food);
    dg.status_dirty = 1;
    return 1;
}

/* In Por. Outdoors: the party jumps along the direction to the farthest grass square (tile 5)
 * of the loaded map window. In combat: the caster jumps to a random free square. */
int spell_in_por(void)
{
    if (dg.location >= 0x80) {
        if (dg.combat_flags & 2) return 0;
        Combatant *c = C(dg.combat_turn);
        MapObject *o = OBJ(c);
        spell_effect(3);
        for (int tries = 0; tries < 7; tries++)
            if (random_combat_pos() && combat_tile_free(dg.dir_y, dg.dir_x, o->tile)) {
                c->x = o->x = (uint8_t)dg.dir_x;
                c->y = o->y = (uint8_t)dg.dir_y;
                return 1;
            }
        return 0;
    }
    if (ask_spell_direction() == 0) return -1;
    spell_effect(3);
    int dx = (int16_t)(dg.dir_x - dg.party_x), dy = (int16_t)(dg.dir_y - dg.party_y), found = 0;
    int max_x = dg.map_origin_x + 0x20, max_y = dg.map_origin_y + 0x20;
    if (max_x > 0x100) max_x = 0x100;
    if (max_y > 0x100) max_y = 0x100;
    for (int x = dg.dir_x & 0xFF, y = dg.dir_y & 0xFF; x >= dg.map_origin_x && x < max_x && y >= dg.map_origin_y && y < max_y; x += dx, y += dy)
        if (*get_map_tile_ptr((unsigned)y, (unsigned)x) == 5) {
            found = -1;
            dg.party_x = (uint8_t)x;
            dg.party_y = (uint8_t)y;
            dg.redraw_flags = 1;
        }
    if (found) enter_outdoors();
    return found;
}

/* Wis Quas: invisible monsters show. */
void spell_wis_quas(void)
{
    spell_effect(4);
    for (int slot = 0; slot < 0x20; slot++) {
        Combatant *c = C(slot);
        if (c->flags == 0 || (c->flags & 0x80) || !(c->flags & 0x10)) continue;
        c->flags &= 0xEF;
        OBJ(c)->tile2 = OBJ(c)->tile;
        update_view_objects();
        draw_view();
    }
}

/* In Bet Xen: four insect swarms (type 0x1F) on the party's side - all on one free square. */
int spell_in_bet_xen(void)
{
    spell_effect(5);
    int tries = 0;
    while (!random_combat_pos() || !combat_tile_free(dg.dir_y, dg.dir_x, 0xBC))
        if (++tries > 7) return 0;
    int made = 0;
    for (int i = 0; i < 4; i++) {
        int n = add_combatant(dg.z_level, (uint8_t)dg.dir_y, (uint8_t)dg.dir_x, 0, 0x1F);
        if (n < 0) return made;
        C(n)->flags |= 1;
        made = 1;
    }
    return made;
}

/* An Ex Por: a door becomes magically locked (0xB8, 0xB9 -> 0x97; 0xBA, 0xBB -> 0x98). */
int spell_an_ex_por(void)
{
    if (ask_spell_direction() == 0) return -1;
    spell_effect(5);
    uint8_t *p = get_map_tile_ptr(dg.dir_y, dg.dir_x);
    if (*p < 0xB8 || *p > 0xBB) return 0;
    *p = *p < 0xBA ? 0x97 : 0x98;
    dg.redraw_flags |= 2;
    return 1;
}

/* Vas Mani: full hit points; not for the dead, nor in a fight before it is won. */
int spell_vas_mani(void)
{
    int member = ask_target_member();
    if (member < 0) return -1;
    if (dg.party[member].status == 'D' || (dg.location > 0x7F && dg.combat_victory == 0)) return 0;
    dg.party[member].hp = dg.party[member].max_hp;
    spell_effect(5);
    dg.status_dirty = 1;
    return 1;
}

/* In Vas Por Ylem: the earth shakes; every enemy whose speed fails a 1-30 roll takes 1-20. */
void spell_in_vas_por_ylem(int caster)
{
    spell_effect(6);
    screen_shake();
    for (int slot = 0; slot < 0x20; slot++) {
        if (C(slot)->flags == 0 || combatant_side(slot) == 0) continue;
        int roll = random_stat_roll();
        if (get_combatant_stat(-2, slot) > roll) continue;
        flash_combatant((unsigned)slot);
        int xp = apply_damage((unsigned)random_between(0x14, 1), slot);
        add_word_capped(9999, (uint16_t)xp, &dg.party[caster & 0x0F].xp);
        report_hit(dg.combat_turn, slot);
    }
}

/* An Xen Ex: an enemy that fails to resist changes sides (not one in stocks or manacles). */
int spell_an_xen_ex(void)
{
    print_text(S(0x45BB));   /* "Creature: " */
    if (aim_cursor(0x0F, dg.combat_turn) == 0) return -1;
    spell_effect(6);
    if ((*get_map_tile_ptr(dg.aim_y, dg.aim_x) & 0xFE) == 0x84) return 0;
    int t = combatant_at(dg.aim_y, dg.aim_x);
    if (t < 0 || is_spell_immune_kind(t) || combatant_side(t) == 0 || magic_resist_roll(0, t, dg.combat_turn) != 0) return 0;
    Combatant *c = C(t);
    c->flags ^= 1;
    if (c->flags & 0x80) {
        dg.party[c->kind & 0x0F].status = 'G';
        draw_status_bar();
    }
    print_combatant_name(t);
    print_text(S(0x45C6));   /* " charmed!\n" */
    return -1;
}

/* Rel Xen Bet: a creature that fails to resist becomes a giant rat (type 0x14). */
int spell_rel_xen_bet(void)
{
    print_text(S(0x45D1));   /* "Creature: " */
    if (aim_cursor(0x0F, dg.combat_turn) == 0) return -1;
    spell_effect(6);
    int t = combatant_at(dg.aim_y, dg.aim_x);
    if (t < 0 || is_spell_immune_kind(t) || magic_resist_roll(0, t, dg.combat_turn) != 0) return 0;
    uint8_t x = C(t)->x, y = C(t)->y;
    combat_remove(-1 - t);
    add_combatant(dg.z_level, y, x, 0, 0x14);
    return 1;
}

/* Sanct Lor: the caster turns invisible. */
int spell_sanct_lor(void)
{
    Combatant *c = C(dg.combat_turn);
    OBJ(c)->tile2 = 0x1D;
    c->flags |= 0x10;
    spell_effect(7);
    return 1;
}

/* In Quas Xen: a copy of the fighter aimed at, friend or foe, on a random free square. */
int spell_in_quas_xen(void)
{
    print_text(S(0x45DC));   /* "Creature: " */
    if (aim_cursor(0x0F, dg.combat_turn) == 0) return -1;
    spell_effect(7);
    int t = combatant_at(dg.aim_y, dg.aim_x);
    if (t < 0) return 0;
    int obj, slot;
    for (obj = 0; obj < 0x20 && dg.objects[obj].tile != 0; obj++) {}
    for (slot = 0; slot < 0x20 && dg.combatants[slot].flags != 0; slot++) {}
    if (obj == 0x20 || slot == 0x20) return 1;   /* (the result is unset in the original; nothing happens) */
    dg.combatants[slot] = dg.combatants[t];
    dg.combatants[slot].object = (uint8_t)obj;
    dg.objects[obj] = *OBJ(C(t));
    for (int tries = 0; tries < 10000; tries++) {   /* the original tries for ever */
        if (!random_combat_pos() || !combat_tile_free(dg.dir_y, dg.dir_x, 0x1C)) continue;
        dg.combatants[slot].x = dg.objects[obj].x = (uint8_t)dg.dir_x;
        dg.combatants[slot].y = dg.objects[obj].y = (uint8_t)dg.dir_y;
        break;
    }
    return 1;
}

/* In Quas Corp: every monster that fails to resist flees. */
void spell_in_quas_corp(void)
{
    spell_effect(7);
    for (int slot = 0; slot < 0x20; slot++) {
        Combatant *c = C(slot);
        if ((c->flags & 0xC0) != 0x40 || is_spell_immune_kind(slot) || magic_resist_roll(0, slot, dg.combat_turn) != 0) continue;
        c->hp = 1;
        c->flags |= 2;
    }
}

/* Vas Rel Por: to moonstone 1-8, as through a moongate. Not aboard ship. */
int spell_vas_rel_por(void)
{
    if ((dg.transport & 0xF0) == 0x20) return 0;
    print_text(S(0x45E7));   /* "To phase: " */
    uint8_t c = (uint8_t)get_command_key();
    if (c > 0x1F) put_char(c);
    put_char('\n');
    if (c > '0' && c < '9') {
        spell_effect(8);
        if (moongate_travel(c - '1') != 0) return -1;
    }
    return 0;
}

/* An Tym: time stops for 10 turns - unless a Shadowlord is present. */
int spell_an_tym(void)
{
    spell_effect(8);
    for (int i = 0; i < 0x20; i++)
        if (dg.objects[i].tile == 0xFC) {
            print_text(S(0x45F2));   /* "Magic absorbed!\n" */
            sound_sweep(2, 1000, 28000, 1, 0x2648);
            return 0;
        }
    dg.active_spell = 0x54;
    dg.active_spell_turns = 10;
    draw_status_bar();
    return -1;
}

/* The caster is the active player (or asked for; in combat whoever's turn it is). The mixed
 * spell is used up before the magic points and the level are checked, so it is lost on
 * "M.P. too low!" and when the caster's level is below the circle. Nothing works in Blackthorn's
 * castle without the Crown, nor in Stonegate. */
int cmd_cast(void)
{
    int turn = 1, result;
    dg.cast_circle = 1;
    dg.magic_attack = 1;
    int member = ask_which_member();
    if (member < 0) return member;
    print_text(S(0x4603));   /* "Spell name:\n:" */
    int spell = select_spell();
    if (spell == -1) {
        print_text(S(0x4611));   /* "None!\n" */
        return 1;
    }
    if (spell == -2) {
        print_text(S(0x4618));   /* "No effect!\n" */
        return 1;
    }
    unsigned circle = (unsigned)spell / 6 + 1;
    dg.cast_circle = (uint8_t)circle;
    uint8_t where = dg.spell_flags[spell], need;
    if (dg.location == 0) {
        need = 8;
    } else if (dg.location < 0x80) {
        if ((dg.location == 0x12 && dg.lb_crown == 0) || dg.location == 0x1D) {
            print_text(S(0x4624));   /* "Absorbed!\n" */
            sound_sweep(2, 1000, 28000, 1, 0x2648);
            return 1;
        }
        need = dg.location < 0x21 ? 4 : 2;
    } else {
        need = 1;
    }
    if (!(where & need)) {
        print_text(S(0x462F));   /* "Not here!\n" */
        spell_fizzle();
        return 1;
    }
    if (dg.spells[spell] == 0) {
        print_text(S(0x463A));   /* "None mixed!\n" */
        return 1;
    }
    dg.spells[spell]--;
    PartyMember *m = &dg.party[member & 0x0F];
    if (m->mp < circle) {
        print_text(S(0x4647));   /* "M.P. too low!\n" */
        result = 0;
    } else {
        m->mp = (uint8_t)(m->mp - circle);
        result = -1;
        if (m->level < circle) result = 0;
        else switch (spell) {
        case 0x00: set_light(100); spell_effect(1); break;                    /* In Lor */
        case 0x01: cast_missile(0x30); break;                                 /* Grav Por */
        case 0x02: result = spell_an_zu(); break;
        case 0x03: result = spell_an_nox(); break;
        case 0x04: result = spell_mani(); break;
        case 0x05: result = spell_an_ylem(); break;
        case 0x06: result = spell_an_sanct(); break;
        case 0x07: spell_an_xen_corp(); break;
        case 0x08: change_wind(0, ask_spell_direction()); break;              /* Rel Hur */
        case 0x09: spell_in_wis(); break;
        case 0x0A: result = spell_kal_xen(); break;
        case 0x0B: result = spell_in_xen_mani(); break;
        case 0x0C: set_light(0xFF); spell_effect(3); break;                   /* Vas Lor */
        case 0x0D: cast_missile(0x31); break;                                 /* Vas Flam */
        case 0x0E: result = cast_field(0); break;                             /* In Flam Grav */
        case 0x0F: result = cast_field(1); break;                             /* In Nox Grav */
        case 0x10: result = cast_field(2); break;                             /* In Zu Grav */
        case 0x11: result = spell_in_por(); break;
        case 0x12: result = dispel_field(1); break;                           /* An Grav */
        case 0x13: start_timed_spell(4, 0x14, 'P'); break;                    /* In Sanct */
        case 0x14: result = cast_field(3); break;                             /* In Sanct Grav */
        case 0x15: case 0x16:                                                 /* Uus Por, Des Por: up / down a dungeon level */
            if (dg.location == 0x28) {
                result = 0;   /* not in Doom */
                break;
            }
            spell_effect(4);
            if (dungeon_change_level(1, spell == 0x15 ? -1 : 1) != 0) exit_dungeon();
            break;
        case 0x17: spell_wis_quas(); break;
        case 0x18: result = spell_in_bet_xen(); break;
        case 0x19: result = spell_an_ex_por(); break;
        case 0x1A:                                                            /* In Ex Por */
            result = magic_unlock_door();
            if (result != -1) spell_effect(5);
            break;
        case 0x1B: result = spell_vas_mani(); break;
        case 0x1C: cast_area_spell(dg.color_sleep, 1, dg.combat_turn); break; /* In Zu */
        case 0x1D: start_timed_spell(5, 0x1E, 'Q'); break;                    /* Rel Tym */
        case 0x1E: spell_in_vas_por_ylem(member); break;
        case 0x1F: start_timed_spell(6, 0x14, 'C'); break;                    /* Quas An Wis */
        case 0x20: start_timed_spell(6, 10, 'N'); break;                      /* In An */
        case 0x21: spell_effect(6); party_view_effect(); break;               /* Wis An Ylem */
        case 0x22: result = spell_an_xen_ex(); break;
        case 0x23: result = spell_rel_xen_bet(); break;
        case 0x24: result = spell_sanct_lor(); break;
        case 0x25: cast_missile(0x32); break;                                 /* Xen Corp */
        case 0x26: result = spell_in_quas_xen(); break;
        case 0x27:                                                            /* In Quas Wis */
            spell_effect(7);
            if (dg.location < 0x21) show_area_map(dg.party_y, dg.party_x);
            else show_dungeon_map();
            break;
        case 0x28: cast_area_spell(dg.color_poison, 2, dg.combat_turn); break;      /* In Nox Hur */
        case 0x29: spell_in_quas_corp(); break;
        case 0x2A:                                                            /* In Mani Corp */
            result = resurrect_member(0, ask_target_member());
            draw_status_bar();
            break;
        case 0x2B: result = summon_daemon(0); break;                          /* Kal Xen Corp */
        case 0x2C: cast_area_spell(dg.color_frame, 4, dg.combat_turn); break; /* In Vas Grav Corp */
        case 0x2D: cast_area_spell(dg.color_fire, 3, dg.combat_turn); break;  /* In Flam Hur */
        case 0x2E:
            result = spell_vas_rel_por();
            if (result != 0) turn = 0;
            break;
        case 0x2F: result = spell_an_tym(); break;
        }
    }
    if (result == 1) {
        print_text(S(0x4656));   /* "Success!\n" */
    } else if (result == 0) {
        print_text(S(0x4660));   /* "Failed!\n" */
        spell_fizzle();
    }
    return turn;
}

/* ---- Use ---- */

/* Scrolls: 0 light, 1 wind change, 2 protection, 3 negate magic, 4 view, 5 summon daemon,
 * 6 resurrection, 7 negate time. */
int use_scroll(int scroll)
{
    dg.scrolls[scroll & 7]--;
    print_text(S(0x466A));   /* "Scroll\n\n" */
    switch (scroll) {
    case 0:
        set_light(0xF0);
        print_text(S(0x4673));   /* "Light!\n" */
        spell_effect(0);
        return 1;
    case 1: {
        print_text(S(0x467B));   /* "Wind change!\n" */
        int dir = ask_spell_direction();
        if (dg.location >= 0x21) return 0;
        change_wind(1, dir);
        return 1;
    }
    case 2:
        print_text(S(0x4689));   /* "Protection!\n" */
        start_timed_spell(2, 100, 'P');
        return 1;
    case 3:
        print_text(S(0x4696));   /* "Negate magic!\n" */
        start_timed_spell(3, 0x14, 'N');
        return 1;
    case 4:
        print_text(S(0x46A5));   /* "View!\n" */
        if (dg.location >= 0x80) {
            print_text(S(0x46AC));   /* "Not here!\n" */
            return 1;
        }
        spell_effect(4);
        if (dg.location < 0x21) show_area_map(dg.party_y, dg.party_x);
        else show_dungeon_map();
        return 1;
    case 5:
        print_text(S(0x46B7));   /* "Summon Daemon!\n" */
        if (dg.location > 0x7F) return summon_daemon(1);
        print_text(S(0x46C7));   /* "Not here!\n" */
        return 1;
    case 6: {
        print_text(S(0x46D2));   /* "Resurrection!\n" */
        if (dg.location >= 0x80) {
            print_text(S(0x46E1));   /* "Not here!\n" */
            return 1;
        }
        int r = resurrect_member(1, ask_target_member());
        draw_status_bar();
        return r;
    }
    case 7:
        if (dg.location == 0x1D || dg.location == 0x28) {
            print_text(S(0x46EC));   /* "No effect!\n" */
            spell_fizzle();
            return 1;
        }
        print_text(S(0x46F8));   /* "Negate time!\n" */
        start_timed_spell(7, 0x14, 'T');
        return 1;
    }
    return 1;
}

/* Potions: 0 blue awakens, 1 yellow heals, 2 red cures, 3 green poisons, 4 orange sleeps,
 * 5 purple turns the drinker into a rat (combat), 6 black makes invisible (combat), 7 white
 * shows the surroundings. 1 time in 16 any potion is a sleeping draught, 1 in 16 a random one.
 * In combat the drinker is the one whose turn it is. */
int use_potion(int potion)
{
    int result = 1, member;
    dg.potions[potion & 7]--;
    print_text(S(0x4706));   /* "Potion\n" */
    member = dg.location < 0x80 ? ask_target_member() : C(dg.combat_turn)->kind;
    if (member < 0) return member;
    spell_effect(potion);
    int r = random_between(0x0F, 0);
    if (r == 0) potion = 4;
    else if (r == 1) potion = random_between(7, 0);
    PartyMember *m = &dg.party[member & 0x0F];
    Combatant *c = C(dg.combat_turn);
    switch (potion) {
    case 0:
        if (m->status != 'S') return 0;
        m->status = 'G';
        if (dg.location > 0x7F) {
            if (c->kind != member || (c->flags & 0xE8) != 0x88) return 0;
            wake_combatant(dg.combat_turn);
            return 1;
        }
        break;
    case 1:
        result = heal_member(member);
        if (result == 0) return 0;
        print_text(S(0x470E));   /* "Healed!\n" */
        break;
    case 2:
        if (m->status != 'P') return 0;
        m->status = 'G';
        print_text(S(0x4717));   /* "Poison cured!\n" */
        break;
    case 3:
        if (m->status != 'G') return 0;
        m->status = 'P';
        print_text(S(0x4726));   /* "POISONED!\n" */
        break;
    case 4:
        if (m->status != 'G') return 0;
        if (dg.location < 0x80) m->status = 'S';
        else sleep_combatant((unsigned)member);   /* the member number is passed as the slot */
        print_text(S(0x4731));   /* "Slept!\n" */
        break;
    case 5:
        if (dg.location > 0x7F) {
            print_text(S(0x4739));   /* "Poof!\n" */
            OBJ(c)->tile = OBJ(c)->tile2 = 0x90;
            return 1;
        }
        print_text(S(0x474C));   /* "\nNo noticeable effect now!\n" */
        return result;
    case 6:
        if (dg.location > 0x7F) {
            c->flags |= 0x10;
            print_text(S(0x4740));   /* "Invisible!\n" */
            OBJ(c)->tile = OBJ(c)->tile2 = 0x1D;
            return 1;
        }
        print_text(S(0x474C));
        return result;
    case 7:
        if (dg.location < 0x21) {
            party_view_effect();
            return 1;
        }
        print_text(S(0x474C));
        return result;
    default:
        return result;
    }
    draw_status_bar();
    return result;
}

/* A moonstone can be buried on grass, scrub, swamp, desert and hills (tiles 4-10) and on fields
 * (0x2C, 0x2D); a moongate will rise there at night. */
void use_moonstone(int moonstone)
{
    uint8_t t = *get_map_tile_ptr(dg.party_y, dg.party_x);
    print_text(S(0x4768));   /* "Moonstone " */
    if (dg.location < 0x21 && (t == 0x2C || t == 0x2D || (t > 3 && t < 0x0B))) {
        print_text(S(0x4773));   /* "buried!\n" */
        dg.moonstone_x[moonstone & 7] = dg.party_x;
        dg.moonstone_y[moonstone & 7] = dg.party_y;
        dg.moonstone_loc[moonstone & 7] = dg.location;
        dg.moonstone_z[moonstone & 7] = dg.z_level;
    } else {
        print_text(S(0x477C));   /* "cannot be buried here!\n" */
    }
}

/* A shard works only at its flame (tables ds:4882-488E: x 15, y 9 / 3 / 16, locations 0x1E,
 * 0x1F, 0x20, level 2 / 1 / 0xFF). Cast in while the matching Shadowlord, summoned by name,
 * stands just north, it destroys him. */
void use_shard(unsigned shard)
{
    static const uint16_t which[3] = {0x47CC, 0x47D9, 0x47E3};    /* Falsehood... Hatred... Cowardice... */
    static const uint16_t flame[3] = {0x4822, 0x482A, 0x4831};    /* Truth! Love! Courage! */
    static const uint16_t name[3] = {0x4858, 0x4861, 0x486A};     /* Faulinei Astaroth Nosfentor */
    print_text(S(0x4794));   /* "Gem Shard\n\nThou dost hold above thee the evil Shard of " */
    if (shard < 3) print_text(S(which[shard]));
    for (int f = 2000; f < 25000; f += 0x32) sound_sweep(0, f, 200, 1, 0xA50);
    for (int f = 25000; f > 2000; f -= 0x32) sound_sweep(0, f, 200, 1, 0xA50);
    if (ds_u8((uint16_t)(0x4882 + shard)) != dg.party_x || ds_u8((uint16_t)(0x4886 + shard)) != dg.party_y ||
        ds_u8((uint16_t)(0x488A + shard)) != dg.location || ds_u8((uint16_t)(0x488E + shard)) != dg.z_level) {
        print_text(S(0x47F0));   /* "\n\nNo effect!\n" */
        spell_fizzle();
        return;
    }
    wait_turns(7);
    print_text(S(0x47FE));   /* "\n\n...and cast it into the Flame of " */
    if (shard < 3) print_text(S(flame[shard]));
    screen_shake();
    screen_shake();
    screen_shake();
    wait_turns(3);
    if (find_object_at(dg.z_level, (uint8_t)(dg.party_y - 1), dg.party_x) != 0xFC) return;
    if (shard != dg.summoned_shadowlord) return;
    int npc = npc_for_object((int)dg.dir_x);
    if (npc >= 0) remove_npc(npc);   /* (the original doesn't check) */
    for (int i = 7; i != 0; i--) flash_square(dg.party_y - 1, dg.party_x);
    dg.shadowlord_locations[shard] = 0xFF;
    dg.shards[shard] = 0;
    dg.npc_dead_bits[0x1D] |= ds_u8((uint16_t)(0x4892 + shard));   /* his place in Stonegate stays empty */
    print_text(S(0x483B));   /* "\nThe doom of the Shadowlord " */
    print_text(S(name[shard]));
    print_text(S(0x4874));   /* " is wrought!\n" */
    victory_fanfare();
}

/* Using a worn item (Amulet, Crown, Badge) again takes it off. 1 if it wasn't on. */
int use_remove_item(unsigned item)
{
    if (item != dg.active_spell) return 1;
    print_text(S(0x4895));   /* "Removed!\n" */
    dg.active_spell_turns = 0;
    dg.active_spell = 0;
    dg.status_dirty = 1;
    return 0;
}

/* The "Items:" list: 0-7 scrolls, 8-15 potions, 16 carpet, 17 skull keys, 18-20 Lord British's
 * Amulet, Crown and Sceptre, 21-28 moonstones, 29-31 shards, 32 spyglass, 33 HMS Cape plans,
 * 34 sextant, 35 pocket watch, 36 black badge, 37 wooden box. */
void cmd_use(void)
{
    int ok = 1;
    collect_special_items();
    int first = list_next_item(0xFF, dg.special_item_list, 0x26, -1);
    if (first == -1) {
        print_text(S(0x489F));   /* "No usable items!\n" */
        return;
    }
    print_text(S(0x48B1));   /* "Item: " */
    clear_side_panel();
    select_text_window(1);
    draw_panel_title(S(0x48B8));   /* "Items:" */
    set_list_window(8);
    int item = ready_item_list('U', 0xFF, first);
    draw_list_header_bar();
    draw_panel_top_bar();
    draw_panel_bottom_bar();
    draw_status_bar();
    if (item < 0) return;
    if (item < 8) {
        ok = use_scroll(item);
    } else if (item < 0x10) {
        ok = use_potion(item - 8);
    } else if (item > 0x14 && item < 0x1D) {
        use_moonstone(item - 0x15);
    } else switch (item) {
    case 0x10:
        print_text(S(0x48BF));   /* "Carpet\n\n" */
        if (!(dg.location < 0x21 && *get_map_tile_ptr(dg.party_y, dg.party_x) != 0x0C)) {
            print_text(S(0x48F3));   /* "Not here!\n" */
        } else if (dg.transport == 0x1C) {
            print_text(S(0x48C8));   /* "Boarded!\n" */
            dg.transport = (uint8_t)(random_between(1, 0) + 0x14);
            dg.magic_carpets--;
        } else {
            print_text(S((dg.transport & 0xF8) == 0x20 ? 0x48D2 : 0x48E4));   /* "X-it ship first!\n" / "Only on foot!\n" */
        }
        break;
    case 0x11:   /* a skull key opens a magically locked door, like In Ex Por */
        dg.odd_keys--;
        print_text(S(0x48FE));   /* "Skull Key\n" */
        if (dg.location < 0x21 || dg.location > 0x7F) {
            ok = magic_unlock_door();
            if (ok != 0 && dg.location < 0x80) flash_square((int)dg.dir_y, (int)dg.dir_x);
        } else {
            print_text(S(0x4909));   /* "Not here!\n" */
        }
        break;
    case 0x12:
        print_text(S(0x4914));   /* "Amulet\n\n" */
        if (use_remove_item(0x0E) != 0) {
            print_text(S(0x491D));   /* "Wearing the Amulet" */
            print_text(S(0x4A84));   /* " of Lord British...\n" */
            start_timed_spell(9, 0xFF, 0x0E);
        }
        break;
    case 0x13:
        print_text(S(0x4930));   /* "Crown\n\n" */
        if (use_remove_item(0x1C) != 0) {
            print_text(S(0x4938));   /* "Thou dost don the Crown" */
            print_text(S(0x4A84));
            start_timed_spell(9, 0xFF, 0x1C);
        }
        break;
    case 0x14: {   /* the Sceptre clears the 0x7n tiles around the party, or else dispels a field */
        print_text(S(0x4950));   /* "Sceptre\n\n" */
        print_text(S(0x495A));   /* "Wielding the Sceptre" */
        print_text(S(0x4A84));
        sound_sweep(1, 5000, -0x3CB0, 1, 0x1450);
        int cleared = 0;
        if (dg.location < 0x21 || dg.location > 0x28)
            for (int dx = -1; dx < 2; dx++)
                for (int dy = -1; dy < 2; dy++) {
                    uint8_t *p = get_map_tile_ptr((unsigned)(dg.party_y + dy), (unsigned)(dx + dg.party_x));
                    if ((*p & 0xF0) != 0x70) continue;
                    *p = 5;
                    world_tick();
                    sound_noise(2000, 3000, 10);
                    cleared++;
                }
        if (cleared == 0) {
            int r = dispel_field(0);
            if (r == 1) print_text(S(0x496F));        /* "Field dissolved!\n" */
            else if (r == 0) print_text(S(0x4981));   /* "No effect!\n" */
        }
        break;
    }
    case 0x1D: case 0x1E: case 0x1F:
        use_shard((unsigned)(item - 0x1D));
        break;
    case 0x20:
        print_text(S(0x498D));   /* "Spyglass\n\n" */
        if (!(dg.location < 0x21 && dg.z_level < 0x80)) {
            print_text(S(0x49AF));   /* "Not here!\n" */
        } else if (dg.hour < 6 || dg.hour > 0x12) {
            print_text(S(0x4998));   /* "Looking...\n" */
            look_sky();
        } else {
            print_text(S(0x49A4));   /* "No stars!\n" */
        }
        break;
    case 0x21:
        print_text(S(0x49BA));   /* "Plans\n\n" */
        if ((dg.transport & 0xF8) == 0x20) {
            dg.hms_cape_plans |= 0x80;
            print_text(S(0x49C2));   /* "Ship rigged for double speed!\n" */
        } else {
            print_text(S(0x49E1));   /* "Only usable on shipboard!\n" */
        }
        break;
    case 0x22:
        print_text(S(0x49FC));   /* "Sextant\n\n" */
        if (!(dg.z_level < 0x80 && dg.location == 0)) {
            print_text(S(0x4A06));   /* "Only outdoors!\n" */
        } else if (dg.hour < 6 || dg.hour > 0x12) {
            print_text(S(0x4A26));   /* "Position:" */
            print_coordinates();
        } else {
            print_text(S(0x4A16));   /* "Only at night!\n" */
        }
        break;
    case 0x23: {
        print_text(S(0x4A30));   /* "Watch\n\nThe pocket watch reads " */
        int h = dg.hour % 12;
        print_number(' ', 1, h == 0 ? 12 : h);
        put_char(':');
        print_number('0', 2, dg.minute);
        print_text(S(dg.hour < 12 ? 0x4A55 : 0x4A4F));   /* " AM.\n" / " PM.\n" */
        break;
    }
    case 0x24:
        print_text(S(0x4A5B));   /* "Badge\n\n" */
        if (use_remove_item(0x1D) != 0) {
            print_text(S(0x4A63));   /* "Badge worn!\n" */
            dg.active_spell = 0x1D;
            dg.active_spell_turns = 0xFF;
            dg.status_dirty = 1;
        }
        break;
    case 0x25:
        print_text(S(0x4A70));   /* "Box\n\nHow?\n" */
        break;
    }
    if (ok == 0) {
        print_text(S(0x4A7B));   /* "Failed!\n" */
        spell_fizzle();
    }
}

/* ---- the area spells ---- */

/* Two pixels of the cloud. 0 ends the ray: outside the view, or (on odd rows only, where the
 * square under the pixel is looked up into dir_x / dir_y) on a square that blocks sight. */
int plot_spell_pixel(unsigned color, unsigned y, int x)
{
    if (x < 8 || x > 0xB6 || (int16_t)y < 8 || (int16_t)y > 0xB6) {
        dg.dir_x = dg.dir_y = 0xFFFF;
        return 0;
    }
    set_draw_color((uint16_t)color);
    plot_pixel((int)y, x);
    plot_pixel((int)y, x + 1);
    speaker_on((unsigned)random_between(10000, 100));
    sound_crackle(300);   /* port: the original's pixel loop takes about this long on its own */
    if ((y & 1) == 0) return 1;
    pixel_to_square((int)y, x);
    if ((int16_t)dg.dir_x > -1 && (int16_t)dg.dir_y > -1) return is_square_visible(dg.dir_y, dg.dir_x);
    return 0;
}

/* A fan of 21 rays from the caster's edge in direction `dir` (1-4 = W E N S). Each ray moves a
 * pixel at a time along the direction and sideways whenever its error term (steps of 10 against
 * the slopes at ds:1CF0: 10 ... 190, 2000, 190 ... 10) runs out, the first ten to one side, the
 * rest to the other. The rays advance in random bursts of 0-14 pixels until all have ended.
 * Squares showing an object (view_mask 0) that a ray crosses are listed, from index 1, at most
 * 62: the column goes to hit_y[] and the row to hit_x[] (the names are the original's). */
int trace_spell_cone(int dir, int caster, int tile, int *hit_x, int *hit_y)
{
    int x0 = C(caster)->x * 0x10 + 8, y0 = C(caster)->y * 0x10 + 8;
    if (dir == 1) { y0 += 8; }
    else if (dir == 2) { x0 += 0x10; y0 += 8; }
    else if (dir == 3) { x0 += 8; }
    else if (dir == 4) { x0 += 8; y0 += 0x10; }
    int done[21] = {0}, px[21], py[21], count = 0;
    int16_t err[21];
    for (int i = 0; i < 21; i++) {
        err[i] = (int16_t)ds_u16((uint16_t)(offsetof(struct DGroup, cone_table) + i * 2));
        px[i] = x0;
        py[i] = y0;
    }
    for (;;) {
        int ended = 0;
        for (int i = 0; i < 21; i++) {
            if (done[i]) {
                ended++;
                continue;
            }
            int burst = random_upto(0x0F);
            for (int step = 0; step < burst && !done[i]; step++) {
                if (plot_spell_pixel((unsigned)(tile + 8), (unsigned)py[i], px[i]) == 0) {
                    done[i] = 1;
                } else if (count < 0x3E && (int16_t)dg.dir_x > -1 && (int16_t)dg.dir_y > -1 && dg.dir_x < 0x0B && dg.dir_y < 0x0B &&
                           dg.view_mask[dg.dir_y * 0x20 + dg.dir_x] == 0) {
                    count++;
                    hit_y[count] = dg.dir_x;
                    hit_x[count] = dg.dir_y;
                    dg.view_mask[dg.dir_y * 0x20 + dg.dir_x] = 0xFF;
                }
                if (done[i]) break;
                err[i] -= 10;
                if (dir == 1) px[i]--;
                else if (dir == 2) px[i]++;
                else if (dir == 3) py[i]--;
                else if (dir == 4) py[i]++;
                if (err[i] < 1) {
                    int side = i < 10 ? -1 : 1;
                    if (dir == 1) py[i] -= side;
                    else if (dir == 2) py[i] += side;
                    else if (dir == 3) px[i] += side;
                    else if (dir == 4) px[i] -= side;
                    err[i] += (int16_t)ds_u16((uint16_t)(offsetof(struct DGroup, cone_table) + i * 2));
                }
            }
        }
        if (ended > 20) {
            speaker_off();
            return count;
        }
        delay_loop(1, 1);   /* port: plays the crackle queued so far and shows the cloud growing */
    }
}

/* In Zu (1: sleep), In Nox Hur (2: poison), In Flam Hur (3: fire, 0-30) and In Vas Grav Corp
 * (4: death). Sleep and death can be resisted and don't touch the immune; poison misses
 * those whose speed beats a 1-30 roll. The cloud strikes friend and foe alike. A fighter is
 * marked in bit 7 of its timer so it is struck once. */
void cast_area_spell(int tile, int kind, int caster)
{
    static int hit_x[64], hit_y[64];
    PartyMember *m = &dg.party[C(caster)->kind & 0x0F];
    int dir = ask_spell_direction();
    sound_noise(700, kind == 1 ? 16000 : kind == 2 ? 0x4B00 : 0x5140, 800);
    int count = trace_spell_cone(dir, caster, tile, hit_x, hit_y);
    for (int k = 1; k <= count; k++) {
        for (int slot = 0x1F; slot >= 0; slot--) {
            Combatant *c = C(slot);
            if (c->x != hit_y[k] || c->y != hit_x[k] || (c->timer & 0x80) || (c->flags & 0x20) || c->flags == 0) continue;
            c->timer |= 0x80;
            if (kind == 1) {
                if (magic_resist_roll(0, slot, caster) != 0 || is_spell_immune_kind(slot)) break;
                flash_combatant((unsigned)slot);
                sleep_combatant((unsigned)slot);
            } else if (kind == 2) {
                int speed = get_combatant_stat(-2, slot);
                if (random_stat_roll() < speed) break;
                flash_combatant((unsigned)slot);
                apply_poison_hit(caster, slot);
            } else if (kind == 3) {
                flash_combatant((unsigned)slot);
                add_word_capped(9999, (uint16_t)apply_damage((unsigned)random_upto(0x1E), slot), &m->xp);
            } else {
                if (kind != 4 || magic_resist_roll(0, slot, caster) != 0 || is_spell_immune_kind(slot)) break;
                flash_combatant((unsigned)slot);
                add_word_capped(9999, (uint16_t)apply_damage(99, slot), &m->xp);
            }
            report_hit(caster, slot);
            break;
        }
    }
    for (int slot = 0; slot < 0x20; slot++) C(slot)->timer &= 0x7F;
}
