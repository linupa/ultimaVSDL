/* world.c - per-turn helpers shared by the town and outdoor loops. See world.h. */
#include <string.h>
#include "data.h"
#include "gfx.h"
#include "input.h"
#include "commands.h"
#include "map.h"
#include "misc.h"
#include "objects.h"
#include "picture.h"
#include "status.h"
#include "text.h"
#include "tiles.h"
#include "town.h"
#include "view.h"
#include "world.h"

const char *objects_file_name(void)
{
    return ds_str(dg.z_level == 0 ? 0x3989 : 0x3992);   /* "BRIT.OOL" / "UNDER.OOL" */
}

void damage_member(int damage, int member)
{
    highlight_member_row(member);
    sound_noise(2000, 0x640, 10);
    highlight_member_row(member);
    PartyMember *m = &dg.party[member];
    m->hp = (uint16_t)(m->hp - damage);
    if ((int16_t)m->hp < 1) {
        m->hp = 0;
        m->status = 'D';
        if (member == dg.active_player) dg.active_player = 0xFF;
    }
    draw_status_bar();
}

void damage_party(void)
{
    for (unsigned i = 0; i < 6; i++)
        if (i < dg.party_size && dg.party[i].status != 'D') damage_member(random_between(8, 1), (int)i);
}

void per_turn_status(void)
{
    uint16_t eaters = 0;
    for (unsigned i = 0; i < dg.party_size; i++) {
        char st = dg.party[i].status;
        if (st == 'D' && i == dg.active_player) dg.active_player = 0xFF;
        if (st != 'D' && st != 'S') {
            if (st == 'P') damage_member(1, (int)i);
            eaters++;
        }
    }
    if (dg.hour != dg.prev_hour) {
        if (dg.food == 0) {
            print_text(ds_str(0x54C8));   /* "Starving!\n" */
            damage_party();
        } else if (dg.hour == 6 || dg.hour == 12 || dg.hour == 18) {
            sub_word_floor0(eaters, &dg.food);
        }
        dg.prev_hour = dg.hour;
    }
    add_byte_capped(0xFF, 1, &dg.alms_counter);
    if (dg.active_spell_turns != 0 && dg.active_spell_turns != 0xFF && --dg.active_spell_turns == 0) {
        dg.active_spell = 0;
        draw_status_bar();
    }
    regen_ring_tick();
}

void poison_member(unsigned member)
{
    if (member < dg.party_size && dg.party[member].status != 'D') {
        dg.party[member].status = 'P';
        draw_status_bar();
    }
}

int random_stat_roll(void)
{
    int r = random_upto(0x3C) / 2;
    return r == 0 ? 1 : r;
}

void spring_trap(unsigned member)
{
    unsigned kind;
    sound_noise(500, 3000, 0x28);
    if (dg.location < 0x80) kind = ds_u8((uint16_t)(0x559E + random_between(7, 0)));   /* 0 0 0 1 1 2 2 3 */
    else kind = (unsigned)random_between(1, 0);
    if (kind == 0) {
        print_text(ds_str(0x5581));   /* "ACID!\n" */
        damage_member(random_stat_roll(), (int)member);
    } else if (kind == 1) {
        print_text(ds_str(0x5588));   /* "POISON!\n" */
        poison_member(member);
    } else if (kind == 2) {
        print_text(ds_str(0x5591));   /* "BOMB!\n" */
        damage_party();
    } else if (kind == 3) {
        print_text(ds_str(0x5598));   /* "GAS!\n" */
        for (unsigned i = 0; i < 6; i++) poison_member(i);
    }
}

int moonstone_here(int moonstone)
{
    if (dg.moonstone_loc[moonstone] != dg.location || dg.moonstone_z[moonstone] != dg.z_level) return 0;
    if (dg.location == 0) {
        if ((uint8_t)(dg.moonstone_x[moonstone] - dg.map_origin_x) > 0x1F) return 0;
        if ((uint8_t)(dg.moonstone_y[moonstone] - dg.map_origin_y) > 0x1F) return 0;
    }
    return 1;
}

/* From 20:00 to 4:59 the gates rise a row a tick (moongate_phase up to 16); by day they sink and
 * the square becomes grass again. */
void update_moongates(void)
{
    uint8_t tile = 0xDC;
    if (dg.hour < 0x14 && dg.hour > 4) {
        sub_byte_floor0(1, &dg.moongate_phase);
        if (dg.moongate_phase == 0) tile = 5;
    } else {
        add_byte_capped(0x10, 1, &dg.moongate_phase);
    }
    for (int i = 0; i < 8; i++) {
        if (!moonstone_here(i)) continue;
        uint8_t *p = get_map_tile_ptr(dg.moonstone_y[i], dg.moonstone_x[i]);
        uint8_t old = *p;
        *p = tile;
        dg.redraw_flags |= 2;
        if (old != tile) update_light_map();
    }
}

