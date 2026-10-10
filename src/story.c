/* story.c - FONT.OVL: story text, attract demo, character creation. See story.h. */
#include <string.h>
#include "commands.h"
#include "data.h"
#include "sound.h"
#include "gfx.h"
#include "input.h"
#include "intro.h"
#include "map.h"
#include "picture.h"
#include "story.h"
#include "text.h"
#include "tiles.h"
#include "view.h"

int wait_any_key(void)
{
    for (;;) {
        int k = poll_key();
        if (k) return k;
        if (platform_quit_requested()) return 0;
        delay_ticks(1);
    }
}

/* Character widths are at ds:50CA + character. */
static int glyph_w(uint8_t c) { return ds_u8((uint16_t)(0x50CA + c)) + 1; }

void draw_story_text(const uint8_t *text, uint16_t font)
{
    set_draw_page(1);
    int alt = (int16_t)dg.story_line_min < (int16_t)dg.story_y && (int16_t)dg.story_y < (int16_t)dg.story_line_max;
    int width = (int16_t)(ds_u16(STORY_RIGHT(alt)) - ds_u16(STORY_LEFT(alt)));
    int pos = 0;
    int used = (int16_t)(dg.story_x - ds_u16(STORY_LEFT(alt)));
    int space = (int16_t)dg.story_space_width, hyphen = dg.story_hyphen_width;
    while (text[pos] != 0) {
        int spaces = 0, i = pos;
        for (; text[i] != 0 && used < width && text[i] != '\n'; i++) {   /* measure */
            uint8_t c = text[i];
            if (c < 0x21) { used += space; spaces++; }
            else if (c == '{') used += 0x0F;
            else if (c != '_') used += glyph_w(c);
        }
        for (;;) {   /* back up to where the line can break */
            if ((text[i] == 0 && used < width) || text[i] == '\n') break;
            if (--i <= pos) break;
            if (text[i] == ' ') {
                used -= space;
                spaces--;
                break;
            }
            if (text[i] == '_' && hyphen + used + 1 < width) {
                used += hyphen + 1;
                break;
            }
            if (text[i] != '_' && text[i] != '{') used -= glyph_w(text[i]);
        }
        int slack = width - used;
        if (pos < i) {
            uint8_t end = text[i];
            do {
                uint8_t c = text[pos];
                if (c < 0x21) {
                    dg.story_x = (uint16_t)(dg.story_x + space);
                    if (spaces != 0 && end != 0 && end != '\n') {   /* justify */
                        dg.story_x = (uint16_t)(dg.story_x + slack / spaces);
                        slack -= slack / spaces;
                        spaces--;
                    }
                } else if (c == '{') {
                    dg.story_x += 0x0F;
                } else if (c != '_') {
                    if ((int16_t)dg.story_y < 0xC0) draw_font_glyph((int16_t)dg.story_y, (int16_t)dg.story_x, c - 0x20, font);
                    dg.story_x = (uint16_t)(dg.story_x + glyph_w(c));
                }
            } while (++pos != i);
        }
        pos = i;
        if (text[i] == 0) break;
        if (text[i] == '_' && (int16_t)dg.story_y < 0xC0) draw_font_glyph((int16_t)dg.story_y, (int16_t)dg.story_x, 0x0D, font);
        used = 0;
        dg.story_y += 9;
        alt = (int16_t)dg.story_line_min < (int16_t)dg.story_y && (int16_t)dg.story_y < (int16_t)dg.story_line_max;
        dg.story_x = ds_u16(STORY_LEFT(alt));
        width = (int16_t)(ds_u16(STORY_RIGHT(alt)) - dg.story_x);
        pos = i + 1;
    }
    set_draw_page(0);
}

/* ---------------------------------------------------------------- attract demo */

/* The demo uses map_buffer as three 4-row maps of 32 columns: [0] what is drawn (0 = an object,
 * its tile in [0x80]; 0xFE = don't draw), [0x100] the scenery underneath. It is drawn at view
 * rows 7-10, with the tile origin moved down 8 pixels. */
