/* intro.cpp - INTRO.OVL: title sequence, main menu and introduction story. */
#include <string.h>
#include "commands.h"
#include "files.h"
#include "gamedata.h"
#include "gfx.h"
#include "input.h"
#include "intro.h"
#include "map.h"
#include "misc.h"
#include "party.h"
#include "picture.h"
#include "pictures.h"
#include "platform.h"
#include "sound.h"
#include "status.h"
#include "story.h"
#include "strings.h"
#include "text.h"
#include "textbuf.h"
#include "tiles.h"
#include "view.h"
#include "world.h"


Intro &Intro::instance()
{
    static Intro the_intro;
    return the_intro;
}

Intro &intro = Intro::instance();

Intro::Intro()
{
    Data &data = Data::instance();   /* not a global: this runs before main, in no set order */
    /* 21 pages; the 22nd is read past each field's end, and its `kind` is the first letter of
     * the text that follows (ds:3105) */
    data.add(0x2F98, 21 * 17 + 9, [](const uint8_t *b, unsigned) { read_pages(story_pages_, 22, b, 21, 22); }, "intro.story_pages",
             Data::SHARED);
    data.add(0x3656, transfer_label_rows_, "intro.transfer_label_rows");
    data.add(0x3666, transfer_labels_, "intro.transfer_labels");
    data.add(0x50CA, glyph_widths_, "intro.glyph_widths", Data::SHARED);
    data.add(0x515C, demo_captions_, "intro.demo_captions");
    data.add(0x5164, virtue_int_, "intro.virtue_int");
    data.add(0x516C, virtue_dex_, "intro.virtue_dex");
    data.add(0x5174, virtue_str_, "intro.virtue_str");
    data.add(0x517C, question_offsets_, "intro.question_offsets");
    data.add(0x51FC, virtue_card_x_, "intro.virtue_card_x");
    data.add(0x5204, virtue_card_y_, "intro.virtue_card_y");
}

/* The introduction's pages (ds:2F98-3105), kept field by field like the ending's: 21 left pairs,
 * 21 right pairs and 21 text offsets, then 22 of each byte field. The last entry is the one
 * past the end that the original reads too - its word fields come from the next field's
 * table - and its `kind` (87) ends the story. */
Intro::StoryPage Intro::story_pages_[22];

int16_t Intro::transfer_label_rows_[8];   /* ds:3656 */
Strings::Id Intro::transfer_labels_[8];   /* ds:3666 */

/* The banner's four animation frames go into the back page for wait_reveal_strips. */
void Intro::draw_menu_banner(void)
{
    set_draw_page(1);
    put_char(0xFF);
    for (int i = 0; i < 4; i++) draw_picture_frame(0, i * 0x32, 0x10, i + 1, intro.title_picture);
}

/* BRITISH.PTH (Intro::signature_path): one byte per pixel step, low nibble dy and high nibble dx
 * (bit 3 = negative, 0-2 pixels); a larger step moves without drawing; 0 ends a stroke. The
 * position in the data is kept in path_pos. Returns 0 if a key stopped it. */
int Intro::intro_trace_path(int y, int x)
{
    const uint8_t *path = intro.signature_path;
    unsigned pen = gfx.color_text, i;
    set_draw_color(gfx.color_text);
    for (i = intro.path_pos; i < Intro::PATH_SIZE; i++) {
        if (pen != 0) plot_pixel(y, x);
        pen = gfx.color_text;
        int dy = path[i] & 7, dx = (path[i] & 0x70) >> 4;
        if (dy > 2) pen = 0;
        if (path[i] & 8) dy = -dy;
        if (dx > 2) pen = 0;
        if (path[i] & 0x80) dx = -dx;
        x += dx;
        y += dy;
        if (poll_key() != 0 || platform->quit_requested()) return 0;
        if ((i & 0x1F) == 0) delay_ticks(1);
        if (path[i] == 0) break;
    }
    intro.path_pos = (uint16_t)(i + 1);
    return (int)(i + 1);
}

/* 21 pages. Per page (tables at ds:2F98-30F0): the picture set and frame, where it goes, the
 * text margins and start, and a kind: 1 = also the scroll-shaped background (TEXT.16), 2 = a
 * second frame dissolves in afterwards, 3 = the moongate page with fixed text, 4-6 = an extra
 * frame below. The text is in STORY.DAT at ds:3016[page]. A key turns the page. */