/* The original saves the objects before leaving Britannia (and handles the disk swap). From a
 * town a gate leads only to another town; between the surface and anything else nothing but the
 * position changes. */
int moongate_travel(int moonstone)
{
    if (dg.moonstone_loc[moonstone] == 0xFF) return 0;
    if (dg.location == 0) save_file_part(0x100, (const uint8_t *)dg.objects, objects_file_name());
    uint8_t dest = dg.moonstone_loc[moonstone];
    dg.party_x = dg.moonstone_x[moonstone];
    dg.party_y = dg.moonstone_y[moonstone];
    dg.z_level = dg.moonstone_z[moonstone];
    if (dest == 0 || dg.location == 0 || dest > 0x20 || dg.location > 0x20) {
        if (dest == 0 && dg.location == 0) {
            load_file_part(0, 0x100, (uint8_t *)dg.objects, objects_file_name());
            enter_outdoors();
        }
    } else {
        dg.location = dest;
        enter_town(1);
    }
    return 1;
}

/* Standing on a moongate: the party dissolves into it, the gate sinks, and the party arrives at
 * the moonstone chosen by the phase of the moon overhead (Trammel before noon, Felucca after).
 * In the first ten minutes after midnight it leads to a shrine instead (returns 1). */
int check_moongate(void)
{
    int shrine = 0;
    if (*get_map_tile_ptr(dg.party_y, dg.party_x) != 0xDC) return 0;
    wait_turns(1);
    sound_sweep(2, 2000, 30000, 1, 0x170C);
    uint8_t transport = dg.transport;
    dg.transport = 0x16;
    dissolve_tile(5, 5, 0xDC);
    dg.transport = 0;
    wait_turns(1);
    dg.moongate_phase = 0x0F;
    do {
        shimmer_tiles(5, 5, dg.moongate_phase);
        delay_ticks(2);
    } while (--dg.moongate_phase != 0);
    *get_map_tile_ptr(dg.party_y, dg.party_x) = 5;
    dg.redraw_flags |= 2;
    draw_tile_at(5, 5, 5);
    if (dg.hour == 0 && dg.minute < 10) shrine = 1;
    else moongate_travel((dg.hour < 0x0C ? dg.moon1_phase : dg.moon2_phase) - 0x30);
    dg.transport = transport;
    return shrine;
}

void set_wind_direction(int dir)
{
    if (dir != -1) {
        dg.wind_direction = (uint8_t)dir;
        dg.sail_counter = 0;
    }
    if (dg.location > 0x20 || dg.location == 0x19) return;
    if (dg.z_level > 0x7F) {   /* underground: a plain frame instead of the label */
        set_draw_color(dg.color_text);
        draw_line(0xB8, 0x98, 0xB8, 0x30);
        set_draw_color(dg.color_frame);
        fill_rect(0xBF, 0x98, 0xB9, 0x30);
        return;
    }
    select_text_window(0);
    set_cursor(0x17, 6);
    draw_label_left();
    static const uint16_t names[5] = {0x555C, 0x5562, 0x5568, 0x556E, 0x5574};   /* "Calm " North South East West */
    if (dg.wind_direction < 5) print_text(ds_str(names[dg.wind_direction]));
    print_text(ds_str(0x557A));   /* " Winds" */
    draw_label_right();
    select_text_window(2);
}

void random_wind_change(void)
{
    if (random_between(0x3F, 0) != 0) return;
    int dir;
    do {
        dir = random_between(4, 0);
        if (dir != 0) break;
    } while (random_between(0xFF, 0) < 0xC0);   /* calm is rarer */
    set_wind_direction(dir);
}

/* Twelve cells above the view: the sun ('*' in the rune font) at column 17 - hour, Trammel at
 * 8 - hour, Felucca at 2 - hour. The moons' glyphs come from the table at ds:1ED8, two per day of
 * the month, and also pick the moongate destination ('0'-'7'). */
