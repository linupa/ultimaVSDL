/* story.cpp - FONT.OVL: story text, attract demo, character creation. See story.h. */
#include <string.h>
#include "commands.h"
#include "dungeon.h"
#include "map.h"
#include "misc.h"
#include "party.h"
#include "pictures.h"
#include "sound.h"
#include "gfx.h"
#include "input.h"
#include "intro.h"
#include "picture.h"
#include "story.h"
#include "strings.h"
#include "text.h"
#include "textbuf.h"
#include "tiles.h"
#include "view.h"

int Intro::wait_any_key(void)
{
    for (;;) {
        int k = poll_key();
        if (k) return k;
        if (platform->quit_requested()) return 0;
        delay_ticks(1);
    }
}

/* Character widths are at ds:50CA + character. */
int Intro::glyph_w(uint8_t c) { return (c < 128 ? glyph_widths_[c] : 0) + 1; }

/* Widths of the story font's glyphs, less the gap of one pixel (ds:50CA). */
uint8_t Intro::glyph_widths_[128];   /* ds:50CA */

Strings::Id Intro::demo_captions_[4];   /* ds:515C */

/* Character creation: what each virtue adds to intelligence, dexterity and strength when it wins
 * a question (ds:5164, 516C, 5174), where the two cards are drawn (ds:51FC, 5204), and where
 * each pair's question is in QUESTION.DAT (ds:517C). */
int8_t Intro::virtue_int_[8];   /* ds:5164 */
int8_t Intro::virtue_dex_[8];   /* ds:516C */
int8_t Intro::virtue_str_[8];   /* ds:5174 */
uint8_t Intro::virtue_card_x_[8];   /* ds:51FC */
uint8_t Intro::virtue_card_y_[8];   /* ds:5204 */
uint16_t Intro::question_offsets_[8][8];   /* ds:517C */

void Intro::draw_story_text(const uint8_t *text, uint16_t font)
{
    set_draw_page(1);
    int alt = (int16_t)intro.story_line_min < (int16_t)intro.story_y && (int16_t)intro.story_y < (int16_t)intro.story_line_max;
    int width = (int16_t)(intro.story_right[alt] - intro.story_left[alt]);
    int pos = 0;
    int used = (int16_t)(intro.story_x - intro.story_left[alt]);
    int space = (int16_t)intro.story_space_width, hyphen = glyph_widths_['-'];   /* without the gap */
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
                    intro.story_x = (uint16_t)(intro.story_x + space);
                    if (spaces != 0 && end != 0 && end != '\n') {   /* justify */
                        intro.story_x = (uint16_t)(intro.story_x + slack / spaces);
                        slack -= slack / spaces;
                        spaces--;
                    }
                } else if (c == '{') {
                    intro.story_x += 0x0F;
                } else if (c != '_') {
                    if ((int16_t)intro.story_y < 0xC0) draw_font_glyph((int16_t)intro.story_y, (int16_t)intro.story_x, c - 0x20, font);
                    intro.story_x = (uint16_t)(intro.story_x + glyph_w(c));
                }
            } while (++pos != i);
        }
        pos = i;
        if (text[i] == 0) break;
        if (text[i] == '_' && (int16_t)intro.story_y < 0xC0) draw_font_glyph((int16_t)intro.story_y, (int16_t)intro.story_x, 0x0D, font);
        used = 0;
        intro.story_y += 9;
        alt = (int16_t)intro.story_line_min < (int16_t)intro.story_y && (int16_t)intro.story_y < (int16_t)intro.story_line_max;
        intro.story_x = intro.story_left[alt];
        width = (int16_t)(intro.story_right[alt] - intro.story_x);
        pos = i + 1;
    }
    set_draw_page(0);
}

/* ---------------------------------------------------------------- attract demo */

/* The demo uses map_buffer as three 4-row maps of 32 columns: [0] what is drawn (0 = an object,
 * its tile in [0x80]; 0xFE = don't draw), [0x100] the scenery underneath. It is drawn at view
 * rows 7-10, with the tile origin moved down 8 pixels. */
