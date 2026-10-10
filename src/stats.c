/* stats.c - inventory list helpers (ZSTATS) and resurrection (CAST2). */
#include "data.h"
#include "magic.h"
#include "combat.h"
#include "commands.h"
#include "endings.h"
#include "gfx.h"
#include "world.h"
#include "platform.h"
#include "stats.h"
#include "status.h"
#include "text.h"

int is_equipped_by(unsigned item, int member)
{
    PartyMember *m = &dg.party[member];
    return m->helm == item || m->body == item || m->left_hand == item || m->right_hand == item ||
           m->ring == item || m->amulet == item;
}

/* Owned (count non-zero), or equipped by member when member isn't 0xFF. */
int list_prev_item(int member, const uint8_t *counts, int size, int index)
{
    (void)size;
    while (--index >= 0)
        if (counts[index] || (member != 0xFF && is_equipped_by((unsigned)index, member))) return index;
    return -1;
}

int list_next_item(int member, const uint8_t *counts, int size, int index)
{
    while (++index < size)
        if (counts[index] || (member != 0xFF && is_equipped_by((unsigned)index, member))) return index;
    return -1;
}

/* "nn-Name" padded to column 14: count (or "--"), the marker (rune font below 0x20), the name.
 * Names starting with '*' / '!' get a rune prefix, '(' are moonstones with a rune letter. */
void print_list_entry(unsigned marker, uint16_t names, const uint8_t *counts, int index)
{
    uint8_t n = counts[index];
    if (n != 0xFF) {
        if (n == 0) print_text(ds_str(0x9778));   /* "--" */
        else print_number(' ', 2, n);
        if (marker < 0x20) select_font(1);
        put_char((uint8_t)marker);
        select_font(0);
    }
    const char *name = ds_str(ds_u16((uint16_t)(names + index * 2)));
    if (*name == '*') {
        select_font(1);
        print_text(ds_str(0x977C));
        print_text(name + 1);
        select_font(0);
    } else if (*name == '!') {
        select_font(1);
        print_text(ds_str(0x9782));
        select_font(0);
        print_text(ds_str(ds_u16((uint16_t)(0x19B2 + index * 2))));
    } else if (*name == '(') {
        print_text(ds_str(0x9788));   /* "Moonstone " */
        select_font(1);
        put_char((uint8_t)name[1]);
        select_font(0);
    } else {
        print_text(name);
    }
    for (int col = get_cursor_col(); col < 0x0E; col++) put_char(' ');
    put_char('\n');
}

/* Back to 'G' with 1 HP; MP from INT (bards half). Karma below 98 costs XP (xp * karma / 100);
 * the level is recomputed from XP and max HP set to level * 30. */
int resurrect_member(int from_scroll, int member)
{
    if (member < 0) return -1;
    PartyMember *m = &dg.party[member];
    if (m->status != 'D') {
        if (from_scroll) print_text(ds_str(0x953C));   /* "Not dead!" */
        return 0;
    }
    m->status = 'G';
    m->hp = 1;
    if (m->cls == 'A' || m->cls == 'M') m->mp = m->intel;
    else if (m->cls == 'B') m->mp = m->intel >> 1;
    if (dg.karma < 0x62) m->xp = (uint16_t)((int32_t)(int16_t)m->xp * dg.karma / 100);
    int level = 1;
    for (int v = (int16_t)m->xp / 100; v > 0; v >>= 1) level++;
    m->level = (uint8_t)level;
    m->max_hp = (uint16_t)(level * 0x1E);
    if (from_scroll == 1) spell_effect(6);
    else if (from_scroll == 0) spell_effect(8);   /* (other callers, like the healer, have their own effect) */
    dg.status_dirty = 1;
    return 1;
}

