/* comsubs.c - COMSUBS.OVL: attacks, aiming and missiles. See comsubs.h. */
#include <string.h>
#include "combat.h"
#include "commands.h"
#include "comsubs.h"
#include "data.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "objects.h"
#include "picture.h"
#include "platform.h"
#include "status.h"
#include "text.h"
#include "view.h"
#include "world.h"

#define S(off) ds_str(off)
#define C(slot) (&dg.combatants[(slot) & 0x1F])
#define OBJ(c) (&dg.objects[(c)->object & 0x1F])
#define ITEM_RANGE(i) ds_u8((uint16_t)(offsetof(struct DGroup, item_range) + (i)))
#define ITEM_MISSILE(i) ds_u8((uint16_t)(offsetof(struct DGroup, item_missile) + (i)))
#define ITEM_DAMAGE(i) ds_u8((uint16_t)(offsetof(struct DGroup, item_damage) + (i)))
#define PATH_X 0xA728   /* 330 bytes each: pixel coordinates of the flight, 0xFF ends */
#define PATH_Y 0xA872

/* Intelligence against intelligence; spells 0x30, 0x31 and 0x33 up can't be resisted. */
int magic_resist_roll(int missile, int target, int attacker)
{
    if (missile == 0x30 || missile == 0x31 || missile >= 0x33) return 0;
    int a = get_combatant_stat(-1, attacker), t = get_combatant_stat(-1, target);
    return random_stat_roll() < (t - a + 0x1E) / 2;
}

/* Each field (objects 0xE8-0xEB) has 1 chance in 16 to vanish at the end of a turn. */
void decay_combat_fields(void)
{
    for (int i = 0; i < 0x20; i++)
        if ((dg.objects[i].tile & 0xFC) == 0xE8 && random_between(0xFF, 0) < 0x10) combat_remove(i + 1);
}

void print_combatant_name(int slot)
{
    Combatant *c = C(slot);
    print_text((c->flags & 0x80) ? dg.party[c->kind & 0x0F].name : MONSTER_NAME(c->kind));
}

/* Called with the target: "<target> missed!". */
void print_missed(int slot)
{
    if (dg.cast_circle != 0) {
        print_text(S(0x99A0));   /* "Failed!\n" */
        return;
    }
    print_combatant_name(slot);
    print_text(S(0x99AA));   /* " missed!\n" */
}

/* Not under Negate Magic. Possessors (flag 0x40) pick a random slot: an able member there who
 * fails to resist changes sides (a daemon, type 0x26, is used up by it). Flag 0x800: turns
 * invisible or visible again, 1 turn in 8. Flag 0x400: gates in a daemon on a free random
 * square, 1 turn in 8. Returns 1 if the turn was used. */
int monster_special(int slot)
{
    Combatant *c = C(slot);
    unsigned type = c->kind, mf = MONSTER_FLAGS(type);
    MapObject *o = OBJ(c);
    if (dg.active_spell == 0x1C || dg.active_spell == 0x4E || (c->flags & 0x80)) return 0;
    if (mf & 0x40) {
        int t = random_between(0x1F, 0);
        Combatant *tc = C(t);
        if ((tc->flags & 0x80) && !(tc->flags & 0x3D)) {
            if (magic_resist_roll(0, t, slot) == 0) {
                put_char('\n');
                tc->flags |= 1;
                if (tc->kind == dg.active_player) dg.active_player = 0xFF;
                draw_status_bar();
                print_combatant_name(t);
                print_text(S(0x99B4));   /* " possessed!\n" */
                sound_sweep(2, 1000, 30000, 1, 0xC1C);
                if (type == 0x26) combat_remove(-1 - slot);
            }
            return 1;
        }
    }
    if ((mf & 0x800) && random_between(0xFF, 0) <= 0x1F) {
        put_char('\n');
        print_combatant_name(slot);
        if (o->tile2 == 0) {
            print_text(S(0x99C2));   /* " reappears!" */
            c->flags &= 0xEF;
            o->tile2 = o->tile;
        } else {
            print_text(S(0x99CE));   /* " disappears!" */
            c->flags |= 0x10;
            o->tile2 = 0;
        }
        return 1;
    }
    if (!(mf & 0x400) || random_between(0xFF, 0) > 0x1F || !random_combat_pos() ||
        !combat_tile_free(dg.dir_y, dg.dir_x, 0xD8)) return 0;
    int n = add_combatant(dg.z_level, (uint8_t)dg.dir_y, (uint8_t)dg.dir_x, 0, 0x26);
    if (n == -1) return 0;
    put_char('\n');
    print_combatant_name(slot);
    print_text(S(0x99DC));   /* " gates in a daemon!\n" */
    sound_sweep(0x0F, 1000, 5000, 1, 0xAC8);
    MapObject *d = OBJ(C(n));
    d->tile = d->tile2 = 0x16;
    dissolve_tile(dg.dir_y, dg.dir_x, 0x1D8);
    d->tile = d->tile2 = 0xD8;
    return 1;
}