#define DEMO_MAP(i)  (map.buffer[(i)])
#define DEMO_OBJ(i)  (map.buffer[0x80 + (i)])
#define DEMO_BASE(i) (map.buffer[0x100 + (i)])

void Intro::demo_draw_cell(int row, int col)
{
    int i = col + row * 0x20, tile;
    if (DEMO_MAP(i) == 0) {
        if (DEMO_OBJ(i) == 0x16) return;
        tile = 0x100 + DEMO_OBJ(i);
    } else {
        tile = view.tile_anim_map[DEMO_MAP(i)];
    }
    draw_tile_at(row + 7, col, tile);
}

/* Captions grow outward from the middle column, two columns every other step. */
int Intro::demo_step(int frames)
{
    do {
        animate_objects();
        wait_reveal_strips();
        for (int o = 0; o < 32; o++) {
            MapObject *ob = &map.objects[o];
            if (ob->tile == 0) continue;
            int i = (ob->y * 0x20 + ob->x) & 0x7F;
            DEMO_MAP(i) = 0;
            DEMO_OBJ(i) = ob->tile2;
        }
        for (unsigned col = intro.demo_caption_first; col <= intro.demo_caption_last; col++)
            for (int row = 0; row < 4; row++)
                if (DEMO_MAP(col + row * 0x20) != 0xFE) demo_draw_cell(row, (int)col);
        if (intro.demo_caption_first != 0 && intro.demo_caption_blink != 0) {
            intro.demo_caption_first--;
            intro.demo_caption_last++;
        }
        intro.demo_caption_blink = intro.demo_caption_blink == 0;
        if (poll_key() != 0 || platform->quit_requested()) return 1;
        if (frames != 0) delay_ticks(1);
        if (intro.demo_caption_mode == 2) {          /* the waterfall scene */
            sound_noise(10000, 0x3C, 0x14);
        } else if (intro.demo_caption_mode == 3) {   /* the clock scene: tick, tock */
            if (++intro.demo_frame > 7) intro.demo_frame = 0;
            if (intro.demo_frame == 0) beep(3, 3000);
            else if (intro.demo_frame == 4) beep(3, 2000);
        }
    } while (--frames != 0);
    return 0;
}

/* Scene n: its title at the bottom, its 4 x 19 map from the loaded data. */
void Intro::demo_caption(int n)
{
    print_centered(strings[demo_captions_[n & 3]]);
    const uint8_t *scene = text_buffer.at(n * 0x80);
    for (int col = 0; col < 0x13; col++)
        for (int row = 0; row < 4; row++) {
            uint8_t t = scene[col + row * 0x20];
            DEMO_MAP(col + row * 0x20) = t;
            DEMO_BASE(col + row * 0x20) = t;
        }
    intro.demo_caption_first = intro.demo_caption_last = 9;
    intro.demo_caption_blink = 0;
    intro.demo_caption_mode = (uint8_t)n;
}

/* The script is at offset 0x200 of the data read from MISCMAPS.DAT (offset 0x2C0, 2000 bytes).
 * Ops: 0 place object (slot, tile, x, y); 1 remove object; 2 move object (slot, direction);
 * 3 wait n steps; 4 moongate rises at (x, y); 5 it sets again; 6 next scene (caption n);
 * 7 object dissolves in; 8 object dissolves out; 9 restart; 10 set tile (tile, x, y);
 * 11 lightning strikes object; 12 clear objects; 13 move object and wait 7; 14 n / 15: repeat. */
