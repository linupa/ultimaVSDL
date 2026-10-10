/* dungeon.c - the dungeons (DUNGEON.OVL, DNGLOOK.OVL, dungeon halves of SJOG.OVL). See dungeon.h. */
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
#include "map.h"
#include "misc.h"
#include "objects.h"
#include "picture.h"
#include "sjog.h"
#include "status.h"
#include "text.h"
#include "tiles.h"
#include "view.h"
#include "world.h"

#define S(off) ds_str(off)
#define W16(off) ((int16_t)ds_u16((uint16_t)(off)))
#define B8(off) ds_u8((uint16_t)(off))
#define FDX ((int16_t)dg.facing_dx[dg.facing & 3])
#define FDY ((int16_t)dg.facing_dy[dg.facing & 3])

static int dungeon_index(void)   /* 0x21 and 0x22 share index 0 (the first is entered from both worlds?) */
{
    int i = dg.location - 0x21;
    if (i > 0) i = dg.location - 0x22;
    return i;
}


/* ---- DUNGEON.OVL ---- */

/* A room is combat without an enemy object: the party comes in on the side it walked from, and
 * leaving by a side moves it one square that way in the dungeon. Leaving with the monsters dead
 * marks the room cleared (rooms 0xF0 become 0xA0: just a door). */
void dungeon_enter_room(uint8_t square)
{
    print_text(S(0x2C58));   /* "Entering room...\n" */
    dg.facing = B8(0x2C76 + dg.dungeon_entry_dir);
    flush_keyboard();
    uint8_t active = dg.active_player, x = dg.party_x, y = dg.party_y;
    MapObject saved[2] = {dg.objects[1], dg.objects[2]};
    dg.active_player = 0xFF;
    dg.combat_turn = 0xFF;
    memset(dg.local_map, 0, 0x160);
    load_file_part((uint32_t)(dungeon_index() * 0x1600 + (square & 0x0F) * 0x160), 0x160, dg.local_map, S(0x2C6A));   /* "dungeon.cbt" */
    dg.prev_location = dg.location;
    dg.location = 0xFF;
    dg.combat_exit = 0;
    place_combatants(3, square);
    world_tick();
    dg.combat_flags = 0x82;
    if (combat_main_loop() == 0) {
        if (dg.combat_exit == 0x4D) rescue_lord_british();
        dg.location = dg.prev_location;
        mark_room_cleared(square & 0x0F);
        DCELL(dg.z_level, y, x) &= 0xAF;
    }
    dg.location = dg.prev_location;
    dg.party_x = x;
    dg.party_y = y;
    apply_combat_exit();
    dg.objects[1] = saved[0];
    dg.objects[2] = saved[1];
    dg.active_player = active;
}

/* The level's wandering monster: one of eight pictures; its type (hull) comes from ds:173C and
 * its animation style (flags) from ds:1744. hull 0xFF = none. */
void dungeon_spawn_monster(int new_monster)
{
    MapObject *m = &dg.objects[1];
    uint8_t had = m->hull;
    if (new_monster != 0) {
        int kind = random_between(7, 0);
        m->tile = m->tile2 = (uint8_t)kind;
        m->skiffs = 0;
        m->flags = B8(0x1744 + kind);
        m->hull = B8(0x173C + kind);
        m->z = dg.z_level;
        if (had != 0xFF) dos_free_mem(dg.dungeon_monster_pic);
        dg.dungeon_monster_pic = 0;
        if (place_dungeon_monster() == 0) {
            m->tile = m->tile2 = 0;
            m->hull = 0xFF;
        }
    }
    if (m->hull != 0xFF) dg.dungeon_monster_pic = load_picture(S(dg.dungeon_monster_pics[m->tile & 7]));
}

void draw_dungeon_hud(void)
{
    static const uint16_t names[5] = {0x2C7C, 0x2C82, 0x2C88, 0x2C8E, 0x2C94};   /* North East South West ???? */
    uint16_t window = dg.cur_window_index;
    select_text_window(0);
    set_cursor(0, 0x0C);
    print_number(' ', 1, dg.z_level + 1);
    set_cursor(0x17, 0x0C);
    print_text(S(names[dg.facing < 4 ? dg.facing : 4]));
    select_text_window(window);
}

/* Eight tries at a random open square of the level that is neither in the party's row nor in
 * its column. Types 0x16 and 0x18 hang from the ceiling half the time (skiffs 0xFF). */
unsigned place_dungeon_monster(void)
{
    MapObject *m = &dg.objects[1];
    for (int tries = 0; tries < 8; tries++) {
        int pos = random_between(0x3F, 0);
        uint8_t kind = dg.dungeon_map[pos + dg.z_level * 0x40] & 0xF0;
        if (!(kind < 0x60 || kind == 0x70)) continue;
        if (pos % 8 == dg.party_x || pos / 8 == dg.party_y) continue;
        m->x = dg.objects[2].x = (uint8_t)(pos % 8);
        m->y = dg.objects[2].y = (uint8_t)(pos / 8);
        if ((m->hull == 0x16 || m->hull == 0x18) && random_between(99, 0) > 0x30) m->skiffs = 0xFF;
        return 1;
    }
    m->x = m->y = dg.objects[2].x = dg.objects[2].y = 0xFF;
    return 0;
}

void draw_dungeon_frame(void)
{
    uint16_t window = dg.cur_window_index;
    set_draw_color(dg.color_frame);
    fill_rect(7, 0x98, 0, 0x28);
    fill_rect(0xBF, 0x98, 0xB9, 0x30);
    set_draw_color(dg.color_text);
    draw_line(7, 0x98, 7, 0x28);
    draw_line(0xB8, 0x98, 0xB8, 0x30);
    select_text_window(0);
    set_cursor(0, 10);
    draw_label_left();
    print_text(S(0x2C9A));   /* "L " */
    draw_label_right();
    set_cursor(0x17, 6);
    draw_label_left();
    print_text(S(0x2C9D));   /* "Dir:      " */
    draw_label_right();
    select_text_window(window);
}

/* While no key comes the view is redrawn (monster, drips, fountains move). After the first pass
 * only the middle is refreshed, except in the style with dripping stalactites. */
unsigned dungeon_read_key(void)
{
    if (dg.status_dirty != 0) {
        draw_status_bar();
        dg.status_dirty = 0;
    }
    put_char('\n');
    draw_label_left();
    int full = 1;
    for (;;) {
        unsigned key = to_upper((uint8_t)wait_key_cursor());
        if (key != 0 && key < 5 && dg.numpad_shift == 0) key += 0xFA;
        if (key != 0 || game_quit()) return key;
        draw_dungeon_view(full);
        if (full) copy_page_rect(0xB2, 0xAF, 0x0E, 0x10, 0, 1);
        else copy_page_rect(0xB2, 0x96, 0x0E, 0x28, 0, 1);
        if (dg.dungeon_style != 1) full = 0;
        footstep_sound();
    }
}

/* The party is thrown back the way it came and takes damage. */
void electric_field_shock(int dir)
{
    redraw_dungeon();
    print_text(S(0x2CA8));   /* "Ouch!\n" */
    print_text(S(0x2CAF));   /* "Electric field!\n" */
    set_draw_color(dg.color_text);
    xor_fill_rect(0xB7, 0xB7, 8, 8);
    delay_ticks(1);
    xor_fill_rect(0xB7, 0xB7, 8, 8);
    sound_noise(20000, 500, 1);
    int sign = dir < 1 ? 1 : -1;
    dg.party_y = (uint8_t)(dg.party_y + sign * FDY);
    dg.party_x = (uint8_t)(dg.party_x + sign * FDX);
    damage_party();
    redraw_dungeon();
}

/* No turning in a doorway. Walls (0xB0-0xD0) block; a room or its door (0xA0, 0xF0) can't be
 * backed into; the monster's square is blocked. Returns 1 for a step attempt (a turn passes). */