#define DEMO_MAP(i)  (dg.map_buffer[(i)])
#define DEMO_OBJ(i)  (dg.map_buffer[0x80 + (i)])
#define DEMO_BASE(i) (dg.map_buffer[0x100 + (i)])

void demo_draw_cell(int row, int col)
{
    int i = col + row * 0x20, tile;
    if (DEMO_MAP(i) == 0) {
        if (DEMO_OBJ(i) == 0x16) return;
        tile = 0x100 + DEMO_OBJ(i);
    } else {
        tile = dg.tile_anim_map[DEMO_MAP(i)];
    }
    draw_tile_at(row + 7, col, tile);
}

/* Captions grow outward from the middle column, two columns every other step. */
int demo_step(int frames)
{
    do {
        animate_objects();
        wait_reveal_strips();
        for (int o = 0; o < 32; o++) {
            MapObject *ob = &dg.objects[o];
            if (ob->tile == 0) continue;
            int i = (ob->y * 0x20 + ob->x) & 0x7F;
            DEMO_MAP(i) = 0;
            DEMO_OBJ(i) = ob->tile2;
        }
        for (unsigned col = dg.demo_caption_first; col <= dg.demo_caption_last; col++)
            for (int row = 0; row < 4; row++)
                if (DEMO_MAP(col + row * 0x20) != 0xFE) demo_draw_cell(row, (int)col);
        if (dg.demo_caption_first != 0 && dg.demo_caption_blink != 0) {
            dg.demo_caption_first--;
            dg.demo_caption_last++;
        }
        dg.demo_caption_blink = dg.demo_caption_blink == 0;
        if (poll_key() != 0 || platform_quit_requested()) return 1;
        if (frames != 0) delay_ticks(1);
        if (dg.demo_caption_mode == 2) {          /* the waterfall scene */
            sound_noise(10000, 0x3C, 0x14);
        } else if (dg.demo_caption_mode == 3) {   /* the clock scene: tick, tock */
            if (++dg.demo_frame > 7) dg.demo_frame = 0;
            if (dg.demo_frame == 0) beep(3, 3000);
            else if (dg.demo_frame == 4) beep(3, 2000);
        }
    } while (--frames != 0);
    return 0;
}

/* Scene n: its title at the bottom, its 4 x 19 map from the loaded data. */
void demo_caption(int n)
{
    print_centered(ds_str(ds_u16((uint16_t)(0x515C + n * 2))));
    const uint8_t *scene = ds_at(offsetof(struct DGroup, text_buffer) + n * 0x80);
    for (int col = 0; col < 0x13; col++)
        for (int row = 0; row < 4; row++) {
            uint8_t t = scene[col + row * 0x20];
            DEMO_MAP(col + row * 0x20) = t;
            DEMO_BASE(col + row * 0x20) = t;
        }
    dg.demo_caption_first = dg.demo_caption_last = 9;
    dg.demo_caption_blink = 0;
    dg.demo_caption_mode = (uint8_t)n;
}

/* The script is at offset 0x200 of the data read from MISCMAPS.DAT (offset 0x2C0, 2000 bytes).
 * Ops: 0 place object (slot, tile, x, y); 1 remove object; 2 move object (slot, direction);
 * 3 wait n steps; 4 moongate rises at (x, y); 5 it sets again; 6 next scene (caption n);
 * 7 object dissolves in; 8 object dissolves out; 9 restart; 10 set tile (tile, x, y);
 * 11 lightning strikes object; 12 clear objects; 13 move object and wait 7; 14 n / 15: repeat. */