void Intro::intro_story(void)
{
    if (map != Map::TITLE) free_tileset();
    map.set(Map::TITLE);
    uint16_t font = load_bitmap(pictures.name(Pictures::STORY_FONT));   /* PROPORT.PCS */
    draw_story_text((const uint8_t *)"", font);
    set_draw_page(1);
    put_char(0xFF);
    set_draw_page(0);
    put_char(0xFF);
    set_cursor(10, 0);
    uint16_t background = load_picture(pictures.name(Pictures::STORY_BACKGROUND));
    uint16_t picture = load_picture(pictures.story(0));
    unsigned set = 0;
    for (int page = 0; page <= 0x14 && !platform->quit_requested(); page++) {
        const StoryPage &pg = story_pages_[page];
        if (pg.set != set) {
            dos_free_mem(picture);
            set = pg.set;
            picture = load_picture(pictures.story(set));
        }
        uint8_t kind = pg.kind;
        set_draw_page(1);
        put_char(0xFF);
        if (kind == 1) {
            if (page == 0) {
                draw_picture_frame(0, 0x1E, 0xE0, 0, background);
                draw_picture_frame(0, 0x3A, 0xA8, 1, background);
            } else if (page == 7) {
                draw_picture_frame(0, 0x1A, 0xE8, 0, background);
                draw_picture_frame(0, 0x36, 200, 2, background);
            } else if (page == 0x0E) {
                draw_picture_frame(0, 0, 0xB8, 0, background);
                draw_picture_frame(0, 0, 0xF8, 3, background);
            }
        }
        int py = pg.py, px = pg.px;
        draw_picture_frame(0, py, px, pg.frame, picture);
        if (kind > 3) draw_picture_frame(0, py + 0x37, px, kind * 2 - 5, picture);
        intro.story_left[0] = pg.left[0];
        intro.story_left[1] = pg.left[1];
        intro.story_right[0] = pg.right[0];
        intro.story_right[1] = pg.right[1];
        intro.story_line_min = pg.line_min;
        intro.story_line_max = pg.line_max;
        uint16_t x = pg.x;
        intro.story_y = pg.y;
        intro.story_x = x;
        const uint8_t *text;
        if (kind == 3) {
            draw_picture_frame(0, 0x27, 0x60, 3, picture);
            draw_story_text((const uint8_t *)strings[Strings::Instantly_a_shimmering_2F31], font);   /* "Instantly, a shimmering blue door springs up!" */
            intro.story_y = 0xB4;
            intro.story_x = x;
            text = (const uint8_t *)strings[Strings::With_heart_beating_2F5F];                   /* "With heart beating rapidly, you step into it." */
        } else {
            load_file_part(pg.text, 2000, text_buffer.data, strings[Strings::STORY_DAT_2F8D]);
            text = text_buffer.data;
        }
        draw_story_text(text, font);
        if (page != 0) {
            flush_keyboard();
            wait_any_key();
        }
        copy_page(0, 1);
        if (kind == 2) {
            set_draw_page(1);
            draw_picture_frame(0, 0x56, 0x28, 2, picture);
            copy_back_rect(0x78, 0x4B, 0x56, 0x28);
        }
    }
    dos_free_mem(picture);
    dos_free_mem(background);
    dos_free_mem2(font);
    flush_keyboard();
    wait_any_key();
    set_draw_page(1);
    put_char(0xFF);
    set_draw_page(0);
}

void Intro::print_centered(const char *text)
{
    int len = (int)strlen(text), col = 0x12 - len / 2;
    set_draw_color(gfx.color_frame);
    fill_rect(199, col * 8, 0xC1, 8);
    int right = (len + col + 2) * 8;
    fill_rect(199, 0x137, 0xC1, right);
    set_draw_color(gfx.color_text);
    draw_line(0xC0, col * 8, 0xC0, 8);
    draw_line(0xC0, 0x137, 0xC0, right);
    set_cursor(0x18, col);
    draw_label_left();
    print_text(text);
    draw_label_right();
}

/* The frame around the menu (rows 15-24), in frame-piece glyphs 0x7B-0x7F. */
void Intro::draw_menu_border(void)
{
    set_text_color((uint8_t)gfx.color_frame);
    set_cursor(0x0F, 0);
    put_char(0x7B);
    for (int i = 0; i < 0x26; i++) put_char(0x7F);
    put_char(0x7C);
    for (int i = 0; i < 8; i++) {
        put_char(0x7F);
        set_cursor(get_cursor_row(), 0x27);
        put_char(0x7F);
    }
    put_char(0x7D);
    for (int i = 0; i < 0x26; i++) put_char(0x7F);
    text_windows.auto_advance = 0;   /* the last cell, without scrolling the screen */
    put_char(0x7E);
    text_windows.auto_advance = 1;
    set_draw_color(gfx.color_text);
    draw_line(0x7F, 0x138, 0x7F, 7);
    draw_line_b(0xC0, 0x138);
    draw_line_b(0xC0, 7);
    draw_line_b(0x7F, 7);
    set_text_color((uint8_t)gfx.color_text);
}

/* The "Ultima V" logo (ULTIMA.16 frame 0) on the top 100 rows and the animated banner below it.
 * quick != 0 (the full sequence): the logo dissolves in and "Warriors of Destiny" is revealed. */
void Intro::show_title_screen(unsigned quick)
{
    if (map != Map::TITLE) {
        free_tileset();
        map.set(Map::TITLE);
    }
    intro.title_picture = load_picture(pictures.name(Pictures::LOGO));
    set_draw_page(0);
    put_char(0xFF);
    set_draw_page(1);
    draw_picture_frame(0, 0, 0, 0, intro.title_picture);
    if (quick != 0) {
        copy_back_rect(100, 0x13F, 0, 0);
        quick = poll_key() == 0;
    }
    if (quick == 0) copy_page_rect(100, 0x13F, 0, 0, 0, 1);
    draw_menu_banner();
    dos_free_mem(intro.title_picture);
    if (quick != 0) {
        uint16_t wd = load_bitmap(strings[Strings::WD_BIT_3105]);   /* WD.BIT */
        reveal_strips(wd);
        dos_free_mem2(wd);
    }
    set_draw_page(0);
    wait_reveal_strips();
    draw_menu_border();
}