void Intro::attract_demo(void)
{
    const uint8_t *sc = text_buffer.data;
    int gate_x = 0, gate_y = 0, loop_pos = 0;
    unsigned loop_count = 0;
    tiles.origin_y = 0x10;
    intro.demo_caption_first = intro.demo_caption_last = 9;
    intro.demo_caption_blink = 0;
    set_draw_color(gfx.color_frame);
    fill_rect(0x7E, 199, 0x78, 0x78);
    set_draw_color(gfx.color_text);
    draw_line(0x7F, 199, 0x7F, 0x78);
    if (map != Map::DEMO) {
        load_file_part(0x2C0, 2000, text_buffer.data, strings[Strings::MISCMAPS_DAT_A044]);   /* MISCMAPS.DAT */
        if (map == Map::TITLE) load_tileset(pictures.name(Pictures::TILES));   /* the original saves the screen around this */
    }
    map.set(Map::DEMO);
    for (int pos = 0x200; pos < 2000; pos++) {
        int steps = -1;
        MapObject *o = &map.objects[sc[pos + 1] & 31];
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
            o->x = (uint8_t)(o->x + Dungeon::facing_dx[dir]);
            o->y = (uint8_t)(o->y + Dungeon::facing_dy[dir]);
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
                set_draw_color(gfx.color_text);
                draw_line(dy + 0x9B, dx + 0x89, dy + 0x98, dx + 0x80);
                draw_line(dy + 0x9C, dx + 0x89, dy + 0x99, dx + 0x80);
            }
            MapObject *t = &map.objects[sc[pos + 2] & 31];
            draw_tile_at(t->y + 7, t->x, 0);
            sound_noise(4000, 0x4B0, 1);
            steps = 3;
            pos += 2;
            break;
        }
        case 0x0C:
            for (int n = 0; n < 32; n++) map.objects[n].flags = map.objects[n].tile = map.objects[n].tile2 = 0;
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
        if (poll_key() != 0 || platform->quit_requested()) return;
    }
}

/* ---------------------------------------------------------------- character creation */

/* A virtue not yet asked in this round (virtue_asked) and not yet beaten (ds:BD32). */
int Intro::random_virtue(void)
{
    int v;
    do v = random_between(7, 0); while (intro.virtue_asked[v] != 0 || intro.virtue_beaten[v] != 0);
    intro.virtue_asked[v] = 1;
    return v;
}

/* Two virtues' cards and the gypsy's question (QUESTION.DAT, offsets ds:517C[a][b]); the answer
 * A or B picks one: its bonus goes to INT / DEX / STR (ds:5164, 516C, 5174) and the other is out. */
void Intro::ask_virtue_question(uint16_t picture, uint16_t font)
{
    set_draw_page(1);
    put_char(0xFF);
    draw_picture_frame(0, 0, 0x10, 1, picture);
    draw_picture_frame(0, 0, 200, 1, picture);
    intro.story_x = 0;
    intro.story_y = 0x98;
    int a = random_virtue(), b = random_virtue();
    int left = a < b ? a : b, right = a < b ? b : a;
    char a_key = b < a ? 'B' : 'A';   /* the key that chooses a (A = the left card) */
    draw_picture_frame(0, virtue_card_y_[left & 7], virtue_card_x_[left & 7], left + 2, picture);
    draw_picture_frame(0, virtue_card_y_[right & 7], virtue_card_x_[right & 7] + 0xB8, right + 2, picture);
    load_file_part(question_offsets_[a & 7][b & 7], 2000, text_buffer.data, strings[Strings::QUESTION_DAT_A052]);
    draw_story_text(text_buffer.data, font);
    copy_page(0, 1);
    char c;
    do {
        c = (char)to_upper((uint8_t)wait_any_key());
        if (platform->quit_requested()) c = 'A';
    } while (c != 'A' && c != 'B');
    int winner = c == a_key ? a : b, loser = c == a_key ? b : a;
    intro.new_int = (uint8_t)(intro.new_int + virtue_int_[winner & 7]);
    intro.new_dex = (uint8_t)(intro.new_dex + virtue_dex_[winner & 7]);
    intro.new_str = (uint8_t)(intro.new_str + virtue_str_[winner & 7]);
    intro.virtue_beaten[loser] = 1;
}

/* INIT.GAM is the template; the name, sex and the seven answers (4 + 2 + 1, a tournament of the
 * eight virtues) set the Avatar's INT, DEX and STR. Writes SAVED.GAM and a cleared SAVED.OOL. */
