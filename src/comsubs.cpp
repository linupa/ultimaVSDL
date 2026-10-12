/* comsubs.cpp - COMSUBS.OVL: attacks, aiming and missiles. See comsubs.h. */
#include <string.h>
#include "combat.h"
#include "commands.h"
#include "comsubs.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "items.h"
#include "map.h"
#include "misc.h"
#include "monsters.h"
#include "objects.h"
#include "party.h"
#include "picture.h"
#include "platform.h"
#include "spells.h"
#include "status.h"
#include "strings.h"
#include "text.h"
#include "view.h"
#include "world.h"

#define C(slot) (&combat.combatants[(slot) & 0x1F])
#define OBJ(c) (&map.objects[(c)->object & 0x1F])

/* Intelligence against intelligence; spells 0x30, 0x31 and 0x33 up can't be resisted. */
int Combat::magic_resist_roll(int missile, int target, int attacker)
{
    if (missile == 0x30 || missile == 0x31 || missile >= 0x33) return 0;
    int a = get_combatant_stat(-1, attacker), t = get_combatant_stat(-1, target);
    return random_stat_roll() < (t - a + 0x1E) / 2;
}

/* Each field (objects 0xE8-0xEB) has 1 chance in 16 to vanish at the end of a turn. */
void Combat::decay_combat_fields(void)
{
    for (int i = 0; i < 0x20; i++)
        if ((map.objects[i].tile & 0xFC) == 0xE8 && random_between(0xFF, 0) < 0x10) combat_remove(i + 1);
}

void Combat::print_combatant_name(int slot)
{
    Combatant *c = C(slot);
    print_text((c->flags & 0x80) ? party[c->kind & 0x0F].name : monsters.name(c->kind));
}

/* Called with the target: "<target> missed!". */
void Combat::print_missed(int slot)
{
    if (combat.cast_circle != 0) {
        print_text(strings[Strings::Failed_99A0]);   /* "Failed!\n" */
        return;
    }
    print_combatant_name(slot);
    print_text(strings[Strings::missed_99AA]);   /* " missed!\n" */
}

/* Not under Negate Magic. Possessors (flag 0x40) pick a random slot: an able member there who
 * fails to resist changes sides (a daemon, type 0x26, is used up by it). Flag 0x800: turns
 * invisible or visible again, 1 turn in 8. Flag 0x400: gates in a daemon on a free random
 * square, 1 turn in 8. Returns 1 if the turn was used. */
