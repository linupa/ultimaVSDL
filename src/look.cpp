/* look.cpp - Look and the area map (LOOKOBJ.OVL). See look.h. */
#include <string.h>
#include "cmds.h"
#include "commands.h"
#include "gamedata.h"
#include "gametime.h"
#include "gfx.h"
#include "input.h"
#include "map.h"
#include "look.h"
#include "misc.h"
#include "objects.h"
#include "party.h"
#include "picture.h"
#include "platform.h"
#include "status.h"
#include "strings.h"
#include "text.h"
#include "textbuf.h"
#include "view.h"
#include "world.h"


void print_tile_description(int tile)
{
    uint8_t off[2] = {0, 0};
    char text[0x81];
    memset(text, 0, sizeof text);
    load_file_part((uint32_t)(tile << 1), 2, off, strings[Strings::LOOK2_DAT_71F8]);   /* "LOOK2.DAT" */
    load_file_part((uint32_t)(off[0] | off[1] << 8), 0x80, (uint8_t *)text, strings[Strings::LOOK2_DAT_7202]);
    print_text(text);
}

void print_object_description(int tile)
{
    uint8_t off[2] = {0, 0};
    char text[0x81];
    memset(text, 0, sizeof text);
    load_file_part((uint32_t)((tile << 1) + 0x200), 2, off, strings[Strings::LOOK2_DAT_7374]);
    load_file_part((uint32_t)(off[0] | off[1] << 8), 0x80, (uint8_t *)text, strings[Strings::LOOK2_DAT_737E]);
    print_text(text);
    if (get_cursor_col() != 0) put_char('\n');
}

/* A coin and a wish: at locations 0x16 and 0x1F a wish for a horse (or one of five sports
 * cars) puts a horse beside the party. */
void look_well(uint8_t z, uint8_t y, uint8_t x)
{
    char wish[16];
    print_text(strings[Strings::a_well_Drop_a_coin_720C]);   /* "a well.\n\nDrop a coin?" */
    char c;
    do {
        c = (char)get_command_key();
        if (platform->quit_requested()) return;
    } while (c != 'Y' && c != 'N');
    if (c == 'N') {
        print_text(strings[Strings::No_7222]);   /* "No\n" */
        return;
    }
    print_text(strings[Strings::Yes_7226]);   /* "Yes\n" */
    if (party.gold == 0) return;
    print_text(strings[Strings::Thy_wish_722C]);   /* "\nThy wish?\n" */
    party.gold--;
    read_line(0x0C, wish);
    if (wish[0] == '\0') {
        print_text(strings[Strings::Nothing_7238]);   /* "Nothing\n" */
        return;
    }
    static const Strings::Id words[6] = {Strings::Corvette_7242, Strings::Ferrari_724C, Strings::Lamborghini_7254, Strings::Lotus_7260, Strings::Porsche_7266, Strings::Horse_726E};   /* Corvette Ferrari Lamborghini Lotus Porsche Horse */
    int horse = 0;
    for (int i = 0; i < 6 && !horse; i++) horse = str_find_nocase(wish, strings[words[i]]) >= 0;
    if (!horse) {
        print_text(strings[Strings::No_effect_728C]);   /* "\nNo effect...\n" */
    } else if (map == Map::PAWS || map == Map::EMPATH_ABBEY) {
        print_text(strings[Strings::Poof_7284]);   /* "\nPoof!\n" */
        sound_noise(2000, 3000, 10);
        set_object_entry(alloc_object_slot(), 0, z, y, (uint8_t)(x + 1), 0x10, 0x10);
    } else {
        print_text(strings[Strings::No_effect_7274]);   /* "\nNo effect...\n" */
    }
}

void look_town_fountain(void)
{
    print_text(strings[Strings::a_gurgling_fountain_729C]);   /* "a gurgling fountain!\n\n" */
    print_text(strings[Strings::Who_will_drink_72B4]);   /* "Who will drink?\n" */
    int member = ask_member_number();
    if (member == -1) print_text(strings[Strings::None_72C6]);   /* "None!\n" */
    else if (party[member].dead() || party[member].asleep()) print_text(strings[Strings::Incapacitated_72CE]);   /* "Incapacitated!\n\n" */
    else print_text(strings[Strings::Refreshing_72E0]);   /* "Refreshing...\n" */
}