/* combat_event tells what the blow did: 0x20 no damage ("grazed!"), 2 the target vanished,
 * 4 slept, 8 poisoned (already said). A monster's state is its morale level; a member is "hit!"
 * or, by type 0x2D, "dragged under!" (swallowed: flags 4, its object hidden). */
void report_hit(int attacker, int target)
{
    Combatant *t = C(target);
    uint8_t f = t->flags;
    dg.combat_event &= 0xFE;
    if (dg.combat_event & 0x20) {
        print_combatant_name(target);
        print_text(S(0x99F2));   /* " grazed!\n" */
        sound_slide(0x28, 1, 2000, 0x4B0);
    }
    if (dg.combat_event & 0x22) {
        dg.combat_event &= 0xDD;
        return;
    }
    if (f == 0 || (f & 0x20)) {
        print_combatant_name(target);
        print_text(S(0x99FC));   /* " killed!\n" */
        dg.combat_event |= 1;
    } else if (dg.combat_event & 4) {
        print_combatant_name(target);
        print_text(S(0x9A06));   /* " slept!\n" */
    } else if (!(dg.combat_event & 8)) {
        print_combatant_name(target);
        if (!(f & 0x80)) {
            static const uint16_t wounds[5] = {0, 0x9A64, 0x9A50, 0x9A3C, 0x9A2A};   /* critical! heavily, lightly, barely wounded! */
            int level = monster_morale(target);
            if (level >= 1 && level <= 4) print_text(S(wounds[level]));
        } else if (attacker != 0xFF && C(attacker)->kind == 0x2D) {
            print_text(S(0x9A10));   /* " dragged under!\n" */
            sound_slide(0x28, 1, 2000, 0x4B0);
            t->flags |= 4;
            OBJ(t)->tile2 = 0;
            wait_turns(4);
        } else {
            print_text(S(0x9A22));   /* " hit!\n" */
        }
    }
    if (f & 0x80) {
        draw_status_bar();
        world_tick();
    }
    dg.combat_event &= 0xF3;
}

long distance_squared(int y1, int x1, int y2, int x2)
{
    return (long)(y2 - y1) * (y2 - y1) + (long)(x2 - x1) * (x2 - x1);
}

int distance(int y1, int x1, int y2, int x2)
{
    int rest = (int)distance_squared(y1, x1, y2, x2), root = 0;
    for (int odd = 1; odd <= rest; odd += 2) {
        rest -= odd;
        root++;
    }
    return root;
}

int combatant_distance(int a, int b)
{
    return distance(C(a)->y, C(a)->x, C(b)->y, C(b)->x);
}

/* The cursor starts on the fighter's last target if that is still alive, visible and in range,
 * else on the fighter. Arrows and Home / End / PgUp / PgDn (the diagonals) move it within range;
 * Return or A fires at any square but one's own; Esc, or Space on one's own square, cancels. */