int Combat::monster_special(int slot)
{
    Combatant *c = C(slot);
    unsigned type = c->kind, mf = monsters.flags(type);
    MapObject *o = OBJ(c);
    if (party.active_spell == 0x1C || party.active_spell == 0x4E || (c->flags & 0x80)) return 0;
    if (mf & 0x40) {
        int t = random_between(0x1F, 0);
        Combatant *tc = C(t);
        if ((tc->flags & 0x80) && !(tc->flags & 0x3D)) {
            if (magic_resist_roll(0, t, slot) == 0) {
                put_char('\n');
                tc->flags |= 1;
                if (tc->kind == party.active_player) party.active_player = 0xFF;
                draw_status_bar();
                print_combatant_name(t);
                print_text(strings[Strings::possessed_99B4]);   /* " possessed!\n" */
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
            print_text(strings[Strings::reappears_99C2]);   /* " reappears!" */
            c->flags &= 0xEF;
            o->tile2 = o->tile;
        } else {
            print_text(strings[Strings::disappears_99CE]);   /* " disappears!" */
            c->flags |= 0x10;
            o->tile2 = 0;
        }
        return 1;
    }
    Square pos = {0, 0};
    if (!(mf & 0x400) || random_between(0xFF, 0) > 0x1F || !random_combat_pos(&pos) ||
        !combat_tile_free(pos.y, pos.x, 0xD8)) return 0;
    int n = add_combatant(map.z, (uint8_t)pos.y, (uint8_t)pos.x, 0, 0x26);
    if (n == -1) return 0;
    put_char('\n');
    print_combatant_name(slot);
    print_text(strings[Strings::gates_in_a_daemon_99DC]);   /* " gates in a daemon!\n" */
    sound_sweep(0x0F, 1000, 5000, 1, 0xAC8);
    MapObject *d = OBJ(C(n));
    d->tile = d->tile2 = 0x16;
    dissolve_tile(pos.y, pos.x, 0x1D8);
    d->tile = d->tile2 = 0xD8;
    return 1;
}

/* combat_event tells what the blow did: 0x20 no damage ("grazed!"), 2 the target vanished,
 * 4 slept, 8 poisoned (already said). A monster's state is its morale level; a member is "hit!"
 * or, by type 0x2D, "dragged under!" (swallowed: flags 4, its object hidden). */
void Combat::report_hit(int attacker, int target)
{
    Combatant *t = C(target);
    uint8_t f = t->flags;
    combat.event &= 0xFE;
    if (combat.event & 0x20) {
        print_combatant_name(target);
        print_text(strings[Strings::grazed_99F2]);   /* " grazed!\n" */
        sound_slide(0x28, 1, 2000, 0x4B0);
    }
    if (combat.event & 0x22) {
        combat.event &= 0xDD;
        return;
    }
    if (f == 0 || (f & 0x20)) {
        print_combatant_name(target);
        print_text(strings[Strings::killed_99FC]);   /* " killed!\n" */
        combat.event |= 1;
    } else if (combat.event & 4) {
        print_combatant_name(target);
        print_text(strings[Strings::slept_9A06]);   /* " slept!\n" */
    } else if (!(combat.event & 8)) {
        print_combatant_name(target);
        if (!(f & 0x80)) {
            static const Strings::Id wounds[5] = {Strings::NONE, Strings::critical_9A64, Strings::heavily_wounded_9A50, Strings::lightly_wounded_9A3C, Strings::barely_wounded_9A2A};   /* critical! heavily, lightly, barely wounded! */
            int level = monster_morale(target);
            if (level >= 1 && level <= 4) print_text(strings[wounds[level]]);
        } else if (attacker != 0xFF && C(attacker)->kind == 0x2D) {
            print_text(strings[Strings::dragged_under_9A10]);   /* " dragged under!\n" */
            sound_slide(0x28, 1, 2000, 0x4B0);
            t->flags |= 4;
            OBJ(t)->tile2 = 0;
            wait_turns(4);
        } else {
            print_text(strings[Strings::hit_9A22]);   /* " hit!\n" */
        }
    }
    if (f & 0x80) {
        draw_status_bar();
        world_tick();
    }
    combat.event &= 0xF3;
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

int Combat::combatant_distance(int a, int b)
{
    return distance(C(a)->y, C(a)->x, C(b)->y, C(b)->x);
}

/* The cursor starts on the fighter's last target if that is still alive, visible and in range,
 * else on the fighter. Arrows and Home / End / PgUp / PgDn (the diagonals) move it within range;
 * Return or A fires at any square but one's own; Esc, or Space on one's own square, cancels. */
int Combat::aim_cursor(int range, unsigned slot)
{
    Combatant *c = C(slot);
    view.aim_mode = 1;
    unsigned t = OBJ(c)->skiffs;
    if (t > 0x1F || (C(t)->flags & 0x30) || C(t)->flags == 0 || OBJ(C(t))->tile == 0 || combatant_distance((int)t, (int)slot) > range) t = slot;
    combat.aim_x = C(t)->x;
    combat.aim_y = C(t)->y;
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
            if (c->x == combat.aim_x && c->y == combat.aim_y) cancel = 1;
            break;
        case 0x0D: case 'A':
            if (c->x != combat.aim_x || c->y != combat.aim_y) {
                put_char('\n');
                if (combat.cast_circle != 0) sound_noise(700, combat.cast_circle * 0x640 + 8000, 800);
                fire = 1;
            }
            break;
        }
        int nx = dx + combat.aim_x, ny = dy + combat.aim_y;
        dist = distance(c->y, c->x, ny, nx);
        if (dist <= range && nx > -1 && nx < 0x0B && ny > -1 && ny < 0x0B) {
            combat.aim_x = (uint8_t)nx;
            combat.aim_y = (uint8_t)ny;
        }
    }
    view.aim_mode = 0;
    if (cancel) {
        put_char('\n');
        return 0;
    }
    return dist;
}