/* The Use list: counts (or 0xFF = owned, shown without a count) of the 38 special items. */
void collect_special_items(void)
{
    uint8_t *l = dg.special_item_list;
    for (int i = 0; i < 8; i++) {
        l[i] = dg.scrolls[i];
        l[8 + i] = dg.potions[i];
        l[0x15 + i] = dg.moonstone_loc[i] == 0xFF ? 0xFF : 0;
    }
    l[0x10] = dg.magic_carpets;
    l[0x11] = dg.odd_keys;
    l[0x12] = dg.lb_amulet;
    l[0x13] = dg.lb_crown;
    l[0x14] = dg.lb_sceptre;
    l[0x1D] = dg.shards[0];
    l[0x1E] = dg.shards[1];
    l[0x1F] = dg.shards[2];
    l[0x20] = dg.quest_item_57ba;                 /* spyglass */
    l[0x21] = dg.hms_cape_plans == 0 ? 0 : 0xFF;
    l[0x22] = dg.quest_item_57bc;                 /* sextant */
    l[0x23] = ds_u8(0x57BD);                      /* pocket watch */
    l[0x24] = dg.quest_item_57be;                 /* black badge */
    l[0x25] = dg.sandalwood_box;
}

/* Clears the status window and draws the list's box, top_row rows high, in frame characters. */
void set_list_window(int top_row)
{
    define_text_window((uint8_t)(top_row + 1), 0x26, 1, 0x18, 1);
    put_char(0xFF);
    define_text_window(9, 0x27, 1, 0x18, 1);
    put_char(0x10);
    for (int i = 0x0D; i != 0; i--) put_char(0x11);
    put_char(0x13);
    for (int row = 1; row != top_row; row++) {
        set_cursor(row, 0);
        put_char(0x17);
        set_cursor(row, 0x0E);
        put_char(0x17);
    }
    put_char('\n');
    put_char(0x14);
    for (int i = 0x0D; i != 0; i--) put_char(0x15);
    put_char(0x16);
}

/* The scrolling list of Ready ('R': the equipment, with a mark on what the member wears) and
 * Use ('U': the special items). Seven rows show; the bar stays on row 4 while the list can
 * scroll. Arrows move, PgUp / PgDn by 7, Home / End jump; Return or Space picks (Ready then
 * readies the item and goes on until Esc); Esc leaves. Returns the item, or -1. */
int ready_item_list(int mode, int member, int first)
{
    int done = 0, sel_row = 1, result = -1, item = -1, shown = 0;
    const uint8_t *counts = mode == 'R' ? dg.inventory : dg.special_item_list;
    int size = mode == 'R' ? 0x30 : 0x26;
    while (!done) {
        select_text_window(1);
        set_cursor(1, 1);
        shown = 0;
        for (item = first; item != -1; item = list_next_item(member, counts, size, item)) {
            shown++;
            if (get_cursor_row() == sel_row) {
                put_char(0xFD);
                result = item;
            }
            if (mode == 'R')
                print_list_entry(is_equipped_by((unsigned)item, member) ? ds_u8((uint16_t)(0x1AE8 + item)) : ' ',
                                 (uint16_t)offsetof(struct DGroup, item_short_names), counts, item);
            else
                print_list_entry(' ', 0x1916, counts, item);
            set_cursor(get_cursor_row(), 1);
            if (get_cursor_row() - 1 == sel_row) put_char(0xFD);
            if (get_cursor_row() == 8) break;
        }
        int more = 0;
        if (list_prev_item(member, counts, size, first) != -1) more = 2;
        if (item != -1 && list_next_item(member, counts, size, item) != -1) more++;
        if (more == 0) draw_list_header_bar();
        else draw_list_marker(more == 1 ? 0x19 : more == 2 ? 0x18 : 0x12);   /* down, up, both */
        select_text_window(2);
        int key = get_command_key();
        if (platform_quit_requested()) key = 0x1B;
        switch (key) {
        case 0x0D: case ' ':
            done = mode == 'R' ? ready_item(result, member) : 1;
            break;
        case 1: case 3: case 0xD5:
            for (int n = key == 0xD5 ? 7 : 1; n != 0; n--) {
                if (sel_row == 4 || sel_row == 1) {
                    int prev = list_prev_item(member, counts, size, first);
                    if (prev != -1) first = prev;
                    else if (sel_row == 4) sel_row--;
                } else {
                    sel_row--;
                }
            }
            break;
        case 2: case 4: case 0xD6:
            for (int n = key == 0xD6 ? 7 : 1; n != 0; n--) {
                if (sel_row == 4 || sel_row == 7 || shown <= sel_row) {
                    if (item == -1 || list_next_item(member, counts, size, item) == -1) {
                        if (sel_row == 4 && shown > 4) sel_row++;
                    } else {
                        first = list_next_item(member, counts, size, first);
                        item = list_next_item(member, counts, size, item);
                    }
                } else {
                    sel_row++;
                }
            }
            break;
        case 0x1B:
            print_text(ds_str(mode == 'R' ? 0x9970 : 0x9976));   /* "Done\n" / "None!\n" */
            done = 1;
            result = -1;
            break;
        case 0xD3:
            first = list_next_item(member, counts, size, -1);
            sel_row = 1;
            break;
        case 0xD4:
            first = list_prev_item(member, counts, size, size);
            sel_row = 1;
            for (;;) {
                int prev = list_prev_item(member, counts, size, first);
                if (prev == -1) break;
                sel_row++;
                first = prev;
                if (sel_row >= 7) break;
            }
            break;
        }
    }
    return result;
}