int aim_cursor(int range, unsigned slot)
{
    Combatant *c = C(slot);
    dg.aim_mode = 1;
    unsigned t = OBJ(c)->skiffs;
    if (t > 0x1F || (C(t)->flags & 0x30) || C(t)->flags == 0 || OBJ(C(t))->tile == 0 || combatant_distance((int)t, (int)slot) > range) t = slot;
    dg.aim_x = C(t)->x;
    dg.aim_y = C(t)->y;
    int cancel = 0, fire = 0, dist = 0;
    while (!fire && !cancel) {
        int dx = 0, dy = 0;
        world_tick();
        int key = get_command_key();
        if (game_quit()) key = 0x1B;
        switch (key) {
        case 0x1B: cancel = 1; break;
        case 1: dx = -1; break;
        case 2: dx = 1; break;
        case 3: dy = -1; break;
        case 4: dy = 1; break;
        case 0xD3: dx = -1; dy = -1; break;
        case 0xD4: dx = -1; dy = 1; break;
        case 0xD5: dx = 1; dy = -1; break;
        case 0xD6: dx = 1; dy = 1; break;
        case ' ':
            if (c->x == dg.aim_x && c->y == dg.aim_y) cancel = 1;
            break;
        case 0x0D: case 'A':
            if (c->x != dg.aim_x || c->y != dg.aim_y) {
                put_char('\n');
                if (dg.cast_circle != 0) sound_noise(700, dg.cast_circle * 0x640 + 8000, 800);
                fire = 1;
            }
            break;
        }
        int nx = dx + dg.aim_x, ny = dy + dg.aim_y;
        dist = distance(c->y, c->x, ny, nx);
        if (dist <= range && nx > -1 && nx < 0x0B && ny > -1 && ny < 0x0B) {
            dg.aim_x = (uint8_t)nx;
            dg.aim_y = (uint8_t)ny;
        }
    }
    dg.aim_mode = 0;
    if (cancel) {
        put_char('\n');
        return 0;
    }
    return dist;
}

/* A living fighter on the square that isn't swallowed (nor shown as tile 0xF4). */
int combatant_at(unsigned y, unsigned x)
{
    for (int i = 0; i < 0x20; i++) {
        Combatant *c = C(i);
        if (c->x != x || c->y != y || OBJ(c)->tile2 == 0xF4) continue;
        if ((c->flags & 0xC0) == 0 || (c->flags & 0x20) || (c->flags & 4)) continue;
        return i;
    }
    return -1;
}

void random_near(int y, int x)
{
    do {
        dg.dir_x = (uint16_t)(random_between(3, 1) + x - 2);
        dg.dir_y = (uint16_t)(random_between(3, 1) + y - 2);
    } while ((int16_t)dg.dir_x < 0 || (int16_t)dg.dir_x > 10 || (int16_t)dg.dir_y < 0 || (int16_t)dg.dir_y > 10);
}

/* A miss flies to a random neighbour of the target. Weapons 0x19 and 0x22 aren't drawn. Where
 * the missile lands, flaming oil (0x13) and the field spells leave a field: 0xEA fire, 0xE8
 * poison, 0xE9 sleep, 0xEB energy. Returns who stands there (never the one whose turn it is);
 * dir_x / dir_y are the square. */
int fire_missile(int anim, int hit, unsigned y, unsigned x, int attacker)
{
    unsigned tx = x, ty = y, ax = C(attacker)->x, ay = C(attacker)->y;
    int arrived = 0;   /* (unset in the original for an undrawn miss) */
    if (hit == 0) {
        do {
            random_near((int)y, (int)x);
        } while (ax == dg.dir_x && ay == dg.dir_y);
        tx = dg.dir_x;
        ty = dg.dir_y;
    }
    if (dg.attack_item == 0x19 || dg.attack_item == 0x22) {
        if (hit != 0) {
            tx = x;
            ty = y;
            arrived = 1;
        }
    } else {
        arrived = animate_missile(anim, (int)ty, (int)tx, (int)ay, (int)ax);
        if (arrived == 0) {
            tx = dg.dir_x;
            ty = dg.dir_y;
        } else {
            combat_room_trigger(ty, tx);
        }
    }
    dg.dir_x = (uint16_t)tx;
    dg.dir_y = (uint16_t)ty;
    if (arrived == 0) return -1;
    int who = combatant_at(ty, tx);
    int field = 0;
    if (dg.attack_item == 0x13 || dg.attack_item == 0x35) field = 0xEA;
    else if (dg.attack_item == 0x33) field = 0xE8;
    else if (dg.attack_item == 0x34) field = 0xE9;
    else if (dg.attack_item == 0x36) field = 0xEB;
    if (field) add_combatant(dg.z_level, (uint8_t)ty, (uint8_t)tx, 2, field);
    dg.dir_x = (uint16_t)tx;
    dg.dir_y = (uint16_t)ty;
    if ((dg.cast_circle == 0 || hit != 0) && who >= 0 && who != dg.combat_turn) return who;
    return -1;
}

/* Thrown weapons (0x10, 0x15, 0x16) are used up when thrown 2 squares or more; the last one
 * leaves the hand. Bows (0x1A, 0x24) use arrows (0x1B), the crossbow (0x1C) quarrels (0x1D);
 * with the last one gone the weapon is unreadied from everyone and goes back to the stock. */