void Intro::draw_menu_item(const char *text, int col, int row, int selected)
{
    if (row == selected) put_char(0xFD);
    set_cursor(row + 0x11, col);
    put_char(' ');
    print_text(text);
    put_char(' ');
    if (row == selected) put_char(0xFD);
}

void Intro::draw_main_menu(int selected)
{
    draw_menu_item(strings[Strings::Journey_Onward_310C], 0x0C, 0, selected);   /* Journey Onward */
    draw_menu_item(strings[Strings::Create_New_Character_311B], 9, 1, selected);      /* Create New Character */
    draw_menu_item(strings[Strings::Transfer_from_Ultima_IV_3130], 8, 2, selected);      /* Transfer from Ultima IV */
    draw_menu_item(strings[Strings::Ultima_V_Introduction_3148], 9, 3, selected);      /* Ultima V Introduction */
    draw_menu_item(strings[Strings::Acknowledgements_315E], 0x0B, 4, selected);   /* Acknowledgements */
    draw_menu_item(strings[Strings::Return_to_the_View_316F], 0x0A, 5, selected);   /* Return to the View */
}

/* Acknowledgements: a scroll (STARTSC.16) unrolls from the middle of the screen to show the
 * credits (frame 1), waits for a key, and rolls up again. */
void Intro::menu_scroll_effect(void)
{
    uint16_t picture = load_picture(pictures.name(Pictures::START_SCROLL));
    set_draw_page(1);
    draw_picture_frame(0, 0x3F, 0x10, 1, picture);
    set_draw_page(0);
    for (int y = 199; y > 0x3E; y--) {   /* the two rollers rise */
        draw_picture_frame(0, y, 0x90, 0, picture);
        draw_picture_frame(0, y, 0xA0, 2, picture);
        if ((y & 7) == 0) delay_ticks(1);   /* port: the original isn't paced here */
    }
    for (int i = 0; i < 0x90; i += 8) {   /* and part, uncovering the text */
        draw_picture_frame(0, 0x3F, 0x88 - i, 0, picture);
        copy_page_rect(199, 0x9F - i, 0x3F, 0x98 - i, 0, 1);
        draw_picture_frame(0, 0x3F, i + 0xA8, 2, picture);
        copy_page_rect(199, i + 0xA7, 0x3F, i + 0xA0, 0, 1);
        delay_ticks(1);
    }
    intro.title_picture = load_picture(pictures.name(Pictures::LOGO));
    set_draw_page(1);
    put_char(0xFF);
    draw_picture_frame(0, 0x41, 0x10, 1, intro.title_picture);
    dos_free_mem(intro.title_picture);
    draw_menu_border();
    draw_main_menu(4);
    wait_any_key();
    set_draw_page(0);
    for (int i = 0x88; i >= 0; i -= 8) {   /* close over the menu waiting in the back page */
        draw_picture_frame(0, 0x3F, 0x90 - i, 0, picture);
        copy_page_rect(199, 0x8F - i, 0x3F, 0x88 - i, 0, 1);
        draw_picture_frame(0, 0x3F, i + 0xA0, 2, picture);
        copy_page_rect(199, i + 0xB7, 0x3F, i + 0xB0, 0, 1);
        delay_ticks(1);
    }
    for (int y = 0x3F; y < 199; y++) {   /* the rollers sink; the original frees the picture first */
        draw_picture_frame(0, y + 1, 0x90, 0, picture);
        draw_picture_frame(0, y + 1, 0xA0, 2, picture);
        copy_page_rect(y, 0xAF, y, 0x90, 0, 1);
        if ((y & 7) == 0) delay_ticks(1);
    }
    dos_free_mem(picture);
    copy_page_rect(199, 0xAF, 199, 0x90, 0, 1);
    draw_menu_banner();
    set_draw_page(0);
    flush_keyboard();
}

int Intro::wait_key_or_ticks(int ticks)
{
    for (int i = 0; i < ticks; i++) {
        int k = poll_key();
        if (k != 0) return k;
        if (platform->quit_requested()) return 0x1B;
        delay_ticks(1);
    }
    return 0;
}

