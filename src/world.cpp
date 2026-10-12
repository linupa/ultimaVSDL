/* world.cpp - per-turn helpers shared by the town and outdoor loops. See world.h. */
#include <string.h>
#include "combat.h"
#include "gamedata.h"
#include "gametime.h"
#include "gfx.h"
#include "input.h"
#include "commands.h"
#include "map.h"
#include "misc.h"
#include "objects.h"
#include "party.h"
#include "picture.h"
#include "status.h"
#include "strings.h"
#include "talk.h"
#include "text.h"
#include "tiles.h"
#include "town.h"
#include "view.h"
#include "world.h"

const char *objects_file_name(void)
{
    return strings[map.z == 0 ? Strings::BRIT_OOL_3989 : Strings::UNDER_OOL_3992];   /* "BRIT.OOL" / "UNDER.OOL" */
}

void damage_member(int damage, int member)
{
    highlight_member_row(member);
    sound_noise(2000, 0x640, 10);
    highlight_member_row(member);
    PartyMember *m = &party[member];
    m->hp = (uint16_t)(m->hp - damage);
    if ((int16_t)m->hp < 1) {
        m->hp = 0;
        m->status = 'D';
        if (member == party.active_player) party.active_player = 0xFF;
    }
    draw_status_bar();
}

void damage_party(void)
{
    for (unsigned i = 0; i < 6; i++)
        if (i < party.size() && !party[i].dead()) damage_member(random_between(8, 1), (int)i);
}

void per_turn_status(void)
{
    uint16_t eaters = 0;
    for (unsigned i = 0; i < party.size(); i++) {
        char st = party[i].status;
        if (st == 'D' && i == party.active_player) party.active_player = 0xFF;
        if (st != 'D' && st != 'S') {
            if (st == 'P') damage_member(1, (int)i);
            eaters++;
        }
    }
    if (game_time.hour != game_time.prev_hour) {
        if (party.food == 0) {
            print_text(strings[Strings::Starving_54C8]);   /* "Starving!\n" */
            damage_party();
        } else if (game_time.hour == 6 || game_time.hour == 12 || game_time.hour == 18) {
            sub_word_floor0(eaters, &party.food);
        }
        game_time.prev_hour = game_time.hour;
    }
    add_byte_capped(0xFF, 1, &conversation.alms_counter);
    if (party.active_spell_turns != 0 && party.active_spell_turns != 0xFF && --party.active_spell_turns == 0) {
        party.active_spell = 0;
        draw_status_bar();
    }
    regen_ring_tick();
}

void poison_member(unsigned member)
{
    if (member < party.size() && !party[member].dead()) {
        party[member].status = 'P';
        draw_status_bar();
    }
}

int random_stat_roll(void)
{
    int r = random_upto(0x3C) / 2;
    return r == 0 ? 1 : r;
}

/* The kind of trap a chest springs outside a scene, one of eight at random (ds:559E, from
 * DATA.OVL): 0 0 0 1 1 2 2 3. */
static uint8_t trap_kinds[8];
[[maybe_unused]] static const bool trap_kinds_added = (Data::instance().add(0x559E, trap_kinds, "world.trap_kinds"), true);

void spring_trap(unsigned member)
{
    unsigned kind;
    sound_noise(500, 3000, 0x28);
    if (!map.in_scene()) kind = trap_kinds[random_between(7, 0)];
    else kind = (unsigned)random_between(1, 0);
    if (kind == 0) {
        print_text(strings[Strings::ACID_5581]);   /* "ACID!\n" */
        damage_member(random_stat_roll(), (int)member);
    } else if (kind == 1) {
        print_text(strings[Strings::POISON_5588]);   /* "POISON!\n" */
        poison_member(member);
    } else if (kind == 2) {
        print_text(strings[Strings::BOMB_5591]);   /* "BOMB!\n" */
        damage_party();
    } else if (kind == 3) {
        print_text(strings[Strings::GAS_5598]);   /* "GAS!\n" */
        for (unsigned i = 0; i < 6; i++) poison_member(i);
    }
}

int moonstone_here(int moonstone)
{
    if (map.moonstone_loc[moonstone] != map.id() || map.moonstone_z[moonstone] != map.z) return 0;
    if (map.is_outdoors()) {
        if ((uint8_t)(map.moonstone_x[moonstone] - map.origin_x) > 0x1F) return 0;
        if ((uint8_t)(map.moonstone_y[moonstone] - map.origin_y) > 0x1F) return 0;
    }
    return 1;
}