int dungeon_move(uint8_t square, int key)
{
    int step = 0;
    if (key == 1 || key == 2) {
        if ((square & 0xF0) == 0xE0) {
            print_text(S(key == 1 ? 0x2CEF : 0x2CC9));   /* "Not in doorway!\n" */
        } else {
            print_text(S(key == 1 ? 0x2D00 : 0x2CDA));   /* "Turn left\n" / "Turn right\n" */
            dg.facing = (uint8_t)((dg.facing + (key == 1 ? 3 : 1)) & 3);
        }
    } else if (key == 3) {
        print_text(S(0x2CC0));   /* "Advance\n" */
        step = 1;
    } else if (key == 4) {
        print_text(S(0x2CE6));   /* "Back up\n" */
        step = -1;
    } else {
        print_text(S(0x2D0B));   /* "Turn around.\n" */
        dg.facing = (uint8_t)((dg.facing + 2) & 3);
    }
    dg.dungeon_entry_dir = dg.facing;
    if (step != 0) {
        int x = (FDX * step + dg.party_x) & 7, y = (FDY * step + dg.party_y) & 7;
        uint8_t cell = DCELL(dg.z_level, y, x);
        if (cell == 0x83) {
            dg.party_x = (uint8_t)x;
            dg.party_y = (uint8_t)y;
            electric_field_shock(step);
            x = dg.party_x;
            y = dg.party_y;
        }
        cell &= 0xF0;
        if ((cell >= 0xA1 && cell <= 0xDF) || (step == -1 && (cell == 0xA0 || cell == 0xF0))) {
            print_text(S(0x2D19));   /* "Blocked!\n" */
        } else if (x == dg.objects[1].x && y == dg.objects[1].y) {
            print_text(S(0x2D23));   /* "Blocked!\n" */
        } else {
            dg.footstep_volume = 0x0F;
            dg.footstep_toggle = 0;
            footstep_sound();
            dg.footstep_toggle = 0;
            dg.party_x = (uint8_t)(x & 7);
            dg.party_y = (uint8_t)(y & 7);
        }
    }
    redraw_dungeon();
    return step != 0;
}

/* Arrows walk and turn; Return or '.' turns around; the rest goes to the dispatcher. */
int dungeon_handle_key(unsigned key)
{
    switch (key) {
    case 0x0B:
        print_number(' ', 1, dg.karma);
        put_char('\n');
        return 1;
    case 5:
        print_text(S(0x2D2D));   /* "Exit to DOS? " */
        if ((get_command_key() & 0xFF) == 'Y') exit_game(0);
        else print_text(S(0x2D3B));   /* "N\n" */
        return 1;
    case 0x13:
        print_text(S(0x2D43));   /* "Sound " */
        print_text(S(dg.sound_enabled == 0 ? 0x2D4F : 0x2D4A));   /* "On\n" / "Off\n" */
        dg.sound_enabled = dg.sound_enabled == 0;
        return 1;
    case 0x16:
        print_text(S(0x2D3E));   /* "1.16" */
        put_char('\n');
        return 1;
    case 1: case 2: case 3: case 4: case 0x0D: case '.':
        return dungeon_move(DHERE, (int)key);
    }
    if (key >= '0' && key <= '9') {
        cmd_set_active_player((int)key);
        return 0;
    }
    return dispatch_command((int)key);
}

/* Up to eight tries at a random step onto a square that isn't a pit, field, wall or door; it
 * steps back where it just was only 1 time in 8. Type 0x1B stays put. If the step lands on the
 * party the monster is put back and attacks. */
int dungeon_monster_move(void)
{
    MapObject *m = &dg.objects[1], *prev = &dg.objects[2];
    if (m->hull == 0xFF) return 0;
    if (m->hull != 0x1B) {
        for (int tries = 0; tries < 8; tries++) {
            int d = random_between(3, 0);
            int x = (int16_t)dg.facing_dx[d] + m->x, y = (int16_t)dg.facing_dy[d] + m->y;
            if (x > 7) x = 0;
            if (x < 0) x = 7;
            if (y > 7) y = 0;
            if (y < 0) y = 7;
            uint8_t kind = DCELL(dg.z_level, y, x) & 0xF0;
            if (kind == 0x60 || kind == 0x80 || kind >= 0xA0) continue;
            if (x == prev->x && y == prev->y && random_between(7, 0) != 1) continue;
            prev->x = m->x;
            prev->y = m->y;
            m->x = (uint8_t)x;
            m->y = (uint8_t)y;
            break;
        }
    }
    if (m->x == dg.party_x && m->y == dg.party_y) {
        m->x = prev->x;
        m->y = prev->y;
        return 1;
    }
    return 0;
}

void sleep_trap(void)
{
    print_text(S(0x2D53));   /* "Sleep spell!\n" */
    for (unsigned i = 0; i < dg.party_size; i++) {
        if (dg.party[i].dex > (unsigned)random_between(0x1E, 1) || dg.party[i].status == 'D') continue;
        dg.party[i].status = 'S';
        highlight_member_row((int)i);
        sound_noise(0xDAC, 0x32, 1);
        dg.status_dirty = 1;
    }
    DHERE &= 8;
}

void poison_trap(void)
{
    print_text(S(0x2D61));   /* "Poison!\n" */
    for (unsigned i = 0; i < dg.party_size; i++) {
        if (dg.party[i].dex > (unsigned)random_between(0x1E, 1) || dg.party[i].status == 'D') continue;
        dg.party[i].status = 'P';
        sound_noise(0xDAC, 0x32, 1);
    }
}

/* A hidden pit (0x61, 0x69) drops the party a level - and again if it lands on another; the
 * square above keeps its hole. Below level 8 is the way out. */
void pit_trap(uint8_t square)
{
    dg.dungeon_entry_dir = 5;
    while ((square == 0x61 || square == 0x69) && dg.z_level < 8) {
        print_text(S(0x2D6A));   /* "Pit Trap!\n" */
        print_text(S(0x2D75));   /* "Falling...\n" */
        DHERE &= 0xF8;
        dg.z_level++;
        if (dg.z_level > 7) break;   /* (the original reads past the map here) */
        square = DHERE;
        if (square < 0x90) {
            square |= 8;
            DHERE = square;
        }
        redraw_dungeon();
        print_text(S(0x2D81));   /* "      ...splat!\n" */
        damage_party();
    }
    if (dg.z_level == 8) dg.location = 0;
    if (dg.location == 0) return;
    uint8_t kind = DHERE & 0xF0;
    if (kind == 0xA0 || kind == 0xF0) {
        unload_dungeon_graphics();
        dungeon_enter_room(DHERE);
        if (dg.location == 0) return;
        load_dungeon_graphics(1);
    }
    dungeon_spawn_monster(1);
}

/* The party turns to face the attacker; the fight is on a map built from the corridor. */
void dungeon_monster_attack(void)
{
    MapObject *prev = &dg.objects[2];
    print_text(S(0x2D92));   /* "Attacked" */
    if (((prev->x - 1) & 7) == dg.party_x) dg.dungeon_entry_dir = 1;
    else if (((prev->x + 1) & 7) == dg.party_x) dg.dungeon_entry_dir = 3;
    else if (((prev->y - 1) & 7) == dg.party_y) dg.dungeon_entry_dir = 2;
    else dg.dungeon_entry_dir = 0;
    if (dg.dungeon_entry_dir != dg.facing) {
        static const uint16_t names[4] = {0x2DA6, 0x2DAC, 0x2DB1, 0x2DB7};   /* north east south west */
        print_text(S(0x2D9B));   /* " from the " */
        print_text(S(names[dg.dungeon_entry_dir & 3]));
        dg.facing = dg.dungeon_entry_dir;
    }
    print_text(S(0x2DBC));   /* "!\n" */
    redraw_dungeon();
    flush_keyboard();
    delay_ticks(2);
    unload_dungeon_graphics();
    dg.combat_exit = 0;
    dg.combat_flags = 2;
    setup_corridor_combat();
    run_combat(0, dg.objects[1].hull, 2);
    apply_combat_exit();
    load_dungeon_graphics(1);
    dungeon_spawn_monster(1);
    if (dg.location != 0) redraw_dungeon();
}

/* After a turn: sleepers wake 1 time in 16, the monster moves, then the square the party is on
 * acts: rooms open, fields and traps strike. */
void dungeon_turn_effects(int monster_moves, uint8_t square)
{
    int woke = 0;
    for (unsigned i = 0; i < dg.party_size; i++)
        if (dg.party[i].status == 'S' && random_between(0x3F, 0) < 4) {
            dg.party[i].status = 'G';
            woke++;
        }
    if (woke) draw_status_bar();
    if (monster_moves && dungeon_monster_move()) {
        dungeon_monster_attack();
        if (dg.location == 0 || game_quit()) {
            per_turn_status();
            return;
        }
        square = DHERE;
    }
    uint8_t kind = square & 0xF0;
    if (kind == 0xF0 || kind == 0xA0) {
        unload_dungeon_graphics();
        while ((kind == 0xF0 || kind == 0xA0) && check_party_state() > -1 && !game_quit()) {
            dungeon_enter_room(square);
            if (dg.location == 0) break;
            square = DHERE;
            kind = square & 0xF0;
        }
        load_dungeon_graphics(1);
        if (dg.location > 0x20) redraw_dungeon();
        per_turn_status();
        return;
    }
    switch (square) {
    case 0x80: case 0x88: sleep_trap(); break;
    case 0x81: case 0x89: poison_trap(); break;
    case 0x82: case 0x8A:
        print_text(S(0x2DBF));   /* "Fire!!\n" */
        damage_party();
        break;
    case 0x61: case 0x69: pit_trap(square); break;
    case 0x62: case 0x6A:
        print_text(S(0x2DC7));   /* "Bomb Trap!\n" */
        print_text(S(0x2DD3));   /* "KABOOM!!\n" */
        DHERE &= 8;
        damage_party();
        break;
    }
    draw_status_bar();
    per_turn_status();
}