/* A planet: a 3 x 3 blob with a pixel above and below, at 8-pixel cells (clipped at the bottom). */
void draw_sky_moon(int col, int phase)
{
    set_draw_color(gfx.color_text);
    int x = phase * 8;
    plot_pixel(col * 8 + 8, x + 6);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            if (x + j < 0xB1) plot_pixel(col * 8 + i + 7, x + j + 7);
    if (x < 0xAE) plot_pixel(col * 8 + 8, x + 10);
}

/* A comet's tail beside the planet of the town a Shadowlord is in. */
void draw_sky_shadowlord(int col, int phase)
{
    int x = phase * 8, y = col * 8;
    set_draw_color(gfx.color_fire);
    if (x > 2) {
        draw_vline(y + 0x0C, y + 10, x + 5);
        draw_vline(y + 0x0C, y + 10, x + 6);
        draw_vline(y + 0x0C, y + 8, x + 7);
    }
    if (x < 0xB0) draw_vline(y + 0x0C, y + 8, x + 8);
    if (x < 0xAF) draw_vline(y + 10, y + 6, x + 9);
    if (x < 0xAE) draw_vline(y + 10, y + 6, x + 10);
    if (x < 0xAD) draw_vline(y + 8, y + 5, x + 0x0B);
    if (x < 0xAC) draw_vline(y + 7, y + 5, x + 0x0C);
}

/* From DATA.OVL: for each moon or Shadowlord row of the telescope view, where the walk starts
 * (ds:3750) and the row it is drawn on (ds:3758), then each one's track of 22 places, 0 = a
 * place it skips (ds:3760). */
static uint8_t sky_start[8], sky_row[8], sky_track[8][0x16];
[[maybe_unused]] static const bool sky_added = (Data::instance().add(0x3750, sky_start, "look.sky_start"),
                                                Data::instance().add(0x3758, sky_row, "look.sky_row"),
                                                Data::instance().add(0x3760, sky_track, "look.sky_track"), true);

/* By day the sun burns the looker for 1. By night: 80 stars and the eight planets, one for each
 * of towns 1-8. A planet's row is fixed (ds:3758); its column starts at ds:3750 for the date
 * 4-5-139 and steps back one of 22 positions for each day since on which its orbit mask (22
 * bytes at ds:3760) has a 1 there - so the planets tell where the Shadowlords are. */
void look_sky(void)
{
    if (!(game_time.hour < 6 || game_time.hour > 0x11)) {
        print_text(strings[Strings::the_sun_72F0]);   /* "the sun!\n" */
        if (party.active_player == 0xFF && check_party_state() == 0) party.active_player = (uint8_t)party.first_able();
        damage_member(1, party.active_player);
        draw_status_bar();
        return;
    }
    view.rebuild = true;
    for (int r = 0; r < 11; r++) memset(&view.mask[r * 0x20], 0xFF, 11);
    view.mask[0x145] = 0x59;   /* the telescope at the bottom */
    draw_view();
    set_draw_color((uint16_t)(gfx.color_frame + 8));
    for (int i = 0x50; i != 0; i--) {
        int x = random_between(0xB6, 9), y = random_between(0xAC, 9);
        plot_pixel(y, x);
    }
    for (int p = 0; p < 8; p++) {
        int yr = (int16_t)game_time.year % 100, mo = game_time.month, dy = game_time.day;
        int pos = sky_start[p];
        int guard = 0;
        while ((yr > 0x27 || (yr == 0x27 && mo > 4) || (yr == 0x27 && mo == 4 && dy > 5)) && guard++ < 100000) {
            if (--dy == 0) {
                dy = 0x1C;
                mo--;
            }
            if (mo == 0) {
                mo = 0x0D;
                yr--;
            }
            do {
                if (--pos < 0) pos = 0x15;
            } while (sky_track[p][pos] == 0);
        }
        uint8_t row = sky_row[p];
        draw_sky_moon(row, pos + 1);
        for (int s = 0; s < 3; s++)
            if (map.shadowlord_locations[s] == p + 1) draw_sky_shadowlord(row, pos);
    }
    print_text(strings[Strings::the_night_sky_72FA]);   /* "the night sky! " */
    while (wait_key_cursor() == 0 && !platform->quit_requested()) {}
}