/* "Ultima" grows, "Lord British presents" with his signature traced, then the title screen. */
int Intro::title_sequence(void)
{
    uint16_t title = load_bitmap(pictures.name(Pictures::TITLE_BITMAP));     /* TITLE.BIT */
    uint16_t british = load_bitmap(pictures.name(Pictures::BRITISH_BITMAP));   /* BRITISH.BIT */
    /* BRITISH.PTH, the signature's path. (The original reads it over the saved game's place in
     * the data segment, to save memory; here it has a buffer of its own.) */
    load_file_part(0, Intro::PATH_SIZE, intro.signature_path, strings[Strings::BRITISH_PTH_31B5]);
    int go = show_title_picture(title) == 0;
    set_draw_page(1);
    fill_rect(199, 0x13F, 0x8C, 0);
    draw_font_glyph(0x8C, 0x6C, 7, title);
    copy_page_rect(199, 0x13F, 0x8C, 0, 0, 1);
    if (go) {
        delay_ticks(0x12);
        go = wait_key_or_ticks(0x14) == 0;
    }
    if (go) {
        put_char(0xFF);
        draw_font_glyph(0, 0x98, 8, title);
        copy_page(0, 1);
        go = wait_key_or_ticks(0x14) == 0;
        if (go) {
            set_draw_page(0);
            intro.path_pos = 0;
            go = 0;
            if (intro_trace_path(0x44, 0x2C) && intro_trace_path(0x5E, 0x40) && intro_trace_path(0x4E, 0x8F))
                go = intro_trace_path(0x69, 0xA7);
            set_draw_page(1);
        }
        draw_font_glyph(0x42, 0x18, 0, british);
        copy_page(0, 1);
        draw_font_glyph(0xA0, 0x68, 9, title);
        copy_page(0, 1);
        if (go) go = wait_key_or_ticks(0x14) == 0;
    }
    dos_free_mem2(british);
    dos_free_mem2(title);
    map.set(Map::TITLE);
    put_char(0xFF);
    set_draw_page(0);
    return go;
}

/* ---- Transfer from Ultima IV (INTRO 91d6-a24f) ---- */

#define U4W(off) ((unsigned)(intro.u4_character[off] | intro.u4_character[(off) + 1] << 8))

/* Ultima IV's PARTY.SAV: 8 bytes of counters, eight 39-byte characters (words hp, max hp,
 * experience, strength, dexterity, intelligence, magic points, three more; 16 bytes of name; sex
 * 0x0B male / 0x0C female; class 0-7; status), then from 0x140 food, gold and the eight virtue
 * scores as words (at 0x146), where 0 means partial Avatarhood - all eight make an Avatar.
 * Only the first character comes over. Attributes above 70, points above 9999, a class above 7
 * or control characters in the name are "bad data". 1 = failed. */
int Intro::read_ultima4_party(void)
{
    static uint8_t rest[0xB6];
    int bad = 0;
    memset(intro.u4_character, 0, sizeof intro.u4_character);
    load_file_part(8, 0x28, intro.u4_character, strings[Strings::party_sav_3278]);   /* "party.sav" */
    if (U4W(6) > 0x46 || U4W(8) > 0x46 || U4W(10) > 0x46 || U4W(4) > 0x270F || U4W(0) > 0x270F || U4W(2) > 0x270F || intro.u4_character[0x25] > 7) bad = 1;
    for (int i = 0; i < 8; i++)
        if (intro.u4_character[0x14 + i] != 0 && intro.u4_character[0x14 + i] < 0x20) bad = 1;
    if (bad) {
        put_char(0xFF);
        put_char(0xFC);
        set_cursor(5, 0);
        print_text(strings[Strings::Error_Your_Ultima_IV_3282]);   /* "Error:  Your Ultima IV game\n\ncontains bad data.\n\n" */
        print_text(strings[Strings::Unable_to_continue_32B4]);   /* "Unable to continue transfer.\n\n\n" */
        print_text(strings[Strings::Press_any_key_to_return_32D4]);   /* "Press any key to return to the menu." */
        put_char(0xFB);
        wait_key_timeout(0);
        return 1;
    }
    PartyMember *m = &party[0];
    int n = 0;
    for (; n < 8 && intro.u4_character[0x14 + n] != 0; n++) m->name[n] = (char)intro.u4_character[0x14 + n];
    m->name[n] = '\0';
    m->sex = intro.u4_character[0x24] == 0x0B ? 0x0B : 0x0C;
    m->cls = "MBFDTPRS"[intro.u4_character[0x25]];
    m->status = 'G';
    m->str = intro.u4_character[6];
    m->dex = intro.u4_character[8];
    m->intel = intro.u4_character[10];
    m->mp = intro.u4_character[0x0C];
    m->hp = (uint16_t)U4W(0);
    m->max_hp = (uint16_t)U4W(2);
    m->xp = (uint16_t)U4W(4);
    m->level = (uint8_t)(U4W(2) / 100);
    memset(rest, 0xFF, sizeof rest);
    load_file_part(0x140, 0xB6, rest, strings[Strings::party_sav_32F9]);   /* (the original reads this to ds:BB1C) */
    int virtues = 0;
    for (int i = 0; i < 8; i++) virtues |= rest[6 + i * 2] | rest[7 + i * 2];
    if (virtues == 0) intro.u4_is_avatar = 1;
    return 0;
}

void Intro::print_ultima4_class(void)
{
    static const char classes[] = "MBFDTPRS";
    static const Strings::Id names[8] = {Strings::Mage_3306, Strings::Bard_330C, Strings::Fighter_3312, Strings::Druid_331B, Strings::Tinker_3322, Strings::Paladin_332A, Strings::Ranger_3333, Strings::Shepherd_333B};   /* Mage Bard Fighter Druid Tinker Paladin Ranger Shepherd */
    const char *c = strchr(classes, party[0].cls);
    if (c && *c) print_text(strings[names[c - classes]]);
}