/* ---- Ztats ---- */

#define S(off) ds_str(off)

/* In combat it is the member whose turn it is. -1 none, -2 the whole party (allow_party). */
int ask_player_number(int allow_party)
{
    int member;
    if (dg.location < 0x81 || !(dg.combatants[dg.combat_turn & 0x1F].flags & 0x80)) {
        print_text(S(0x96B4));   /* "Player: " */
        member = select_member(allow_party);
        if (member > -1) {
            print_text(dg.party[member].name);
            if (get_cursor_col() != 0) put_char('\n');
        }
    } else {
        member = dg.combatants[dg.combat_turn & 0x1F].kind;
    }
    if (member == -1) print_text(S(0x96BE));   /* "None!\n" */
    else if (member == -2) put_char('\n');
    return member;
}

static void clear_status_window(void)
{
    define_text_window(9, 0x26, 1, 0x18, 1);
    put_char(0xFF);
    define_text_window(9, 0x27, 1, 0x18, 1);
}

/* Sex sign, level and class (centred by the indents at ds:1A58), the state in words, then the
 * attributes, hit points, experience and magic points. */
void show_member_stats(int member)
{
    PartyMember *m = &dg.party[member];
    draw_list_header_bar();
    draw_panel_title(m->name);
    clear_status_window();
    int cls = str_index((uint8_t)m->cls, S(0x96C6));       /* "AMBFDTPRS" */
    int state = str_index((uint8_t)m->status, S(0x96D0));  /* "GPDSC" */
    for (int i = 0; i < (int16_t)ds_u16((uint16_t)(0x1A58 + cls * 2)); i++) put_char(' ');
    put_char(m->sex);
    print_text(S(0x96D6));   /* " Lv-" */
    print_number(' ', 1, m->level);
    put_char(' ');
    print_text(S(ds_u16((uint16_t)(0x1A44 + cls * 2))));
    define_text_window(9, 0x26, 1, 0x18, 1);
    set_cursor(1, 0);
    put_char(0xFC);
    print_text(S(ds_u16((uint16_t)(0x1A6A + state * 2))));   /* Good Health, Poisoned, Dead, Asleep, Charmed */
    define_text_window(9, 0x27, 1, 0x18, 1);
    set_cursor(3, 0);
    put_char(0xFB);
    print_text(S(0x96DC));   /* "Str=" */
    print_number('0', 2, m->str);
    print_text(S(0x96E2));   /* "  HP:" */
    print_number(' ', 4, (int16_t)m->hp);
    print_text(S(0x96E8));   /* "\nInt=" */
    print_number('0', 2, m->intel);
    print_text(S(0x96EE));   /* "  HM:" */
    print_number(' ', 4, (int16_t)m->max_hp);
    print_text(S(0x96F4));   /* "\nDex=" */
    print_number('0', 2, m->dex);
    print_text(S(0x96FA));   /* "  Ex:" */
    print_number(' ', 4, (int16_t)m->xp);
    print_text(S(0x9700));   /* "\n\n    Magic:" */
    print_number(' ', 2, m->mp);
}

int print_equipped_name(int item)
{
    if (item == 0xFF) return 0;
    put_char(' ');
    print_text(S(ds_u16((uint16_t)(offsetof(struct DGroup, item_short_names) + item * 2))));
    put_char('\n');
    return 1;
}