void consume_ammo(int hit, unsigned weapon)
{
    if (dg.cast_circle != 0) return;
    if (weapon < 0x17) {
        if (weapon < 0x15 && weapon != 0x10) return;
        if (hit < 2) return;
        if (dg.inventory[weapon] != 0) dg.inventory[weapon]--;
        else unequip_item(weapon, C(dg.combat_turn)->kind);
        return;
    }
    uint8_t left;
    if (weapon == 0x1C) left = --dg.inventory[0x1D];
    else if (weapon == 0x1A || weapon == 0x24) left = --dg.inventory[0x1B];
    else return;
    if (left == 0) dg.inventory[weapon] = (uint8_t)(dg.inventory[weapon] + count_members_where(weapon));
}

/* An enemy that struck this fighter since its last turn and still stands next to it spoils
 * missile fire and spells. */
int check_interference(int slot)
{
    unsigned who = dg.last_attacker[slot & 0x1F];
    if (who == 0xFF) return 0;
    Combatant *w = C(who);
    if (w->flags == 0 || combatant_side((int)who) == 0 || (w->flags & 0x0C) || dg.active_spell == 0x54 ||
        combatant_distance((int)who, slot) != 1) return 0;
    put_char('\n');
    print_combatant_name((int)who);
    print_text(S(0x9A70));   /* " interferes!\n" */
    return 1;
}

/* Weapon 0x26 comes back to the hand. Flaming oil (0x13) is used up when thrown. */
void ranged_attack(int anim, int weapon, unsigned slot)
{
    Combatant *c = C(slot);
    if ((weapon == 0x1A || weapon == 0x1C || weapon == 0x24 || weapon == 0x13 || weapon == 0x11) && check_interference((int)slot) != 0) return;
    print_text(S(0x9A7E));   /* "Aim! " */
    int dist = aim_cursor(ITEM_RANGE(weapon), slot);
    if (dist == 0) return;
    sound_slide(100, 5, 300, 0x514);
    if (weapon == 0x13) {
        if (dg.inventory[0x13] == 0) unequip_item(0x13, C(dg.combat_turn)->kind);
        else dg.inventory[0x13]--;
    }
    MapObject *o = OBJ(c);
    o->skiffs = 0xFF;
    int defender = combatant_at(dg.aim_y, dg.aim_x), hit = 1;
    if (defender >= 0) {
        o->skiffs = (uint8_t)defender;
        consume_ammo(dist, (unsigned)weapon);
        hit = roll_to_hit(weapon, -(int)dg.cast_circle, (int)slot, defender);
    }
    int who = fire_missile(anim, hit, dg.aim_y, dg.aim_x, (int)slot);
    int fx = dg.dir_x, fy = dg.dir_y;
    if (who < 0) {
        if (defender >= 0) {
            put_char('\n');
            print_missed(defender);
        }
    } else {
        flash_combatant((unsigned)who);
        put_char('\n');
        resolve_hit((int)slot, who);
        report_hit((int)slot, who);
    }
    if (weapon == 0x26) animate_missile(anim, c->y, c->x, fy, fx);
}

void melee_attack(int weapon, unsigned target, int slot)
{
    sound_slide(0x96, 5, 0x2EE, 400);
    put_char('\n');
    if (roll_to_hit(weapon, -(int)dg.cast_circle, slot, (int)target) == 0) {
        print_missed((int)target);
        return;
    }
    flash_combatant(target);
    resolve_hit(slot, (int)target);
    report_hit(slot, (int)target);
}

/* A monster on the party's side uses its type's range and missile. */
void attack_with(int weapon, unsigned slot)
{
    Combatant *c = C(slot);
    uint8_t range, missile;
    if (weapon > 0x22) dg.magic_attack = 1;
    if (!(c->flags & 0x80)) {
        range = ds_u8((uint16_t)(offsetof(struct DGroup, monster_attack_range) + c->kind));
        if (range == 1) range = 0;
        missile = ds_u8((uint16_t)(offsetof(struct DGroup, monster_missile) + c->kind));
    } else {
        range = weapon == 0xFF ? 0 : ITEM_RANGE(weapon);
        missile = ITEM_MISSILE(weapon);
    }
    if (range != 0) {
        ranged_attack(missile, weapon, slot);
        return;
    }
    print_text(S(0x9A84));   /* "Aim! " */
    int ok = aim_cursor(1, slot);
    int target = combatant_at(dg.aim_y, dg.aim_x);
    if (ok == 0 || target == -1) {
        print_text(S(0x9A8A));   /* "Nothing!\n" */
        return;
    }
    OBJ(c)->skiffs = (uint8_t)target;
    melee_attack(weapon, (unsigned)target, (int)slot);
}

