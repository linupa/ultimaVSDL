/* stats.cpp - inventory list helpers (ZSTATS) and resurrection (CAST2). */
#include "gamedata.h"
#include "items.h"
#include "map.h"
#include "magic.h"
#include "combat.h"
#include "commands.h"
#include "endings.h"
#include "gfx.h"
#include "misc.h"
#include "party.h"
#include "spells.h"
#include "strings.h"
#include "world.h"
#include "platform.h"
#include "stats.h"
#include "status.h"
#include "text.h"

int is_equipped_by(unsigned item, int member)
{
    PartyMember *m = &party[member];
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
void print_list_entry(unsigned marker, NameTable names, const uint8_t *counts, int index)
{
    uint8_t n = counts[index];
    if (n != 0xFF) {
        if (n == 0) print_text(strings[Strings::Dash_Dash_9778]);   /* "--" */
        else print_number(' ', 2, n);
        if (marker < 0x20) select_font(1);
        put_char((uint8_t)marker);
        select_font(0);
    }
    const char *name = strings[names(index)];
    if (*name == '*') {
        select_font(1);
        print_text(strings[Strings::Up_SP_Plus_SP_977C]);
        print_text(name + 1);
        select_font(0);
    } else if (*name == '!') {
        select_font(1);
        print_text(strings[Strings::Down_SP_Plus_SP_9782]);
        select_font(0);
        print_text(items.short_name((unsigned)(0x28 + index)));   /* the colour: ds:19B2 is short name 0x28 */
    } else if (*name == '(') {
        print_text(strings[Strings::Moonstone_9788]);   /* "Moonstone " */
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
    PartyMember *m = &party[member];
    if (!m->dead()) {
        if (from_scroll) print_text(strings[Strings::Not_dead_953C]);   /* "Not dead!" */
        return 0;
    }
    m->status = 'G';
    m->hp = 1;
    if (m->cls == 'A' || m->cls == 'M') m->mp = m->intel;
    else if (m->cls == 'B') m->mp = m->intel >> 1;
    if (party.karma < 0x62) m->xp = (uint16_t)((int32_t)(int16_t)m->xp * party.karma / 100);
    int level = 1;
    for (int v = (int16_t)m->xp / 100; v > 0; v >>= 1) level++;
    m->level = (uint8_t)level;
    m->max_hp = (uint16_t)(level * 0x1E);
    if (from_scroll == 1) spell_effect(6);
    else if (from_scroll == 0) spell_effect(8);   /* (other callers, like the healer, have their own effect) */
    status.dirty = true;
    return 1;
}

/* The Use list: counts (or 0xFF = owned, shown without a count) of the 38 special items. */
void collect_special_items(void)
{
    uint8_t *l = items.special_list;
    for (int i = 0; i < 8; i++) {
        l[i] = party.scrolls[i];
        l[8 + i] = party.potions[i];
        l[0x15 + i] = map.moonstone_loc[i] == 0xFF ? 0xFF : 0;
    }
    l[0x10] = party.magic_carpets;
    l[0x11] = party.odd_keys;
    l[0x12] = party.lb_amulet;
    l[0x13] = party.lb_crown;
    l[0x14] = party.lb_sceptre;
    l[0x1D] = party.shards[0];
    l[0x1E] = party.shards[1];
    l[0x1F] = party.shards[2];
    l[0x20] = party.spyglass;                 /* spyglass */
    l[0x21] = party.hms_cape_plans == 0 ? 0 : 0xFF;
    l[0x22] = party.sextant;                 /* sextant */
    l[0x23] = party.pocket_watch;                      /* pocket watch */
    l[0x24] = party.black_badge;                 /* black badge */
    l[0x25] = party.sandalwood_box;
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
    const uint8_t *counts = mode == 'R' ? party.inventory : items.special_list;
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
                print_list_entry(is_equipped_by((unsigned)item, member) ? items.ready_mark((uint8_t)item) : ' ',
                                 Items::short_name_of, counts, item);
            else
                print_list_entry(' ', Items::special_name_of, counts, item);
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
        if (platform->quit_requested()) key = 0x1B;
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
            print_text(strings[mode == 'R' ? Strings::Done_9970 : Strings::None_9976]);   /* "Done\n" / "None!\n" */
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


/* In combat it is the member whose turn it is. -1 none, -2 the whole party (allow_party). */
int ask_player_number(int allow_party)
{
    int member;
    if (map.id() < 0x81 || !(combat.combatants[combat.turn & 0x1F].flags & 0x80)) {
        print_text(strings[Strings::Player_96B4]);   /* "Player: " */
        member = select_member(allow_party);
        if (member > -1) {
            print_text(party[member].name);
            if (get_cursor_col() != 0) put_char('\n');
        }
    } else {
        member = combat.combatants[combat.turn & 0x1F].kind;
    }
    if (member == -1) print_text(strings[Strings::None_96BE]);   /* "None!\n" */
    else if (member == -2) put_char('\n');
    return member;
}

static void clear_status_window(void)
{
    define_text_window(9, 0x26, 1, 0x18, 1);
    put_char(0xFF);
    define_text_window(9, 0x27, 1, 0x18, 1);
}

/* From DATA.OVL, by class ("AMBFDTPRS", 9 = none): its name (ds:1A44) and the spaces before
 * the sex that centre the line (ds:1A58); and the health states "GPDSC" (ds:1A6A; the sixth,
 * none of them, reads the next table's first word as the original does). */
static Strings::Id class_names[10], health_names[6];
static int16_t class_indent[10];
[[maybe_unused]] static const bool member_tables_added = (Data::instance().add(0x1A44, class_names, "stats.class_names"),
                                                         Data::instance().add(0x1A58, class_indent, "stats.class_indent", Data::SHARED),
                                                         Data::instance().add(0x1A6A, health_names, "stats.health_names", Data::SHARED), true);

/* Sex sign, level and class (centred by the indents at ds:1A58), the state in words, then the
 * attributes, hit points, experience and magic points. */
void show_member_stats(int member)
{
    PartyMember *m = &party[member];
    draw_list_header_bar();
    draw_panel_title(m->name);
    clear_status_window();
    int cls = str_index((uint8_t)m->cls, strings[Strings::AMBFDTPRS_96C6]);       /* "AMBFDTPRS" */
    int state = str_index((uint8_t)m->status, strings[Strings::GPDSC_96D0]);  /* "GPDSC" */
    for (int i = 0; i < class_indent[cls]; i++) put_char(' ');
    put_char(m->sex);
    print_text(strings[Strings::Lv_96D6]);   /* " Lv-" */
    print_number(' ', 1, m->level);
    put_char(' ');
    print_text(strings[class_names[cls]]);
    define_text_window(9, 0x26, 1, 0x18, 1);
    set_cursor(1, 0);
    put_char(0xFC);
    print_text(strings[health_names[state]]);   /* Good Health, Poisoned, Dead, Asleep, Charmed */
    define_text_window(9, 0x27, 1, 0x18, 1);
    set_cursor(3, 0);
    put_char(0xFB);
    print_text(strings[Strings::Str_96DC]);   /* "Str=" */
    print_number('0', 2, m->str);
    print_text(strings[Strings::HP_96E2]);   /* "  HP:" */
    print_number(' ', 4, (int16_t)m->hp);
    print_text(strings[Strings::Int_96E8]);   /* "\nInt=" */
    print_number('0', 2, m->intel);
    print_text(strings[Strings::HM_96EE]);   /* "  HM:" */
    print_number(' ', 4, (int16_t)m->max_hp);
    print_text(strings[Strings::Dex_96F4]);   /* "\nDex=" */
    print_number('0', 2, m->dex);
    print_text(strings[Strings::Ex_96FA]);   /* "  Ex:" */
    print_number(' ', 4, (int16_t)m->xp);
    print_text(strings[Strings::Magic_9700]);   /* "\n\n    Magic:" */
    print_number(' ', 2, m->mp);
}

int print_equipped_name(int item)
{
    if (item == 0xFF) return 0;
    put_char(' ');
    print_text(items.short_name(item));
    put_char('\n');
    return 1;
}

void show_member_arms(int member)
{
    PartyMember *m = &party[member];
    draw_list_header_bar();
    draw_panel_title(m->name);
    define_text_window(9, 0x26, 1, 0x18, 1);
    put_char(0xFF);
    put_char(0xFC);
    put_char(0xFE);
    print_text(strings[Strings::Arms_970E]);   /* "Arms\n\n" */
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
        print_text(strings[Strings::None_ready_9716]);   /* "(None ready)" */
        put_char(0xFB);
    }
    define_text_window(9, 0x27, 1, 0x18, 1);
}

void show_equipment_page(void)
{
    draw_list_header_bar();
    draw_panel_title(strings[Strings::Equipment_9724]);   /* "Equipment" */
    clear_status_window();
    print_text(strings[Strings::Food_972E]);   /* "\n Food: " */
    print_number(' ', 4, (int16_t)party.food);
    print_text(strings[Strings::Gold_9738]);   /* "\n Gold: " */
    print_number(' ', 4, (int16_t)party.gold);
    print_text(strings[Strings::Keys_9742]);   /* "\n\n Keys......." */
    print_number(' ', 2, party.keys);
    print_text(strings[Strings::Gems_9752]);   /* "\n Gems......." */
    print_number(' ', 2, party.gems);
    print_text(strings[Strings::Torches_9760]);   /* "\n Torches...." */
    print_number(' ', 2, party.torches);
    if (party.grapples != 0) print_text(strings[Strings::Grapple_976E]);   /* "\n Grapple" */
}

/* A list page of Ztats: up / down scroll a line, PgUp / PgDn seven, Home / End jump. Any key
 * that turns the page or ends Ztats (left, right, 0-6, Space, Esc) is returned. */
int browse_item_list(NameTable names, const uint8_t *counts, int size, const char *title)
{
    draw_panel_title(title);
    set_list_window(8);
    int first = list_next_item(0xFF, counts, size, -1), key;
    if (first == -1) {
        set_list_window(8);
        set_cursor(4, 1);
        print_text(strings[Strings::None_owned_9794]);   /* "(None owned!)" */
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
        if (platform->quit_requested()) return 0x1B;
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
    print_text(strings[Strings::Status_97A2]);   /* "\nStatus: " */
    collect_special_items();
    clear_side_panel();
    int page = member * 2, last = party.size() * 2 - 1;
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
        } else if (key > '0' && key < '7' && (unsigned)(key - '1') < party.size()) {
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
            key = (uint8_t)browse_item_list(Spells::reagent_name_of, party.reagents, 8, strings[Strings::Reagents_97AC]);   /* "Reagents" */
        } else if (page == 0x0E) {
            key = (uint8_t)browse_item_list(Spells::name_of, party.mixed_spells, 0x30, strings[Strings::Spells_97B6]);    /* "Spells" */
        } else if (page == 0x0F) {
            key = (uint8_t)browse_item_list(Items::special_name_of, items.special_list, 0x26, strings[Strings::Items_97BE]);                                  /* "Items" */
        } else {
            key = (uint8_t)browse_item_list(Items::short_name_of, party.inventory, 0x30, strings[Strings::Armaments_97C4]);   /* "Armaments" */
        }
        if (platform->quit_requested()) break;
    }
    draw_list_header_bar();
    draw_panel_top_bar();
    draw_panel_bottom_bar();
    draw_status_bar();
    print_text(strings[Strings::Done_97CE]);   /* "Done\n" */
}

/* ---- Ready ---- */

void print_item_prompt(const char *text)
{
    print_text(strings[Strings::NL_NL_97D4]);   /* "\n\n" */
    print_text(text);
    print_text(strings[Strings::Item_97D8]);   /* "\n\nItem: " */
}


/* 2 both hands free, 0 the left is free, 1 only the right (and the left doesn't hold a
 * two-handed item), 0xFF none. */
int free_hands(int member)
{
    PartyMember *m = &party[member];
    if (m->left_hand == 0xFF && m->right_hand == 0xFF) return 2;
    if (m->left_hand == 0xFF) return 0;
    if (m->right_hand == 0xFF && items.hands(m->left_hand) != 0x30) return 1;
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
    PartyMember *m = &party[member];
    MapObject *o = NULL;
    if (map.in_scene()) o = &map.objects[combat.combatants[combat.turn & 0x1F].object & 0x1F];
    if (item < 0 || item == 0x1B || item == 0x1D) return 0;
    if (item > 8 && item < 0x10 && map.in_scene() && combat.victory == 0) {
        print_item_prompt(strings[Strings::Thou_canst_not_change_97E2]);   /* "Thou canst not change armour in heated battle!" */
        return 0;
    }
    if (is_equipped_by((unsigned)item, member)) {
        combat.unequip_item((unsigned)item, member);
        if (party.inventory[item] < 99) party.inventory[item]++;
        if (o && item == 0x2A)   /* visible again */
            o->tile = o->tile2 = party.class_tile((unsigned)str_index((uint8_t)m->cls, strings[Strings::AMBFDTPRS_9812]));
        return 0;
    }
    if (((item == 0x1A || item == 0x24) && party.inventory[0x1B] == 0) || (item == 0x1C && party.inventory[0x1D] == 0)) {
        print_item_prompt(strings[Strings::Thou_hast_no_ammunition_981C]);   /* "Thou hast no ammunition for that weapon!" */
        return 0;
    }
    unsigned load = 0;
    const uint8_t *worn = &m->helm;
    for (int i = 0; i < 6; i++)
        if (worn[i] != 0xFF) load += items.weight(worn[i]);
    uint8_t *slot = NULL;
    switch (items.hands(item)) {
    case 2:
        if (m->ring != 0xFF) { print_item_prompt(strings[Strings::Only_one_magic_ring_may_9916]); return 0; }   /* "Only one magic ring may be worn at a time!" */
        slot = &m->ring;
        break;
    case 4:
        if (m->amulet != 0xFF) { print_item_prompt(strings[Strings::Thou_must_remove_thine_98F0]); return 0; } /* "Thou must remove thine other amulet!" */
        slot = &m->amulet;
        break;
    case 0x20: {
        int hand = free_hands(member);
        if (hand == 0xFF) { print_item_prompt(strings[Strings::Thou_must_free_one_of_9892]); return 0; }      /* "Thou must free one of thy hands first!" */
        slot = hand == 1 ? &m->right_hand : &m->left_hand;
        break;
    }
    case 0x30:
        if (free_hands(member) != 2) { print_item_prompt(strings[Strings::Both_hands_must_be_free_98BA]); return 0; }   /* "Both hands must be free before thou canst wield that!" */
        slot = &m->left_hand;
        break;
    case 0x40:
        if (m->body != 0xFF) { print_item_prompt(strings[Strings::Thou_must_first_remove_9866]); return 0; }   /* "Thou must first remove thine other armour!" */
        slot = &m->body;
        break;
    case 0x80:
        if (m->helm != 0xFF) { print_item_prompt(strings[Strings::Remove_first_thy_present_9846]); return 0; }   /* "Remove first thy present helm!" */
        slot = &m->helm;
        break;
    }
    if (items.weight((uint8_t)item) + load > m->str) {
        print_item_prompt(strings[Strings::Thou_art_not_strong_9942]);   /* "Thou art not strong enough!" */
        return 0;
    }
    if (!slot) return 0;   /* (an item of no class: the original writes through a stale pointer) */
    *slot = (uint8_t)item;
    party.inventory[item]--;
    if ((item == 0x2A || item == 0x2C) && random_between(0x0F, 0) == 0) {
        print_text(strings[Strings::Ring_vanishes_995E]);   /* "\n\nRing vanishes!\n" */
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
    int first = list_next_item(member, party.inventory, 0x30, -1);
    if (first == -1) {
        print_text(strings[Strings::Thou_art_empty_handed_997E]);   /* "Thou art empty-\nhanded!\n" */
        return;
    }
    print_text(strings[Strings::Item_9998]);   /* "Item: " */
    clear_side_panel();
    select_text_window(1);
    draw_panel_title(party[member].name);
    set_list_window(8);
    ready_item_list('R', member, first);
    draw_list_header_bar();
    draw_panel_top_bar();
    draw_panel_bottom_bar();
    draw_status_bar();
}