void attract_demo(void)
{
    const uint8_t *sc = ds_at(offsetof(struct DGroup, text_buffer));
    int gate_x = 0, gate_y = 0, loop_pos = 0;
    unsigned loop_count = 0;
    dg.tile_origin_y = 0x10;
    dg.demo_caption_first = dg.demo_caption_last = 9;
    dg.demo_caption_blink = 0;
    set_draw_color(dg.color_frame);
    fill_rect(0x7E, 199, 0x78, 0x78);
    set_draw_color(dg.color_text);
    draw_line(0x7F, 199, 0x7F, 0x78);
    if (dg.location != 0x42) {
        load_file_part(0x2C0, 2000, ds_ptr((uint16_t)offsetof(struct DGroup, text_buffer)), ds_str(0xA044));   /* MISCMAPS.DAT */
        if (dg.location == 0x40) load_tileset(ds_str(dg.tileset_file));   /* the original saves the screen around this */
    }
    dg.location = 0x42;
    for (int pos = 0x200; pos < 2000; pos++) {
        int steps = -1;
        MapObject *o = &dg.objects[sc[pos + 1] & 31];
        switch (sc[pos]) {
        case 0:
            o->tile = o->tile2 = sc[pos + 2];
            o->x = sc[pos + 3];
            o->y = sc[pos + 4];
            o->flags = 0;
            pos += 4;
            break;
        case 1: {
            o->flags = o->tile2 = o->tile = 0;
            int i = (o->y * 0x20 + o->x) & 0x7F;
            DEMO_MAP(i) = DEMO_BASE(i);
            pos += 1;
            break;
        }
        case 2:
        case 0x0D: {
            unsigned dir = sc[pos + 2] & 3;
            int i = (o->y * 0x20 + o->x) & 0x7F;
            DEMO_MAP(i) = DEMO_BASE(i);
            o->x = (uint8_t)(o->x + dg.facing_dx[dir]);
            o->y = (uint8_t)(o->y + dg.facing_dy[dir]);
            if (sc[pos] == 0x0D) steps = 7;
            pos += 2;
            break;
        }
        case 3:
            steps = sc[pos + 1];
            pos += 1;
            break;
        case 4:
        case 5: {
            int rising = sc[pos] == 4;
            if (rising) {
                gate_x = sc[pos + 1];
                gate_y = sc[pos + 2];
            }
            int i = (gate_y * 0x20 + gate_x) & 0x7F;
            DEMO_BASE(i) = DEMO_MAP(i) = 0xFE;
            for (int f = rising ? 1 : 15; rising ? f < 0x10 : f > 0; f += rising ? 1 : -1) {
                shimmer_tiles(gate_y + 7, gate_x, f);
                if (demo_step(1)) return;
            }
            DEMO_BASE(i) = DEMO_MAP(i) = rising ? 0xDC : 5;
            if (rising) pos += 2;
            steps = 2;
            break;
        }
        case 6:
            demo_caption(sc[pos + 1]);
            pos += 1;
            break;
        case 7:
        case 8: {
            uint8_t t = o->tile;
            int ox = o->x, oy = o->y;
            o->tile = o->tile2 = 0x16;   /* hidden while it dissolves */
            int tile = sc[pos] == 7 ? 0x100 + t : DEMO_BASE((ox + oy * 0x20) & 0x7F);
            if (dissolve_tile(oy + 7, ox, tile)) return;
            pos += 1;
            o->tile = o->tile2 = t;
            break;
        }
        case 9:
            pos = 0x1FF;
            break;
        case 0x0A: {
            int i = (sc[pos + 3] * 0x20 + sc[pos + 2]) & 0x7F;
            DEMO_BASE(i) = DEMO_MAP(i) = sc[pos + 1];
            pos += 3;
            break;
        }
        case 0x0B: {   /* lightning from the top of the scene, then the object vanishes */
            for (int n = 0, dx = 0, dy = 0; n < 5; n++, dx += 9, dy += 3) {
                if (demo_step(1)) return;
                set_draw_color(dg.color_text);
                draw_line(dy + 0x9B, dx + 0x89, dy + 0x98, dx + 0x80);
                draw_line(dy + 0x9C, dx + 0x89, dy + 0x99, dx + 0x80);
            }
            MapObject *t = &dg.objects[sc[pos + 2] & 31];
            draw_tile_at(t->y + 7, t->x, 0);
            sound_noise(4000, 0x4B0, 1);
            steps = 3;
            pos += 2;
            break;
        }
        case 0x0C:
            for (int n = 0; n < 32; n++) dg.objects[n].flags = dg.objects[n].tile = dg.objects[n].tile2 = 0;
            break;
        case 0x0E:
            loop_pos = pos + 1;
            loop_count = sc[pos + 1];
            pos = loop_pos;
            break;
        case 0x0F:
            if (--loop_count != 0) pos = loop_pos;
            break;
        }
        if (steps >= 0 && demo_step(steps)) return;
        if (poll_key() != 0 || platform_quit_requested()) return;
    }
}

