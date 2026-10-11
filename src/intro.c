/* intro.c - INTRO.OVL: title sequence, main menu and introduction story. */
#include <string.h>
#include "commands.h"
#include "data.h"
#include "files.h"
#include "gfx.h"
#include "input.h"
#include "intro.h"
#include "map.h"
#include "misc.h"
#include "picture.h"
#include "platform.h"
#include "status.h"
#include "story.h"
#include "text.h"
#include "tiles.h"
#include "view.h"
#include "world.h"

#define TEXT_BUFFER ((uint16_t)offsetof(struct DGroup, text_buffer))
#define FILE_NAME(off) ds_str(ds_u16(off))   /* the picture file table at ds:25EA */

/* The banner's four animation frames go into the back page for wait_reveal_strips. */
void draw_menu_banner(void)
{
    set_draw_page(1);
    put_char(0xFF);
    for (int i = 0; i < 4; i++) draw_picture_frame(0, i * 0x32, 0x10, i + 1, dg.title_picture);
}

/* BRITISH.PTH, loaded over the save area: one byte per pixel step, low nibble dy and high nibble
 * dx (bit 3 = negative, 0-2 pixels); a larger step moves without drawing; 0 ends a stroke.
 * The position in the data is kept in the word at ds:BB18. Returns 0 if a key stopped it. */
int intro_trace_path(int y, int x)
{
    const uint8_t *path = ds_ptr(SAVE_OFFSET);
    unsigned pen = dg.color_text, i;
    set_draw_color(dg.color_text);
    for (i = ds_u16(0xBB18); i < 0x1060; i++) {
        if (pen != 0) plot_pixel(y, x);
        pen = dg.color_text;
        int dy = path[i] & 7, dx = (path[i] & 0x70) >> 4;
        if (dy > 2) pen = 0;
        if (path[i] & 8) dy = -dy;
        if (dx > 2) pen = 0;
        if (path[i] & 0x80) dx = -dx;
        x += dx;
        y += dy;
        if (poll_key() != 0 || platform_quit_requested()) return 0;
        if ((i & 0x1F) == 0) delay_ticks(1);
        if (path[i] == 0) break;
    }
    ds_set_u16(0xBB18, (uint16_t)(i + 1));
    return (int)(i + 1);
}

/* 21 pages. Per page (tables at ds:2F98-30F0): the picture set and frame, where it goes, the
 * text margins and start, and a kind: 1 = also the scroll-shaped background (TEXT.16), 2 = a
 * second frame dissolves in afterwards, 3 = the moongate page with fixed text, 4-6 = an extra
 * frame below. The text is in STORY.DAT at ds:3016[page]. A key turns the page. */