/* Styles: 3 for Deceit, Wrong and Covetous-like dungeons 0x21, 0x24, 0x25 (stone: fights on
 * tiles 0x4F / 0x45), 2 for 0x26 and 0x27, 1 for the rest (caves: 0x4D / 5). A minute passes per
 * turn (none under Time Stop, every other turn under Quickness, when the monster also rests). */
void dungeon_main_loop(int reset_monster)
{
    int state = 0, moves = 0;
    dg.dungeon_gfx_flags = 2;
    apply_cleared_rooms();
    uint8_t kind = DHERE & 0xF0;
    draw_dungeon_frame();
    draw_dungeon_hud();
    if (dg.location == 0x21 || dg.location == 0x24 || dg.location == 0x25) {
        dg.corridor_wall_tile = 0x4F;
        dg.corridor_floor_tile = 0x45;
        dg.dungeon_style = 3;
    } else {
        dg.corridor_wall_tile = 0x4D;
        dg.corridor_floor_tile = 5;
        dg.dungeon_style = (dg.location == 0x26 || dg.location == 0x27) ? 2 : 1;
    }
    if (kind == 0xA0 || kind == 0xF0) {
        unload_dungeon_graphics();
        dungeon_enter_room(DHERE);
    }
    if (dg.location > 0x20) {
        load_dungeon_graphics(reset_monster);
        dungeon_spawn_monster(reset_monster);
        redraw_dungeon();
    }
    while (dg.location >= 0x21 && !game_quit()) {
        state = check_party_state();
        if (state < 0) break;
        if (state == 1) {
            put_char('\n');
            draw_label_left();
            print_text(S(0x2DDD));   /* "Zzzzzz...\n" */
            /* the original loops here without a turn until someone wakes - which only a turn can
             * do, so it hangs; the port lets the dungeon's turn pass */
            dungeon_turn_effects(1, DHERE);
            wait_turns(1);
            continue;
        }
        if (dg.active_spell == 0x54) {
            moves = 0;
        } else if (dg.active_spell == 0x51) {
            moves ^= 1;
            if (moves) pass_time(1);
        } else {
            moves = 1;
            pass_time(1);
        }
        unsigned key = dungeon_read_key();
        if (game_quit()) break;
        int turn = dungeon_handle_key(key);
        if (dg.location < 0x21 || game_quit()) break;
        if (turn) dungeon_turn_effects(moves, DHERE);
    }
    if (dg.dungeon_gfx_flags & 2) dg.dungeon_gfx_flags &= 0xFB;
    else dg.dungeon_gfx_flags |= 4;
    unload_dungeon_graphics();
    if (state < 0) party_death();
}

void footstep_sound(void)
{
    dg.footstep_toggle ^= 1;
    if (dg.footstep_toggle != 0 && dg.footstep_volume != 0) {
        sound_noise(20000, dg.footstep_volume, 1);
        sub_byte_floor0(4, &dg.footstep_volume);
    }
}

/* Writing on a wall (0xBn, n = which of the dungeon's signs), in runes. */
void draw_dungeon_sign(unsigned sign)
{
    if (sign > B8(0x2DCF + dg.location)) return;
    int i = B8(0x2DC7 + dg.location) + (int)sign;
    define_text_window(0x18, 0x27, B8(0x2E03 + i), B8(0x2DF7 + i), 0);
    select_text_window(0);
    select_font(1);
    set_cursor(0, 0);
    put_char(0xFD);
    print_text(S(ds_u16((uint16_t)(0x2E10 + (i - 1) * 2))));
    put_char(0xFD);
    select_font(0);
    select_text_window(2);
    define_text_window(0x18, 0x27, 0, 0, 0);
}

uint8_t dungeon_cell_at(unsigned y, unsigned x)
{
    uint8_t c = DCELL(dg.z_level, y, x);
    return c < 0x90 ? (c & 0xF7) : c;
}

/* The monster's picture has two poses of three sizes; the left half is drawn and the right
 * half mirrored, each in its own pose. flags (the object's): 0x20 both halves alternate,
 * 0x60 a four-step cycle (ds:2E26), 0x40 at random; 0x80 / 0x90 tie the right half to the left
 * (same / opposite). A hanging monster (skiffs 0xFF) is drawn upside down from the ceiling. */
void draw_dungeon_monster(int distance)
{
    MapObject *m = &dg.objects[1];
    if (dg.active_spell == 0x54) m->flags = B8(0x1744 + (m->tile & 7));
    int hanging = m->skiffs == 0xFF;
    unsigned f = m->flags, tie = f & 0x90, mode = f & 0x60;
    int phase = (int)(f & 0x0F), left, right;
    if (dg.active_spell == 0x54) {
        left = right = 1;
    } else {
        left = random_between(100, 0) < 0x32;
        right = random_between(100, 0) < 0x32;
        if (mode == 0x20) {
            left = right = phase = (f & 0x0F) == 0;
        } else if (mode == 0x60) {
            if (--phase < 0) phase = 3;
            left = B8(0x2E26 + phase) >> 1;
            right = B8(0x2E26 + phase) & 1;
        }
    }
    if (tie != 0) right = (tie == 0x90) ^ left;
    if (dg.dungeon_monster_pic == 0) {
        print_text(S(0x6C4E));   /* "Hey!! What's going on here???" */
        return;
    }
    int y = W16(0x2E32 + hanging * 8 + distance * 2);
    draw_picture_frame(hanging, y, W16(0x2E2A + distance * 2), left * 3 + distance - 1, dg.dungeon_monster_pic);
    draw_picture_frame2(hanging, y, 0x60, right * 3 + distance - 1, dg.dungeon_monster_pic);
    m->flags = (uint8_t)(mode + tie + phase);
}

/* Short streaks at random in the square's area (tables ds:2E42-2E5A by distance). */
void draw_field_sparkles(int field, int distance)
{
    uint16_t clip = dg.clip_enabled;
    dg.clip_enabled = 0;
    if (field == 0) set_draw_color((uint16_t)(dg.color_sleep + 8));
    else if (field == 1) set_draw_color((uint16_t)(dg.color_poison + 8));
    else if (field == 2) set_draw_color((uint16_t)(dg.color_fire + 8));
    else if (field == 3) set_draw_color((uint16_t)(dg.color_frame + 8));
    int lo = W16(0x2E42 + distance * 2), hi = W16(0x2E4A + distance * 2), len = W16(0x2E5A + distance * 2);
    for (int n = W16(0x2E52 + distance * 2); n > 0; n--) {
        int x1 = random_between(hi - len, lo), y = random_between(hi, lo);
        draw_hline(len + x1, y, x1);
    }
    dg.clip_enabled = clip;
}

/* Pieces below 0x1F are frames of the wall picture, at row 14: the left one at the column for
 * its depth (ds:2E62), the right one mirrored. Higher numbers are frames of the door picture
 * (ladders, chests, fountains, pits, holes): frame (piece + 1) / 2 - 16, sized by depth; kind 1
 * draws it upside down for the ceiling. */
void draw_wall_piece(int side, int kind, unsigned piece)
{
    int x, y;
    uint16_t pic;
    if ((int)piece < 0x1F) {
        x = W16(0x2E62 + side * 8 + (piece & 3) * 2);
        if (side != 0 && ((piece & 0xF8) == 8 || (piece & 0xFC) == 0x18)) x = 0x60;
        y = 0x0E;
        kind = 0;
        pic = dg.dungeon_wall_pic;
    } else {
        unsigned u = piece + 1, depth = (u & 6) >> 1;
        piece = (u >> 1) - 0x10;
        x = side == 0 ? W16(0x2E72 + depth * 2) : 0x60;
        if ((int)piece < 8) y = kind == 0 ? 0x60 : W16(0x2E82 + depth * 2);
        else y = W16(0x2E7A + depth * 2 + kind * 8);
        pic = dg.dungeon_door_pic;
    }
    if (side == 0) draw_picture_frame(kind, y, x, (int)piece, pic);
    else draw_picture_frame2(kind, y, x, (int)piece, pic);
}

/* A drop grows on the stalactite (frames 0-3), falls (4) and splashes (5: a sound). The frame
 * is kept in the square's low bits. */