/* Telescopes (0xE0-0xE2) pass the look on to the tile they point at. Clocks (0xFA, 0xFB) tell
 * the time; the flame's plate (0xDE) and the dungeon seals (0xDF, by column) their names. */
void look_tile(unsigned y, unsigned x, unsigned tile)
{
    int steps = 0;
    while ((tile == 0xE0 || tile == 0xE1 || tile == 0xE2) && steps++ < 64) {
        if (tile == 0xE0) y--;
        else if (tile == 0xE1) x++;
        else x--;
        tile = *map.get_map_tile_ptr(y, x);
    }
    if (tile == 0x59) {
        look_sky();
    } else if (tile == 0xA1) {
        look_well(map.z, map.y, map.x);
    } else if ((tile & 0xFC) == 0xD8) {
        look_town_fountain();
    } else {
        print_tile_description((int)tile);
        if ((tile & 0xFE) == 0xFA) {
            int h = game_time.hour % 12;
            print_number(' ', 1, h == 0 ? 12 : h);
            put_char(':');
            print_number('0', 2, game_time.minute);
            print_text(strings[game_time.hour < 12 ? Strings::AM_7310 : Strings::PM_730A]);   /* " AM.\n" / " PM.\n" */
        } else if (tile == 0xDE) {
            if (map == Map::LYCAEUM) print_text(strings[Strings::Truth_7316]);        /* "Truth\n" */
            else if (map == Map::EMPATH_ABBEY) print_text(strings[Strings::Love_731E]);   /* "Love\n" */
            else if (map == Map::SERPENTS_HOLD) print_text(strings[Strings::Courage_7324]);   /* "Courage\n" */
        } else if (tile == 0xDF) {
            Strings::Id name = Strings::NONE;
            switch (x) {
            case 0x3A: name = Strings::Shame_735C; break;   /* "Shame\n" */
            case 0x48: name = Strings::Destard_7340; break;   /* "Destard\n" */
            case 0x5B: name = Strings::Despise_7336; break;   /* "Despise\n" */
            case 0x7E: name = Strings::Wrong_734A; break;   /* "Wrong\n" */
            case 0x80: name = Strings::Doom_736E; break;   /* "Doom\n" */
            case 0x9C: name = Strings::Covetous_7352; break;   /* "Covetous\n" */
            case 0xEF: name = Strings::Hythloth_7364; break;   /* "Hythloth\n" */
            case 0xF0: name = Strings::Deceit_732E; break;   /* "Deceit\n" */
            }
            if (name) print_text(strings[name]);
        } else if (get_cursor_col() != 0) {
            put_char('\n');
        }
    }
}

/* The frame and line pieces signs and books draw for bytes 0x29-0x31 (ds:3810, from DATA.OVL;
 * the original reaches it as ds:37BE + byte * 2). */
static Strings::Id sign_pieces[9];
[[maybe_unused]] static const bool sign_pieces_added = (Data::instance().add(0x3810, sign_pieces, "look.sign_pieces"), true);

void print_sign_text(int offset)
{
    if (offset == -1) {
        select_font(1);
        print_text(strings[Strings::abbbbbbbbbbbbbbc_g_g_g_742A]);   /* a framed "LIVE BY THE" */
        print_text(strings[Strings::g_EIGHT_LAWS_g_g_745E]);   /* "EIGHT LAWS" */
        select_font(0);
        return;
    }
    const uint8_t *buf = text_buffer.data;
    int i = offset + 4;
    while (i < 1990 && buf[i] == 10) i += 6;   /* an entry that is only "\n" shares the next one's text */
    for (; i < 2000 && buf[i] != 0; i++) {
        uint8_t b = buf[i];
        select_font((b & 0x80) == 0);
        if (b == 0x26 || b == 0x27) {
            put_char(0x6C);
        } else if (b >= 0x29 && b <= 0x31) {
            select_font(1);
            for (const char *p = strings[sign_pieces[b - 0x29]]; *p; p++) put_char((uint8_t)*p);
        } else if (b == 0x0D) {
            select_font(0);
            get_command_key();
        } else {
            put_char(b & 0x7F);
        }
    }
    select_font(0);
    if (get_cursor_col() != 0) put_char('\n');
}

/* The poster in Yew (location 4, at 17,21) names the party. Otherwise the location's signs are
 * searched for the square; a square with no entry reads "LIVE BY THE EIGHT LAWS". */