void intro_story(void)
{
    if (dg.location != 0x40) free_tileset();
    dg.location = 0x40;
    uint16_t font = load_bitmap(FILE_NAME(0x25EA));   /* PROPORT.PCS */
    draw_story_text(ds_at(0x2F30), font);
    set_draw_page(1);
    put_char(0xFF);
    set_draw_page(0);
    put_char(0xFF);
    set_cursor(10, 0);
    uint16_t background = load_picture(ds_str(dg.story_bg_pic));
    uint16_t picture = load_picture(ds_str(dg.story_pics[0]));
    unsigned set = 0;
    for (int page = 0; page <= 0x14 && !platform_quit_requested(); page++) {
        if (ds_u8((uint16_t)(0x30AE + page)) != set) {
            dos_free_mem(picture);
            set = ds_u8((uint16_t)(0x30AE + page));
            picture = load_picture(ds_str(dg.story_pics[set]));
        }
        uint8_t kind = ds_u8((uint16_t)(0x30F0 + page));
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
        int py = ds_u8((uint16_t)(0x30DA + page)), px = ds_u8((uint16_t)(0x30C4 + page));
        draw_picture_frame(0, py, px, ds_u8((uint16_t)(0x3098 + page)), picture);
        if (kind > 3) draw_picture_frame(0, py + 0x37, px, kind * 2 - 5, picture);
        ds_set_u16(STORY_LEFT(0), ds_u8((uint16_t)(0x2F98 + page * 2)));
        ds_set_u16(STORY_LEFT(1), ds_u8((uint16_t)(0x2F99 + page * 2)));
        ds_set_u16(STORY_RIGHT(0), ds_u16((uint16_t)(0x2FC2 + page * 4)));
        ds_set_u16(STORY_RIGHT(1), ds_u16((uint16_t)(0x2FC4 + page * 4)));
        dg.story_line_min = ds_u8((uint16_t)(0x3040 + page));
        dg.story_line_max = ds_u8((uint16_t)(0x3056 + page));
        uint16_t x = ds_u8((uint16_t)(0x306C + page));
        dg.story_y = ds_u8((uint16_t)(0x3082 + page));
        dg.story_x = x;
        const uint8_t *text;
        if (kind == 3) {
            draw_picture_frame(0, 0x27, 0x60, 3, picture);
            draw_story_text(ds_at(0x2F31), font);   /* "Instantly, a shimmering blue door springs up!" */
            dg.story_y = 0xB4;
            dg.story_x = x;
            text = ds_at(0x2F5F);                   /* "With heart beating rapidly, you step into it." */
        } else {
            load_file_part(ds_u16((uint16_t)(0x3016 + page * 2)), 2000, ds_ptr(TEXT_BUFFER), ds_str(0x2F8D));
            text = ds_at(TEXT_BUFFER);
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

void print_centered(const char *text)
{
    int len = (int)strlen(text), col = 0x12 - len / 2;
    set_draw_color(dg.color_frame);
    fill_rect(199, col * 8, 0xC1, 8);
    int right = (len + col + 2) * 8;
    fill_rect(199, 0x137, 0xC1, right);
    set_draw_color(dg.color_text);
    draw_line(0xC0, col * 8, 0xC0, 8);
    draw_line(0xC0, 0x137, 0xC0, right);
    set_cursor(0x18, col);
    draw_label_left();
    print_text(text);
    draw_label_right();
}

/* The frame around the menu (rows 15-24), in frame-piece glyphs 0x7B-0x7F. */
void draw_menu_border(void)
{
    set_text_color((uint8_t)dg.color_frame);
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
    dg.text_auto_advance = 0;   /* the last cell, without scrolling the screen */
    put_char(0x7E);
    dg.text_auto_advance = 1;
    set_draw_color(dg.color_text);
    draw_line(0x7F, 0x138, 0x7F, 7);
    draw_line_b(0xC0, 0x138);
    draw_line_b(0xC0, 7);
    draw_line_b(0x7F, 7);
    set_text_color((uint8_t)dg.color_text);
}

/* The "Ultima V" logo (ULTIMA.16 frame 0) on the top 100 rows and the animated banner below it.
 * quick != 0 (the full sequence): the logo dissolves in and "Warriors of Destiny" is revealed. */
void show_title_screen(unsigned quick)
{
    if (dg.location != 0x40) {
        free_tileset();
        dg.location = 0x40;
    }
    dg.title_picture = load_picture(ds_str(dg.title_pic_file));
    set_draw_page(0);
    put_char(0xFF);
    set_draw_page(1);
    draw_picture_frame(0, 0, 0, 0, dg.title_picture);
    if (quick != 0) {
        copy_back_rect(100, 0x13F, 0, 0);
        quick = poll_key() == 0;
    }
    if (quick == 0) copy_page_rect(100, 0x13F, 0, 0, 0, 1);
    draw_menu_banner();
    dos_free_mem(dg.title_picture);
    if (quick != 0) {
        uint16_t wd = load_bitmap(ds_str(0x3105));   /* WD.BIT */
        reveal_strips(wd);
        dos_free_mem2(wd);
    }
    set_draw_page(0);
    wait_reveal_strips();
    draw_menu_border();
}

void draw_menu_item(const char *text, int col, int row, int selected)
{
    if (row == selected) put_char(0xFD);
    set_cursor(row + 0x11, col);
    put_char(' ');
    print_text(text);
    put_char(' ');
    if (row == selected) put_char(0xFD);
}

void draw_main_menu(int selected)
{
    draw_menu_item(ds_str(0x310C), 0x0C, 0, selected);   /* Journey Onward */
    draw_menu_item(ds_str(0x311B), 9, 1, selected);      /* Create New Character */
    draw_menu_item(ds_str(0x3130), 8, 2, selected);      /* Transfer from Ultima IV */
    draw_menu_item(ds_str(0x3148), 9, 3, selected);      /* Ultima V Introduction */
    draw_menu_item(ds_str(0x315E), 0x0B, 4, selected);   /* Acknowledgements */
    draw_menu_item(ds_str(0x316F), 0x0A, 5, selected);   /* Return to the View */
}

/* Acknowledgements: a scroll (STARTSC.16) unrolls from the middle of the screen to show the
 * credits (frame 1), waits for a key, and rolls up again. */
void menu_scroll_effect(void)
{
    uint16_t picture = load_picture(FILE_NAME(0x2620));
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
    dg.title_picture = load_picture(ds_str(dg.title_pic_file));
    set_draw_page(1);
    put_char(0xFF);
    draw_picture_frame(0, 0x41, 0x10, 1, dg.title_picture);
    dos_free_mem(dg.title_picture);
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

int wait_key_or_ticks(int ticks)
{
    for (int i = 0; i < ticks; i++) {
        int k = poll_key();
        if (k != 0) return k;
        if (platform_quit_requested()) return 0x1B;
        delay_ticks(1);
    }
    return 0;
}

/* "Ultima" grows, "Lord British presents" with his signature traced, then the title screen. */
static int title_sequence(void)
{
    uint16_t title = load_bitmap(FILE_NAME(0x25EE));     /* TITLE.BIT */
    uint16_t british = load_bitmap(FILE_NAME(0x25EC));   /* BRITISH.BIT */
    load_file_part(0, 0x1060, ds_ptr(SAVE_OFFSET), ds_str(0x31B5));   /* BRITISH.PTH */
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
            ds_set_u16(0xBB18, 0);
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
    dg.location = 0x40;
    put_char(0xFF);
    set_draw_page(0);
    return go;
}

/* ---- Transfer from Ultima IV (INTRO 91d6-a24f) ---- */

#define S(off) ds_str(off)
#define U4W(off) ((unsigned)(dg.u4_character[off] | dg.u4_character[(off) + 1] << 8))

/* Ultima IV's PARTY.SAV: 8 bytes of counters, eight 39-byte characters (words hp, max hp,
 * experience, strength, dexterity, intelligence, magic points, three more; 16 bytes of name; sex
 * 0x0B male / 0x0C female; class 0-7; status), then from 0x140 food, gold and the eight virtue
 * scores as words (at 0x146), where 0 means partial Avatarhood - all eight make an Avatar.
 * Only the first character comes over. Attributes above 70, points above 9999, a class above 7
 * or control characters in the name are "bad data". 1 = failed. */
int read_ultima4_party(void)
{
    static uint8_t rest[0xB6];
    int bad = 0;
    dg.install_type = 1;
    dg.cur_disk = 4;
    memset(dg.u4_character, 0, sizeof dg.u4_character);
    load_file_part(8, 0x28, dg.u4_character, S(0x3278));   /* "party.sav" */
    if (U4W(6) > 0x46 || U4W(8) > 0x46 || U4W(10) > 0x46 || U4W(4) > 0x270F || U4W(0) > 0x270F || U4W(2) > 0x270F || dg.u4_character[0x25] > 7) bad = 1;
    for (int i = 0; i < 8; i++)
        if (dg.u4_character[0x14 + i] != 0 && dg.u4_character[0x14 + i] < 0x20) bad = 1;
    if (bad) {
        put_char(0xFF);
        put_char(0xFC);
        set_cursor(5, 0);
        print_text(S(0x3282));   /* "Error:  Your Ultima IV game\n\ncontains bad data.\n\n" */
        print_text(S(0x32B4));   /* "Unable to continue transfer.\n\n\n" */
        print_text(S(0x32D4));   /* "Press any key to return to the menu." */
        put_char(0xFB);
        wait_key_timeout(0);
        return 1;
    }
    PartyMember *m = &dg.party[0];
    int n = 0;
    for (; n < 8 && dg.u4_character[0x14 + n] != 0; n++) m->name[n] = (char)dg.u4_character[0x14 + n];
    m->name[n] = '\0';
    m->sex = dg.u4_character[0x24] == 0x0B ? 0x0B : 0x0C;
    m->cls = "MBFDTPRS"[dg.u4_character[0x25]];
    m->status = 'G';
    m->str = dg.u4_character[6];
    m->dex = dg.u4_character[8];
    m->intel = dg.u4_character[10];
    m->mp = dg.u4_character[0x0C];
    m->hp = (uint16_t)U4W(0);
    m->max_hp = (uint16_t)U4W(2);
    m->xp = (uint16_t)U4W(4);
    m->level = (uint8_t)(U4W(2) / 100);
    memset(rest, 0xFF, sizeof rest);
    load_file_part(0x140, 0xB6, rest, S(0x32F9));   /* (the original reads this to ds:BB1C) */
    int virtues = 0;
    for (int i = 0; i < 8; i++) virtues |= rest[6 + i * 2] | rest[7 + i * 2];
    if (virtues == 0) dg.u4_is_avatar = 1;
    return 0;
}

void print_ultima4_class(void)
{
    static const char classes[] = "MBFDTPRS";
    static const uint16_t names[8] = {0x3306, 0x330C, 0x3312, 0x331B, 0x3322, 0x332A, 0x3333, 0x333B};   /* Mage Bard Fighter Druid Tinker Paladin Ranger Shepherd */
    const char *c = strchr(classes, dg.party[0].cls);
    if (c && *c) print_text(S(names[c - classes]));
}

/* Ultima IV's attributes go to 50, Ultima V's to 30: up to 9 unchanged, 10-29 halved above 9
 * (10-19), 30 and up quartered above 30 (20-30). */
int convert_ultima4_stat(int stat)
{
    if (stat <= 9) return stat;
    if (stat < 0x1E) return (stat - 9) / 2 + 10;
    return (stat - 0x1E) / 4 + 0x14;
}

/* The row labels (Name, Sex, Class, Exp, Level, STR, DEX, INT), in both panels. */
void print_transfer_label(int row)
{
    for (uint16_t w = 0; w < 2; w++) {
        select_text_window(w);
        set_cursor((int16_t)ds_u16((uint16_t)(0x3656 + row * 2)), 3);
        print_text(S(ds_u16((uint16_t)(0x3666 + row * 2))));
    }
}

void draw_transfer_frame(void)
{
    set_text_color((uint8_t)dg.color_frame);
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
    set_draw_color(dg.color_text);
    draw_line(0x9F, 0x138, 0x9F, 7);
    draw_line_b(0xB8, 0x138);
    draw_line_b(0xB8, 7);
    draw_line_b(0x9F, 7);
    set_text_color((uint8_t)dg.color_text);
}

/* One of the two panels (in the current window), titled " Ultima IV " - both of them: the right
 * one's title is corrected to "V" by draw_transfer_screen with a space over the last letter. */
void draw_transfer_panel(int x)
{
    set_draw_color(dg.color_frame);
    fill_rect(0x8F, x + 6, 0, x);
    fill_rect(0x89, x + 0x97, 0, x + 0x8F);
    fill_rect(0x8F, x + 0x96, 0x89, x + 7);
    set_text_color((uint8_t)dg.color_frame);
    set_cursor(0, 0);
    print_text(S(0x3676));
    draw_label_left();
    set_text_color((uint8_t)dg.color_text);
    print_text(S(0x6D3A));   /* " Ultima IV " */
    set_text_color((uint8_t)dg.color_frame);
    draw_label_right();
    print_text(S(0x367A));
    put_char(0x7C);
    set_cursor(0x11, 0);
    put_char(0x7D);
    set_cursor(0x11, 0x12);
    put_char(0x7E);
    set_draw_color(dg.color_text);
    draw_line(7, x + 7, 7, x + 0x18);
    draw_line_b(0x88, x + 7);
    draw_line_b(0x88, x + 0x8F);
    draw_line_b(7, x + 0x8F);
    draw_line_b(7, x + 0x80);
    set_text_color((uint8_t)dg.color_text);
}

void draw_transfer_screen(void)
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

static int transfer_yes_no(void)
{
    for (;;) {
        char c = (char)to_upper((uint8_t)wait_key_timeout(0));
        if (c == 'Y' || c == 'N') return c;
        if (platform_quit_requested()) return 'Y';
    }
}

/* Moves the highlight (0xFD toggles inverse) from one row label to the next in both panels. */
static void transfer_mark(int row, int col, uint16_t text, int on)
{
    for (uint16_t w = 0; w < 2; w++) {
        select_text_window(w);
        set_cursor(row, col);
        if (on) put_char(0xFD);
        print_text(S(text));
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
void transfer_from_ultima4(void)
{
    static uint8_t ool[0x200];
    PartyMember *m = &dg.party[0];
    uint8_t location = dg.location == 0x42 ? 0x41 : dg.location;
    dg.location = location;
    load_file_part(0, 0x1060, ds_ptr(SAVE_OFFSET), S(0x3345));   /* "INIT.GAM" */
    memset(ool, 0, sizeof ool);
    load_file_part(0, 0x100, ool + 0x100, S(0x334E));            /* "INIT.OOL" */
    define_text_window(0x18, 0x27, 0, 0, 0);
    select_text_window(0);
    put_char(0xFF);
    for (;;) {
        FILE *f = game_fopen(S(0x3278), "rb");
        if (f) {
            fclose(f);
            break;
        }
        put_char(0xFF);
        set_cursor(8, 3);
        print_text(S(0x3357));    /* "Transfer Character from Ultima IV" */
        set_cursor(11, 0);
        print_text(S(0x3379));    /* "Please insert the Ultima IV Player Disk" */
        set_cursor(13, 2);
        print_text("(PARTY.SAV, in the save or game dir)");
        set_cursor(15, 4);
        print_text("and press a key");
        set_cursor(17, 4);
        print_text(S(0x33B8));    /* "or press <Esc> to abort transfer" */
        if (to_upper((uint8_t)wait_key_timeout(0)) == 0x1B || platform_quit_requested()) return;
    }
    dg.u4_is_avatar = 0;
    if (read_ultima4_party() != 0) return;
    if (dg.u4_is_avatar != 0) m->cls = 'A';
    put_char(0xFF);
    set_cursor(0x0B, 0);
    put_char(0xFC);
    print_text(S(0x33D9));   /* "Found:\n" */
    set_cursor(0x0C, 0);
    print_text(m->name);
    put_char(0xFB);
    set_cursor(0x0D, 0x0C);
    print_text(S(0x33E1));   /* "a level " */
    print_number(' ', 1, m->level);
    print_text(S(m->sex == 0x0B ? 0x33EA : 0x33F1));   /* " Male " / " Female " */
    print_ultima4_class();
    set_cursor(0x0F, 0x11);
    print_text(S(0x33FA));   /* "STR:  " */
    print_number(' ', 1, m->str);
    set_cursor(0x10, 0x11);
    print_text(S(0x3401));   /* "DEX:  " */
    print_number(' ', 1, m->dex);
    set_cursor(0x11, 0x11);
    print_text(S(0x3408));   /* "INT:  " */
    print_number(' ', 1, m->intel);
    set_cursor(0x14, 10);
    print_text(m->name);
    print_text(S(0x340F));   /* " is " */
    print_text(S(dg.u4_is_avatar ? 0x3414 : 0x341F));   /* "an Avatar." / "not an Avatar" */
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
    print_text(S(m->sex == 0x0B ? 0x342D : 0x3432));   /* "Male" / "Female" */
    set_cursor(6, 10);
    if (dg.u4_is_avatar) print_text(S(0x3439));   /* "Avatar" */
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
    print_text(S(dg.u4_is_avatar ? 0x3440 : 0x3447));   /* "Avatar" / "Non-Avatar" */
    put_char(0xFB);

    /* the name */
    transfer_mark(2, 7, 0x3452, 1);   /* "Name:" */
    select_text_window(2);
    put_char(0xFF);
    set_cursor(0, 10);
    print_text(S(0x345E));   /* "Keep this name?" */
    int c = transfer_yes_no();
    put_char(0xFF);
    if (c == 'N')
        do {
            set_cursor(0, 1);
            print_text(S(0x346E));   /* "Enter new name: " */
            read_name(8, m->name);
        } while (m->name[0] == '\0' && !platform_quit_requested());
    select_text_window(1);
    set_cursor(3, 0);
    put_char(0xFC);
    print_text(m->name);
    put_char(0xFB);
    transfer_mark(2, 7, 0x347F, 0);

    /* the sex */
    transfer_mark(5, 5, 0x3485, 1);   /* "Sex:" */
    select_text_window(2);
    put_char(0xFF);
    put_char(0xFC);
    print_text(S(0x3495));   /* "Keep same sex?" */
    put_char(0xFB);
    c = transfer_yes_no();
    select_text_window(1);
    set_cursor(5, 10);
    if ((c == 'Y' && m->sex == 0x0B) || (c == 'N' && m->sex == 0x0C)) {
        print_text(S(0x34A4));   /* "Male" */
        m->sex = 0x0B;
    } else {
        print_text(S(0x34A9));   /* "Female" */
        m->sex = 0x0C;
    }
    transfer_mark(5, 5, 0x34B0, 0);

    /* the class */
    transfer_mark(6, 3, 0x34B5, 1);   /* "Class:" */
    select_text_window(1);
    set_cursor(6, 10);
    if (dg.u4_is_avatar) print_text(S(0x34BC));   /* "Avatar" */
    else print_ultima4_class();
    select_text_window(2);
    put_char(0xFF);
    set_cursor(0, 2);
    print_text(S(dg.u4_is_avatar ? 0x34CF : 0x34E7));   /* "Thou art now an Avatar:" / "Class remains intact" */
    wait_key_timeout(0);
    transfer_mark(6, 3, 0x34FC, 0);

    /* experience */
    transfer_mark(8, 5, 0x3503, 1);   /* "Exp:" */
    select_text_window(1);
    set_cursor(8, 10);
    m->xp = (uint16_t)((int16_t)m->xp / 10);
    print_number(' ', 1, (int16_t)m->xp);
    select_text_window(2);
    put_char(0xFF);
    put_char(0xFC);
    print_text(S(0x3514));   /* "Experience has been converted" */
    put_char(0xFB);
    wait_key_timeout(0);
    transfer_mark(8, 5, 0x3532, 0);

    /* level */
    transfer_mark(9, 3, 0x3537, 1);   /* "Level:" */
    select_text_window(1);
    set_cursor(9, 10);
    m->level = 1;
    for (int n = (int16_t)m->xp / 100; n > 0; n >>= 1) m->level++;
    m->hp = m->max_hp = (uint16_t)(m->level * 0x1E);
    print_number(' ', 1, m->level);
    select_text_window(2);
    put_char(0xFF);
    put_char(0xFC);
    print_text(S(0x354A));   /* "Level has been converted" */
    put_char(0xFB);
    wait_key_timeout(0);
    transfer_mark(9, 3, 0x3563, 0);

    /* the three attributes */
    static const struct { int row; uint16_t label, was, now, end; } attr[3] = {
        {0x0B, 0x356A, 0x357B, 0x358A, 0x3595},   /* "STR:", "Strength: was ", "(50), now ", "(30)" */
        {0x0C, 0x359F, 0x35AE, 0x35BE, 0x35C9},   /* "DEX:", "Dexterity: was " */
        {0x0D, 0x35D3, 0x35E2, 0x35F2, 0x35FD},   /* "INT:", "Intellect: was " */
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
        print_text(S(attr[i].was));
        print_number(' ', 1, old);
        print_text(S(attr[i].now));
        print_number(' ', 1, *stat);
        print_text(S(attr[i].end));
        wait_key_timeout(0);
        transfer_mark(attr[i].row, 5, attr[i].label, 0);
    }
    select_text_window(1);
    set_cursor(0x0F, 0);
    put_char(0xFC);
    print_text(S(dg.u4_is_avatar ? 0x360C : 0x3613));   /* "Avatar" / "Non-Avatar" */
    put_char(0xFB);
    select_text_window(2);
    define_text_window(0x16, 0x25, 0x15, 2, 2);
    put_char(0xFF);
    print_text(S(0x361E));   /* "\n\n Conversion complete, saving...\n" */
    save_file_part(0x200, ool, S(0x3641));                    /* SAVED.OOL: no Britannia objects, INIT.OOL for the Underworld */
    save_file_part(0x1060, ds_ptr(SAVE_OFFSET), S(0x364B));   /* SAVED.GAM */
    dg.install_type = 0;
    dg.location = location;
}

/* Loads SAVED.GAM over the save area and splits SAVED.OOL into BRIT.OOL and UNDER.OOL.
 * Returns 0 if there is no character. */
static int journey_onward(void)
{
    draw_screen_frame();
    set_wind_direction(0);
    select_text_window(2);
    set_cursor(0x0C, 0);
    if (dg.location == 0x40) {
        load_tileset(ds_str(dg.tileset_file));
        dg.location = 0x41;
    }
    load_file_part(0, 0x1060, ds_ptr(SAVE_OFFSET), ds_str(0x31E6));   /* SAVED.GAM */
    if (dg.party[0].name[0] != 0) {
        uint8_t ool[0x200] = {0};
        load_file_part(0, 0x200, ool, ds_str(0x323F));     /* SAVED.OOL */
        save_file_part(0x100, ool, ds_str(0x3249));        /* BRIT.OOL */
        save_file_part(0x100, ool + 0x100, ds_str(0x3252));   /* UNDER.OOL */
        set_wind_direction(-1);
        dg.tile_origin_y = 8;
        return 1;
    }
    print_text(ds_str(0x31F0));   /* "No active game. " */
    print_text(ds_str(0x3203));   /* "Please create a character " */
    print_text(ds_str(0x321E));   /* "or transfer one from Ultima IV. " */
    wait_key_timeout(0);
    select_text_window(0);
    put_char(0xFF);
    set_draw_page(1);
    put_char(0xFF);
    set_draw_page(0);
    show_title_screen(0);
    return 0;
}

static void back_to_title(void)
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
 * (dg.video_type). The menu waits about 200 cursor blinks, then starts the demo by itself. */
int intro_main(int skip_title)
{
    static const char keys[] = "JCTUAR";
    dg.sound_enabled = 1;
    reset_tile_anim_map();
    video_start();
    alloc_back_page();
    select_font(0);
    define_text_window(0x18, 0x27, 0, 0, 0);
    select_text_window(0);
    int go = 0;
    if (!skip_title) go = title_sequence();
    else dg.location = 0x40;
    if (platform_quit_requested()) return 0;
    show_title_screen((unsigned)go);
    if (go) attract_demo();
    set_random_seed((uint16_t)time_seed());

    for (;;) {
        if (platform_quit_requested()) return 0;
        define_text_window(0x17, 0x26, 0x10, 1, 0);
        select_text_window(0);
        put_char(0xFF);
        define_text_window(0x18, 0x27, 0, 0, 0);
        define_text_window(9, 0x27, 1, 0x18, 1);
        define_text_window(0x17, 0x27, 0x0B, 0x18, 2);
        int selected = 0;
        uint8_t choice;
        print_centered(ds_str(0x31C1));   /* "Copyright 1988 Lord British" */
        draw_main_menu(0);
        do {
            set_cursor(0x0F, 0x0F);
            draw_label_left();
            print_text(ds_str(0x31DD));   /* "Select: " */
            draw_label_right();
            set_cursor(0x0F, 0x17);
            unsigned k = 0;
            for (int i = 0; i < 200 && k == 0; i++) {
                k = to_upper((uint8_t)wait_key_cursor());
                if (k == 0) wait_reveal_strips();
                if (platform_quit_requested()) return 0;
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
            if (dg.location != 0x40) {
                free_tileset();
                dg.location = 0x40;
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