/* ---------------------------------------------------------------- character creation */

/* A virtue not yet asked in this round (virtue_asked) and not yet beaten (ds:BD32). */
int random_virtue(void)
{
    int v;
    do v = random_between(7, 0); while (dg.virtue_asked[v] != 0 || ds_u8((uint16_t)(0xBD32 + v)) != 0);
    dg.virtue_asked[v] = 1;
    return v;
}

/* Two virtues' cards and the gypsy's question (QUESTION.DAT, offsets ds:517C[a][b]); the answer
 * A or B picks one: its bonus goes to INT / DEX / STR (ds:5164, 516C, 5174) and the other is out. */
void ask_virtue_question(uint16_t picture, uint16_t font)
{
    set_draw_page(1);
    put_char(0xFF);
    draw_picture_frame(0, 0, 0x10, 1, picture);
    draw_picture_frame(0, 0, 200, 1, picture);
    dg.story_x = 0;
    dg.story_y = 0x98;
    int a = random_virtue(), b = random_virtue();
    int left = a < b ? a : b, right = a < b ? b : a;
    char a_key = b < a ? 'B' : 'A';   /* the key that chooses a (A = the left card) */
    draw_picture_frame(0, ds_u8((uint16_t)(0x5204 + left)), ds_u8((uint16_t)(0x51FC + left)), left + 2, picture);
    draw_picture_frame(0, ds_u8((uint16_t)(0x5204 + right)), ds_u8((uint16_t)(0x51FC + right)) + 0xB8, right + 2, picture);
    load_file_part(ds_u16((uint16_t)(0x517C + b * 2 + a * 0x10)), 2000, ds_ptr((uint16_t)offsetof(struct DGroup, text_buffer)), ds_str(0xA052));
    draw_story_text(ds_at(offsetof(struct DGroup, text_buffer)), font);
    copy_page(0, 1);
    char c;
    do {
        c = (char)to_upper((uint8_t)wait_any_key());
        if (platform_quit_requested()) c = 'A';
    } while (c != 'A' && c != 'B');
    int winner = c == a_key ? a : b, loser = c == a_key ? b : a;
    dg.new_int = (uint8_t)(dg.new_int + (int8_t)ds_u8((uint16_t)(0x5164 + winner)));
    dg.new_dex = (uint8_t)(dg.new_dex + (int8_t)ds_u8((uint16_t)(0x516C + winner)));
    dg.new_str = (uint8_t)(dg.new_str + (int8_t)ds_u8((uint16_t)(0x5174 + winner)));
    ds_set_u8((uint16_t)(0xBD32 + loser), 1);
}

/* INIT.GAM is the template; the name, sex and the seven answers (4 + 2 + 1, a tournament of the
 * eight virtues) set the Avatar's INT, DEX and STR. Writes SAVED.GAM and a cleared SAVED.OOL. */