void read_sign(uint8_t y, uint8_t x, uint8_t z)
{
    if (map == Map::YEW && z == 0 && x == 0x11 && y == 0x15) {
        select_font(1);
        print_text(strings[Strings::abbbbbbbbbbbbbc_g_7492]);   /* the top of the frame */
        select_font(0);
        print_text(strings[Strings::Wanted_74A8]);   /* "Wanted:   " */
        select_font(1);
        print_text(strings[Strings::g_g_74B4]);
        set_cursor(get_cursor_row(), 0x0E);
        print_text(strings[Strings::g_g_74B8]);
        for (unsigned m = 0; m < 3; m++) {
            if (m < party.size()) {
                select_font(0);
                set_cursor(get_cursor_row(), 7 - (int)strlen(party[m].name) / 2);
                print_text(party[m].name);
                select_font(1);
            }
            set_cursor(get_cursor_row(), 0x0E);
            print_text(strings[Strings::g_g_74BC]);
        }
        set_cursor(get_cursor_row(), 0x0E);
        print_text(strings[Strings::g_g_74C0]);
        select_font(0);
        print_text(strings[Strings::Dead_or_Alive_74C4]);   /* "Dead or Alive" */
        select_font(1);
        print_text(strings[Strings::g_deeeeeeeeeeeeef_74D2]);   /* the bottom of the frame */
        select_font(0);
        return;
    }
    uint8_t index[0x42];
    memset(index, 0, sizeof index);
    load_file_part(0, 0x42, index, strings[Strings::signs_dat_74E6]);   /* "signs.dat" */
    unsigned off = map.on_tile_map() ? (unsigned)(index[map.id() * 2] | index[map.id() * 2 + 1] << 8) : 0;
    uint8_t *buf = text_buffer.data;
    memset(buf, 0xFF, 2000);
    if (off == 0) {
        print_sign_text(-1);
        return;
    }
    load_file_part(off, 2000, buf, strings[Strings::signs_dat_74F0]);
    for (int i = 0; i < 1990; ) {
        if (buf[i + 1] == z && buf[i + 2] == x && buf[i + 3] == y) {
            print_sign_text(i);
            return;
        }
        i += 4;
        while (i < 1999 && buf[i] != 0) i++;
        i++;
        if (buf[i] == 0xFF) break;
    }
    print_sign_text(-1);
}

/* A crystal ball (0x29) shows the area map to one whose intelligence beats a 1-30 roll, and
 * hurts otherwise. Signs are tiles 0x89, 0x8A, 0xA0, 0xA4 and 0xF8. */
void cmd_look(void)
{
    Direction dir = {0, 0};
    if (ask_direction(&dir) == 0) return;
    unsigned x = (uint16_t)(map.x + dir.dx), y = (uint16_t)(map.y + dir.dy);
    unsigned tile = *map.get_map_tile_ptr(y, x);
    int object = find_object_at(map.z, (uint8_t)y, (uint8_t)x);
    if (tile == 0x29) {
        int member = ask_which_member();
        if (member == -1) return;
        if (party[member].intel <= (unsigned)random_between(0x1E, 1)) {
            print_text(strings[Strings::Death_vision_74FA]);   /* "Death vision!\n" */
            damage_member(1, member);
            draw_status_bar();
            return;
        }
        print_text(strings[Strings::Strange_vision_750A]);   /* "Strange vision!\n" */
        show_area_map(map.y, map.x);
        return;
    }
    print_text(strings[Strings::Thou_dost_see_751C]);   /* "\nThou dost see\n" */
    if (object != 0) {
        print_object_description(object);
    } else if (tile == 0xA0 || tile == 0x89 || tile == 0x8A || tile == 0xA4 || tile == 0xF8) {
        print_text(strings[Strings::NL_752C]);   /* "\n" */
        read_sign((uint8_t)y, (uint8_t)x, map.z);
    } else {
        look_tile(y, x, tile);
    }
}

/* ---- the area map: every square of the 32 x 32 map window as a 4 x 4 glyph ---- */

/* Where the next glyph goes, in pixels: set by map_cell_pos for the glyph routines below. (The
 * original leaves it in dir_x / dir_y.) */
static int glyph_x, glyph_y;

