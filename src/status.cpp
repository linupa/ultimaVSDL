/* status.cpp - the party panel and panel title bars. */
#include <string.h>
#include "combat.h"
#include "commands.h"
#include "gametime.h"
#include "gfx.h"
#include "input.h"
#include "map.h"
#include "misc.h"
#include "party.h"
#include "status.h"
#include "strings.h"
#include "text.h"

Status &Status::instance()
{
    static Status the_status;
    return the_status;
}

Status &status = Status::instance();

int Status::check_party_state(void)
{
    int asleep = 0;
    for (unsigned i = 0; i < party.size(); i++) {
        char st = party[i].status;
        if (st == 'G' || st == 'P') return 0;
        if (st == 'S') asleep++;
    }
    return asleep ? 1 : -1;
}

int Status::is_combat_turn_of(unsigned member)
{
    Combatant *c = &combat.combatants[combat.turn];
    return map.in_scene() && combat.turn != 0xFF && (c->flags & 0x80) && c->kind == member;
}

/* "Name      HHHS": name padded to 9, an arrow (0x1A) for the active player, HP in 4, status. In
 * combat a marker (0xFD toggles inverse) frames the member whose turn it is. */
void Status::draw_member_status_row(unsigned member)
{
    set_cursor((int)member, 0);
    if (member >= party.size()) {
        for (int i = 0; i < 15; i++) put_char(' ');
        return;
    }
    PartyMember *m = &party[member];
    int turn = is_combat_turn_of(member);
    if (turn) put_char(0xFD);
    print_text(m->name);
    for (int n = (int)strlen(m->name); n < 9; n++) put_char(' ');
    if (member == party.active_player) {
        if (m->dead() || m->asleep()) {
            put_char(' ');
            party.active_player = 0xFF;
        } else {
            put_char(0x1A);
        }
    } else {
        put_char(' ');
    }
    print_number(' ', 4, (int16_t)m->hp);
    if (!map.in_scene()) {
        put_char((uint8_t)m->status);
        return;
    }
    Combatant *c = &combat.combatants[member];
    put_char((c->flags & 0xE1) == 0x81 && c->kind == member ? 'C' : (uint8_t)m->status);
    if (turn) put_char(0xFD);
}

void Status::draw_gold(void)
{
    WindowScope scope(1);
    set_cursor(7, 8);
    if ((int16_t)party.gold < 1000) put_char(' ');
    if ((int16_t)party.gold < 100) put_char(' ');
    if ((int16_t)party.gold < 10) put_char(' ');
    print_text(strings[Strings::G_54B6]);   /* " G:" */
    print_number(' ', 1, (int16_t)party.gold);
    while (get_cursor_col() <= 0x0E) put_char(' ');
}

void Status::draw_status_bar(void)
{
    select_text_window(1);
    for (unsigned m = 0; m < 6; m++) draw_member_status_row(m);
    set_cursor(7, 0);
    print_text(strings[Strings::F_54BA]);   /* "F:" */
    print_number(' ', 1, (int16_t)party.food);
    while (get_cursor_col() <= 7) put_char(' ');
    if (!map.in_scene() && (party.transport & 0xF8) == 0x20) {
        print_text(strings[Strings::Ship_54BD]);   /* "Ship:" */
        print_number(' ', 1, map.objects[0].hull);
        if (map.objects[0].hull < 10) put_char(' ');
    } else {
        draw_gold();
    }
    print_text(strings[Strings::NL_SP_SP_SP_54C3]);
    if (game_time.month < 10 && game_time.day < 10) put_char(' ');
    print_number(' ', 1, game_time.month);
    put_char('-');
    print_number(' ', 1, game_time.day);
    put_char('-');
    print_number('0', 3, (int16_t)game_time.year);
    while (get_cursor_col() <= 0x0E) put_char(' ');
    set_cursor(6, 6);
    if (party.active_spell == 0) {
        draw_panel_bottom_bar();
    } else {
        draw_label_left();
        put_char(party.active_spell);
        draw_label_right();
    }
    select_text_window(2);
}

void Status::highlight_member_row(int member)
{
    set_draw_color(gfx.color_text);
    xor_fill_rect(member * 8 + 0x0F, 0x137, member * 8 + 8, 0xC0);
}

/* Digits 1-6 pick directly; arrows move the bar; Return/Space choose, Esc = none (-1), 0 = all
 * (-2) when allowed. Keys above '7' are ignored. */