void create_character(void)
{
    uint8_t *text = ds_ptr((uint16_t)offsetof(struct DGroup, text_buffer));
    if (dg.location != 0x40) free_tileset();
    uint16_t font = load_bitmap(ds_str(ds_u16(0x25EA)));      /* PROPORT.PCS */
    uint16_t picture = load_picture(ds_str(ds_u16(0x260A)));  /* CREATE.16 */
    load_file_part(0, 0x1060, ds_ptr(SAVE_OFFSET), ds_str(0xA060));   /* INIT.GAM */
    set_draw_color(dg.color_frame);
    fill_rect(0x7E, 200, 0x78, 0x78);
    set_draw_color(dg.color_text);
    draw_line(0x7F, 200, 0x7F, 0x78);
    set_draw_color(0);
    fill_rect(0xBF, 0x137, 0x80, 8);
    set_cursor(0x11, 3);
    print_text(ds_str(0xA06A));   /* "By what name shalt thou be known?" */
    set_cursor(0x13, 0x0E);
    put_char(':');
    read_name(8, dg.party[0].name);
    if (dg.party[0].name[0] != 0) {
        set_cursor(0x15, 8);
        print_text(ds_str(0xA08C));   /* "Art thou Male or Female? " */
        uint8_t k;
        do {
            k = (uint8_t)to_upper((uint8_t)wait_key_timeout(0));
            if (platform_quit_requested()) k = 'M';
        } while (k != 'M' && k != 'F');
        put_char(k);
        dg.party[0].sex = k == 'M' ? 0x0B : 0x0C;
        dg.story_x = 0;
        dg.story_y = 9;
        ds_set_u16(STORY_LEFT(0), 0);
        ds_set_u16(STORY_RIGHT(0), 0x140);
        ds_set_u16(STORY_LEFT(1), 0xAF);
        ds_set_u16(STORY_RIGHT(1), 0x140);
        dg.story_line_min = 0x59;
        dg.story_line_max = 200;
        load_file_part(0, 2000, text, ds_str(0xA0A6));   /* QUESTION.DAT: the gypsy's welcome */
        set_draw_page(1);
        put_char(0xFF);
        draw_picture_frame(0, 0x60, 0, 0, picture);
        draw_story_text(text, font);
        copy_page(0, 1);
        dg.story_line_min = 200;
        wait_any_key();
        dg.new_int = dg.party[0].intel;
        dg.new_dex = dg.party[0].dex;
        dg.new_str = dg.party[0].str;
        for (int i = 0; i < 8; i++) {
            ds_set_u8((uint16_t)(0xBD32 + i), 0);
            dg.virtue_asked[i] = 0;
        }
        for (int i = 0; i < 4; i++) ask_virtue_question(picture, font);
        memset(dg.virtue_asked, 0, 8);
        for (int i = 0; i < 2; i++) ask_virtue_question(picture, font);
        memset(dg.virtue_asked, 0, 8);
        ask_virtue_question(picture, font);
        dg.story_y = 0;
        dg.story_x = 0;
        dg.story_line_min = 0x5A;
        ds_set_u16(STORY_LEFT(1), 0);
        ds_set_u16(STORY_RIGHT(1), 0xA6);
        dg.story_space_width = 4;
        load_file_part(0x322, 2000, text, ds_str(0xA0B4));   /* the gypsy's farewell */
        set_draw_page(1);
        put_char(0xFF);
        draw_picture_frame(0, 100, 0xA8, 10, picture);
        draw_story_text(text, font);
        dg.story_space_width = 5;
        copy_page(0, 1);
        wait_any_key();
        dos_free_mem(picture);
        dos_free_mem2(font);
        set_draw_page(1);
        put_char(0xFF);
        set_draw_page(0);
        dg.party[0].intel = dg.new_int;
        dg.party[0].mp = dg.new_int;
        dg.party[0].dex = dg.new_dex;
        dg.party[0].str = dg.new_str < 0x14 ? 0x14 : dg.new_str;
        put_char(0xFF);
        set_cursor(10, 0);
        uint8_t ool[0x200] = {0};   /* SAVED.OOL: Britannia empty, the Underworld from INIT.OOL */
        load_file_part(0, 0x100, ool + 0x100, ds_str(0xA0C2));
        save_file_part(0x200, ool, ds_str(0xA0CC));
        save_file_part(0x1060, ds_ptr(SAVE_OFFSET), ds_str(0xA0D6));   /* SAVED.GAM */
    } else {
        dos_free_mem(picture);
        dos_free_mem2(font);
    }
    dg.location = 0x40;
}