int draw_drip(int distance, int frame, int y, int x)
{
    if (frame == 5) {
        sound_slide(-(distance * 8 - 0x14), 1, 0xDAC, 0xC80);
        return 0;
    }
    uint16_t color = dg.color_frame;
    if (frame == 4) color = (dg.video_type == 0 || dg.video_type == 3) ? 3 : 0x0B;
    set_draw_color(color);
    draw_hline(x + 1, y, x - 1);
    draw_vline(y + 1, y - 1, x);
    if (frame < 4) {
        set_draw_color((uint16_t)(dg.color_frame + 8));
        plot_pixel(y, x);
    }
    if (frame != 0 || random_between(0x40, 0) < 4) frame++;
    return frame;
}

/* The square straight ahead at this depth. A wall, door or room ends the view (0) - except the
 * doorway the party stands in, which only narrows it. */
int draw_dungeon_center(int distance, unsigned y, unsigned x)
{
    uint8_t cell = dungeon_cell_at(y, x);
    if (cell < 0xA0) return 1;
    unsigned piece = distance == 0 ? 0x0C : (unsigned)(B8(0x2E80 + (cell >> 4)) + distance);
    draw_wall_piece(0, 0, piece);
    draw_wall_piece(1, 0, piece);
    if ((distance == 1 || distance == 2) && dg.dungeon_style == 1 && (cell & 0xF0) == 0xC0) {
        int frame = draw_drip(distance, cell & 7, B8(0x2E8B + (cell & 7) + distance * 5) + 0x0E, 0x5F);
        DCELL(dg.z_level, y, x) = (uint8_t)((DCELL(dg.z_level, y, x) & 0xF8) + frame);
    }
    if (distance == 1) {
        if ((cell & 0xF0) == 0xB0 && (cell & 0x0F) != 0) {
            draw_dungeon_sign(cell & 0x0F);
        } else if (dg.dungeon_style == 3 && (cell & 0xF0) == 0xC0 && random_between(0x40, 0) < 4) {
            set_draw_color((uint16_t)(dg.color_fire + 8));   /* the skeleton's eyes glint */
            draw_hline(0x5D, 0x57, 0x5C);
            draw_hline(0x5D, 0x58, 0x5B);
            draw_hline(0x62, 0x57, 0x61);
            draw_hline(99, 0x58, 0x61);
        }
    }
    if ((cell & 0xF0) != 0xE0 || distance != 0) return 0;
    dg.clip_enabled = 1;
    return 1;
}

/* The square to the left (side 0) or right (1) at this depth: a wall (piece = depth), an
 * opening (+0x10), a door (+4), or a special wall (+0x14, with its drip). */
void draw_dungeon_side(int distance, int side, unsigned y, unsigned x)
{
    uint8_t cell = dungeon_cell_at(y, x), kind = cell & 0xF0;
    int piece = distance;
    if (cell < 0xA0) {
        piece += 0x10;
    } else if (kind == 0xC0) {
        draw_wall_piece(side, 0, (unsigned)(distance + 0x14));
        if (distance > 1 || dg.dungeon_style != 1) return;
        int px = distance == 0 ? 0x21 : 0x43;
        if (side != 0) px = 0xBE - px;
        int frame = draw_drip(distance, cell & 7, B8(0x2E9A + (cell & 7) + distance * 5) + 0x0E, px);
        DCELL(dg.z_level, y, x) = (uint8_t)((DCELL(dg.z_level, y, x) & 0xF8) + frame);
        return;
    } else if (kind == 0xA0 || kind == 0xE0 || kind == 0xF0) {
        piece += 4;
    }
    draw_wall_piece(side, 0, (unsigned)piece);
}

/* Spray as point lists by depth, each point drawn on both sides of the middle (x = 95). */
void draw_fountain(int frame, int distance)
{
    int mono = dg.video_type == 0 || dg.video_type == 3;
    set_draw_color(mono ? 0 : dg.color_frame);
    if (distance == 0) {
        if (!mono) set_draw_color((uint16_t)(dg.color_frame + 8));
        for (int i = 0; i < 8; i++) {
            int px = B8(0x2EA4 + frame * 0x10 + i * 2), py = B8(0x2EA5 + frame * 0x10 + i * 2);
            plot_pixel(py, px);
            plot_pixel(py, 0xBE - px);
        }
        int px = B8(0x2F10 + frame * 2), py = B8(0x2F11 + frame * 2);
        plot_pixel(py, px);
        plot_pixel(py, 0xBE - px);
    } else if (distance == 1) {
        for (int i = 0; i < 5; i++) {
            int px = B8(0x2ED4 + frame * 10 + i * 2), py = B8(0x2ED5 + frame * 10 + i * 2);
            plot_pixel(py + 0x60, px + 0x48);
            plot_pixel(py + 0x60, 0x76 - px);
        }
    } else if (distance == 2) {
        for (int i = 0; i < 4; i++) {
            int px = B8(0x2EF2 + frame * 8 + i * 2), py = B8(0x2EF3 + frame * 8 + i * 2);
            plot_pixel(py + 0x60, px + 0x50);
            plot_pixel(py + 0x60, 0x6E - px);
        }
    } else if (distance == 3) {
        int px = B8(0x2F0A + frame * 2), py = B8(0x2F0B + frame * 2) + 0x60;
        plot_pixel(py, px + 0x58);
        plot_pixel(py, 0x66 - px);
    }
}

/* What stands in an open square: ladders (up ones on the ceiling), chests, fountains, pits,
 * fields, the hole a pit left in the ceiling, and the monster. */
void draw_dungeon_contents(int distance, unsigned y, unsigned x)
{
    uint8_t cell = dungeon_cell_at(y, x);
    int kind = cell >> 4;
    if (kind < 1 || kind > 7 || (kind == 6 && (cell & 7) != 0)) {
        if (kind == 8) draw_field_sparkles(cell & 7, distance);
    } else {
        uint8_t up = B8(0x2F16 + kind), down = B8(0x2F1E + kind);
        if (up != 0) {
            draw_wall_piece(0, 1, (unsigned)(up + distance * 2));
            draw_wall_piece(1, 1, (unsigned)(up + distance * 2));
        }
        if (down != 0) {
            draw_wall_piece(0, 0, (unsigned)(down + distance * 2));
            draw_wall_piece(1, 0, (unsigned)(down + distance * 2));
        }
        if (kind == 5) draw_fountain(dg.fountain_frame, distance);
    }
    if (kind < 9 && (DCELL(dg.z_level, y, x) & 8)) {
        unsigned piece = (unsigned)(distance * 2 + B8(0x2F24));
        draw_wall_piece(0, 1, piece);
        draw_wall_piece(1, 1, piece);
    }
    if (distance != 0 && (x & 7) == dg.objects[1].x && (y & 7) == dg.objects[1].y) draw_dungeon_monster(distance);
    if (distance == 0 && ++dg.fountain_frame > 2) dg.fountain_frame = 0;
}

/* Without torch or light spell all is black. Otherwise walls are drawn from the party's square
 * outward, up to four deep, until something closes the view; then the contents of the squares
 * from the far end back to the party. `full` 0 skips the side walls of the party's own square. */
void draw_dungeon_view(int full)
{
    dg.last_disk_checked = 0xFF;
    set_draw_page(1);
    set_draw_color(0);
    fill_rect(0xB2, 0xAF, 0x0E, 0x60);
    set_clip_rect(0xB7, 0x98, 0x2A, 0x28);
    dg.clip_enabled = 0;
    if (dg.light_spell == 0 && dg.torch_light == 0) {
        fill_rect(0xB7, 0xB7, 8, 8);
    } else {
        unsigned x = dg.party_x, y = dg.party_y;
        int dx = FDX, dy = FDY, depth;
        uint8_t side_dx = B8(0x2F28 + (dg.facing & 3)), side_dy = B8(0x2F2C + (dg.facing & 3));
        for (depth = 0; depth < 4; depth++) {
            if (draw_dungeon_center(depth, y, x) == 0) break;
            if (depth != 0 || (dungeon_cell_at(dg.party_y, dg.party_x) != 0xE0 && full != 0)) {
                draw_dungeon_side(depth, 0, side_dy + y, side_dx + x);
                draw_dungeon_side(depth, 1, y - side_dy, x - side_dx);
            }
            x += (unsigned)dx;
            y += (unsigned)dy;
        }
        while (--depth > -1) {
            x -= (unsigned)dx;
            y -= (unsigned)dy;
            draw_dungeon_contents(depth, y, x);
        }
    }
    set_draw_page(0);
    dg.clip_enabled = 0;
}

void redraw_dungeon(void)
{
    draw_status_bar();
    draw_dungeon_view(1);
    copy_page_rect(0xB2, 0xAF, 0x0E, 0x10, 0, 1);
    footstep_sound();
    draw_dungeon_hud();
}