void attack_with_item(int weapon_count, int item)
{
    if (item == 0xFF || ITEM_DAMAGE(item) == 0) return;
    if (weapon_count > 1) {
        dg.cast_circle = 0;
        dg.magic_attack = 0;
        put_char('\n');
        print_text(S(ds_u16((uint16_t)(offsetof(struct DGroup, item_names) + item * 2))));
        print_text(S(0x9A94));   /* ":\n" */
    }
    dg.attack_item = (uint8_t)item;
    print_text(S(0x9A98));   /* "Attack-" */
    attack_with(item, dg.combat_turn);
}

/* A member attacks once with each weapon held: helm slot, left hand, right hand. */
void cmd_attack_combat(int weapon_count, int slot)
{
    Combatant *c = C(slot);
    if (!(c->flags & 0x80) || weapon_count == 0) {
        dg.attack_item = 0xFF;
        print_text(S(0x9AA0));   /* "Attack-" */
        attack_with(0xFF, (unsigned)slot);
        return;
    }
    PartyMember *m = &dg.party[c->kind & 0x0F];
    if (weapon_count > 1) {
        set_cursor(get_cursor_row(), 0);
        put_char(' ');
    }
    attack_with_item(weapon_count, m->helm);
    attack_with_item(weapon_count, m->left_hand);
    attack_with_item(weapon_count, m->right_hand);
}

/* A line in pixels from one square's centre to another's, stepping x and catching up in y by
 * the slope in hundredths; at most 328 points, ended by 0xFF. */
void trace_line(uint8_t *path_y, uint8_t *path_x, int to_y, int to_x, int from_y, int from_x)
{
    int sx = to_x < from_x ? -1 : 1, sy = to_y < from_y ? -1 : 1, room = 0x148;
    int slope = to_x == from_x ? 0x4B00 : ((to_y - from_y) * 100) / (to_x - from_x);
    if (slope < 0) slope = -slope;
    int nx = to_x - from_x, ny = to_y - from_y;
    if (nx < 0) nx = -nx;
    if (ny < 0) ny = -ny;
    *path_x = (uint8_t)from_x;
    *path_y = (uint8_t)from_y;
    int acc = slope;
    while (path_x++, path_y++, nx > 0 || ny > 0) {
        do {
            if (acc < 1 || ny < 1) break;
            acc -= 100;
            from_y += sy;
            *path_y++ = (uint8_t)from_y;
            ny--;
            *path_x++ = (uint8_t)from_x;
        } while (--room != 0);
        if (room == 0) break;
        nx--;
        acc += slope;
        from_x += sx;
        *path_y = (uint8_t)from_y;
        *path_x = (uint8_t)from_x;
        room--;
    }
    *path_x = 0xFF;
    *path_y = 0xFF;
}

/* kind 0: a streak along the last 9 points of the path; 1: a ball; 2 and 7: line figures from
 * the point lists at ds:2188 / ds:2198, turned by the frame (1-4); 3-6: a burst of 48 pixels in
 * the bright version of the fire, energy, poison or sleep colour. */