/* Ultima IV's attributes go to 50, Ultima V's to 30: up to 9 unchanged, 10-29 halved above 9
 * (10-19), 30 and up quartered above 30 (20-30). */
int Intro::convert_ultima4_stat(int stat)
{
    if (stat <= 9) return stat;
    if (stat < 0x1E) return (stat - 9) / 2 + 10;
    return (stat - 0x1E) / 4 + 0x14;
}

/* The row labels (Name, Sex, Class, Exp, Level, STR, DEX, INT), in both panels. */
void Intro::print_transfer_label(int row)
{
    for (uint16_t w = 0; w < 2; w++) {
        select_text_window(w);
        set_cursor(transfer_label_rows_[row], 3);
        print_text(strings[transfer_labels_[row]]);
    }
}

void Intro::draw_transfer_frame(void)
{
    set_text_color((uint8_t)gfx.color_frame);
    set_cursor(0x13, 0);
    put_char(0x7B);
    for (int i = 0x26; i != 0; i--) put_char(0x7F);
    put_char(0x7C);
    for (int i = 3; i != 0; i--) {
        put_char(0x7F);
        set_cursor(get_cursor_row(), 0x27);
        put_char(0x7F);
    }
    put_char(0x7D);
    for (int i = 0x26; i != 0; i--) put_char(0x7F);
    put_char(0x7E);
    set_draw_color(gfx.color_text);
    draw_line(0x9F, 0x138, 0x9F, 7);
    draw_line_b(0xB8, 0x138);
    draw_line_b(0xB8, 7);
    draw_line_b(0x9F, 7);
    set_text_color((uint8_t)gfx.color_text);
}

/* One of the two panels (in the current window), titled " Ultima IV " - both of them: the right
 * one's title is corrected to "V" by draw_transfer_screen with a space over the last letter. */
void Intro::draw_transfer_panel(int x)
{
    set_draw_color(gfx.color_frame);
    fill_rect(0x8F, x + 6, 0, x);
    fill_rect(0x89, x + 0x97, 0, x + 0x8F);
    fill_rect(0x8F, x + 0x96, 0x89, x + 7);
    set_text_color((uint8_t)gfx.color_frame);
    set_cursor(0, 0);
    print_text(strings[Strings::X7B_Block_Block_3676]);
    draw_label_left();
    set_text_color((uint8_t)gfx.color_text);
    print_text(strings[Strings::Ultima_IV_6D3A]);   /* " Ultima IV " */
    set_text_color((uint8_t)gfx.color_frame);
    draw_label_right();
    print_text(strings[Strings::Block_Block_367A]);
    put_char(0x7C);
    set_cursor(0x11, 0);
    put_char(0x7D);
    set_cursor(0x11, 0x12);
    put_char(0x7E);
    set_draw_color(gfx.color_text);
    draw_line(7, x + 7, 7, x + 0x18);
    draw_line_b(0x88, x + 7);
    draw_line_b(0x88, x + 0x8F);
    draw_line_b(7, x + 0x8F);
    draw_line_b(7, x + 0x80);
    set_text_color((uint8_t)gfx.color_text);
}

void Intro::draw_transfer_screen(void)
{
    draw_transfer_frame();
    define_text_window(0x12, 0x13, 0, 0, 0);
    define_text_window(0x12, 0x27, 0, 0x15, 1);
    define_text_window(0x15, 0x25, 0x15, 3, 2);
    select_text_window(0);
    draw_transfer_panel(0);
    select_text_window(1);
    draw_transfer_panel(0xA8);
    set_cursor(0, 0x0C);
    put_char(' ');   /* "Ultima IV" -> "Ultima V" */
}

int Intro::transfer_yes_no(void)
{
    for (;;) {
        char c = (char)to_upper((uint8_t)wait_key_timeout(0));
        if (c == 'Y' || c == 'N') return c;
        if (platform->quit_requested()) return 'Y';
    }
}

/* Moves the highlight (0xFD toggles inverse) from one row label to the next in both panels. */
void Intro::transfer_mark(int row, int col, Strings::Id text, int on)
{
    for (uint16_t w = 0; w < 2; w++) {
        select_text_window(w);
        set_cursor(row, col);
        if (on) put_char(0xFD);
        print_text(strings[text]);
        if (on) put_char(0xFD);
    }
}

/* A new game is made from INIT.GAM with the Ultima IV hero in place of the first member. The
 * left panel shows the old values, the right one the new as each is converted: the name and the
 * sex may be changed; a full Avatar keeps the title, anyone else keeps the class; experience is
 * divided by 10 and the level follows from it (30 hit points a level); strength (at least 20),
 * dexterity and intelligence are scaled down; magic points equal intelligence.
 *
 * The original asks for the drive with the Ultima IV player disk. The port looks for PARTY.SAV
 * in the save directory and the game directory, and shows the original's prompt if it is not
 * there (any key tries again, Esc gives up). */