/* With `check` (the Uus Por / Des Por spells) the square on the other level must be open. */
int can_klimb_into(int check, int level)
{
    uint8_t kind = DCELL(level, dg.party_y, dg.party_x) & 0xF0;
    return !(check != 0 && kind != 0);
}

/* 1 if the move leads out of the dungeon (above level 1 or below level 8). */
int dungeon_change_level(int check, int dz)
{
    print_text(S(dz < 1 ? 0x6C74 : 0x6C6C));   /* "Up!\n" / "Down!\n" */
    if ((dz >= 1 && dg.z_level == 7) || (dz < 0 && dg.z_level == 0)) return 1;
    if (!can_klimb_into(check, dz + dg.z_level)) {
        print_text(S(0x6C7A));   /* "Failed!\n" */
        sound_slide(0x32, 1, 2000, 800);
        return 0;
    }
    dg.z_level = (uint8_t)(dg.z_level + dz);
    dg.dungeon_entry_dir = dz < 1 ? 4 : 5;
    dungeon_spawn_monster(1);
    redraw_dungeon();
    return 0;
}

/* From level 1 up to Britannia; from any other level out is the Underworld. */
void exit_dungeon(void)
{
    dg.party_x = dg.town_exit_x[dg.location];
    dg.party_y = dg.town_exit_y[dg.location];
    print_text(S(0x6C84));   /* "\nExit to " */
    if (dg.z_level == 0) {
        print_text(S(0x6C9C));   /* "Britannia!\n\n" */
    } else {
        dg.z_level = 0xFF;
        print_text(S(0x6C8E));   /* "Underworld!\n\n" */
    }
    dg.location = 0;
}

int cmd_attack_dungeon(void)
{
    print_text(S(0x6CAA));   /* "Attack\n" */
    if (((FDX + dg.party_x) & 7) != dg.objects[1].x || ((FDY + dg.party_y) & 7) != dg.objects[1].y) {
        print_text(S(0x6CB2));   /* "What?\n" */
        return 0;
    }
    dg.combat_flags = 2;
    unload_dungeon_graphics();
    build_corridor_map();
    setup_corridor_combat();
    run_combat(0, dg.objects[1].hull, 2);
    if (dg.combat_exit == 5) {
        if (dg.z_level != 0) dg.z_level--;
        else exit_dungeon();
    } else if (dg.combat_exit == 6) {
        if (dg.z_level < 7) dg.z_level++;
        else exit_dungeon();
    }
    load_dungeon_graphics(1);
    if (dg.location != 0) {
        dungeon_spawn_monster(1);
        redraw_dungeon();
    }
    return 0;
}

/* Up by an up ladder, or with a grapple through a hole in the ceiling; down by a down ladder
 * or a pit. Where both are possible: "U/D". */
int cmd_klimb_dungeon(void)
{
    uint8_t kind = dungeon_cell_at(dg.party_y, dg.party_x) & 0xF0;
    int hole = (DHERE & 8) != 0;
    int up = kind == 0x10 || kind == 0x30 || (hole && dg.grapples != 0);
    int down = kind == 0x20 || kind == 0x30 || kind == 0x60;
    if (up && down) {
        print_text(S(0x6CBA));   /* "Klimb-U/D-" */
        while (up && down) {
            int key = get_command_key();
            if (game_quit()) return 0;
            if (key == 3 || key == 'U') down = 0;
            else if (key == 4 || key == 'D') up = 0;
            else if (key == ' ') {
                print_text(S(0x6CC6));   /* "Pass\n\n" */
                up = down = 0;
            }
        }
    } else if (!up && !down) {
        print_text(S(hole ? 0x6CD6 : 0x6CEA));   /* "Klimb-\nWith What?\n" / "Klimb-what?\n" */
        return 0;
    } else {
        print_text(S(0x6CCE));   /* "Klimb-" */
    }
    if ((up && dungeon_change_level(0, -1) != 0) || (down && dungeon_change_level(0, 1) != 0)) exit_dungeon();
    return 1;
}

/* ---- DNGLOOK.OVL ---- */

/* Fountains (0x50 cure, 0x51 heal, 0x52 poison, others bad water) can be drunk from. */
void cmd_look_dungeon(void)
{
    int member = ask_which_member();
    if (member == -1) return;
    if (dg.torch_light == 0 && dg.light_spell == 0) {
        print_text(S(0x752E));   /* "You see:\ndarkness.\n" */
        return;
    }
    if (ask_dungeon_direction(dg.facing) == 0) return;
    uint8_t cell = DCELL(dg.z_level, dg.dir_y, dg.dir_x);
    print_text(S(0x7542));   /* "You see:\n" */
    if (cell == 0x61) cell = 0;   /* a hidden pit looks like a passage */
    uint8_t kind = cell & 0xF0;
    uint16_t text = 0;
    if (kind == 0x80) {
        static const uint16_t fields[5] = {0x754C, 0x755C, 0x7572, 0x7584, 0x7598};   /* sleep, poison gas, fire, electric, energy */
        text = fields[cell - 0x80 < 4 ? cell - 0x80 : 4];
    } else if (kind == 0xC0) {
        if ((dg.dungeon_style & 0x0F) == 1) text = 0x75AA;        /* "a dripping stalactite.\n" */
        else if ((dg.dungeon_style & 0x0F) == 2) text = 0x75C2;   /* "a caved in passage.\n" */
        else text = random_between(0xFF, 1) == 0xFF ? 0x75D8 : 0x75FA;   /* "an unfortunate software pirate.\n" / "a less fortunate adventurer.\n" */
    } else {
        switch (kind) {
        case 0x00: text = 0x7618; break;   /* "a passage.\n" */
        case 0x10: text = 0x7624; break;   /* "an up ladder.\n" */
        case 0x20: text = 0x7634; break;   /* "a down ladder.\n" */
        case 0x30: text = 0x7644; break;   /* "a ladder.\n" */
        case 0x40: text = 0x7650; break;   /* "a wooden chest.\n" */
        case 0x50: text = 0x7662; break;   /* "a fountain.\n" */
        case 0x60: text = 0x7670; break;   /* "a pit.\n" */
        case 0x70: text = 0x7678; break;   /* "an open chest.\n" */
        case 0x90: text = 0x769A; break;   /* "nothing of note.\n" */
        case 0xA0: text = 0x76AC; break;   /* "a heavy door.\n" */
        case 0xB0: text = 0x76BC; break;   /* "a wall.\n" */
        case 0xD0: text = 0x76D6; break;   /* "a wall.\n" (a secret door) */
        case 0xE0: text = 0x76E0; break;   /* "a heavy door.\n" */
        case 0xF0: text = 0x76F0; break;   /* "a heavy door.\n" */
        }
    }
    if (text) print_text(S(text));
    if (kind != 0x50) return;
    print_text(S(0x7700));   /* "Will you drink?\n" */
    char c;
    do {
        c = (char)get_command_key();
        if (game_quit()) return;
    } while (c != 'Y' && c != 'N');
    if (c == 'N') {
        print_text(S(0x7712));   /* "No.\n" */
        return;
    }
    print_text(S(0x7718));   /* "Yes.  Gulp!\n" */
    if (cell == 0x50) {
        print_text(S(0x7726));   /* "Cured!\n" */
        dg.party[member].status = 'G';
    } else if (cell == 0x51) {
        print_text(S(0x772E));   /* "Healed!\n" */
        dg.party[member].hp = dg.party[member].max_hp;
    } else if (cell == 0x52) {
        print_text(S(0x7738));   /* "Poisoned!\n" */
        dg.party[member].status = 'P';
    } else {
        print_text(S(0x7744));   /* "Bad taste.\n" */
        damage_member(random_between(7, 0), member);
    }
}

void draw_map_field(int y, int x)
{
    static const int order[4] = {0, 2, 1, 3};
    for (int i = 0; i < 4; i++) {
        uint16_t c = order[i] == 0 ? dg.color_sleep : order[i] == 2 ? dg.color_fire : order[i] == 1 ? dg.color_frame : dg.color_poison;
        set_draw_color((uint16_t)(c + 8));
        draw_line(y + i * 2, x + 6, y + i * 2, x + 1);
        draw_line(y + i * 2 + 1, x + 6, y + i * 2 + 1, x + 1);
    }
}

/* The map is 22 x 22 character cells centred on the party (the level repeats, as it wraps).
 * A square is drawn once (local_map marks the cells still to do) as a rune-font glyph; returns
 * 1 if the map goes on through it: open squares do, walls don't. */