void map_cell_pos(int row, int col)
{
    glyph_x = col * 4 + 0x20;
    glyph_y = row * 4 + 0x20;
}

static int X(void) { return glyph_x; }
static int Y(void) { return glyph_y; }
static uint16_t dim_color(void)   /* the "sleep" colour on CGA and Hercules, bright frame colour otherwise */
{
    return (gfx.video_type != 0 && gfx.video_type != 3) ? (uint16_t)(gfx.color_frame + 8) : gfx.color_sleep;
}

static void glyph_dots(void)
{
    set_draw_color((uint16_t)(gfx.color_poison + 8));
    plot_pixel(Y(), X() + 1);
    plot_pixel(Y() + 2, X() + 1);
    plot_pixel(Y() + 1, X() + 3);
    plot_pixel(Y() + 3, X() + 3);
}

static void glyph_filled(void)
{
    set_draw_color((uint16_t)(gfx.color_poison + 8));
    fill_rect(Y() + 3, X() + 3, Y(), X());
}

static void glyph_vbars(void)
{
    set_draw_color(gfx.color_text);
    draw_line(Y(), X() + 3, Y(), X());
    draw_line(Y() + 3, X() + 3, Y() + 3, X());
}

static void glyph_inner_bars(void)
{
    set_draw_color(gfx.color_text);
    draw_line(Y() + 1, X() + 2, Y() + 1, X() + 1);
    draw_line(Y() + 2, X() + 2, Y() + 2, X() + 1);
}

static void glyph_box(void)
{
    set_draw_color(gfx.color_text);
    draw_line(Y(), X() + 3, Y(), X());
    draw_line(Y() + 3, X() + 3, Y() + 3, X());
    draw_line(Y() + 2, X(), Y() + 1, X());
    draw_line(Y() + 2, X() + 3, Y() + 1, X() + 3);
}

static void glyph_steps(void)
{
    set_draw_color(gfx.color_chest);
    draw_line(Y(), X() + 1, Y(), X());
    draw_line(Y() + 1, X() + 1, Y() + 1, X());
    draw_line(Y() + 2, X() + 3, Y() + 2, X() + 2);
    draw_line(Y() + 3, X() + 3, Y() + 3, X() + 2);
}

static void glyph_hbars(void)
{
    set_draw_color((uint16_t)(gfx.color_poison + 8));
    draw_line(Y(), X() + 3, Y(), X());
    draw_line(Y() + 2, X() + 3, Y() + 2, X());
    plot_pixel(Y() + 1, X() + 2);
    plot_pixel(Y() + 3, X());
}

/* From DATA.OVL: for the river tiles, which of the four dots are water (ds:3822); for the
 * building tiles 0x20-0x26, which sides have walls (ds:3832; the original reaches it as
 * ds:3812 + tile). */
static uint8_t river_dots[16], building_walls[7];
[[maybe_unused]] static const bool area_glyphs_added = (Data::instance().add(0x3822, river_dots, "look.river_dots"),
                                                        Data::instance().add(0x3832, building_walls, "look.building_walls"), true);

/* Four dots; for the river tiles (0x6n) those whose bit in river_dots is clear are land-coloured. */
static void glyph_terrain(unsigned tile)
{
    uint8_t bits = river_dots[tile & 0x0F];
    uint16_t c[4];
    for (int i = 0; i < 4; i++)
        c[i] = ((tile & 0xF0) == 0x60 && !(bits & (8 >> i))) ? (uint16_t)(gfx.color_poison + 8) : dim_color();
    set_draw_color(c[0]);
    plot_pixel(Y(), X() + 1);
    set_draw_color(c[1]);
    plot_pixel(Y() + 1, X() + 3);
    set_draw_color(c[2]);
    plot_pixel(Y() + 2, X() + 1);
    set_draw_color(c[3]);
    plot_pixel(Y() + 3, X() + 3);
}

static void glyph_two_dots(void)
{
    set_draw_color(dim_color());
    plot_pixel(Y(), X());
    plot_pixel(Y() + 2, X() + 2);
}

static void glyph_mixed_dots(void)
{
    set_draw_color((uint16_t)(gfx.color_poison + 8));
    plot_pixel(Y(), X() + 1);
    plot_pixel(Y() + 1, X() + 3);
    set_draw_color(dim_color());
    plot_pixel(Y() + 2, X());
    plot_pixel(Y() + 3, X() + 2);
}