void Intro::transfer_from_ultima4(void)
{
    static uint8_t ool[0x200];
    PartyMember *m = &party[0];
    Map::Id loc = map == Map::DEMO ? Map::TITLE_MENU : map.id();
    map.set(loc);
    load_game_file(strings[Strings::INIT_GAM_3345]);   /* "INIT.GAM" */
    memset(ool, 0, sizeof ool);
    load_file_part(0, 0x100, ool + 0x100, strings[Strings::INIT_OOL_334E]);            /* "INIT.OOL" */
    define_text_window(0x18, 0x27, 0, 0, 0);
    select_text_window(0);
    put_char(0xFF);
    for (;;) {
        FILE *f = game_fopen(strings[Strings::party_sav_3278], "rb");
        if (f) {
            fclose(f);
            break;
        }
        put_char(0xFF);
        set_cursor(8, 3);
        print_text(strings[Strings::Transfer_Character_from_3357]);    /* "Transfer Character from Ultima IV" */
        set_cursor(11, 0);
        print_text(strings[Strings::Please_insert_the_Ultima_3379]);    /* "Please insert the Ultima IV Player Disk" */
        set_cursor(13, 2);
        print_text("(PARTY.SAV, in the save or game dir)");
        set_cursor(15, 4);
        print_text("and press a key");
        set_cursor(17, 4);
        print_text(strings[Strings::or_press_Esc_to_abort_33B8]);    /* "or press <Esc> to abort transfer" */
        if (to_upper((uint8_t)wait_key_timeout(0)) == 0x1B || platform->quit_requested()) return;
    }
    intro.u4_is_avatar = 0;
    if (read_ultima4_party() != 0) return;
    if (intro.u4_is_avatar != 0) m->cls = 'A';
    put_char(0xFF);
    set_cursor(0x0B, 0);
    put_char(0xFC);
    print_text(strings[Strings::Found_33D9]);   /* "Found:\n" */
    set_cursor(0x0C, 0);
    print_text(m->name);
    put_char(0xFB);
    set_cursor(0x0D, 0x0C);
    print_text(strings[Strings::a_level_33E1]);   /* "a level " */
    print_number(' ', 1, m->level);
    print_text(strings[m->sex == 0x0B ? Strings::Male_33EA : Strings::Female_33F1]);   /* " Male " / " Female " */
    print_ultima4_class();
    set_cursor(0x0F, 0x11);
    print_text(strings[Strings::STR_33FA]);   /* "STR:  " */
    print_number(' ', 1, m->str);
    set_cursor(0x10, 0x11);
    print_text(strings[Strings::DEX_3401]);   /* "DEX:  " */
    print_number(' ', 1, m->dex);
    set_cursor(0x11, 0x11);
    print_text(strings[Strings::INT_3408]);   /* "INT:  " */
    print_number(' ', 1, m->intel);
    set_cursor(0x14, 10);
    print_text(m->name);
    print_text(strings[Strings::is_340F]);   /* " is " */
    print_text(strings[intro.u4_is_avatar ? Strings::an_Avatar_3414 : Strings::not_an_Avatar_341F]);   /* "an Avatar." / "not an Avatar" */
    wait_key_timeout(0);

    put_char(0xFF);
    draw_transfer_screen();
    for (int i = 0; i < 8; i++) print_transfer_label(i);
    select_text_window(0);
    set_cursor(3, 0);
    put_char(0xFC);
    print_text(m->name);
    put_char(0xFB);
    set_cursor(5, 10);
    print_text(strings[m->sex == 0x0B ? Strings::Male_342D : Strings::Female_3432]);   /* "Male" / "Female" */
    set_cursor(6, 10);
    if (intro.u4_is_avatar) print_text(strings[Strings::Avatar_3439]);   /* "Avatar" */
    else print_ultima4_class();
    set_cursor(8, 10);
    print_number(' ', 1, (int16_t)m->xp);
    set_cursor(9, 10);
    print_number(' ', 1, m->level);
    set_cursor(0x0B, 10);
    print_number(' ', 1, m->str);
    set_cursor(0x0C, 10);
    print_number(' ', 1, m->dex);
    set_cursor(0x0D, 10);
    print_number(' ', 1, m->intel);
    set_cursor(0x0F, 0);
    put_char(0xFC);
    print_text(strings[intro.u4_is_avatar ? Strings::Avatar_3440 : Strings::Non_Avatar_3447]);   /* "Avatar" / "Non-Avatar" */
    put_char(0xFB);

    /* the name */
    transfer_mark(2, 7, Strings::Name_3452, 1);   /* "Name:" */
    select_text_window(2);
    put_char(0xFF);
    set_cursor(0, 10);
    print_text(strings[Strings::Keep_this_name_345E]);   /* "Keep this name?" */
    int c = transfer_yes_no();
    put_char(0xFF);
    if (c == 'N')
        do {
            set_cursor(0, 1);
            print_text(strings[Strings::Enter_new_name_346E]);   /* "Enter new name: " */
            read_name(8, m->name);
        } while (m->name[0] == '\0' && !platform->quit_requested());
    select_text_window(1);
    set_cursor(3, 0);
    put_char(0xFC);
    print_text(m->name);
    put_char(0xFB);
    transfer_mark(2, 7, Strings::Name_347F, 0);

    /* the sex */
    transfer_mark(5, 5, Strings::Sex_3485, 1);   /* "Sex:" */
    select_text_window(2);
    put_char(0xFF);
    put_char(0xFC);
    print_text(strings[Strings::Keep_same_sex_3495]);   /* "Keep same sex?" */
    put_char(0xFB);
    c = transfer_yes_no();
    select_text_window(1);
    set_cursor(5, 10);
    if ((c == 'Y' && m->sex == 0x0B) || (c == 'N' && m->sex == 0x0C)) {
        print_text(strings[Strings::Male_34A4]);   /* "Male" */
        m->sex = 0x0B;
    } else {
        print_text(strings[Strings::Female_34A9]);   /* "Female" */
        m->sex = 0x0C;
    }
    transfer_mark(5, 5, Strings::Sex_34B0, 0);

    /* the class */
    transfer_mark(6, 3, Strings::Class_34B5, 1);   /* "Class:" */
    select_text_window(1);
    set_cursor(6, 10);
    if (intro.u4_is_avatar) print_text(strings[Strings::Avatar_34BC]);   /* "Avatar" */
    else print_ultima4_class();
    select_text_window(2);
    put_char(0xFF);
    set_cursor(0, 2);
    print_text(strings[intro.u4_is_avatar ? Strings::Thou_art_now_an_Avatar_34CF : Strings::Class_remains_intact_34E7]);   /* "Thou art now an Avatar:" / "Class remains intact" */
    wait_key_timeout(0);
    transfer_mark(6, 3, Strings::Class_34FC, 0);

    /* experience */
    transfer_mark(8, 5, Strings::Exp_3503, 1);   /* "Exp:" */
    select_text_window(1);
    set_cursor(8, 10);
    m->xp = (uint16_t)((int16_t)m->xp / 10);
    print_number(' ', 1, (int16_t)m->xp);
    select_text_window(2);
    put_char(0xFF);
    put_char(0xFC);
    print_text(strings[Strings::Experience_has_been_3514]);   /* "Experience has been converted" */
    put_char(0xFB);
    wait_key_timeout(0);
    transfer_mark(8, 5, Strings::Exp_3532, 0);

    /* level */
    transfer_mark(9, 3, Strings::Level_3537, 1);   /* "Level:" */
    select_text_window(1);
    set_cursor(9, 10);
    m->level = 1;
    for (int n = (int16_t)m->xp / 100; n > 0; n >>= 1) m->level++;
    m->hp = m->max_hp = (uint16_t)(m->level * 0x1E);
    print_number(' ', 1, m->level);
    select_text_window(2);
    put_char(0xFF);
    put_char(0xFC);
    print_text(strings[Strings::Level_has_been_converted_354A]);   /* "Level has been converted" */
    put_char(0xFB);
    wait_key_timeout(0);
    transfer_mark(9, 3, Strings::Level_3563, 0);

    /* the three attributes */
    static const struct { int row; Strings::Id label, was, now, end; } attr[3] = {
        {0x0B, Strings::STR_356A, Strings::Strength_was_357B, Strings::N50_now_358A, Strings::N30_3595},   /* "STR:", "Strength: was ", "(50), now ", "(30)" */
        {0x0C, Strings::DEX_359F, Strings::Dexterity_was_35AE, Strings::N50_now_35BE, Strings::N30_35C9},   /* "DEX:", "Dexterity: was " */
        {0x0D, Strings::INT_35D3, Strings::Intellect_was_35E2, Strings::N50_now_35F2, Strings::N30_35FD},   /* "INT:", "Intellect: was " */
    };
    for (int i = 0; i < 3; i++) {
        uint8_t *stat = i == 0 ? &m->str : i == 1 ? &m->dex : &m->intel;
        int old = *stat;
        transfer_mark(attr[i].row, 5, attr[i].label, 1);
        select_text_window(1);
        set_cursor(attr[i].row, 10);
        *stat = (uint8_t)convert_ultima4_stat(old);
        if (i == 0 && m->str < 0x14) m->str = 0x14;
        if (i == 2) m->mp = m->intel;
        print_number(' ', 1, *stat);
        select_text_window(2);
        put_char(0xFF);
        set_cursor(0, 1);
        print_text(strings[attr[i].was]);
        print_number(' ', 1, old);
        print_text(strings[attr[i].now]);
        print_number(' ', 1, *stat);
        print_text(strings[attr[i].end]);
        wait_key_timeout(0);
        transfer_mark(attr[i].row, 5, attr[i].label, 0);
    }
    select_text_window(1);
    set_cursor(0x0F, 0);
    put_char(0xFC);
    print_text(strings[intro.u4_is_avatar ? Strings::Avatar_360C : Strings::Non_Avatar_3613]);   /* "Avatar" / "Non-Avatar" */
    put_char(0xFB);
    select_text_window(2);
    define_text_window(0x16, 0x25, 0x15, 2, 2);
    put_char(0xFF);
    print_text(strings[Strings::Conversion_complete_361E]);   /* "\n\n Conversion complete, saving...\n" */
    save_file_part(0x200, ool, strings[Strings::SAVED_OOL_3641]);                    /* SAVED.OOL: no Britannia objects, INIT.OOL for the Underworld */
    save_game_file(strings[Strings::SAVED_GAM_364B]);   /* SAVED.GAM */
    map.set(loc);
}