void show_member_arms(int member)
{
    PartyMember *m = &dg.party[member];
    draw_list_header_bar();
    draw_panel_title(m->name);
    define_text_window(9, 0x26, 1, 0x18, 1);
    put_char(0xFF);
    put_char(0xFC);
    put_char(0xFE);
    print_text(S(0x970E));   /* "Arms\n\n" */
    put_char(0xFB);
    put_char(0xFE);
    int n = print_equipped_name(m->helm);
    n += print_equipped_name(m->body);
    n += print_equipped_name(m->left_hand);
    n += print_equipped_name(m->right_hand);
    n += print_equipped_name(m->ring);
    n += print_equipped_name(m->amulet);
    if (n == 0) {
        set_cursor(4, 0);
        put_char(0xFC);
        print_text(S(0x9716));   /* "(None ready)" */
        put_char(0xFB);
    }
    define_text_window(9, 0x27, 1, 0x18, 1);
}

void show_equipment_page(void)
{
    draw_list_header_bar();
    draw_panel_title(S(0x9724));   /* "Equipment" */
    clear_status_window();
    print_text(S(0x972E));   /* "\n Food: " */
    print_number(' ', 4, (int16_t)dg.food);
    print_text(S(0x9738));   /* "\n Gold: " */
    print_number(' ', 4, (int16_t)dg.gold);
    print_text(S(0x9742));   /* "\n\n Keys......." */
    print_number(' ', 2, dg.keys);
    print_text(S(0x9752));   /* "\n Gems......." */
    print_number(' ', 2, dg.gems);
    print_text(S(0x9760));   /* "\n Torches...." */
    print_number(' ', 2, dg.torches);
    if (dg.grapples != 0) print_text(S(0x976E));   /* "\n Grapple" */
}

/* A list page of Ztats: up / down scroll a line, PgUp / PgDn seven, Home / End jump. Any key
 * that turns the page or ends Ztats (left, right, 0-6, Space, Esc) is returned. */
int browse_item_list(uint16_t names, const uint8_t *counts, int size, const char *title)
{
    draw_panel_title(title);
    set_list_window(8);
    int first = list_next_item(0xFF, counts, size, -1), key;
    if (first == -1) {
        set_list_window(8);
        set_cursor(4, 1);
        print_text(S(0x9794));   /* "(None owned!)" */
        draw_list_header_bar();
        select_text_window(2);
        return get_command_key();
    }
    for (;;) {
        int item;
        select_text_window(1);
        set_cursor(1, 1);
        for (item = first; item != -1; item = list_next_item(0xFF, counts, size, item)) {
            print_list_entry('-', names, counts, item);
            set_cursor(get_cursor_row(), 1);
            if (get_cursor_row() == 8) break;
        }
        int more = 0;
        if (list_prev_item(0xFF, counts, size, first) != -1) more = 2;
        if (item != -1 && list_next_item(0xFF, counts, size, item) != -1) more++;
        if (more == 0) draw_list_header_bar();
        else draw_list_marker(more == 1 ? 0x19 : more == 2 ? 0x18 : 0x12);
        select_text_window(2);
        key = get_command_key();
        if (platform_quit_requested()) return 0x1B;
        if (key == ' ' || key == 0x1B || key == 1 || key == 2 || (key >= '0' && key <= '6')) return key;
        if (key == 3 || key == 0xD5) {
            for (int n = key == 0xD5 ? 7 : 1; n > 0; n--) {
                int prev = list_prev_item(0xFF, counts, size, first);
                if (prev != -1) first = prev;
            }
        } else if (key == 4 || key == 0xD6) {
            for (int n = key == 0xD6 ? 7 : 1; n > 0; n--)
                if (item != -1 && list_next_item(0xFF, counts, size, item) != -1) {
                    first = list_next_item(0xFF, counts, size, first);
                    item = list_next_item(0xFF, counts, size, item);
                }
        } else if (key == 0xD3) {
            first = list_next_item(0xFF, counts, size, -1);
        } else if (key == 0xD4) {
            first = list_prev_item(0xFF, counts, size, size);
            for (int n = 0; n < 6; n++) {
                int prev = list_prev_item(0xFF, counts, size, first);
                if (prev == -1) break;
                first = prev;
            }
        }
    }
}