int Status::select_member(int allow_all)
{
    uint16_t sel = 0, shown = 0xFFFF;
    int done = 0;
    if (map.in_scene() && (combat.combatants[combat.turn].flags & 0x80))
        highlight_member_row(combat.combatants[combat.turn].kind);
    draw_panel_title(strings[Strings::Select_5554]);   /* "Select:" */
    for (;;) {
        if (shown != sel) {
            highlight_member_row(sel);
            shown = sel;
        }
        unsigned key;
        do {
            key = (unsigned)get_command_key();
            if (platform->quit_requested()) key = 0x1B;
        } while (key > 0x37);
        if (key >= '1' && key <= '6' && key - '1' < party.size()) {
            sel = (uint16_t)(key - '1');
        } else if (key == 4 || key == 2) {   /* down / right */
            add_word_capped((uint16_t)(party.size() - 1), 1, &sel);
        } else if (key == 1 || key == 3) {   /* left / up */
            sub_word_floor0(1, &sel);
        } else if (key == 0x0D || key == ' ') {
            done = 1;
        } else if (key == 0x1B) {
            sel = 0xFFFF;
            done = 1;
        } else if (key == '0' && allow_all) {
            sel = 0xFFFE;
            done = 1;
        }
        if (shown != sel) highlight_member_row(shown);
        if (done) {
            if ((int16_t)sel >= 0) highlight_member_row(sel);
            draw_panel_top_bar();
            return (int16_t)sel;
        }
    }
}

int Status::ask_member_number(void) { return select_member(0); }

void Status::regen_ring_tick(void)
{
    for (unsigned i = 0; i < party.size(); i++) {
        PartyMember *m = &party[i];
        if (!m->dead() && m->ring == ',' && random_between(7, 0) == 7) {
            add_word_capped(m->max_hp, 1, &m->hp);
            dirty = true;
        }
    }
}

/* The caps of a label box: glyph 2 / 1 in the frame colour, plus two slanted lines. */
void Status::label_cap(uint8_t glyph, int left)
{
    TextWindow *w = &text_windows.windows[text_windows.cur_window_index];
    int x = (w->left + get_cursor_col()) * 8, y = (w->top + get_cursor_row()) * 8;
    uint8_t attr = w->attr;
    set_text_color((uint8_t)gfx.color_frame);
    set_text_bg_color(0);
    put_char(glyph);
    set_draw_color(gfx.color_text);
    if (left) {
        draw_line(y + 3, x + 5, y, x);
        draw_line(y + 7, x, y + 4, x + 5);
    } else {
        draw_line(y + 3, x + 2, y, x + 7);
        draw_line(y + 7, x + 7, y + 4, x + 2);
    }
    set_text_color(attr & 0x0F);
    set_text_bg_color((uint8_t)(attr >> 4));
}

void Status::draw_label_left(void) { label_cap(2, 1); }
void Status::draw_label_right(void) { label_cap(1, 0); }

void Status::draw_panel_top_bar(void)
{
    set_draw_color(gfx.color_frame);
    fill_rect(6, 0x137, 0, 0xC0);
    set_draw_color(gfx.color_text);
    draw_hline(0x137, 7, 0xC0);
}

void Status::draw_panel_title(const char *title)
{
    int len = (int)strlen(title);
    int col = 0x1E - len / 2;
    set_draw_color(gfx.color_frame);
    fill_rect(6, col * 8, 0, 0xC0);
    int right = (len + col + 2) * 8;
    fill_rect(6, 0x137, 0, right);
    set_draw_color(gfx.color_text);
    draw_hline(col * 8, 7, 0xC0);
    draw_hline(0x137, 7, right);
    WindowScope scope(0);
    set_cursor(0, col);
    draw_label_left();
    print_text(title);
    draw_label_right();
}

void Status::clear_side_panel(void)
{
    set_draw_color(gfx.color_text);
    draw_vline(0x3F, 0x38, 0xBF);
    draw_vline(0x3F, 0x38, 0x138);
    set_draw_color(0);
    fill_rect(0x3F, 0x137, 0x38, 0xC0);
}

void Status::draw_panel_bottom_bar(void)
{
    set_draw_color(gfx.color_frame);
    fill_rect(0x3E, 0x138, 0x39, 0xBF);
    set_draw_color(gfx.color_text);
    draw_hline(0x137, 0x38, 0xC0);
    draw_hline(0x137, 0x3F, 0xC0);
}

/* The bar under the status window, plain. */
void Status::draw_list_header_bar(void)
{
    set_draw_color(gfx.color_frame);
    fill_rect(0x56, 0x107, 0x51, 0xF0);
    set_draw_color(gfx.color_text);
    draw_hline(0x107, 0x50, 0xF0);
    draw_hline(0x107, 0x57, 0xF0);
}

/* An arrow character on that bar: the list goes on. */
void Status::draw_list_marker(uint8_t c)
{
    WindowScope scope(0);
    set_cursor(10, 0x1E);
    draw_label_left();
    put_char(c);
    draw_label_right();
}