/* Loads SAVED.GAM over the save area and splits SAVED.OOL into BRIT.OOL and UNDER.OOL.
 * Returns 0 if there is no character. */
int Intro::journey_onward(void)
{
    draw_screen_frame();
    set_wind_direction(0);
    select_text_window(2);
    set_cursor(0x0C, 0);
    if (map == Map::TITLE) {
        load_tileset(pictures.name(Pictures::TILES));
        map.set(Map::TITLE_MENU);
    }
    load_game_file(strings[Strings::SAVED_GAM_31E6]);   /* SAVED.GAM */
    if (party[0].name[0] != 0) {
        uint8_t ool[0x200] = {0};
        load_file_part(0, 0x200, ool, strings[Strings::SAVED_OOL_323F]);     /* SAVED.OOL */
        save_file_part(0x100, ool, strings[Strings::BRIT_OOL_3249]);        /* BRIT.OOL */
        save_file_part(0x100, ool + 0x100, strings[Strings::UNDER_OOL_3252]);   /* UNDER.OOL */
        set_wind_direction(-1);
        tiles.origin_y = 8;
        return 1;
    }
    print_text(strings[Strings::No_active_game_31F0]);   /* "No active game. " */
    print_text(strings[Strings::Please_create_a_3203]);   /* "Please create a character " */
    print_text(strings[Strings::or_transfer_one_from_321E]);   /* "or transfer one from Ultima IV. " */
    wait_key_timeout(0);
    select_text_window(0);
    put_char(0xFF);
    set_draw_page(1);
    put_char(0xFF);
    set_draw_page(0);
    show_title_screen(0);
    return 0;
}