int draw_map_square(int row, int col)
{
    if (col < 0 || col > 0x15 || row < 0 || row > 0x15) return 0;
    if (dg.local_map[row * 0x20 + col] == 0) return 0;
    dg.local_map[row * 0x20 + col] = 0;
    int through = 1;
    uint8_t cell = DCELL(dg.z_level, row + dg.party_y - 0x0B, col + dg.party_x - 0x0B);
    set_cursor(row + 1, col + 1);
    int px = col * 8, py = row * 8, glyph = -1;
    set_draw_color(dg.color_text);
    switch (cell >> 4) {
    case 0:
        if (cell & 8) {
            set_text_color((uint8_t)dg.color_ladder);
            glyph = 0x18;
        }
        break;
    case 1: select_font(1); set_text_color((uint8_t)dg.color_ladder); glyph = 0x2E; break;
    case 2: select_font(1); set_text_color((uint8_t)dg.color_ladder); glyph = 0x2D; break;
    case 3: select_font(1); set_text_color((uint8_t)dg.color_ladder); glyph = 0x2F; break;
    case 4: select_font(1); set_text_color((uint8_t)dg.color_chest); glyph = 0x70; break;
    case 5:
        draw_line(py + 0x0C, px + 0x0E, py + 0x0C, px + 9);
        draw_line(py + 0x0D, px + 0x0D, py + 0x0D, px + 10);
        draw_line(py + 0x0E, px + 10, py + 0x0E, px + 9);
        draw_line(py + 0x0E, px + 0x0E, py + 0x0E, px + 0x0D);
        set_draw_color((uint16_t)(dg.color_frame + 8));
        plot_pixel(py + 10, px + 9);
        plot_pixel(py + 9, px + 10);
        draw_line(py + 10, px + 0x0C, py + 10, px + 0x0B);
        draw_line(py + 0x0B, px + 0x0C, py + 0x0B, px + 0x0B);
        plot_pixel(py + 9, px + 0x0D);
        plot_pixel(py + 10, px + 0x0E);
        break;
    case 6:
        set_text_color((uint8_t)dg.color_ladder);
        if (cell == 0x60) glyph = 0x19;
        else if (cell == 0x68) glyph = 0x12;
        else {
            select_font(1);
            if (cell == 0x61 || cell == 0x69) glyph = 0x71;
            else {
                set_text_color((uint8_t)(dg.color_fire + 8));
                glyph = 0x72;
            }
        }
        break;
    case 8: draw_map_field(py + 8, px + 8); break;
    case 0x0A: case 0x0F: select_font(1); set_text_color((uint8_t)dg.color_chest); glyph = 0x73; break;
    case 0x0B:
        if (cell == 0xB0) put_char(0x7F);
        else {
            select_font(1);
            put_char(0x74);
        }
        through = 0;
        break;
    case 0x0C:
        select_font(1);
        set_text_bg_color((uint8_t)dg.color_frame);
        put_char(0x75);
        set_text_bg_color(0);
        through = 0;
        break;
    case 0x0D:
        select_font(1);
        if (dg.video_type == 0) set_text_bg_color((uint8_t)dg.color_frame);
        else if (dg.video_type == 1 || dg.video_type == 2) set_text_bg_color((uint8_t)dg.color_ladder);
        put_char(0x76);
        set_text_bg_color(0);
        through = 0;
        break;
    case 0x0E: select_font(1); set_text_color((uint8_t)dg.color_chest); glyph = 0x77; break;
    }
    if (glyph >= 0) put_char((uint8_t)glyph);
    set_text_color((uint8_t)dg.color_text);
    select_font(0);
    return through;
}