/* From 20:00 to 4:59 the gates rise a row a tick (moongate_phase up to 16); by day they sink and
 * the square becomes grass again. */
void update_moongates(void)
{
    uint8_t tile = 0xDC;
    if (game_time.hour < 0x14 && game_time.hour > 4) {
        sub_byte_floor0(1, &game_time.moongate_phase);
        if (game_time.moongate_phase == 0) tile = 5;
    } else {
        add_byte_capped(0x10, 1, &game_time.moongate_phase);
    }
    for (int i = 0; i < 8; i++) {
        if (!moonstone_here(i)) continue;
        uint8_t *p = map.get_map_tile_ptr(map.moonstone_y[i], map.moonstone_x[i]);
        uint8_t old = *p;
        *p = tile;
        view.rebuild = true;
        if (old != tile) update_light_map();
    }
}

/* The original saves the objects before leaving Britannia. From a
 * town a gate leads only to another town; between the surface and anything else nothing but the
 * position changes. */
int moongate_travel(int moonstone)
{
    if (map.moonstone_loc[moonstone] == 0xFF) return 0;
    if (map.is_outdoors()) save_file_part(0x100, map.objects.bytes(), objects_file_name());
    uint8_t dest = map.moonstone_loc[moonstone];
    map.x = map.moonstone_x[moonstone];
    map.y = map.moonstone_y[moonstone];
    map.z = map.moonstone_z[moonstone];
    if (dest == 0 || map.is_outdoors() || dest > 0x20 || !map.on_tile_map()) {
        if (dest == 0 && map.is_outdoors()) {
            load_file_part(0, 0x100, map.objects.bytes(), objects_file_name());
            map.enter_outdoors();
        }
    } else {
        map.set(Map::Id(dest));
        town.enter_town(1);
    }
    return 1;
}

/* Standing on a moongate: the party dissolves into it, the gate sinks, and the party arrives at
 * the moonstone chosen by the phase of the moon overhead (Trammel before noon, Felucca after).
 * In the first ten minutes after midnight it leads to a shrine instead (returns 1). */
int check_moongate(void)
{
    int shrine = 0;
    if (*map.get_map_tile_ptr(map.y, map.x) != 0xDC) return 0;
    wait_turns(1);
    sound_sweep(2, 2000, 30000, 1, 0x170C);
    uint8_t transport = party.transport;
    party.transport = 0x16;
    dissolve_tile(5, 5, 0xDC);
    party.transport = 0;
    wait_turns(1);
    game_time.moongate_phase = 0x0F;
    do {
        shimmer_tiles(5, 5, game_time.moongate_phase);
        delay_ticks(2);
    } while (--game_time.moongate_phase != 0);
    *map.get_map_tile_ptr(map.y, map.x) = 5;
    view.rebuild = true;
    draw_tile_at(5, 5, 5);
    if (game_time.hour == 0 && game_time.minute < 10) shrine = 1;
    else moongate_travel((game_time.hour < 0x0C ? game_time.moon1_phase : game_time.moon2_phase) - 0x30);
    party.transport = transport;
    return shrine;
}

void set_wind_direction(int dir)
{
    if (dir != -1) {
        map.wind_direction = (uint8_t)dir;
        party.sail_counter = 0;
    }
    if (!map.on_tile_map() || map == Map::ARARAT) return;
    if (map.z > 0x7F) {   /* underground: a plain frame instead of the label */
        set_draw_color(gfx.color_text);
        draw_line(0xB8, 0x98, 0xB8, 0x30);
        set_draw_color(gfx.color_frame);
        fill_rect(0xBF, 0x98, 0xB9, 0x30);
        return;
    }
    select_text_window(0);
    set_cursor(0x17, 6);
    draw_label_left();
    static const Strings::Id names[5] = {Strings::Calm_555C, Strings::North_5562, Strings::South_5568, Strings::East_556E, Strings::West_5574};   /* "Calm " North South East West */
    if (map.wind_direction < 5) print_text(strings[names[map.wind_direction]]);
    print_text(strings[Strings::Winds_557A]);   /* " Winds" */
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
    if (!map.on_tile_map()) return;
    uint8_t sky[13];
    memset(sky, ' ', 12);
    sky[12] = 0;
    WindowScope scope(0);
    select_font(1);
    int col = 0x11 - game_time.hour;
    if (col < 12 && col >= 0) sky[col] = '*';
    game_time.moon1_phase = game_time.moon_phase(game_time.day, 0);
    col = 8 - game_time.hour;
    if (col < -12) col += 24;
    if (col < 12 && col >= 0) sky[col] = game_time.moon1_phase;
    game_time.moon2_phase = game_time.moon_phase(game_time.day, 1);
    col = 2 - game_time.hour;
    if (col < -12) col += 24;
    if (col < 12 && col >= 0) sky[col] = game_time.moon2_phase;
    set_cursor(0, 6);
    if (map == Map::ARARAT || map.z > 0x7F) {
        set_draw_color(gfx.color_frame);
        fill_rect(6, 0x98, 0, 0x28);
        set_draw_color(gfx.color_text);
        draw_line(7, 0x98, 7, 0x28);
    } else {
        for (int i = 0; sky[i]; i++) {
            set_text_color((uint8_t)(sky[i] == '*' ? gfx.color_chest : gfx.color_ladder));
            put_char(sky[i]);
        }
        set_text_color((uint8_t)gfx.color_text);
    }
    select_font(0);
}