/* A living fighter on the square that isn't swallowed (nor shown as tile 0xF4). */
int Combat::combatant_at(unsigned y, unsigned x)
{
    for (int i = 0; i < 0x20; i++) {
        Combatant *c = C(i);
        if (c->x != x || c->y != y || OBJ(c)->tile2 == 0xF4) continue;
        if ((c->flags & 0xC0) == 0 || (c->flags & 0x20) || (c->flags & 4)) continue;
        return i;
    }
    return -1;
}

Square Combat::random_near(int y, int x)
{
    Square sq;
    do {
        sq.x = (uint16_t)(random_between(3, 1) + x - 2);
        sq.y = (uint16_t)(random_between(3, 1) + y - 2);
    } while ((int16_t)sq.x < 0 || (int16_t)sq.x > 10 || (int16_t)sq.y < 0 || (int16_t)sq.y > 10);
    return sq;
}

/* A miss flies to a random neighbour of the target. Weapons 0x19 and 0x22 aren't drawn. Where
 * the missile lands, flaming oil (0x13) and the field spells leave a field: 0xEA fire, 0xE8
 * poison, 0xE9 sleep, 0xEB energy. Returns who stands there (never the one whose turn it is);
 * *landed is the square. */
int Combat::fire_missile(int anim, int hit, unsigned y, unsigned x, int attacker, Square *landed)
{
    unsigned tx = x, ty = y, ax = C(attacker)->x, ay = C(attacker)->y;
    int arrived = 0;   /* (unset in the original for an undrawn miss) */
    if (hit == 0) {
        Square near;
        do {
            near = random_near((int)y, (int)x);
        } while (ax == near.x && ay == near.y);
        tx = near.x;
        ty = near.y;
    }
    if (combat.attack_item == 0x19 || combat.attack_item == 0x22) {
        if (hit != 0) {
            tx = x;
            ty = y;
            arrived = 1;
        }
    } else {
        Square stopped = {0, 0};
        arrived = animate_missile(anim, (int)ty, (int)tx, (int)ay, (int)ax, &stopped);
        if (arrived == 0) {
            tx = stopped.x;
            ty = stopped.y;
        } else {
            combat_room_trigger(ty, tx);
        }
    }
    if (landed) *landed = Square{(uint16_t)tx, (uint16_t)ty};
    if (arrived == 0) return -1;
    int who = combatant_at(ty, tx);
    int field = 0;
    if (combat.attack_item == 0x13 || combat.attack_item == 0x35) field = 0xEA;
    else if (combat.attack_item == 0x33) field = 0xE8;
    else if (combat.attack_item == 0x34) field = 0xE9;
    else if (combat.attack_item == 0x36) field = 0xEB;
    if (field) add_combatant(map.z, (uint8_t)ty, (uint8_t)tx, 2, field);
    if ((combat.cast_circle == 0 || hit != 0) && who >= 0 && who != combat.turn) return who;
    return -1;
}

/* Thrown weapons (0x10, 0x15, 0x16) are used up when thrown 2 squares or more; the last one
 * leaves the hand. Bows (0x1A, 0x24) use arrows (0x1B), the crossbow (0x1C) quarrels (0x1D);
 * with the last one gone the weapon is unreadied from everyone and goes back to the stock. */
void Combat::consume_ammo(int hit, unsigned weapon)
{
    if (combat.cast_circle != 0) return;
    if (weapon < 0x17) {
        if (weapon < 0x15 && weapon != 0x10) return;
        if (hit < 2) return;
        if (party.inventory[weapon] != 0) party.inventory[weapon]--;
        else unequip_item(weapon, C(combat.turn)->kind);
        return;
    }
    uint8_t left;
    if (weapon == 0x1C) left = --party.inventory[0x1D];
    else if (weapon == 0x1A || weapon == 0x24) left = --party.inventory[0x1B];
    else return;
    if (left == 0) party.inventory[weapon] = (uint8_t)(party.inventory[weapon] + count_members_where(weapon));
}

/* An enemy that struck this fighter since its last turn and still stands next to it spoils
 * missile fire and spells. */