void Intro::back_to_title(void)
{
    define_text_window(0x18, 0x27, 0, 0, 0);
    select_text_window(0);
    put_char(0xFF);
    set_draw_page(1);
    put_char(0xFF);
    show_title_screen(0);
    draw_menu_banner();
    set_draw_page(0);
}

/* The original first detects the video card and loads the driver; the port is told which
 * (gfx.video_type). The menu waits about 200 cursor blinks, then starts the demo by itself. */
int Intro::intro_main(int skip_title)
{
    static const char keys[] = "JCTUAR";
    sound.enabled = 1;
    reset_tile_anim_map();
    video_start();
    alloc_back_page();
    select_font(0);
    define_text_window(0x18, 0x27, 0, 0, 0);
    select_text_window(0);
    int go = 0;
    if (!skip_title) go = title_sequence();
    else map.set(Map::TITLE);
    if (platform->quit_requested()) return 0;
    show_title_screen((unsigned)go);
    if (go) attract_demo();
    set_random_seed((uint16_t)time_seed());

    for (;;) {
        if (platform->quit_requested()) return 0;
        define_text_window(0x17, 0x26, 0x10, 1, 0);
        select_text_window(0);
        put_char(0xFF);
        define_text_window(0x18, 0x27, 0, 0, 0);
        define_text_window(9, 0x27, 1, 0x18, 1);
        define_text_window(0x17, 0x27, 0x0B, 0x18, 2);
        int selected = 0;
        uint8_t choice;
        print_centered(strings[Strings::Copyright_1988_Lord_31C1]);   /* "Copyright 1988 Lord British" */
        draw_main_menu(0);
        do {
            set_cursor(0x0F, 0x0F);
            draw_label_left();
            print_text(strings[Strings::Select_31DD]);   /* "Select: " */
            draw_label_right();
            set_cursor(0x0F, 0x17);
            unsigned k = 0;
            for (int i = 0; i < 200 && k == 0; i++) {
                k = to_upper((uint8_t)wait_key_cursor());
                if (k == 0) wait_reveal_strips();
                if (platform->quit_requested()) return 0;
            }
            choice = 0;
            switch (k) {
            case 0: choice = 'R'; break;                       /* idle: the demo */
            case 0x0D: case ' ': choice = (uint8_t)keys[selected]; break;
            case 1: case 3: if (--selected < 0) selected = 5; draw_main_menu(selected); break;
            case 2: case 4: if (++selected > 5) selected = 0; draw_main_menu(selected); break;
            case 'J': case 'C': case 'T': case 'U': case 'A': case 'R':
                selected = (int)(strchr(keys, (int)k) - keys);
                draw_main_menu(selected);
                choice = (uint8_t)k;
                break;
            }
        } while (choice < 0x21);

        switch (choice) {
        case 'A':
            if (map != Map::TITLE) {
                free_tileset();
                map.set(Map::TITLE);
            }
            menu_scroll_effect();
            break;
        case 'C':
            create_character();
            back_to_title();
            break;
        case 'J':
            if (journey_onward()) return 1;
            break;
        case 'R':
            attract_demo();
            break;
        case 'T':
            put_char(0xFF);
            transfer_from_ultima4();
            back_to_title();
            break;
        case 'U':
            intro_story();
            show_title_screen(0);
            break;
        }
    }
}