void draw_sky_bar(void)
{
    uint16_t window = dg.cur_window_index;
    if (dg.location > 0x20) return;
    uint8_t sky[13];
    memset(sky, ' ', 12);
    sky[12] = 0;
    select_text_window(0);
    select_font(1);
    int col = 0x11 - dg.hour;
    if (col < 12 && col >= 0) sky[col] = '*';
    dg.moon1_phase = ds_u8((uint16_t)(0x1ED8 + dg.day * 2));
    col = 8 - dg.hour;
    if (col < -12) col += 24;
    if (col < 12 && col >= 0) sky[col] = dg.moon1_phase;
    dg.moon2_phase = ds_u8((uint16_t)(0x1ED9 + dg.day * 2));
    col = 2 - dg.hour;
    if (col < -12) col += 24;
    if (col < 12 && col >= 0) sky[col] = dg.moon2_phase;
    set_cursor(0, 6);
    if (dg.location == 0x19 || dg.z_level > 0x7F) {
        set_draw_color(dg.color_frame);
        fill_rect(6, 0x98, 0, 0x28);
        set_draw_color(dg.color_text);
        draw_line(7, 0x98, 7, 0x28);
    } else {
        for (int i = 0; sky[i]; i++) {
            set_text_color((uint8_t)(sky[i] == '*' ? dg.color_chest : dg.color_ladder));
            put_char(sky[i]);
        }
        set_text_color((uint8_t)dg.color_text);
    }
    select_text_window(window);
    select_font(0);
}

void clear_location_label(void)
{
    uint16_t window = dg.cur_window_index;
    select_text_window(0);
    if (dg.location != 0x19 && dg.z_level < 0x80) {
        set_cursor(0, 5);
        draw_label_left();
        print_text(ds_str(0xA3E2));   /* twelve spaces */
        draw_label_right();
    }
    select_text_window(window);
}

/* The member a command is for: the active player if one is set; otherwise the only able member,
 * or "Player: " asks. In combat it is the member whose turn it is. -1 = none. */
int ask_which_member(void)
{
    int member = -1;
    if (dg.location >= 0x81) {
        member = dg.combatants[dg.combat_turn].kind;
    } else if (dg.active_player != 0xFF) {
        member = dg.active_player;
    } else {
        int able = 0;
        for (unsigned i = 0; i != dg.party_size; i++)
            if (dg.party[i].status == 'G' || dg.party[i].status == 'P') {
                able++;
                member = (int)i;
            }
        if (able > 1) {
            for (;;) {
                print_text(ds_str(0xA3C4));   /* "Player: " */
                member = ask_member_number();
                if (member < 0) break;
                if (dg.party[member].status == 'G' || dg.party[member].status == 'P') {
                    print_text(dg.party[member].name);
                    if (get_cursor_col() != 0) put_char('\n');
                    break;
                }
                print_text(ds_str(0xA3CE));   /* "Disabled!\n\n" */
            }
        }
    }
    if (member == -1) print_text(ds_str(0xA3DA));   /* "None!\n" */
    else if (member == -2) put_char('\n');
    return member;
}

/* Sixteen 16-square shapes (pairs dx, dy at ds:1F7E) are the positions of the lighthouse beam as
 * it sweeps; a shape is lit (0xFF) or cleared in the light map. The port checks the bounds. */
void stamp_view_shape(int y, int x, int set, int shape)
{
    uint8_t v = set == 1 ? 0xFF : 0;
    const int8_t *p = (const int8_t *)ds_ptr((uint16_t)(0x1F7E + (shape % 0x10) * 0x20));
    for (int i = 0; i < 0x10; i++, p += 2) {
        int off = (p[1] + y) * 0x20 + p[0] + x;
        if (off >= 0 && off < (int)sizeof dg.local_map) dg.local_map[off] = v;
    }
}

/* Runs while it is dark (light radius below 50) and the level has a light source. Three
 * consecutive shapes are lit at a time. */
void animate_light_beams(void)
{
    if (dg.light_radius > 0x31 || dg.beam1_x == 0xFFFF) {
        dg.beam_frame = 0xFF;
        return;
    }
    int two = dg.beam2_x != 0xFFFF;
    dg.redraw_flags = 1;
    if (dg.beam_frame == 0xFF) {
        dg.beam_frame = 0;
        for (int s = 0; s < 3; s++) stamp_view_shape(dg.beam1_y, dg.beam1_x, 1, s);
        if (two)
            for (int s = 0; s < 3; s++) stamp_view_shape(dg.beam2_y, dg.beam2_x, 1, s);
    } else {
        stamp_view_shape(dg.beam1_y, dg.beam1_x, 0, dg.beam_frame);
        if (two) stamp_view_shape(dg.beam2_y, dg.beam2_x, 0, dg.beam_frame);
        dg.beam_frame++;
        stamp_view_shape(dg.beam1_y, dg.beam1_x, 1, dg.beam_frame + 2);
        if (two) stamp_view_shape(dg.beam2_y, dg.beam2_x, 1, dg.beam_frame + 2);
    }
    if (dg.beam_frame > 0x0F) dg.beam_frame = 0;
}