void Intro::create_character(void)
{
    uint8_t *text = text_buffer.data;
    if (map != Map::TITLE) free_tileset();
    uint16_t font = load_bitmap(pictures.name(Pictures::STORY_FONT));      /* PROPORT.PCS */
    uint16_t picture = load_picture(pictures.name(Pictures::CREATE));  /* CREATE.16 */
    load_game_file(strings[Strings::INIT_GAM_A060]);   /* INIT.GAM */
    set_draw_color(gfx.color_frame);
    fill_rect(0x7E, 200, 0x78, 0x78);
    set_draw_color(gfx.color_text);
    draw_line(0x7F, 200, 0x7F, 0x78);
    set_draw_color(0);
    fill_rect(0xBF, 0x137, 0x80, 8);
    set_cursor(0x11, 3);
    print_text(strings[Strings::By_what_name_shalt_thou_A06A]);   /* "By what name shalt thou be known?" */
    set_cursor(0x13, 0x0E);
    put_char(':');
    read_name(8, party[0].name);
    if (party[0].name[0] != 0) {
        set_cursor(0x15, 8);
        print_text(strings[Strings::Art_thou_Male_or_Female_A08C]);   /* "Art thou Male or Female? " */
        uint8_t k;
        do {
            k = (uint8_t)to_upper((uint8_t)wait_key_timeout(0));
            if (platform->quit_requested()) k = 'M';
        } while (k != 'M' && k != 'F');
        put_char(k);
        party[0].sex = k == 'M' ? 0x0B : 0x0C;
        intro.story_x = 0;
        intro.story_y = 9;
        intro.story_left[0] = 0;
        intro.story_right[0] = 0x140;
        intro.story_left[1] = 0xAF;
        intro.story_right[1] = 0x140;
        intro.story_line_min = 0x59;
        intro.story_line_max = 200;
        load_file_part(0, 2000, text, strings[Strings::QUESTION_DAT_A0A6]);   /* QUESTION.DAT: the gypsy's welcome */
        set_draw_page(1);
        put_char(0xFF);
        draw_picture_frame(0, 0x60, 0, 0, picture);
        draw_story_text(text, font);
        copy_page(0, 1);
        intro.story_line_min = 200;
        wait_any_key();
        intro.new_int = party[0].intel;
        intro.new_dex = party[0].dex;
        intro.new_str = party[0].str;
        for (int i = 0; i < 8; i++) {
            intro.virtue_beaten[i] = 0;
            intro.virtue_asked[i] = 0;
        }
        for (int i = 0; i < 4; i++) ask_virtue_question(picture, font);
        memset(intro.virtue_asked, 0, 8);
        for (int i = 0; i < 2; i++) ask_virtue_question(picture, font);
        memset(intro.virtue_asked, 0, 8);
        ask_virtue_question(picture, font);
        intro.story_y = 0;
        intro.story_x = 0;
        intro.story_line_min = 0x5A;
        intro.story_left[1] = 0;
        intro.story_right[1] = 0xA6;
        intro.story_space_width = 4;
        load_file_part(0x322, 2000, text, strings[Strings::QUESTION_DAT_A0B4]);   /* the gypsy's farewell */
        set_draw_page(1);
        put_char(0xFF);
        draw_picture_frame(0, 100, 0xA8, 10, picture);
        draw_story_text(text, font);
        intro.story_space_width = 5;
        copy_page(0, 1);
        wait_any_key();
        dos_free_mem(picture);
        dos_free_mem2(font);
        set_draw_page(1);
        put_char(0xFF);
        set_draw_page(0);
        party[0].intel = intro.new_int;
        party[0].mp = intro.new_int;
        party[0].dex = intro.new_dex;
        party[0].str = intro.new_str < 0x14 ? 0x14 : intro.new_str;
        put_char(0xFF);
        set_cursor(10, 0);
        uint8_t ool[0x200] = {0};   /* SAVED.OOL: Britannia empty, the Underworld from INIT.OOL */
        load_file_part(0, 0x100, ool + 0x100, strings[Strings::INIT_OOL_A0C2]);
        save_file_part(0x200, ool, strings[Strings::SAVED_OOL_A0CC]);
        save_game_file(strings[Strings::SAVED_GAM_A0D6]);   /* SAVED.GAM */
    } else {
        dos_free_mem(picture);
        dos_free_mem2(font);
    }
    map.set(Map::TITLE);
}