/* Pages: two per member (stats, arms), then 12 Equipment, 13 Reagents, 14 Spells, 15 Items,
 * 16 Armaments. Left / up and right / down turn the pages (the members' pages and the party's
 * join in a ring), 1-6 jump to a member, 0 to Equipment; Space or Esc ends. */
void cmd_zstats(void)
{
    int member = ask_player_number(1);
    if (member == -2) member = 6;
    else if (member < 0) return;
    print_text(S(0x97A2));   /* "\nStatus: " */
    collect_special_items();
    clear_side_panel();
    int page = member * 2, last = dg.party_size * 2 - 1;
    uint8_t key = 0;
    while (key != ' ' && key != 0x1B) {
        if (key == 3 || key == 1) {
            if (page == 0x0C) page = last;
            else if (page < 1) page = 0x10;
            else page--;
        } else if (key == 2 || key == 4) {
            if (page == last) page = 0x0C;
            else if (page < 0x10) page++;
            else page = 0;
        } else if (key == '0') {
            page = 0x0C;
        } else if (key > '0' && key < '7' && (unsigned)(key - '1') < dg.party_size) {
            page = (key - '1') * 2;
        }
        select_text_window(1);
        if (page <= 0x0C) {
            if (page == 0x0C) show_equipment_page();
            else if (page & 1) show_member_arms(page >> 1);
            else show_member_stats(page >> 1);
            select_text_window(2);
            key = (uint8_t)get_command_key();
        } else if (page == 0x0D) {
            key = (uint8_t)browse_item_list((uint16_t)offsetof(struct DGroup, reagent_names), dg.reagents, 8, S(0x97AC));   /* "Reagents" */
        } else if (page == 0x0E) {
            key = (uint8_t)browse_item_list((uint16_t)offsetof(struct DGroup, spell_names), dg.spells, 0x30, S(0x97B6));    /* "Spells" */
        } else if (page == 0x0F) {
            key = (uint8_t)browse_item_list(0x1916, dg.special_item_list, 0x26, S(0x97BE));                                  /* "Items" */
        } else {
            key = (uint8_t)browse_item_list((uint16_t)offsetof(struct DGroup, item_short_names), dg.inventory, 0x30, S(0x97C4));   /* "Armaments" */
        }
        if (platform_quit_requested()) break;
    }
    draw_list_header_bar();
    draw_panel_top_bar();
    draw_panel_bottom_bar();
    draw_status_bar();
    print_text(S(0x97CE));   /* "Done\n" */
}

/* ---- Ready ---- */

void print_item_prompt(const char *text)
{
    print_text(S(0x97D4));   /* "\n\n" */
    print_text(text);
    print_text(S(0x97D8));   /* "\n\nItem: " */
}

#define ITEM_HANDS_TABLE 0x1A7E   /* item_hands */
#define ITEM_HANDS(i) ds_u8((uint16_t)(ITEM_HANDS_TABLE + (i)))

/* 2 both hands free, 0 the left is free, 1 only the right (and the left doesn't hold a
 * two-handed item), 0xFF none. */
int free_hands(int member)
{
    PartyMember *m = &dg.party[member];
    if (m->left_hand == 0xFF && m->right_hand == 0xFF) return 2;
    if (m->left_hand == 0xFF) return 0;
    if (m->right_hand == 0xFF && ITEM_HANDS(m->left_hand) != 0x30) return 1;
    return 0xFF;
}

/* Picking a worn item takes it off; otherwise it is put on if its slot is free and the member
 * can carry it: the weights (ds:1AAE) of everything worn must not exceed strength. The slot
 * comes from the item's class: 0x80 helm, 0x40 body, 0x20 one hand, 0x30 both hands, 2 ring,
 * 4 amulet. Arrows and quarrels (0x1B, 0x1D) can't be readied, and bows need them. Armour can't
 * be changed in a fight before it is won. A ring of invisibility or regeneration has 1 chance
 * in 16 to vanish when put on (returns 1: the list closes). */