void clear_location_label(void)
{
    WindowScope scope(0);
    if (map != Map::ARARAT && map.z < 0x80) {
        set_cursor(0, 5);
        draw_label_left();
        print_text(strings[Strings::SP_SP_SP_SP_A3E2]);   /* twelve spaces */
        draw_label_right();
    }
}

/* The member a command is for: the active player if one is set; otherwise the only able member,
 * or "Player: " asks. In combat it is the member whose turn it is. -1 = none. */
int ask_which_member(void)
{
    int member = -1;
    if (map.id() >= 0x81) {
        member = combat.combatants[combat.turn].kind;
    } else if (party.active_player != 0xFF) {
        member = party.active_player;
    } else {
        int able = 0;
        for (unsigned i = 0; i != party.size(); i++)
            if (party[i].good() || party[i].poisoned()) {
                able++;
                member = (int)i;
            }
        if (able > 1) {
            for (;;) {
                print_text(strings[Strings::Player_A3C4]);   /* "Player: " */
                member = ask_member_number();
                if (member < 0) break;
                if (party[member].good() || party[member].poisoned()) {
                    print_text(party[member].name);
                    if (get_cursor_col() != 0) put_char('\n');
                    break;
                }
                print_text(strings[Strings::Disabled_A3CE]);   /* "Disabled!\n\n" */
            }
        }
    }
    if (member == -1) print_text(strings[Strings::None_A3DA]);   /* "None!\n" */
    else if (member == -2) put_char('\n');
    return member;
}

/* Sixteen 16-square shapes (pairs dx, dy, Map::beam_shapes) are the positions of the lighthouse beam as
 * it sweeps; a shape is lit (0xFF) or cleared in the light map. The port checks the bounds. */
void stamp_view_shape(int y, int x, int set, int shape)
{
    uint8_t v = set == 1 ? 0xFF : 0;
    const int8_t (*p)[2] = map.beam_shapes[shape % 0x10];
    for (int i = 0; i < 0x10; i++, p++) {
        int off = ((*p)[1] + y) * 0x20 + (*p)[0] + x;
        if (off >= 0 && off < (int)sizeof view.local_map) view.local_map[off] = v;
    }
}

/* Runs while it is dark (light radius below 50) and the level has a light source. Three
 * consecutive shapes are lit at a time. */
void animate_light_beams(void)
{
    if (view.light_radius > 0x31 || map.beam1_x == 0xFFFF) {
        map.beam_frame = 0xFF;
        return;
    }
    int two = map.beam2_x != 0xFFFF;
    view.rebuild = true;
    if (map.beam_frame == 0xFF) {
        map.beam_frame = 0;
        for (int s = 0; s < 3; s++) stamp_view_shape(map.beam1_y, map.beam1_x, 1, s);
        if (two)
            for (int s = 0; s < 3; s++) stamp_view_shape(map.beam2_y, map.beam2_x, 1, s);
    } else {
        stamp_view_shape(map.beam1_y, map.beam1_x, 0, map.beam_frame);
        if (two) stamp_view_shape(map.beam2_y, map.beam2_x, 0, map.beam_frame);
        map.beam_frame++;
        stamp_view_shape(map.beam1_y, map.beam1_x, 1, map.beam_frame + 2);
        if (two) stamp_view_shape(map.beam2_y, map.beam2_x, 1, map.beam_frame + 2);
    }
    if (map.beam_frame > 0x0F) map.beam_frame = 0;
}