int Combat::check_interference(int slot)
{
    unsigned who = combat.last_attacker[slot & 0x1F];
    if (who == 0xFF) return 0;
    Combatant *w = C(who);
    if (w->flags == 0 || combatant_side((int)who) == 0 || (w->flags & 0x0C) || party.active_spell == 0x54 ||
        combatant_distance((int)who, slot) != 1) return 0;
    put_char('\n');
    print_combatant_name((int)who);
    print_text(strings[Strings::interferes_9A70]);   /* " interferes!\n" */
    return 1;
}

/* Weapon 0x26 comes back to the hand. Flaming oil (0x13) is used up when thrown. */
void Combat::ranged_attack(int anim, int weapon, unsigned slot)
{
    Combatant *c = C(slot);
    if ((weapon == 0x1A || weapon == 0x1C || weapon == 0x24 || weapon == 0x13 || weapon == 0x11) && check_interference((int)slot) != 0) return;
    print_text(strings[Strings::Aim_9A7E]);   /* "Aim! " */
    int dist = aim_cursor(items.range(weapon), slot);
    if (dist == 0) return;
    sound_slide(100, 5, 300, 0x514);
    if (weapon == 0x13) {
        if (party.inventory[0x13] == 0) unequip_item(0x13, C(combat.turn)->kind);
        else party.inventory[0x13]--;
    }
    MapObject *o = OBJ(c);
    o->skiffs = 0xFF;
    int defender = combatant_at(combat.aim_y, combat.aim_x), hit = 1;
    if (defender >= 0) {
        o->skiffs = (uint8_t)defender;
        consume_ammo(dist, (unsigned)weapon);
        hit = roll_to_hit(weapon, -(int)combat.cast_circle, (int)slot, defender);
    }
    Square landed = {0, 0};
    int who = fire_missile(anim, hit, combat.aim_y, combat.aim_x, (int)slot, &landed);
    int fx = landed.x, fy = landed.y;
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

void Combat::melee_attack(int weapon, unsigned target, int slot)
{
    sound_slide(0x96, 5, 0x2EE, 400);
    put_char('\n');
    if (roll_to_hit(weapon, -(int)combat.cast_circle, slot, (int)target) == 0) {
        print_missed((int)target);
        return;
    }
    flash_combatant(target);
    resolve_hit(slot, (int)target);
    report_hit(slot, (int)target);
}

/* A monster on the party's side uses its type's range and missile. */
void Combat::attack_with(int weapon, unsigned slot)
{
    Combatant *c = C(slot);
    uint8_t range, missile;
    if (weapon > 0x22) combat.magic_attack = 1;
    if (!(c->flags & 0x80)) {
        range = monsters.attack_range(c->kind);
        if (range == 1) range = 0;
        missile = monsters.missile(c->kind);
    } else {
        range = weapon == 0xFF ? 0 : items.range(weapon);
        missile = items.missile(weapon);
    }
    if (range != 0) {
        ranged_attack(missile, weapon, slot);
        return;
    }
    print_text(strings[Strings::Aim_9A84]);   /* "Aim! " */
    int ok = aim_cursor(1, slot);
    int target = combatant_at(combat.aim_y, combat.aim_x);
    if (ok == 0 || target == -1) {
        print_text(strings[Strings::Nothing_9A8A]);   /* "Nothing!\n" */
        return;
    }
    OBJ(c)->skiffs = (uint8_t)target;
    melee_attack(weapon, (unsigned)target, (int)slot);
}

void Combat::attack_with_item(int weapon_count, int item)
{
    if (item == 0xFF || items.damage(item) == 0) return;
    if (weapon_count > 1) {
        combat.cast_circle = 0;
        combat.magic_attack = 0;
        put_char('\n');
        print_text(items.name(item));
        print_text(strings[Strings::Colon_NL_9A94]);   /* ":\n" */
    }
    combat.attack_item = (uint8_t)item;
    print_text(strings[Strings::Attack_9A98]);   /* "Attack-" */
    attack_with(item, combat.turn);
}

/* A member attacks once with each weapon held: helm slot, left hand, right hand. */
void Combat::cmd_attack_combat(int weapon_count, int slot)
{
    Combatant *c = C(slot);
    if (!(c->flags & 0x80) || weapon_count == 0) {
        combat.attack_item = 0xFF;
        print_text(strings[Strings::Attack_9AA0]);   /* "Attack-" */
        attack_with(0xFF, (unsigned)slot);
        return;
    }
    PartyMember *m = &party[c->kind & 0x0F];
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
void Combat::trace_line(uint8_t *path_y, uint8_t *path_x, int to_y, int to_x, int from_y, int from_x)
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
void Combat::draw_missile_frame(uint8_t *path_y, uint8_t *path_x, int kind, int frame, uint8_t y, uint8_t x)
{
    int sx = 1, sy = 1, burst = -1;
    const int8_t *py = NULL, *px = NULL;
    if (kind == 2) py = px = Combat::missile_shapes;
    if (kind > 2) py = px = Combat::missile_shapes + 0x10;
    if (frame == 1) { if (py) py++; }
    else if (frame == 2) { if (px) px++; sx = -1; }
    else if (frame == 3) { if (py) py++; sy = -1; sx = -1; }
    else if (frame == 4) { if (px) px++; sy = -1; }
    set_draw_color(gfx.color_text);
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
    case 3: burst = gfx.color_fire + 8; break;
    case 4: burst = gfx.color_frame + 8; break;
    case 5: burst = gfx.color_poison + 8; break;
    case 6: burst = gfx.color_sleep + 8; break;
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
        const int8_t *burst_offsets = &missile_shapes[0x21BA - 0x2188];   /* ds:21BA: 16 offsets 0-5, inside the shapes */
        int fx = -1, fy = 1;
        for (int i = 0; i < 0x30; i++) {
            int a = random_between(0x0F, 0), b = random_between(0x0F, 0);
            if (random_between(1, 0)) fx = -fx;
            if (random_between(1, 0)) fy = -fy;
            set_draw_color((uint16_t)burst);
            plot_pixel(burst_offsets[b] * fy + y, burst_offsets[a] * fx + x);
        }
    }
}

Square pixel_to_square(int py, int px)
{
    if (px < 8 || px > 0xB7 || py < 8 || py > 0xB7) return Square{0xFFFF, 0xFFFF};
    return Square{(uint16_t)((px - 8) >> 4), (uint16_t)((py - 8) >> 4)};
}

/* By the bit table at ds:6A14 of tiles that can be seen (and shot) across. */
int is_square_visible(int y, int x)
{
    uint8_t t = view.mask[(x + y * 0x20) & 0x1FF];
    return view.seen_across(t) != 0;
}

/* The screen is copied to the back page, which then restores what each frame covered. The
 * missile advances 13 pixels a frame (6 in towns, 8 for kind 7) and stops, returning 0 with
 * *stopped the square, when it enters a square that blocks sight. */
int Combat::animate_missile(int kind, int to_y, int to_x, int from_y, int from_x, Square *stopped)
{
    Square sq = {0, 0};
    int step = 0x0D, frame = 0, n = 0;
    uint8_t *path_x = combat.path, *path_y = combat.path + Combat::PATH_SIZE;
    copy_page(1, 0);
    memset(path_x, 0xFF, 0x100);
    memset(path_y, 0xFF, 0x100);
    trace_line(path_y, path_x, to_y * 0x10 + 0x10, to_x * 0x10 + 0x10, from_y * 0x10 + 0x10, from_x * 0x10 + 0x10);
    if (map.is_outdoors() || !map.on_tile_map()) {
        if (kind == 7) step = 8;
    } else {
        step = 6;
    }
    int i = kind == 0 ? 4 : 0;
    for (;;) {
        if (path_x[i] == 0xFF) return 1;
        frame = (frame & 3) + 1;
        sq = pixel_to_square(path_y[i], path_x[i]);
        if (stopped) *stopped = sq;
        if (sq.outside()) return 1;
        draw_missile_frame(path_y + i, path_x + i, kind, frame, path_y[i], path_x[i]);
        (void)n;
        platform->present();
        delay_loop(0x28, 1);
        copy_page_rect(path_y[i] + 0x0F, (path_x[i] & 0xF8) + 0x0F, path_y[i] - 8, (path_x[i] & 0xF8) - 8, 0, 1);
        i += step;
        if (i > Combat::PATH_SIZE) return 1;
        if (path_x[i] == 0xFF) return 1;
        if (!is_square_visible(sq.y, sq.x) && (from_x != (int)sq.x || from_y != (int)sq.y)) return 0;
    }
}