/* The map spreads from the party through the open squares (a queue of 256 cells). */
void show_dungeon_map(void)
{
    set_draw_color(0);
    fill_rect(0xB7, 0xB7, 8, 8);
    memset(dg.local_map, 0xFF, 0x2E0);
    unsigned head = 0, tail = 1;
    dg.map_queue_x[0] = dg.map_queue_y[0] = 0x0B;
    dg.local_map[0x0B * 0x20 + 0x0B] = 0;
    set_draw_color(dg.color_text);
    select_text_window(0);
    set_cursor(0x0C, 0x0C);
    select_font(1);
    set_text_color((uint8_t)(dg.video_type == 3 ? dg.color_text : dg.color_poison + 8));
    put_char(0x60);   /* the party */
    set_text_color((uint8_t)dg.color_text);
    select_font(0);
    while (head != tail) {
        static const int8_t dc[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, dr[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
        int col = dg.map_queue_x[head], row = dg.map_queue_y[head];
        for (int i = 0; i < 8; i++) {
            if (!draw_map_square(row + dr[i], col + dc[i])) continue;
            dg.map_queue_x[tail] = (uint8_t)(col + dc[i]);
            dg.map_queue_y[tail] = (uint8_t)(row + dr[i]);
            tail = (tail + 1) & 0xFF;
        }
        head = (head + 1) & 0xFF;
    }
    select_text_window(2);
    wait_key_timeout(0);
    set_draw_color(0);
    fill_rect(0xB7, 0xB7, 8, 8);
    redraw_dungeon();
}

/* cleared_rooms has a bit per room (16 per dungeon); the rooms listed at ds:383A (as dungeon
 * low nibble x 16 + room) always fill again. */
void mark_room_cleared(int room)
{
    for (unsigned i = 0; i < dg.fixed_room_count; i++)
        if (B8(0x383A + i) == (dg.location & 0x0F) * 0x10 + room) return;
    unsigned bit = (unsigned)(dungeon_index() * 0x10 + room);
    dg.cleared_rooms[(bit >> 3) % sizeof dg.cleared_rooms] |= (uint8_t)(1 << (bit & 7));
}

int is_room_cleared(int room)
{
    unsigned bit = (unsigned)(dungeon_index() * 0x10 + room);
    return (dg.cleared_rooms[(bit >> 3) % sizeof dg.cleared_rooms] & (1 << (bit & 7))) != 0;
}

void apply_cleared_rooms(void)
{
    for (int i = 0; i < 0x200; i++) {
        uint8_t *c = ds_ptr((uint16_t)(offsetof(struct DGroup, dungeon_map) + i));
        if ((*c & 0xF0) == 0xF0 && is_room_cleared(*c & 0x0F)) *c &= 0xAF;
    }
}

/* The corridor fight's map: sides 0-3 = north, east, south, west. A walled side is closed off
 * (0xFF, unseen); in a doorway the doorposts are added. */
void corridor_edge_wall(int side, uint8_t square_type)
{
    int dx = side == 0 || side == 2, dy = !dx;
    int x = side == 1 ? 10 : 0, y = side == 2 ? 10 : 0;
    for (int i = 0; i < 11; i++, x += dx, y += dy) dg.local_map[y * 0x20 + x] = 0xFF;
    if (side == 0 && square_type == 0xE0) dg.local_map[0x105] = dg.local_map[0x45] = dg.corridor_wall_tile;
    if (side == 3 && square_type == 0xE0) dg.local_map[0xA8] = dg.local_map[0xA2] = dg.corridor_wall_tile;
}

void corridor_edge_open(int side)
{
    int dx = side == 0 || side == 2, dy = !dx;
    int x = side == 1 ? 9 : side == 3 ? 1 : 3, y = side == 0 ? 1 : side == 2 ? 9 : 3;
    for (int i = 0; i < 5; i++, x += dx, y += dy) dg.local_map[y * 0x20 + x] = dg.corridor_floor_tile;
}

void corridor_edge_passage(int side)
{
    int dx = side == 0 || side == 2, dy = !dx;
    int x = side == 1 ? 9 : side == 3 ? 1 : 2, y = side == 0 ? 1 : side == 2 ? 9 : 2;
    for (int i = 0; i < 7; i++, x += dx, y += dy) dg.local_map[y * 0x20 + x] = dg.corridor_floor_tile;
}

void build_corridor_edge(int side)
{
    int x = dg.party_x, y = dg.party_y;
    if (side == 0) y--;
    else if (side == 1) x++;
    else if (side == 2) y++;
    else if (side == 3) x--;
    uint8_t kind = DCELL(dg.z_level, y, x) & 0xF0;
    if (kind < 0xA0) corridor_edge_passage(side);
    if (kind == 0xB0 || kind == 0xC0 || kind == 0xD0) corridor_edge_wall(side, DHERE & 0xF0);
    else corridor_edge_open(side);
}

/* A walled ring with the corners cut; whatever the party's square holds (ladder, chest,
 * fountain: tiles at ds:244A) stands in the middle. On a two-way ladder the ladder tiles are
 * patched so both ends show. */
void build_corridor_map(void)
{
    uint8_t *m = dg.local_map;
    memset(m + 0x20, dg.corridor_wall_tile, 11);
    memset(m + 0x120, dg.corridor_wall_tile, 11);
    for (int r = 0; r < 11; r++) m[r * 0x20 + 1] = m[r * 0x20 + 9] = dg.corridor_wall_tile;
    m[0x14A] = m[0x140] = m[10] = m[0] = 0xFF;
    uint8_t kind = DHERE & 0xF0;
    if (kind != 0 && kind < 0x80) {
        uint8_t tile = B8(0x244A + (DHERE >> 4));
        if (tile != 0) m[0xA5] = tile;
        if ((DHERE >> 4) == 3) {
            dg.on_ladder = 1;
            ladder_tiles_patch(0);
        } else {
            dg.on_ladder = 0;
        }
    }
    for (int side = 0; side < 4; side++) build_corridor_edge(side);
}

/* Fills in what load_combat_map would have read: the party's start squares for each side it can
 * face (rows 1-4 of the map's right-hand columns), 16 monster squares by facing, and the
 * monsters themselves: 1 to the type's group size of the wandering monster's type (for a camp,
 * a random one of the eight types and the camp's own squares, with a fire in the middle). */
void setup_corridor_combat(void)
{
    uint8_t *m = dg.local_map;
    int camp = (dg.combat_flags & 4) != 0;
    for (int r = 0; r < 11; r++) memset(m + r * 0x20, dg.corridor_floor_tile, 11);
    if (camp) m[0xA5] = 0xB3;
    build_corridor_map();
    memset(m + 0x113, 0xFF, 8);
    memset(m + 0x10B, 0xFF, 8);
    for (int i = 0; i < 6; i++) {
        if (camp) {
            m[0x6B + i] = m[0x4B + i] = m[0x8B + i] = m[0x2B + i] = B8(0x2452 + i);
            m[0x71 + i] = m[0x51 + i] = m[0x91 + i] = m[0x31 + i] = B8(0x2458 + i);
        } else {
            m[0x6B + i] = B8(0x2470 + i);
            m[0x4B + i] = B8(0x246A + i);
            m[0x8B + i] = B8(0x245E + i);
            m[0x2B + i] = B8(0x2464 + i);
            m[0x71 + i] = B8(0x2464 + i);
            m[0x51 + i] = B8(0x245E + i);
            m[0x91 + i] = B8(0x246A + i);
            m[0x31 + i] = B8(0x2470 + i);
        }
    }
    for (int i = 0; i < 16; i++) {
        if (camp) {
            m[0xEB + i] = B8(0x24C6 + i);
            m[0xCB + i] = B8(0x24B6 + i);
        } else {
            static const uint16_t xs[4] = {0x2476, 0x2486, 0x24A6, 0x2496}, ys[4] = {0x2496, 0x24A6, 0x2486, 0x2476};
            if (dg.facing < 4) {
                m[0xCB + i] = B8(xs[dg.facing] + i);
                m[0xEB + i] = B8(ys[dg.facing] + i);
            }
        }
    }
    int order[16];
    for (int i = 0; i < 16; i++) order[i] = i;
    for (int i = 0; i < 16; i++) {
        int j = random_between(0x0F, 0), t = order[i];
        order[i] = order[j];
        order[j] = t;
        m[0xAB + i] = 0;
    }
    uint8_t type = dg.objects[1].hull;
    if (camp) type = B8(0x173C + random_between(7, 0));
    uint8_t size = MONSTER_STAT(type, 6);
    int count = random_between(size, 1);
    if (size == 8 || size == 0x10) count = size;
    for (int i = 0; i < count && i < 16; i++) m[0xAB + order[i]] = (uint8_t)(type * 4 + 0x40);
}

/* The way the party left a fight or a room becomes a step in the dungeon. */
void apply_combat_exit(void)
{
    switch (dg.combat_exit) {
    case 1: dg.party_x = (dg.party_x - 1) & 7; dg.facing = dg.dungeon_entry_dir = 3; break;
    case 2: dg.party_x = (dg.party_x + 1) & 7; dg.facing = dg.dungeon_entry_dir = 1; break;
    case 3: dg.party_y = (dg.party_y - 1) & 7; dg.facing = dg.dungeon_entry_dir = 0; break;
    case 4: dg.party_y = (dg.party_y + 1) & 7; dg.facing = dg.dungeon_entry_dir = 2; break;
    case 5:
        if (dg.z_level != 0) {
            dg.z_level--;
            dg.dungeon_entry_dir = 4;
        } else {
            exit_dungeon();
        }
        break;
    case 6:
        if (dg.z_level != 7) {
            dg.z_level++;
            dg.dungeon_entry_dir = 5;
        } else {
            exit_dungeon();
        }
        break;
    }
}

/* The original frees the tile set to make room for the wall and door pictures and loads it
 * again for fights; the port keeps everything in memory but follows the same flags
 * (dungeon_gfx_flags: 1 pictures loaded, 2 tiles loaded, 4 don't swap the room colours), and
 * reloads the tile set where the original does, which also undoes the colour swap. */
void load_dungeon_graphics(int reset_monster)
{
    if (dg.location == 0) return;
    if (!(dg.dungeon_gfx_flags & 1)) {
        dg.dungeon_wall_pic = load_picture(S(dg.dungeon_pic_files[dg.dungeon_style & 3]));
        dg.dungeon_door_pic = load_picture(S(dg.dungeon_pic_files[0]));
    }
    dg.dungeon_gfx_flags = 1;
    if (reset_monster != 0) {
        dg.objects[1].tile = dg.objects[1].tile2 = 0;
        dg.objects[1].x = dg.objects[1].y = 0xFF;
        dg.objects[1].hull = 0xFF;
    }
    set_draw_color(0);
    fill_rect(0xB7, 0xB7, 8, 8);
    set_draw_page(1);
    fill_rect(0xB7, 0xB7, 8, 8);
    set_draw_page(0);
}

void unload_dungeon_graphics(void)
{
    if (dg.dungeon_gfx_flags & 1) {
        if (dg.objects[1].hull != 0xFF) dos_free_mem(dg.dungeon_monster_pic);
        dg.dungeon_monster_pic = 0;
        dos_free_mem(dg.dungeon_door_pic);
        dos_free_mem(dg.dungeon_wall_pic);
    }
    if (!(dg.dungeon_gfx_flags & 2)) load_tileset(ds_str(dg.tileset_file));
    if (!(dg.dungeon_gfx_flags & 4)) patch_tiles(0);   /* rooms get their own colours */
    dg.dungeon_gfx_flags = 6;
}

/* mode 3: a room; 0: a dungeon camp; 1: its ambush (monsters only); 2: a corridor fight.
 * The party starts on the squares for the side it enters from (rows 1-4 of the map's right-hand
 * columns, by facing) - assigned to combatants 0, 1, 2... in order, so with a dead member the
 * squares shift. Then up to 16 things from rows 5-7 (tile, x, y): monsters, or objects (tiles
 * below 0x40, fields, 0xB4-0xB7); tiles 0xEC-0xEF stand for one of four random monster types
 * drawn per fight from ds:385E. Chests get a level and gold an amount by dungeon level. A
 * cleared room (0xA0) has nothing in it. */
void place_combatants(int mode, int room_tile)
{
    uint8_t *m = dg.local_map;
    if (mode != 1) {
        int row = 4;
        if (dg.facing == 0 || dg.facing == 5) row = 3;
        else if (dg.facing == 1) row = 2;
        else if (dg.facing == 3) row = 1;
        clear_combat_tables();
        for (unsigned i = 0; i < dg.party_size; i++) {
            dg.combatants[i].x = dg.objects[i].x = m[row * 0x20 + 0x0B + i];
            dg.combatants[i].y = dg.objects[i].y = m[row * 0x20 + 0x11 + i];
        }
    }
    if (!(mode > 0 && ((room_tile > 0xEF && mode == 3) || mode < 3))) return;
    uint8_t random_types[4];
    for (int i = 0; i < 4; i++) random_types[i] = B8(0x385E + random_between(7, 0));
    for (int i = 0; i < 16; i++) {
        uint8_t tile = m[0xAB + i];
        if (tile == 0) continue;
        int kind, type;
        if (tile < 0x40 || (tile & 0xFC) == 0xB4 || (tile & 0xFC) == 0xE8) {
            kind = 2;
            type = tile;
        } else {
            kind = 0;
            type = (tile - 0x40) >> 2;
        }
        if ((tile & 0xFC) == 0xEC) type = random_types[tile & 3];
        int n = add_combatant(dg.z_level, m[0xEB + i], m[0xCB + i], kind, type);
        if (kind != 2 || n < 0) continue;
        if (type == 1) dg.objects[n & 0x1F].hull = (uint8_t)(dg.z_level * 3 + 7);
        else if (type == 2) dg.objects[n & 0x1F].hull = (uint8_t)random_between(dg.z_level * 10 + 10, 1);
        else if (type <= 0x0F) dg.objects[n & 0x1F].hull = (uint8_t)(B8(0x384D + type) + random_between(B8(0x383F + type) - 1, 0));
    }
}

/* ---- SJOG.OVL: Search, Jimmy, Open and Get in a dungeon ---- */

void step_in_facing(unsigned y, unsigned x, int facing)
{
    dg.dir_x = (uint16_t)x;
    dg.dir_y = (uint16_t)y;
    if (facing == 0) dg.dir_y--;
    else if (facing == 1) dg.dir_x++;
    else if (facing == 2) dg.dir_y++;
    else if (facing == 3) dg.dir_x--;
}

/* Up = ahead, left, right; down = the party's own square. */
int ask_dungeon_direction(int facing)
{
    print_text(S(0x84E6));   /* "Dir-" */
    unsigned key;
    do {
        key = (unsigned)get_command_key() & 0xFF;
        if (game_quit()) key = ' ';
    } while (key != ' ' && (key < 1 || key > 4));
    if (key == ' ') {
        print_text(S(0x84EC));   /* "Pass\n" */
        return 0;
    }
    if (key == 4) {
        print_text(S(0x84FA));   /* "Here\n" */
        dg.dir_x = dg.party_x;
        dg.dir_y = dg.party_y;
        return 1;
    }
    if (key == 1) {
        print_text(S(0x8508));   /* "Left\n" */
        facing = (facing + 3) % 4;
    } else if (key == 2) {
        print_text(S(0x8500));   /* "Right\n" */
        facing = (facing + 1) % 4;
    } else {
        print_text(S(0x84F2));   /* "Ahead\n" */
    }
    step_in_facing(dg.party_y, dg.party_x, facing);
    return 1;
}

/* Searching reveals hidden pits (0x61 -> 0x60, with the hole in the ceiling below), disarms bomb
 * traps on a roll of (2 x level - DEX + 30) / 2, opens secret doors (0xD0 -> 0xE0), crumbles
 * skeletons, and judges chest traps. */
void dungeon_search(void)
{
    int member = ask_which_member();
    if (member == -1) return;
    if (dg.torch_light == 0 && dg.light_spell == 0) {
        print_text(S(0x86DE));   /* "\nYou find:\ndarkness.\n" */
        return;
    }
    if (ask_dungeon_direction(dg.facing) == 0) return;
    uint8_t *cp = &DCELL(dg.z_level, dg.dir_y, dg.dir_x), cell = *cp, kind = cell & 0xF0;
    int need = (int)((unsigned)(dg.z_level * 2 - dg.party[member].dex + 0x1E) & 0xFFFF) >> 1;
    uint16_t text = 0;
    int changed = 0;
    print_text(S(0x86F4));   /* "You find:\n" */
    switch (kind) {
    case 0x00: text = 0x8700; break;   /* "Nothing of note.\n" */
    case 0x10: case 0x20: case 0x30: text = 0x8712; break;   /* "Nothing hidden on the ladder.\n" */
    case 0x40:
        if (need < random_between(0x1E, 1)) {
            if (cell == 0x40) {
                text = 0x8732;   /* "No trap\n" */
                break;
            }
            need = dg.z_level;
        } else {
            need = random_between(8, 1);
        }
        text = need < 4 ? 0x873C : need < 7 ? 0x875C : 0x874C;   /* "A simple trap\n" / "A trap\n" / "A complex trap\n" */
        break;
    case 0x50: text = 0x8764; break;   /* "Nothing hidden on the fountain.\n" */
    case 0x60:
        if (cell == 0x60) {
            text = 0x8786;   /* "Nothing hidden\nin the pit.\n" */
        } else if (cell == 0x61) {
            print_text(S(0x87A2));   /* "A pit!\n" */
            *cp = 0x60;
            if (dg.z_level < 7) DCELL(dg.z_level + 1, dg.dir_y, dg.dir_x) |= 8;
            changed = 1;
        } else if (cell == 0x62) {
            if (need < random_between(0x1E, 1)) {
                print_text(S(0x87AA));   /* "A bomb trap!\n" */
                *cp &= 8;
            } else {
                text = 0x87B8;   /* "Nothing of note.\n" */
            }
        }
        break;
    case 0x70: text = 0x87CA; break;   /* "Treasure!\n" */
    case 0x80: {
        static const uint16_t fields[5] = {0x87D6, 0x87E6, 0x87FC, 0x880E, 0x8822};
        text = fields[cell - 0x80 < 4 ? cell - 0x80 : 4];
        break;
    }
    case 0x90: text = 0x8834; break;   /* "This tile is impossible.\n" */
    case 0xA0: text = 0x884E; break;   /* "Nothing hidden on the door.\n" */
    case 0xB0: text = 0x886C; break;   /* "Nothing hidden on the wall.\n" */
    case 0xC0:
        if ((dg.dungeon_style & 0x0F) == 1) text = 0x88AC;        /* "Nothing on the stalactite.\n" */
        else if ((dg.dungeon_style & 0x0F) == 2) text = 0x888A;   /* "Nothing in the caved in passage.\n" */
        else {
            print_text(S(0x88C8));   /* "Nothing hidden on the skeleton.\n" */
            print_text(S(0x88EA));   /* "It crumbles away.\n" */
            *cp = (uint8_t)((cell & 8) + 0xB0);
            changed = 1;
        }
        break;
    case 0xD0:
        print_text(S(0x88FE));   /* "A hidden door!\n" */
        *cp = (uint8_t)((cell & 8) + 0xE0);
        changed = 1;
        break;
    case 0xE0: case 0xF0: text = 0x890E; break;   /* "Nothing hidden on the door.\n" */
    }
    if (text) print_text(S(text));
    if (changed) {
        draw_dungeon_view(1);
        copy_back_rect(0xB7, 0xB7, 8, 8);
    }
}

/* A locked chest (0x4n, n != 0 and != 8) opens on the same roll; the key breaks otherwise, and
 * always on a chest that isn't locked. */
void dungeon_jimmy(void)
{
    print_text(S(0x8A7A));   /* "\n" */
    int member = ask_which_member();
    if (member == -1) return;
    uint8_t cell = DHERE;
    if ((cell & 0xF0) == 0x70) {
        print_text(S(0x8AB8));   /* "Already open!\n" */
    } else if ((cell & 0xF0) != 0x40) {
        print_text(S(0x8AC8));   /* "What?\n" */
    } else if (dg.keys == 0) {
        print_text(S((cell & 0xF7) == 0x40 ? 0x8A7C : 0x8A92));   /* "No keys!\n" */
    } else if ((cell & 0xF7) != 0x40 &&
               (int)((unsigned)(dg.z_level * 2 - dg.party[member].dex + 0x1E) & 0xFFFF) >> 1 < random_between(0x1E, 1)) {
        print_text(S(0x8A9C));   /* "Chest unlocked\n" */
        DHERE = (uint8_t)((cell & 8) + 0x40);
    } else {
        print_text(S((cell & 0xF7) == 0x40 ? 0x8A86 : 0x8AAC));   /* "Key broke!\n" */
        dg.keys--;
    }
}

void dungeon_open_chest(void)
{
    uint8_t cell = DHERE;
    if ((cell & 0xF0) == 0x40) {
        int member = ask_which_member();
        if (member == -1) return;
        if (cell & 7) spring_trap((unsigned)member);
        DHERE = (uint8_t)((cell & 8) + 0x70);
        print_text(S(0x8B96));   /* "\nChest opened\n" */
    } else if ((cell & 0xF0) == 0x70) {
        print_text(S(0x8BA6));   /* "Already Open!\n" */
    } else {
        print_text(S(0x8BB6));   /* "What?\n" */
    }
}

/* An open chest is emptied: seven kinds of thing (ds:41CC), each if a roll of 1 to 4 x level + 4
 * reaches its threshold (ds:41BC); gold 1 to 8 x level, potions and scrolls of a random type. */
void dungeon_get(void)
{
    print_text(S(0x8DA4));   /* "Get\n" */
    uint8_t kind = DHERE & 0xF0;
    if (kind == 0x40) {
        print_text(S(0x8DAA));   /* "Must open first!\n" */
        return;
    }
    if (kind != 0x70) {
        print_text(S(0x8DDA));   /* "Not here!\n" */
        return;
    }
    DHERE &= 8;
    print_text(S(0x8DBC));   /* "contents\nof chest\nYou find:\n" */
    for (int i = 0; i < 7; i++) {
        if (B8(0x41BC + i) > (unsigned)random_between(dg.z_level * 4 + 4, 1)) continue;
        if (i == 5) get_item(0x20, (unsigned)random_between(7, 0), 3);
        else if (i == 6) get_item(0x20, (unsigned)random_between(7, 0), 4);
        else get_item(0x20, (unsigned)random_between(i == 1 ? dg.z_level << 3 : B8(0x41C4 + i), 1), B8(0x41CC + i));
    }
}