void draw_missile_frame(uint8_t *path_y, uint8_t *path_x, int kind, int frame, uint8_t y, uint8_t x)
{
    int sx = 1, sy = 1, burst = -1;
    const int8_t *py = NULL, *px = NULL;
    if (kind == 2) py = px = (const int8_t *)ds_ptr(0x2188);
    if (kind > 2) py = px = (const int8_t *)ds_ptr((uint16_t)offsetof(struct DGroup, missile_shapes));
    if (frame == 1) { if (py) py++; }
    else if (frame == 2) { if (px) px++; sx = -1; }
    else if (frame == 3) { if (py) py++; sy = -1; sx = -1; }
    else if (frame == 4) { if (px) px++; sy = -1; }
    set_draw_color(dg.color_text);
    switch (kind) {
    case 0:
        for (int i = 0; i < 9 && path_x[i - 4] != 0xFF; i++) draw_line(path_y[i - 4], path_x[i - 4] + 1, path_y[i - 4], path_x[i - 4]);
        break;
    case 1:
        draw_line(y - 2, x + 1, y - 2, x - 1);
        draw_line(y - 1, x + 2, y - 1, x - 2);
        draw_line(y, x + 2, y, x - 2);
        draw_line(y + 1, x + 2, y + 1, x - 2);
        draw_line(y + 2, x + 1, y + 2, x - 1);
        break;
    case 2:
        for (int i = 0; i < 4; i++, px += 4, py += 4) draw_line(py[2] * sy + y, px[2] * sx + x, py[0] * sy + y, px[0] * sx + x);
        break;
    case 3: burst = dg.color_fire + 8; break;
    case 4: burst = dg.color_frame + 8; break;
    case 5: burst = dg.color_poison + 8; break;
    case 6: burst = dg.color_sleep + 8; break;
    case 7:
        for (int i = 0; i < 7; i++, px += 4, py += 4) draw_line(py[2] * sy + y, px[2] * sx + x, py[0] * sy + y, px[0] * sx + x);
        px -= 4;
        py -= 4;
        plot_pixel(py[4] * sy + y, px[4] * sx + x);
        plot_pixel(py[6] * sy + y, px[6] * sx + x);
        plot_pixel(py[8] * sy + y, px[8] * sx + x);
        break;
    }
    if (burst >= 0) {
        int fx = -1, fy = 1;
        for (int i = 0; i < 0x30; i++) {
            int a = random_between(0x0F, 0), b = random_between(0x0F, 0);
            if (random_between(1, 0)) fx = -fx;
            if (random_between(1, 0)) fy = -fy;
            set_draw_color((uint16_t)burst);
            plot_pixel(ds_u8((uint16_t)(0x21BA + b)) * fy + y, ds_u8((uint16_t)(0x21BA + a)) * fx + x);
        }
    }
}

void pixel_to_square(int py, int px)
{
    if (px < 8 || px > 0xB7 || py < 8 || py > 0xB7) {
        dg.dir_x = dg.dir_y = 0xFFFF;
        return;
    }
    dg.dir_x = (uint16_t)((px - 8) >> 4);
    dg.dir_y = (uint16_t)((py - 8) >> 4);
}

/* By the bit table at ds:6A14 of tiles that can be seen (and shot) across. */
int is_square_visible(int y, int x)
{
    uint8_t t = dg.view_mask[(x + y * 0x20) & 0x1FF];
    return ((0x80 >> (t & 7)) & ds_u8((uint16_t)(0x6A14 + (t >> 3)))) != 0;
}

/* The screen is copied to the back page, which then restores what each frame covered. The
 * missile advances 13 pixels a frame (6 in towns, 8 for kind 7) and stops, returning 0 with
 * dir_x / dir_y the square, when it enters a square that blocks sight. */
int animate_missile(int kind, int to_y, int to_x, int from_y, int from_x)
{
    int step = 0x0D, frame = 0, n = 0;
    uint8_t *path_x = ds_ptr(PATH_X), *path_y = ds_ptr(PATH_Y);
    copy_page(1, 0);
    memset(path_x, 0xFF, 0x100);
    memset(path_y, 0xFF, 0x100);
    trace_line(path_y, path_x, to_y * 0x10 + 0x10, to_x * 0x10 + 0x10, from_y * 0x10 + 0x10, from_x * 0x10 + 0x10);
    if (dg.location == 0 || dg.location > 0x20) {
        if (kind == 7) step = 8;
    } else {
        step = 6;
    }
    int i = kind == 0 ? 4 : 0;
    for (;;) {
        if (path_x[i] == 0xFF) return 1;
        frame = (frame & 3) + 1;
        pixel_to_square(path_y[i], path_x[i]);
        if (dg.dir_x == 0xFFFF) return 1;
        draw_missile_frame(path_y + i, path_x + i, kind, frame, path_y[i], path_x[i]);
        (void)n;
        platform_present();
        delay_loop(0x28, 1);
        copy_page_rect(path_y[i] + 0x0F, (path_x[i] & 0xF8) + 0x0F, path_y[i] - 8, (path_x[i] & 0xF8) - 8, 0, 1);
        i += step;
        if (i > PATH_Y - PATH_X) return 1;
        if (path_x[i] == 0xFF) return 1;
        if (!is_square_visible(dg.dir_y, dg.dir_x) && (from_x != (int)dg.dir_x || from_y != (int)dg.dir_y)) return 0;
    }
}