/* A red core with a wall on each side named in building_walls (8 N, 4 E, 2 S, 1 W); tiles
 * 0x22-0x25 get a black corner. */
static void glyph_building(int tile)
{
    glyph_dots();
    set_draw_color(gfx.color_fire);
    fill_rect(Y() + 2, X() + 2, Y() + 1, X() + 1);
    uint8_t walls = tile >= 0x20 && tile < 0x27 ? building_walls[tile - 0x20] : 0;
    if (walls & 8) draw_line(Y(), X() + 2, Y(), X() + 1);
    if (walls & 4) draw_line(Y() + 2, X() + 3, Y() + 1, X() + 3);
    if (walls & 2) draw_line(Y() + 3, X() + 2, Y() + 3, X() + 1);
    if (walls & 1) draw_line(Y() + 2, X(), Y() + 1, X());
    set_draw_color(0);
    if (tile == 0x22) plot_pixel(Y() + 2, X() + 1);
    else if (tile == 0x23) plot_pixel(Y() + 1, X() + 1);
    else if (tile == 0x24) plot_pixel(Y() + 1, X() + 2);
    else if (tile == 0x25) plot_pixel(Y() + 2, X() + 2);
}

/* Each tile's glyph class on the area map (ds:1D1A; 0 = nothing), from DATA.OVL. The original's
 * table has 255 entries; tile 0xFF reads the byte after it (0), so 256 are taken. */
static uint8_t map_glyph_class[256];
[[maybe_unused]] static const bool map_glyph_class_added =
    (Data::instance().add(0x1D1A, map_glyph_class, "look.map_glyph_class", Data::SHARED), true);

/* The glyph comes from the tile's class in map_glyph_class (0 = nothing). */
void draw_map_glyph(int row, int col, int tile)
{
    uint8_t cls = map_glyph_class[tile & 0xFF];
    map_cell_pos(row, col);
    switch (cls) {
    case 1: glyph_dots(); break;
    case 2: glyph_filled(); break;
    case 3: set_draw_color(gfx.color_fire); fill_rect(Y() + 3, X() + 3, Y(), X()); break;
    case 4: glyph_vbars(); break;
    case 5: glyph_inner_bars(); break;
    case 6: glyph_box(); break;
    case 7: case 0x5A: set_draw_color(gfx.color_text); fill_rect(Y() + 3, X() + 3, Y(), X()); break;
    case 8: glyph_steps(); break;
    case 9: glyph_hbars(); break;
    case 0x0A: glyph_terrain((unsigned)tile); break;
    case 0x0B: glyph_two_dots(); break;
    case 0x0C: set_draw_color(dim_color()); plot_pixel(Y() + 2, X() + 2); break;
    case 0x0D: glyph_mixed_dots(); break;
    case 0x0E:
        set_draw_color(gfx.color_text);
        draw_line(Y() + 3, X() + 1, Y(), X() + 1);
        draw_line(Y() + 3, X() + 2, Y(), X() + 2);
        break;
    case 0x0F: set_draw_color((uint16_t)(gfx.color_frame + 8)); fill_rect(Y() + 3, X() + 3, Y(), X()); break;
    case 0x10: glyph_building(tile); break;
    }
}

/* Drawn on the back page and copied over the view; the party's cell blinks until a key. */
void show_area_map(int y, int x)
{
    int col = (uint8_t)(x - map.origin_x), row = (uint8_t)(y - map.origin_y);
    set_draw_page(1);
    set_draw_color(0);
    fill_rect(0xB7, 0xB7, 8, 8);
    for (int r = 0; r < 0x20; r++)
        for (int c = 0; c < 0x20; c++)
            draw_map_glyph(r, c, *map.get_map_tile_ptr((unsigned)(map.origin_y + r), (unsigned)(map.origin_x + c)));
    copy_page_rect(0xB7, 0xB7, 8, 8, 0, 1);
    set_draw_page(0);
    map_cell_pos(row, col);
    for (int n = 0; wait_key_cursor() == 0 && !platform->quit_requested(); )
        if (++n == 4) {
            n = 0;
            set_draw_color(gfx.color_text);
            xor_fill_rect(Y() + 3, X() + 3, Y(), X());
        }
    world_tick();
}