int ready_item(int item, int member)
{
    PartyMember *m = &dg.party[member];
    MapObject *o = NULL;
    if (dg.location > 0x7F) o = &dg.objects[dg.combatants[dg.combat_turn & 0x1F].object & 0x1F];
    if (item < 0 || item == 0x1B || item == 0x1D) return 0;
    if (item > 8 && item < 0x10 && dg.location > 0x7F && dg.combat_victory == 0) {
        print_item_prompt(S(0x97E2));   /* "Thou canst not change armour in heated battle!" */
        return 0;
    }
    if (is_equipped_by((unsigned)item, member)) {
        unequip_item((unsigned)item, member);
        if (dg.inventory[item] < 99) dg.inventory[item]++;
        if (o && item == 0x2A)   /* visible again */
            o->tile = o->tile2 = ds_u8((uint16_t)(0x1ADE + str_index((uint8_t)m->cls, S(0x9812))));
        return 0;
    }
    if (((item == 0x1A || item == 0x24) && dg.inventory[0x1B] == 0) || (item == 0x1C && dg.inventory[0x1D] == 0)) {
        print_item_prompt(S(0x981C));   /* "Thou hast no ammunition for that weapon!" */
        return 0;
    }
    unsigned load = 0;
    const uint8_t *worn = &m->helm;
    for (int i = 0; i < 6; i++)
        if (worn[i] != 0xFF) load += ds_u8((uint16_t)(0x1AAE + worn[i]));
    uint8_t *slot = NULL;
    switch (ITEM_HANDS(item)) {
    case 2:
        if (m->ring != 0xFF) { print_item_prompt(S(0x9916)); return 0; }   /* "Only one magic ring may be worn at a time!" */
        slot = &m->ring;
        break;
    case 4:
        if (m->amulet != 0xFF) { print_item_prompt(S(0x98F0)); return 0; } /* "Thou must remove thine other amulet!" */
        slot = &m->amulet;
        break;
    case 0x20: {
        int hand = free_hands(member);
        if (hand == 0xFF) { print_item_prompt(S(0x9892)); return 0; }      /* "Thou must free one of thy hands first!" */
        slot = hand == 1 ? &m->right_hand : &m->left_hand;
        break;
    }
    case 0x30:
        if (free_hands(member) != 2) { print_item_prompt(S(0x98BA)); return 0; }   /* "Both hands must be free before thou canst wield that!" */
        slot = &m->left_hand;
        break;
    case 0x40:
        if (m->body != 0xFF) { print_item_prompt(S(0x9866)); return 0; }   /* "Thou must first remove thine other armour!" */
        slot = &m->body;
        break;
    case 0x80:
        if (m->helm != 0xFF) { print_item_prompt(S(0x9846)); return 0; }   /* "Remove first thy present helm!" */
        slot = &m->helm;
        break;
    }
    if (ds_u8((uint16_t)(0x1AAE + item)) + load > m->str) {
        print_item_prompt(S(0x9942));   /* "Thou art not strong enough!" */
        return 0;
    }
    if (!slot) return 0;   /* (an item of no class: the original writes through a stale pointer) */
    *slot = (uint8_t)item;
    dg.inventory[item]--;
    if ((item == 0x2A || item == 0x2C) && random_between(0x0F, 0) == 0) {
        print_text(S(0x995E));   /* "\n\nRing vanishes!\n" */
        m->ring = 0xFF;
        sound_slide(0x28, 1, 2000, 0x4B0);
        return 1;
    }
    if (item == 0x2A && o) o->tile = o->tile2 = 0x1D;
    return 0;
}

void cmd_ready(void)
{
    int member = ask_player_number(0);
    if (member < 0) return;
    int first = list_next_item(member, dg.inventory, 0x30, -1);
    if (first == -1) {
        print_text(S(0x997E));   /* "Thou art empty-\nhanded!\n" */
        return;
    }
    print_text(S(0x9998));   /* "Item: " */
    clear_side_panel();
    select_text_window(1);
    draw_panel_title(dg.party[member].name);
    set_list_window(8);
    ready_item_list('R', member, first);
    draw_list_header_bar();
    draw_panel_top_bar();
    draw_panel_bottom_bar();
    draw_status_bar();
}
