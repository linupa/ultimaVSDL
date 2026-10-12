/* shops.cpp - merchants (SHOPPES, SHOPPES2, SHOPPES3 overlays). See shops.h. String and table
 * addresses are ds offsets from the original; the comments quote the strings. */
#include <stdio.h>
#include <string.h>
#include "commands.h"
#include "gametime.h"
#include "items.h"
#include "map.h"
#include "party.h"
#include "sound.h"
#include "gfx.h"
#include "input.h"
#include "misc.h"
#include "objects.h"
#include "shops.h"
#include "stats.h"
#include "status.h"
#include "strings.h"
#include "talk.h"
#include "text.h"
#include "textbuf.h"
#include "town.h"
#include "view.h"


/* get_command_key, but when the window is closed it cycles through keys that end every shop
 * prompt, so the original's wait-for-Y/N loops can't hang. */
static int key(void)
{
    static const int quit_keys[] = {'N', ' ', 0x1B, 0x0D};
    static int q;
    int k = get_command_key();
    if (platform->quit_requested()) k = quit_keys[q++ & 3];
    return k;
}

static int yes_no_key(void)
{
    int k;
    do k = key(); while (k != 'Y' && k != 'N');
    return k;
}

int Town::intel_of(int member) { return party[member].intel; }

/* price + price * (100 - 3 * INT) / 100, the division unsigned as in the original (MSC
 * __aNuldiv): from INT 34 the negative product wraps. */
uint16_t Town::adjust_price_u(uint16_t price, int intel)
{
    int32_t prod = (int32_t)(int16_t)price * (int32_t)(100 - 3 * intel);
    return (uint16_t)(price + (uint16_t)((uint32_t)prod / 100u));
}

/* The inn's version divides signed. */
uint16_t Town::adjust_price_s(uint16_t price, int intel)
{
    int32_t prod = (int32_t)(int16_t)price * (int32_t)(100 - 3 * intel);
    return (uint16_t)(price + (uint16_t)(prod / 100));
}

static int rnd(int hi) { return random_between(hi, 0); }

/* ---------------------------------------------------------------- SHOPPES: common */

static char *copy_end(const char *src, char *dst)
{
    while ((*dst = *src++) != 0) dst++;
    return dst;
}

void Town::shop_print(const char *text)
{
    char *buf = (char *)town.shop_text_buf, *dst = buf;
    char num[8];
    const char *run = text, *p = text;
    while (*p) {
        uint8_t b = (uint8_t)*p;
        int special = b == '#' || b == '$' || b == '@' || b == '%' || b == '^' || b == '&' || b == '*' || b >= 0x80;
        if (special) {   /* copy the literal text before it */
            memcpy(dst, run, (size_t)(p - run));
            dst += p - run;
            *dst = 0;
            run = p + 1;
        }
        p++;
        if (!special) continue;
        const char *src;
        if (b >= 0x80) {   /* a word from the talk word table, with spaces around it */
            *dst = ' ';
            dst = copy_end(conversation.word(b - 0x7Fu), dst + 1);
            if (*p != 0 && (uint8_t)*p < 0x80) *dst++ = ' ';
            continue;
        }
        switch (b) {
        case '%': snprintf(num, sizeof num, "%d", (int16_t)town.shop_price); src = num; break;
        case '^': snprintf(num, sizeof num, "%d", (int16_t)town.shop_quantity); src = num; break;
        case '#': src = strings[Strings::Id(town.shop_name)]; break;
        case '$': src = strings[Strings::Id(town.shopkeeper_name)]; break;
        case '&': src = strings[Strings::Id(town.shop_item_name)]; break;
        case '*': src = strings[Strings::Id(town.shop_item_name2)]; break;
        default:  src = game_time.hour < 12 ? strings[Strings::morning_7826] : game_time.hour < 18 ? strings[Strings::afternoon_782E] : strings[Strings::evening_7838]; break;   /* @ */
        }
        dst = copy_end(src, dst);
    }
    strcpy(dst, run);
    print_text(buf);
}

/* The original reads 1500 bytes into text_buffer, which runs on to shop_text_buf. */
void Town::shop_say(uint16_t text_offset)
{
    load_file_part(text_offset, 0x5DC, text_buffer.data, strings[Strings::SHOPPE_DAT_7840]);   /* SHOPPE.DAT */
    shop_print((const char *)text_buffer.data);
}

void Town::shop_take_gold(void)
{
    if (town.shadowlord_here == 0) sub_word_floor0((uint16_t)random_between(0x40, 1), &party.gold);
}

void Town::shop_greeting(void)
{
    put_char('"');
    shop_say(table_word(SHOP_GREETING + town.shop_type * 8 + rnd(3) * 2));
    if (get_cursor_col() < 0x0C) {
        if (get_cursor_col() != 0) {
            put_char(' ');
            return;
        }
        print_text(strings[Strings::NL_Colon_7850]);
    } else {
        print_text(strings[Strings::NL_NL_Colon_784C]);
    }
}

void Town::shop_farewell(int kind)
{
    if (kind == 0) {
        print_text(strings[Strings::NL_NL_Quote_7854]);
        shop_say(table_word(SHOP_FAREWELL_A + town.shop_type * 8 + rnd(3) * 2));
    } else if (kind == 1) {
        print_text(strings[Strings::NL_NL_Quote_7858]);
        shop_say(table_word(SHOP_FAREWELL_B + town.shop_type * 8 + rnd(3) * 2));
    } else {
        return;
    }
    if (get_cursor_col() != 0) put_char('\n');
    shop_print(strings[Strings::says_785C]);   /* "says $." */
}

int Town::ask_yes_no(void)
{
    int k = yes_no_key();
    print_text(k == 'N' ? strings[Strings::No_7866] : strings[Strings::Yes_786A]);
    return k;
}

/* ---------------------------------------------------------------- provisioner (type 5) */

int Town::provisioner_offer(int result, int item, int member)
{
    town.shop_price = adjust_price_u(table_word(PROVISION_PRICE + town.shop_town * 8 + item * 2), intel_of(member));
    print_text(strings[Strings::NL_NL_Quote_786E]);
    shop_say(table_word(PROVISION_TEXT + item * 2));
    print_text(strings[Strings::Interested_7872]);   /* "Interested?" */
    int k = yes_no_key();
    if (k == 'N') {
        print_text(strings[Strings::No_What_else_then_7882]);   /* "No ... What else, then?" */
        return result;
    }
    print_text(strings[Strings::Yes_789A]);
    if ((int16_t)town.shop_price > (int16_t)party.gold) {
        shop_say(0x21E6);
        return -1;
    }
    party.gold = (uint16_t)(party.gold - town.shop_price);
    shop_take_gold();
    draw_status_bar();
    if (item == 0) add_byte_capped(99, 3, &party.keys);
    else if (item == 1) add_byte_capped(99, 4, &party.gems);
    else if (item == 2) add_byte_capped(99, 5, &party.torches);
    shop_print(strings[Strings::Sold_says_What_else_78A0]);   /* "Sold!" says $. "What else, " */
    print_text(party[member].sex == 0x0C ? strings[Strings::m_lady_78C0] : strings[Strings::m_lord_78C8]);   /* m'lady / m'lord */
    print_text(strings[Strings::Qm_NL_NL_78D0]);
    return 1;
}

int Town::provisioner_menu(int member)
{
    int result = 0;
    for (;;) {
        print_text(strings[Strings::a_Keys_78D4]);   /* a.........Keys */
        print_text(strings[Strings::b_Gems_78E4]);
        print_text(strings[Strings::c_Torches_78F4]);
        print_text(strings[Strings::Thy_concern_7906]);   /* Thy concern? */
        for (;;) {
            unsigned k = (unsigned)key() & 0xFF;
            if (k == ' ' || k == 0x1B) return result;
            if (k >= 'A' && k <= 'C') {
                put_char((uint8_t)(k | 0x20));
                result = provisioner_offer(result, (int)k - 'A', member);
                if (result == -1) return result;
                break;
            }
            if (k == 'D') {
                shop_say(0x2215);
                break;
            }
        }
    }
}

void Town::shop_provisioner(int member)
{
    int kind = 0;
    shop_greeting();
    for (;;) {
        unsigned k = (unsigned)key() & 0xFF;
        if (k == ' ' || k == 'N') {
            print_text(strings[Strings::No_7928]);
            break;
        }
        if (k == 'Y') {
            print_text(strings[Strings::Yes_We_sell_7916]);   /* "We sell:" */
            kind = provisioner_menu(member);
            break;
        }
    }
    shop_farewell(kind);
}

/* ---------------------------------------------------------------- reagents (type 4) */

int Town::reagent_offer(int result, int reagent, int member)
{
    int i, n = 0;
    for (i = 0; i < 8; i++)   /* the reagent-th one this shop sells */
        if (table_byte(REAGENT_PRICE + town.shop_town * 8 + i) != 0) {
            if (n == reagent) break;
            n++;
        }
    if (i == 8) return -2;
    if (party.reagents[i] == 99) {
        print_text(strings[Strings::Thou_canst_not_carry_any_792C]);   /* "Thou canst not carry any more!" */
        key();
        return result;
    }
    town.shop_price = adjust_price_u(table_byte(REAGENT_PRICE + town.shop_town * 8 + i), intel_of(member));
    town.shop_quantity = table_byte(REAGENT_QUANTITY + town.shop_town * 8 + i);
    print_text(strings[Strings::NL_NL_Quote_7952]);
    shop_print((const char *)text_buffer.at(table_word(REAGENT_TEXT + i * 2) - 0x1A67));   /* text loaded by reagent_menu */
    print_text(strings[Strings::Is_this_thy_need_7956]);   /* " Is this thy need?" */
    if (get_cursor_col() > 0x0C) print_text(strings[Strings::NL_Colon_796C]);
    int k = yes_no_key();
    if (k == 'N') {
        print_text(strings[Strings::No_What_else_7970]);
        return result;
    }
    print_text(strings[Strings::Yes_7982]);
    if ((int16_t)town.shop_price > (int16_t)party.gold) {
        shop_print((const char *)text_buffer.at(0x4C4));   /* in the loaded text: "not enough gold" */
        return -1;
    }
    party.gold = (uint16_t)(party.gold - town.shop_price);
    shop_take_gold();
    draw_status_bar();
    add_byte_capped(99, (uint8_t)town.shop_quantity, &party.reagents[i]);
    shop_print(strings[Strings::I_thank_thee_says_7988]);   /* "I thank thee!" says $. */
    print_text(strings[Strings::Anything_else_79A2]);   /* "Anything else?" */
    return 1;
}

int Town::reagent_menu(int member)
{
    int result = 0;
    load_file_part(0x1A67, 0x5DC, text_buffer.data, strings[Strings::SHOPPE_DAT_7A0A]);   /* SHOPPE.DAT */
    for (;;) {
        uint8_t letter = 'A';
        for (int i = 0; i < 8; i++)
            if (table_byte(REAGENT_PRICE + town.shop_town * 8 + i) != 0) {
                put_char(letter++);
                print_text(strings[Strings::Dot_Dot_Dot_7A16]);   /* "..." */
                print_text(strings[Strings::Id(table_word(REAGENT_NAME + i * 2))]);
                put_char('\n');
            }
        print_text(strings[Strings::Thy_interest_7A1A]);   /* "Thy interest?" */
        for (;;) {
            unsigned k = (unsigned)key() & 0xFF;
            if (k == 0x1B || k == 0x0D || k == ' ') return result;
            if (k < 'A' || k > 'E') continue;
            int r = reagent_offer(result, (int)k - 'A', member);
            if (r == -1) return -1;
            if (r == -2) continue;
            result = r;
            break;
        }
    }
}

void Town::shop_reagents(int member)
{
    int kind = 0;
    shop_greeting();
    for (;;) {
        unsigned k = (unsigned)key() & 0xFF;
        if (k == ' ' || k == 'N') {
            print_text(strings[Strings::No_7A44]);
            break;
        }
        if (k == 'Y') {
            print_text(strings[Strings::Yes_Fine_We_sell_7A2C]);   /* "Fine! We sell:" */
            kind = reagent_menu(member);
            break;
        }
    }
    shop_farewell(kind);
}

/* ---------------------------------------------------------------- stables (type 2) */

/* The horse appears on a free floor (0x44, 0x45) or grass (5) square at one of four offsets
 * (ds:3C38 / ds:3C40) from the party. */
void Town::shop_stables(int member)
{
    int slot = 0;
    while (slot < 32 && map.objects[slot].tile != 0) slot++;
    int k4;
    uint8_t x = 0, y = 0;
    for (k4 = 0; k4 < 4; k4++) {
        int dx = (int16_t)table_word(HORSE_DX + k4 * 2), dy = (int16_t)table_word(HORSE_DY + k4 * 2);
        uint8_t t = *map.get_map_tile_ptr((unsigned)(dy + map.y), (unsigned)(dx + map.x));
        y = (uint8_t)(dy + map.y);
        x = (uint8_t)(dx + map.x);
        if (find_object_at(map.z, y, x) == 0 && (t == 0x44 || t == 0x45 || t == 5)) break;
    }
    if (k4 == 4 || slot == 32) {
        print_text(strings[Strings::The_stables_are_closed_7A48]);   /* "The stables are closed." */
        return;
    }
    int result = 0;
    town.shop_price = adjust_price_u(table_word(HORSE_PRICE + town.shop_town * 2), intel_of(member));
    shop_greeting();
    for (;;) {
        unsigned k = (unsigned)key() & 0xFF;
        if (k == ' ' || k == 'N') {
            print_text(strings[Strings::No_7AB2]);
            break;
        }
        if (k != 'Y') continue;
        print_text(strings[Strings::Yes_7A62]);
        shop_say(0x1643);
        print_text(strings[Strings::Deal_7A6A]);   /* "Deal?" */
        if (yes_no_key() == 'N') {
            print_text(strings[Strings::No_7A74]);
            break;
        }
        print_text(strings[Strings::Yes_7A78]);   /* "Yes!" */
        if ((int16_t)party.gold < (int16_t)town.shop_price) {
            print_text(strings[Strings::Thou_couldst_not_afford_7A7E]);   /* "Thou couldst not afford to " */
            shop_print(strings[Strings::feed_it_yells_7A9E]);   /* "feed it!" yells $. */
            result = -1;
        } else {
            party.gold = (uint16_t)(party.gold - town.shop_price);
            shop_take_gold();
            MapObject *o = &map.objects[slot];
            o->hull = o->skiffs = o->flags = 0;
            o->tile2 = o->tile = 0x10;
            o->x = x;
            o->y = y;
            o->z = map.z;
            world_tick();
            draw_status_bar();
            result = 1;
        }
        break;
    }
    shop_farewell(result);
}

/* ---------------------------------------------------------------- armoury (type 0) */

int Town::armoury_offer(int result, int item, int member)
{
    town.shop_price = adjust_price_u(items.price(item), intel_of(member));
    print_text(strings[Strings::NL_NL_Quote_7B5E]);
    shop_say(table_word(ARMOURY_ITEM_TEXT + item * 2));
    print_text(strings[Strings::NL_NL_7B62]);
    print_text(strings[Strings::Id(table_word(ARMOURY_OFFER_A + rnd(3) * 2))]);
    print_text(strings[Strings::Quote_SP_7B66]);
    int k = yes_no_key();
    if (k == 'N') {
        print_text(strings[Strings::No_7B6A]);
    } else {
        print_text(strings[Strings::Yes_7B70]);
        if (party.inventory[item] == 99) {
            print_text(strings[Strings::Thou_canst_not_carry_any_7B76]);   /* "Thou canst not carry any more!" */
            shop_print(strings[Strings::says_7B9A]);
            key();
        } else if ((int16_t)party.gold < (int16_t)town.shop_price) {
            print_text(strings[Strings::NL_Quote_7BA4]);
            print_text(strings[Strings::Id(table_word(ARMOURY_OFFER_B + rnd(3) * 2))]);
            shop_print(strings[Strings::yells_7BA8]);   /* yells $. */
            result = -1;
        } else {
            party.gold = (uint16_t)(party.gold - town.shop_price);
            shop_take_gold();
            draw_status_bar();
            if (item == 0x1B || item == 0x1D) party.inventory[item] = 99;   /* a full quiver */
            else add_byte_capped(99, 1, &party.inventory[item]);
            shop_print(strings[Strings::Sold_7BB4]);   /* "Sold!" */
            result = 1;
        }
    }
    if (result != -1) {
        print_text(strings[Strings::Anything_else_7BBC]);   /* "Anything else, " */
        print_text(result == 0 ? strings[Strings::then_7BDC] : party[member].sex == 0x0C ? strings[Strings::milady_7BCE] : strings[Strings::sir_7BD6]);
    }
    return result;
}

int Town::armoury_buy_menu(int member)
{
    int result = 0;
    for (;;) {
        print_text(strings[Strings::NL_NL_7C44]);
        int n;
        for (n = 0; n < 8; n++) {   /* this shop's stock, ds:3AE2, 0xFF ends */
            unsigned id = table_byte(ARMOURY_STOCK + town.shop_town * 8 + n);
            if (id == 0xFF) break;
            put_char((uint8_t)('a' + n));
            print_text(strings[Strings::Dot_Dot_Dot_7C48]);   /* "..." */
            const char *name = items.name(id);
            if (strlen(name) >= 0x0D) name = items.short_name(id);
            print_text(name);
            if (get_cursor_col() != 0) put_char('\n');
        }
        put_char('\n');
        print_text(strings[Strings::Id(table_word(ARMOURY_MENU_PHRASE + rnd(3) * 2))]);
        print_text(strings[Strings::Quote_SP_7C4C]);
        for (;;) {
            unsigned k = (unsigned)key();
            int idx = (int)(k & 0xFF) - 'A';
            if (idx >= 0 && idx < n) {
                put_char((uint8_t)(k + 0x20));
                result = armoury_offer(result, table_byte(ARMOURY_STOCK + town.shop_town * 8 + idx), member);
                if (result == -1) return result;
                break;
            }
            if ((k & 0xFF) == 0x1B || (k & 0xFF) == ' ') return result;
        }
    }
}

int Town::sell_list_init(void)
{
    for (int i = 0; i < 0x30; i++)
        if (party.inventory[i]) return 1;
    return 0;
}

#define NEXT(i) list_next_item(0xFF, party.inventory, 0x30, (i))
#define PREV(i) list_prev_item(0xFF, party.inventory, 0x30, (i))

/* Four rows of owned items in the panel box: *cursor is highlighted (0xFD inverse), *top the
 * first row shown. Arrows (0x18 up, 0x19 down, 0x12 both) show if there is more. Returns the
 * cursor's screen row. */
uint8_t Town::armoury_sell_list(int *cursor, int *top)
{
    uint8_t cursor_row = 0;   /* uninitialised in the original if the cursor isn't shown */
    if (party.inventory[*cursor] == 0) {   /* the item was sold out: move to a neighbour */
        if (*top == *cursor) {
            *cursor = PREV(*cursor);
            if (*cursor == -1) *cursor = NEXT(-1);
            *top = *cursor;
        } else {
            *cursor = NEXT(*cursor);
            if (*cursor == -1) {
                *cursor = PREV(0x30);
                if (PREV(*top) > -1) *top = PREV(*top);
            }
        }
    }
    select_text_window(1);
    set_cursor(1, 1);
    int item = *top;
    if (item != -1) {
        do {
            if (item == *cursor) {
                put_char(0xFD);
                cursor_row = get_cursor_row();
            }
            print_list_entry(0x2D, Items::short_name_of, party.inventory, item);
            if (item == *cursor) put_char(0xFD);
            set_cursor(get_cursor_row(), 1);
        } while (get_cursor_row() != 5 && (item = NEXT(item)) != -1);
    }
    for (int r = get_cursor_row(); r != 5; r++) {
        set_cursor(r, 1);
        print_text(strings[Strings::SP_SP_SP_SP_7C50]);
    }
    int arrows = 0;
    if (PREV(*top) != -1) arrows = 2;
    if (item != -1 && NEXT(item) != -1) arrows++;
    if (arrows == 0) {
        draw_panel_bottom_bar();
    } else {
        set_cursor(6, 6);
        draw_label_left();
        put_char(arrows == 1 ? 0x19 : arrows == 2 ? 0x18 : 0x12);
        draw_label_right();
    }
    select_text_window(2);
    return cursor_row;
}

int Town::armoury_sell_item(int item, int member)
{
    if (item == 0x1B || item == 0x1D) {
        shop_print(strings[Strings::We_don_t_deal_in_7D32]);   /* "We don't deal in used ammunition!" */
        return 1;
    }
    print_text(strings[Strings::NL_NL_Quote_7D64]);
    uint16_t base = items.price(item);
    if (base == 0) {
        shop_print(strings[Strings::That_I_cannot_buy_from_7D8C]);   /* "That, I cannot buy from thee." */
    } else {
        uint16_t v = (uint16_t)(intel_of(member) * base);
        town.shop_price = (uint16_t)((uint32_t)v * 3 / 100 + 1);   /* INT * price * 3 / 100 + 1 */
        town.shop_item_name = items.trade_name_id(item);
        if (town.shop_item_name == 0) town.shop_item_name = items.name_id(item);
        shop_print((const char *)text_buffer.at(table_word(ARMOURY_SELL_TEXT + rnd(7) * 2) - 0x0BC0));   /* loaded by armoury_sell */
        print_text(strings[Strings::Deal_7D68]);   /* "Deal?" */
        if (yes_no_key() == 'N') {
            print_text(strings[Strings::No_7D72]);
        } else {
            shop_print(strings[Strings::Yes_Done_says_7D76]);   /* "Done!" says $. */
            add_word_capped(9999, town.shop_price, &party.gold);
            sub_byte_floor0(1, &party.inventory[item]);
        }
        draw_gold();
    }
    return 0;
}

void Town::draw_panel_box(int inner_rows)
{
    put_char(0x10);
    for (int i = 0; i < 0x0D; i++) put_char(0x11);
    put_char(0x13);
    for (int r = 1; r <= inner_rows; r++) {
        set_cursor(r, 0);
        put_char(0x17);
        set_cursor(r, 0x0E);
        put_char(0x17);
    }
    put_char('\n');
    put_char(0x14);
    for (int i = 0; i < 0x0D; i++) put_char(0x15);
    put_char(0x16);
}

void Town::armoury_sell(int member)
{
    if (!sell_list_init()) {
        shop_print(strings[Strings::Thou_hast_nothing_to_7F20]);   /* "Thou hast nothing to sell!" growls $. */
        return;
    }
    print_text(strings[Strings::Id(table_word(ARMOURY_SELL_A + rnd(3) * 2))]);
    print_text(strings[Strings::Quote_SP_7EF0]);
    draw_panel_title(strings[Strings::Arms_7EF4]);
    select_text_window(1);
    define_text_window(6, 0x26, 1, 0x18, 1);
    put_char(0xFF);
    define_text_window(9, 0x27, 1, 0x18, 1);
    draw_panel_box(4);
    int cursor = NEXT(-1), top = cursor;
    int row = armoury_sell_list(&cursor, &top);
    int result = 0, done = 0;
    load_file_part(0x0BC0, 0x5DC, text_buffer.data, strings[Strings::SHOPPE_DAT_7EFA]);   /* SHOPPE.DAT: sell phrases */
    while (!done) {
        unsigned k = (unsigned)key() & 0xFF;
        if (k == 0x0D || k == ' ') {
            result = armoury_sell_item(cursor, member);
            if (result == 0 && sell_list_init()) {
                row = armoury_sell_list(&cursor, &top);
                print_text(strings[Strings::NL_NL_Quote_7F06]);
                print_text(strings[Strings::Id(table_word(ARMOURY_SELL_B + rnd(3) * 2))]);
                print_text(strings[Strings::Quote_SP_7F0A]);
            } else {
                done = 1;
            }
            continue;
        }
        if (k == 0x1B) {
            done = 1;
            continue;
        }
        if (k == 0xD3) {   /* Home */
            cursor = top = NEXT(-1);
        } else if (k == 0xD4) {   /* End: the last item, showing up to three before it */
            cursor = top = PREV(0x30);
            for (int n = 1; n < 4; n++) {
                int p = PREV(top);
                if (p == -1) break;
                top = p;
            }
        } else if (k == 0xD5 || k == 1 || k == 3) {   /* PgUp / left / up */
            for (int n = k == 0xD5 ? 4 : 1; n; n--) {
                int p = PREV(cursor);
                if (cursor != top) cursor = p;
                else if (p > -1) cursor = top = p;
            }
        } else if (k == 0xD6 || k == 2 || k == 4) {   /* PgDn / right / down */
            for (int n = k == 0xD6 ? 4 : 1; n; n--) {
                int nx = NEXT(cursor);
                if (nx > -1 && (cursor = nx, ++row > 4)) top = NEXT(top);
            }
        } else {
            continue;
        }
        row = armoury_sell_list(&cursor, &top);
    }
    draw_status_bar();
    draw_panel_top_bar();
    if (result != 0) return;
    print_text(strings[Strings::NL_NL_Quote_7F0E]);
    print_text(strings[Strings::Id(table_word(ARMOURY_SELL_C + rnd(3) * 2))]);
    print_text(strings[Strings::Quote_NL_7F12]);
    if (sell_list_init()) shop_print(strings[Strings::says_7F16]);   /* says $. */
}

void Town::shop_armoury(int member)
{
    int kind = 0;
    shop_print(strings[Strings::Good_and_welcome_to_8018]);   /* "Good @, and welcome to #!" */
    key();
    shop_print(strings[Strings::says_8036]);   /* $ says, " */
    print_text(strings[Strings::Id(table_word(ARMOURY_ASK_A + rnd(1) * 2))]);
    print_text(strings[Strings::Quote_SP_8042]);
    for (;;) {
        unsigned k = (unsigned)key() & 0xFF;
        if (k == ' ') {
            print_text(strings[Strings::No_8056]);
            break;
        }
        if (k == 'B') {
            print_text(strings[Strings::Buy_8046]);   /* "Buy" */
            print_text(strings[Strings::Id(table_word(ARMOURY_ASK_B + rnd(3) * 2))]);
            print_text(strings[Strings::Id(table_word(ARMOURY_ASK_C + rnd(3) * 2))]);
            kind = armoury_buy_menu(member);
            break;
        }
        if (k == 'S') {
            print_text(strings[Strings::Sell_804E]);   /* "Sell" */
            armoury_sell(member);
            kind = -1;
            break;
        }
    }
    shop_farewell(kind);
}

/* ---------------------------------------------------------------- healer (type 6) */

int Town::healer_ask_who(void)
{
    if (party.alone()) return 0;
    print_text(strings[Strings::Who_needs_my_aid_805A]);   /* "Who needs my aid?" */
    int m = ask_member_number();
    if (m == -1) print_text(strings[Strings::No_one_8072]);   /* "No one" */
    return m;
}

/* XOR flashes over the view; the original's sound sweeps give them their length. */
void Town::healer_effect(void)
{
    set_draw_color(gfx.color_fire);
    xor_fill_rect(0xB7, 0xB7, 8, 8);
    sound_sweep(1, 5000, 0x57E4, 1, 0x100E);
    sound_sweep(-1, 0x6B6C, 0x57E4, 1, 0x100E);
    set_draw_color(gfx.color_text);
    xor_fill_rect(0xB7, 0xB7, 8, 8);
    sound_sweep(1, 1, -0x63C0, 1, 0x11B2);
    sound_sweep(-1, 40000, -0x63C0, 1, 0x11B2);
    xor_fill_rect(0xB7, 0xB7, 8, 8);
    sound_sweep(2, 1, 18000, 1, 0x8FC);
    sound_sweep(-2, 36000, 18000, 1, 0x8FC);
}

/* At location 7, a price up to 100 that the party can't pay is waived. */
int Town::healer_ask_pay(void)
{
    shop_print(strings[Strings::for_gold_Wilt_thou_pay_807A]);   /* "for % gold. Wilt thou pay?" */
    int k = yes_no_key();
    print_text(k == 'Y' ? strings[Strings::Yes_8098] : strings[Strings::No_809C]);
    int refused = k == 'N';
    if (!refused && (int16_t)party.gold < (int16_t)town.shop_price && ((int16_t)town.shop_price > 100 || map != Map::SKARA_BRAE)) {
        shop_say(0x23AB);
        refused = 1;
    }
    if (!refused && (int16_t)town.shop_price <= (int16_t)party.gold) {
        party.gold = (uint16_t)(party.gold - town.shop_price);
        shop_take_gold();
    }
    return refused;
}

void Town::shop_healer(void)
{
    int done = 0;
    shop_greeting();
    while (!done) {
        int k = yes_no_key();
        print_text(k == 'N' ? strings[Strings::No_80A0] : strings[Strings::Yes_80A4]);
        if (k == 'N') break;
        print_text(strings[Strings::We_have_powers_to_Cure_80AA]);   /* "We have powers to Cure, Heal, or Resurrect." */
        shop_print(strings[Strings::says_What_is_the_nature_80DA]);   /* says $. "What is the nature of thy need?" */
        unsigned c;
        do c = (unsigned)key() & 0xFF; while (c != 'C' && c != 'H' && c != 'R' && c != ' ' && c != 0x0D);
        if (c == 0x0D || c == ' ') {
            print_text(strings[Strings::Nothing_81C0]);   /* "Nothing" */
            done = 1;
        } else if (c == 'C') {
            print_text(strings[Strings::Curing_8106]);   /* "Curing" */
            int m = healer_ask_who();
            if (m != -1) {
                if (party[m].poisoned()) {
                    print_text(strings[Strings::NL_NL_Quote_810E]);
                    int refused = 0;
                    if (map == Map::MINOC) {
                        print_text(strings[Strings::Receive_now_the_Light_8112]);   /* "Receive now the Light!" */
                    } else {
                        print_text(strings[Strings::I_can_cure_thy_poisoned_812A]);   /* "I can cure thy poisoned body " */
                        town.shop_price = table_byte(CURE_PRICE + town.shop_town);
                        refused = healer_ask_pay();
                    }
                    if (!refused) {
                        healer_effect();
                        party[m].status = 'G';
                    }
                } else {
                    shop_print(strings[Strings::Thou_hast_no_need_of_3D5A]);   /* "Thou hast no need of this art!" says $. */
                }
            }
        } else if (c == 'H') {
            print_text(strings[Strings::Healing_8148]);   /* "Healing" */
            int m = healer_ask_who();
            if (m != -1) {
                PartyMember *p = &party[m];
                if (p->dead() || p->hp == p->max_hp) {
                    shop_print(strings[Strings::Thou_hast_no_need_of_3D5A]);
                } else {
                    print_text(strings[Strings::NL_NL_Quote_8150]);
                    int refused = 0;
                    if (map == Map::MINOC) {
                        print_text(strings[Strings::Receive_now_the_Light_8154]);
                    } else {
                        print_text(strings[Strings::I_can_heal_thee_816C]);   /* "I can heal thee " */
                        town.shop_price = table_byte(HEAL_PRICE + town.shop_town);
                        refused = healer_ask_pay();
                    }
                    if (!refused) {
                        healer_effect();
                        p->hp = p->max_hp;
                    }
                }
            }
        } else {
            print_text(strings[Strings::Resurrect_817E]);   /* "Resurrect" */
            int m = healer_ask_who();
            if (m != -1) {
                if (!party[m].dead()) {
                    shop_print(strings[Strings::Thou_hast_no_need_of_3D5A]);
                } else {
                    print_text(strings[Strings::NL_NL_Quote_8188]);
                    print_text(strings[Strings::I_can_raise_this_818C]);   /* "I can raise this unfortunate person from " */
                    print_text(strings[Strings::the_dead_81B6]);   /* "the dead " */
                    town.shop_price = table_word(RESURRECT_PRICE + town.shop_town * 2);
                    if (!healer_ask_pay()) {
                        healer_effect();
                        resurrect_member(0xFF, m);
                        party[m].hp = party[m].max_hp;
                        draw_status_bar();
                    }
                }
            }
        }
        if (!done) {
            draw_status_bar();
            print_text(strings[Strings::Is_there_any_other_way_81C8]);   /* "Is there any other way in which I may aid thee?" */
            print_text(strings[Strings::aid_thee_81F2]);
        }
    }
    shop_farewell(1);
}

/* ---------------------------------------------------------------- SHOPPES2: tavern (type 1) */

/* The tavern's letters for food, drink, rations and rumours are per tavern (ds:4C1E, 4C24,
 * 4C2A, 4C30, indexed by tavern_index from ds:4D4C). */
#define TAVERN_KEY(base) ((char)table_byte((base) + town.tavern_index))

void Town::tavern_price_for_party(int price_each)
{
    town.shop_price = 0;
    town.tavern_diners2 = 0;   /* tavern_diners2 and tavern_diners are words in the original */
    town.tavern_diners = 0;
    for (unsigned i = 0; i < party.size(); i++)
        if (!party[i].dead()) {
            town.tavern_diners2++;
            town.tavern_diners++;
            town.shop_price = (uint16_t)(town.shop_price + price_each);
        }
    put_char('\n');
    put_char('\n');
}

void Town::number_word(void)
{
    static const Strings::Id words[] = {Strings::two_9AA8, Strings::three_9AAC, Strings::four_9AB2, Strings::five_9AB8, Strings::six_9ABE};   /* two .. six */
    if (town.tavern_diners2 >= 2 && town.tavern_diners2 <= 6) print_text(strings[words[town.tavern_diners2 - 2]]);
}

void Town::sir_or_milady(void)
{
    party.active_player = 0xFF;
    print_text(party[0].sex == 0x0B ? strings[Strings::sir_9AC2] : strings[Strings::milady_9AC6]);
}

/* Food goes onto the table next to the party (0x95 -> 0x9B above / 0x9A below). */
int Town::tavern_serve_food(void)
{
    put_char('"');
    print_text(strings[Strings::That_will_be_9ACE]);   /* "That will be " */
    print_number(' ', 1, (int16_t)town.shop_price);
    print_text(strings[Strings::gold_for_the_9ADC]);   /* " gold for the " */
    number_word();
    print_text(strings[Strings::of_ye_9AEC]);   /* " of ye," */
    sir_or_milady();
    put_char('.');
    if ((int16_t)party.gold < (int16_t)town.shop_price) {
        print_text(strings[Strings::CAN_T_PAY_Beat_it_9AF6]);   /* "CAN'T PAY? Beat it!" yells */
        print_text(strings[Strings::Id(town.shopkeeper_name)]);
        put_char('.');
        put_char('\n');
        return 1;
    }
    party.gold = (uint16_t)(party.gold - town.shop_price);
    shop_take_gold();
    if (town.tavern_diners == 0) {
        town.tavern_drinks++;
    } else {
        add_word_capped(9999, town.tavern_diners, &party.food);
        uint8_t *above = map.get_map_tile_ptr((unsigned)(map.y - 1), map.x);
        uint8_t *below = map.get_map_tile_ptr((unsigned)(map.y + 1), map.x);
        if (*above == 0x95) {
            *above = 0x9B;
            town.tavern_food_tile = 0x9B;
            world_tick();
        } else if (*below == 0x95) {
            *below = 0x9A;
            town.tavern_food_tile = 0x9A;
            world_tick();
        }
    }
    print_text(strings[Strings::Enjoy_9B16]);   /* "Enjoy!" */
    return 0;
}

int Town::tavern_order_food(void)
{
    put_char((uint8_t)TAVERN_KEY(TAVERN_FOOD_KEY));
    tavern_price_for_party((int16_t)table_word(MEAL_PRICE + town.shop_town * 2));
    return tavern_serve_food();
}

/* The fourth drink can be refused ("haven't ye had enough?"); insisting makes the party drunk
 * (25 turns of "Hic!") and costs 1 karma. Wine taverns ('W') have a wine list. Returns 1 if the
 * party was thrown out, 2 if it chose nothing. */
int Town::tavern_wine(void)
{
    put_char((uint8_t)TAVERN_KEY(TAVERN_WINE_KEY));
    if (town.tavern_drinks == 3) {
        print_text(strings[Strings::I_beg_thy_pardon_9B22]);   /* "I beg thy pardon, " */
        sir_or_milady();
        print_text(strings[Strings::says_9B38]);
        print_text(strings[Strings::Id(town.shopkeeper_name)]);
        print_text(strings[Strings::But_haven_t_ye_had_9B42]);   /* "But haven't ye had enough to drink?" */
        if (yes_no_key() == 'Y') {
            print_text(strings[Strings::Yes_9B6C]);
            return 0;
        }
        print_text(strings[Strings::No_9B72]);   /* "No!" */
        town.drunk_turns = 0x19;
        sub_byte_floor0(1, &party.karma);
    }
    tavern_price_for_party(1);
    if (TAVERN_KEY(TAVERN_WINE_KEY) != 'W') {
        town.tavern_diners = 0;   /* a drink, not a meal */
        return tavern_serve_food();
    }
    print_text(strings[Strings::Our_wine_list_9B76]);   /* "Our wine list, " */
    sir_or_milady();
    print_text(strings[Strings::Dot_NL_NL_9B88]);
    for (uint16_t o = 0x9B8C; o <= 0x9BE6; o += 0x12) print_text(strings[Strings::Id(o)]);   /* a) Rose ... f) Chablis */
    print_text(strings[Strings::Thy_choice_9BFA]);   /* "Thy choice?" */
    unsigned k;
    do k = (unsigned)key() & 0xFF; while (k != ' ' && (k < 'A' || k > 'F'));
    if (k != ' ') {
        put_char((uint8_t)k);
        print_text(strings[Strings::Ah_a_fine_choice_9C08]);   /* "Ah, a fine choice, " */
        sir_or_milady();
        put_char('.');
        int16_t price = (int16_t)table_word(WINE_PRICE + (k - 'A') * 2);
        if ((int16_t)party.gold < price) {
            print_text(strings[Strings::CAN_T_PAY_Beat_it_9C20]);
            print_text(strings[Strings::Id(town.shopkeeper_name)]);
            put_char('.');
            put_char('\n');
            return 1;
        }
        party.gold = (uint16_t)(party.gold - price);
        shop_take_gold();
        town.tavern_drinks++;
        print_text(strings[Strings::Enjoy_9C40]);   /* "Enjoy!" */
    }
    put_char('\n');
    put_char('\n');
    if (k == ' ') {
        shop_say(0x1413);
        return 2;
    }
    return 0;
}

/* 25 food per ration; an empty order gets "Hrumph." */
int Town::tavern_buy_rations(int member)
{
    put_char((uint8_t)TAVERN_KEY(TAVERN_RATIONS_KEY));
    put_char('\n');
    put_char('\n');
    town.shop_price = adjust_price_u(table_word(RATIONS_PRICE + town.shop_town * 2), intel_of(member));
    put_char('"');
    shop_say(table_word(RATIONS_TEXT + random_between(6, 0) * 2));
    print_text(strings[Strings::How_many_wouldst_thou_9C4A]);   /* "How many wouldst thou like?" */
    int want = read_number(2);
    if (want == 0) {
        print_text(strings[Strings::Hrumph_9C6A]);   /* "Hrumph." */
        return 2;
    }
    for (int left = want; left != 0; left--) {
        if ((int16_t)party.gold < (int16_t)town.shop_price) {
            print_text(strings[Strings::NL_NL_9C76]);
            if (want - left == 0) {
                if ((int16_t)party.food < 3) {   /* pity on the starving */
                    add_word_capped(9999, (uint16_t)(random_between(1, 0) + 1), &party.food);
                    shop_say(0x143F);
                } else {
                    print_text(strings[Strings::Thou_hast_neither_gold_9C7A]);   /* "Thou hast neither gold nor need! Out!" */
                    print_text(strings[Strings::yells_9CA4]);
                    print_text(strings[Strings::Id(town.shopkeeper_name)]);
                    print_text(strings[Strings::Dot_NL_9CAC]);
                }
                return 1;
            }
            print_text(strings[Strings::Thou_canst_afford_only_9CB0]);   /* "Thou canst afford only " */
            print_number(' ', 1, want - left);
            print_text(strings[Strings::Bang_Quote_NL_NL_9CCA]);
            return 0;
        }
        sub_word_floor0(town.shop_price, &party.gold);
        add_word_capped(9999, 0x19, &party.food);
        if (party.food == 9999) break;
    }
    print_text(strings[Strings::NL_NL_9CD0]);
    shop_take_gold();
    return 0;
}

/* 26 topics (ds:4C74); each has a price (ds:4D10) and fills & / * for the answer text. */
int Town::tavern_rumours(void)
{
    int topic = -1;
    put_char((uint8_t)TAVERN_KEY(TAVERN_RUMOUR_KEY));
    print_text(strings[Strings::NL_NL_Quote_9EFC]);
    for (;;) {
        print_text(strings[Strings::Of_what_wouldst_thou_9F00]);   /* "Of what wouldst thou hear my lore, " */
        sir_or_milady();
        print_text(strings[Strings::You_respond_9F24]);   /* "?" You respond: */
        read_line(0x0F, conversation.input);
        print_text(strings[Strings::NL_NL_9F36]);
        if (conversation.input[0] == 0) return 0;
        for (int i = 0; i < 0x1A; i++) {
            int pos = str_find_nocase(conversation.input, strings[Strings::Id(table_word(RUMOUR_KEYWORD + i * 2))]);
            if (pos == 0 || (pos > 0 && conversation.input[pos - 1] == ' ')) {
                topic = i;
                break;
            }
        }
        if (topic != -1) break;
        print_text(strings[Strings::That_I_cannot_help_thee_9F3A]);   /* "That, I cannot help thee with." */
    }
    town.shop_price = table_word(RUMOUR_PRICE + topic * 2);
    shop_say(0x134E);
    print_text(strings[Strings::Fair_nuff_9F5C]);   /* "Fair 'nuff?" */
    if (yes_no_key() == 'N') {
        print_text(strings[Strings::No_9F6C]);
        return 0;
    }
    print_text(strings[Strings::Yes_9F72]);
    if ((int16_t)party.gold < (int16_t)town.shop_price) {
        print_text(strings[Strings::Sorry_9F78]);   /* "Sorry, " */
        sir_or_milady();
        shop_say(0x146A);
        return 1;
    }
    party.gold = (uint16_t)(party.gold - town.shop_price);
    draw_status_bar();
    town.shop_item_name = table_word(RUMOUR_SUBJECT + topic * 2);
    town.shop_item_name2 = table_word(RUMOUR_PLACE_NAME + table_byte(RUMOUR_PLACE + topic) * 2);
    shop_say(table_word(RUMOUR_TEXT + rnd(3) * 2));
    print_text(strings[Strings::says_9F82]);   /* "says " */
    print_text(strings[Strings::Id(town.shopkeeper_name)]);
    print_text(strings[Strings::Dot_NL_NL_9F8A]);
    return 0;
}

void Town::tavern_farewell(void) { shop_farewell(town.tavern_farewell_kind); }

/* Rumours are only offered once the party has bought something. */
void Town::shop_tavern(int member)
{
    town.tavern_index = table_byte(TAVERN_INDEX + town.shop_town);
    town.tavern_farewell_kind = 0;   /* tavern_farewell_kind */
    town.tavern_food_tile = 0;   /* tavern_food_tile */
    shop_greeting();
    int k;
    do {
        k = key();
        if (k == 'N' || k == ' ') {
            print_text(strings[Strings::No_9F8E]);
            tavern_farewell();
            return;
        }
    } while (k != 'Y');
    print_text(strings[Strings::Yes_9F92]);
    shop_say(table_word(TAVERN_GREETING + town.tavern_index * 2));
    put_char('"');
    put_char(' ');
    if (get_cursor_col() >= 0x0F) put_char('\n');
    for (;;) {
        int r = 0;
        char c;
        for (;;) {
            c = (char)key();
            r = 0;
            if (c == ' ' || c == 0x1B || c == 0x0D) {
                tavern_farewell();
                return;
            }
            if (TAVERN_KEY(TAVERN_FOOD_KEY) == c && tavern_order_food() != 0) return;
            if (TAVERN_KEY(TAVERN_WINE_KEY) == c && (r = tavern_wine()) == 1) return;
            if (TAVERN_KEY(TAVERN_RATIONS_KEY) == c && (r = tavern_buy_rations(member)) == 1) return;
            if (TAVERN_KEY(TAVERN_RUMOUR_KEY) == c) {
                if (town.tavern_farewell_kind == 0) continue;
                if (tavern_rumours() != 0) return;
            }
            if (TAVERN_KEY(TAVERN_FOOD_KEY) == c || TAVERN_KEY(TAVERN_WINE_KEY) == c || TAVERN_KEY(TAVERN_RATIONS_KEY) == c || TAVERN_KEY(TAVERN_RUMOUR_KEY) == c)
                break;
        }
        if (r != 2) town.tavern_farewell_kind = 1;
        draw_status_bar();
        print_text(strings[Strings::Anything_else_for_thee_9F9A]);   /* "Anything else for thee?" */
        if (yes_no_key() == 'N') {
            print_text(strings[Strings::No_9FB6]);
            tavern_farewell();
            return;
        }
        print_text(strings[Strings::Yes_9FBA]);
        shop_say(table_word(TAVERN_MENU + town.tavern_index * 2));
        put_char('"');
        put_char(' ');
    }
}

/* ---------------------------------------------------------------- SHOPPES2: shipwright (type 3) */

int Town::can_afford(int price)
{
    if (price <= (int16_t)party.gold) return 1;
    shop_say(0x198E);
    shop_print(strings[Strings::yells_9FC2]);   /* yells $. */
    town.shipwright_can_buy = 1;   /* shipwright_can_buy: skips the farewell */
    return 0;
}

/* pay: charge shop_price; place: put the ship at this town's dock (ds:4D76 / 4D7A). The "milady"
 * test compares the sex byte (0x0B/0x0C) with 'F', so everyone is "sir". Returns 1 to stop. */
int Town::shipwright_offer(int member, int pay, int place)
{
    if (place) {
        shop_say(0x19AB);
        town.stored_ship_x = table_byte(SHIP_X + town.shop_town);
        town.stored_ship_y = table_byte(SHIP_Y + town.shop_town);
    }
    if (pay) {
        party.gold = (uint16_t)(party.gold - town.shop_price);
        town.shipwright_bought = 1;   /* shipwright_bought */
        shop_take_gold();
    }
    draw_status_bar();
    shop_say(0x19DA);
    print_text(party[member].sex == 'F' ? strings[Strings::milady_9FCC] : strings[Strings::sir_9FD4]);
    print_text(strings[Strings::Qm_Quote_SP_9FD8]);
    int k = yes_no_key();
    print_text(k == 'Y' ? strings[Strings::Yes_9FDC] : strings[Strings::No_9FE0]);
    return k != 'Y';
}

/* stored_ship: 0x80 a frigate, 0x40 a skiff, low bits extra skiffs. */
void Town::shipwright_menu(int member)
{
    int stop = 0;
    do {
        shop_say(0x18EB);
        int k = key();
        if (k == 0x1B || k == ' ') {
            stop = 1;
        } else if (k == 'F') {
            print_text(strings[Strings::F_9FE4]);
            stop = 1;
            if ((town.stored_ship & 0xC0) == 0) {
                town.shop_price = adjust_price_u(table_word(SHIP_PRICE + town.shop_town * 2), intel_of(member));
                shop_say(0x183E);
                shop_say(0x1A50);
                if (yes_no_key() != 'Y') {
                    print_text(strings[Strings::No_9FF6]);
                    return;
                }
                print_text(strings[Strings::Yes_9FF0]);
                if (!can_afford((int16_t)town.shop_price)) return;
                town.stored_ship = 0x82;
                stop = shipwright_offer(member, 1, 1);
            } else {   /* already has a ship: trade it in? (costs 10000, never affordable) */
                shop_say(0x19F2);
                if (yes_no_key() == 'Y') {
                    print_text(strings[Strings::Yes_9FE8]);
                    can_afford(10000);
                } else {
                    print_text(strings[Strings::No_9FEC]);
                    shipwright_menu(member);
                }
            }
        } else if (k == 'S') {
            print_text(strings[Strings::S_9FFA]);
            town.shop_price = adjust_price_u(table_word(SKIFF_PRICE + town.shop_town * 2), intel_of(member));
            shop_say(0x188C);
            shop_say(0x1A50);
            int yes = yes_no_key() == 'Y';
            stop = 1;
            if (!yes) {
                print_text(strings[Strings::No_A004]);
                return;
            }
            print_text(strings[Strings::Yes_9FFE]);
            if (!can_afford((int16_t)town.shop_price)) return;
            if ((town.stored_ship & 0xC0) == 0) {
                town.stored_ship = 0x40;
                stop = shipwright_offer(member, 1, 1);
            } else if (town.stored_ship & 0x80) {   /* a skiff for the frigate */
                shop_say(0x193B);
                town.stored_ship++;
                stop = shipwright_offer(member, 1, 0);
            } else if (town.stored_ship & 0x40) {
                shop_say(0x195F);
                stop = shipwright_offer(member, 0, 0);
            }
        }
    } while (stop == 0);
}

void Town::shop_shipwright(int member)
{
    town.shipwright_can_buy = 0;
    town.shipwright_bought = 0;
    shop_greeting();
    for (;;) {
        int k = key();
        if (k == ' ' || k == 'N') {
            print_text(strings[Strings::No_A00C]);
            break;
        }
        if (k == 'Y') {
            print_text(strings[Strings::Yes_A008]);
            shipwright_menu(member);
            break;
        }
    }
    if (town.shipwright_can_buy == 0) shop_farewell(town.shipwright_bought);
}

/* ---------------------------------------------------------------- SHOPPES3: inn (type 7) */

int Town::count_left_companions(void)
{
    int n = 0;
    for (int i = 0; i < 16; i++) n += party[i].where == map.id();
    return n;
}

int Town::inn_sir_or_milady(uint8_t sex)
{
    print_text(strings[Strings::NL_NL_4D84]);
    if (count_left_companions() < table_byte(INN_ROOMS + town.shop_town)) return 1;
    print_text(strings[Strings::I_am_sorry_4D87]);   /* "I am sorry, " */
    print_text(sex == 0x0C ? strings[Strings::milady_4D95] : strings[Strings::sir_4D9C]);
    print_text(strings[Strings::but_we_have_no_room_4DA0]);   /* ", but we have no room available." */
    return 0;
}

void Town::restore_mp(PartyMember *p)
{
    if (p->cls == 'A' || p->cls == 'M') p->mp = p->intel;
    else if (p->cls == 'B') p->mp = p->intel >> 1;
}

/* A night's rest: the party sleeps in the inn's bed (ds:4E7A / 4E80) until 6:00, then is
 * healed; anyone still poisoned dies. Returns -1 after resting, -2 if no room. */
int Town::inn_rest(int result, uint8_t sex, int intel)
{
    put_char('R');
    if (!inn_sir_or_milady(sex)) return -2;
    put_char('"');
    town.shop_price = adjust_price_s((uint16_t)(table_byte(INN_PRICE + town.shop_town) * party.size()), intel);
    shop_say(table_word(INN_TEXT + town.shop_town * 2));
    print_text(strings[Strings::Wilt_thou_take_it_4DCA]);   /* "Wilt thou take it?" */
    int k = ask_yes_no();
    print_text(strings[Strings::NL_NL_4DE0]);
    if (k != 'Y') return result;
    if ((int16_t)party.gold < (int16_t)town.shop_price) {
        print_text(strings[Strings::Highwaymen_Cheap_at_that_4DE3]);   /* "Highwaymen! Cheap, at that! OUT!" */
        shop_print(strings[Strings::screams_4E07]);   /* screams $. */
        return result;
    }
    party.gold = (uint16_t)(party.gold - town.shop_price);
    shop_take_gold();
    draw_status_bar();
    print_text(strings[Strings::Have_a_pleasant_night_4E13]);   /* "Have a pleasant night, " */
    print_text(sex == 0x0C ? strings[Strings::milady_4E2C] : strings[Strings::sir_4E33]);
    shop_print(strings[Strings::says_4E37]);
    wait_turns(10);
    restore_open_door(map.open_door_y, map.open_door_x, map.open_door_tile);
    uint8_t active = party.active_player;
    map.x = table_byte(INN_BED_X + town.shop_town);
    map.y = table_byte(INN_BED_Y + town.shop_town);
    view.rebuild = true;
    for (unsigned i = 0; i < party.size(); i++)
        if (party[i].good()) party[i].status = 'S';
    draw_status_bar();
    world_tick();
    print_text(strings[Strings::Zzzzzz_4E44]);   /* "Zzzzzz...." */
    for (int i = 0; i < 12; i++) pass_time(5);
    while (game_time.hour != 6 && !platform->quit_requested()) {
        wait_turns(1);
        regen_ring_tick();
        draw_status_bar();
        pass_time(9);
        if (game_time.hour == 0x14 || game_time.hour == 5) update_lamps();
    }
    print_text(strings[Strings::Morning_4E51]);   /* "Morning!" */
    place_npcs();
    for (unsigned i = 0; i < party.size(); i++) {
        PartyMember *p = &party[i];
        if (p->dead()) continue;
        p->hp = p->max_hp;
        restore_mp(p);
        if (p->poisoned()) {
            p->status = 'D';
            p->hp = 0;
            put_char('\n');
            print_text(p->name);
            print_text(strings[Strings::has_passed_away_4E5B]);   /* " has passed away." */
        } else if (p->asleep()) {
            p->status = 'G';
        }
    }
    map.x++;
    view.rebuild = true;
    party.active_player = active;
    draw_status_bar();
    return -1;
}

/* A member (not the Avatar) stays at the inn: marked with this location and moved to the end of
 * the 16-member roster; billed per month at pick-up. */
int Town::inn_leave_companion(int result, uint8_t sex, int intel)
{
    put_char('L');
    if (!inn_sir_or_milady(sex)) return result;
    if (party.alone()) {
        shop_say(0x26E8);
        return -1;
    }
    for (;;) {
        shop_print(strings[Strings::asks_Who_will_stay_4E86]);   /* $ asks, "Who will stay?" */
        int m = ask_member_number();
        if (m == -1) {
            print_text(strings[Strings::Nobody_4EA0]);   /* "Nobody" */
            return -2;
        }
        print_text(strings[Strings::NL_NL_4EA9]);
        if (m == 0) {
            print_text(strings[Strings::Thy_friend_4EAC]);   /* "Thy friend" */
            if (party.size() > 2) put_char('s');
            print_text(strings[Strings::will_not_leave_thee_4EB7]);   /* " will not leave thee!" */
            continue;
        }
        if (party[m].dead()) {
            shop_say(0x2723);
            return -1;
        }
        town.shop_price = adjust_price_s((uint16_t)(table_byte(INN_PRICE + town.shop_town) * 10), intel);
        print_text(strings[Strings::The_rate_for_our_most_4ECF]);   /* "The rate for our most comfortable room will be " */
        shop_print(strings[Strings::gold_per_month_due_at_4F00]);   /* "% gold per month, due at check-out." */
        print_text(strings[Strings::Wilt_thou_take_it_4F24]);
        int k = ask_yes_no();
        print_text(strings[Strings::NL_NL_4F3A]);
        if (k == 'Y') {
            result = 1;
            if (m == party.active_player) party.active_player = 0xFF;
            else if (m < party.active_player && party.active_player != 0xFF) party.active_player--;
            party[m].where = map.id();
            party[m].inn_months = 0;
            party.move_out(m);
            draw_status_bar();
            shop_print(strings[Strings::I_thank_thee_says_4F3D]);   /* "I thank thee." says $. */
        }
        return result;
    }
}

int Town::prev_inn_guest(int slot)
{
    do {
        if (--slot == 0) return 0;
    } while (party[slot].where != map.id());
    return slot;
}

int Town::next_inn_guest(int slot)
{
    do {
        if (++slot > 0x0F) break;
    } while (party[slot].where != map.id());
    return slot == 0x10 ? 0 : slot;
}

int Town::inn_pick_up_companion(int result, int intel)
{
    int guests = count_left_companions(), sel;
    if (party.full()) {
        print_text(strings[Strings::One_must_first_be_left_4F57]);   /* "One must first be left behind!" */
        return result;
    }
    if (guests == 0) {
        shop_print(strings[Strings::No_one_here_is_from_4F7A]);   /* "No one here is from thy party!" says $. */
        return result;
    }
    if (guests < 2) {
        sel = next_inn_guest(0);
    } else {   /* the guest register in the side panel, a bar to choose with */
        print_text(strings[Strings::Who_will_check_out_4FA7]);   /* "Who will check out?" */
        select_text_window(1);
        clear_side_panel();
        define_text_window(9, 0x26, 1, 0x18, 1);
        put_char(0xFF);
        define_text_window(9, 0x27, 1, 0x18, 1);
        draw_panel_box(7);
        set_cursor(1, 1);
        print_text(strings[Strings::GUEST_4FC0]);   /* "    GUEST" */
        set_cursor(2, 1);
        print_text(strings[Strings::REGISTER_4FCA]);   /* "  REGISTER:" */
        for (int i = 1; i < 16; i++)
            if (party[i].where == map.id()) {
                set_cursor(get_cursor_row(), 4);
                print_text(party[i].name);
                put_char('\n');
            }
        select_text_window(2);
        sel = next_inn_guest(0);
        int y = 0x28, done = 0;
        set_draw_color(gfx.color_text);
        while (!done) {
            xor_fill_rect(y + 7, 0x131, y, 0xC6);
            for (;;) {
                unsigned k = (unsigned)key() & 0xFF;
                int moved = 0;
                if (k == 3 || k == 1) {
                    int p = prev_inn_guest(sel);
                    if (p) {
                        xor_fill_rect(y + 7, 0x131, y, 0xC6);
                        y -= 8;
                        sel = p;
                        moved = 1;
                    }
                } else if (k == 4 || k == 2) {
                    int n = next_inn_guest(sel);
                    if (n) {
                        xor_fill_rect(y + 7, 0x131, y, 0xC6);
                        y += 8;
                        sel = n;
                        moved = 1;
                    }
                } else if (k == 0x0D || k == ' ' || k == 0x1B) {
                    if (k == 0x1B) {
                        print_text(strings[Strings::No_one_4FD8]);   /* "No one" */
                        sel = 0;
                    }
                    done = 1;
                    break;
                }
                if (moved) break;
            }
        }
        draw_panel_bottom_bar();
        draw_status_bar();
    }
    if (sel == 0) return result;
    unsigned months = party[sel].inn_months ? party[sel].inn_months : 1;
    town.shop_price = (uint16_t)(months * adjust_price_s((uint16_t)(table_byte(INN_PRICE + town.shop_town) * 10), intel));
    shop_print(strings[Strings::That_will_be_gold_please_4FE1]);   /* "That will be % gold, please." */
    if ((int16_t)party.gold < (int16_t)town.shop_price) {
        shop_say(0x275A);
        alert_town();
        return -1;
    }
    party.gold = (uint16_t)(party.gold - town.shop_price);
    shop_take_gold();
    draw_status_bar();
    PartyMember *p = &party.move_in(sel);
    const char *text;
    if (p->poisoned()) {   /* poison kills while away */
        p->status = 'D';
        p->hp = 0;
        text = strings[Strings::Thy_friend_has_died_by_5005];   /* "Thy friend has died, by the way." */
    } else {
        p->hp = p->max_hp;
        restore_mp(p);
        text = strings[Strings::I_hope_thou_hast_found_5028];   /* "I hope thou hast found thy stay enjoyable," */
    }
    print_text(text);
    shop_print(strings[Strings::says_5055]);   /* says $. */
    draw_status_bar();
    return 1;
}

void Town::shop_inn(int member)
{
    int result = 0, done = 0;
    int intel = intel_of(member);
    uint8_t sex = party[member].sex;
    party.active_spell_turns = 0;
    party.active_spell = 0;
    draw_status_bar();
    shop_greeting();
    while (!done) {
        int k;
        do {
            k = key();
            if (k == ' ') k = 'N';
        } while (k != 'N' && k != 'Y');
        print_text(k == 'N' ? strings[Strings::No_505F] : strings[Strings::Yes_5062]);
        if (k == 'N') break;
        shop_print(strings[Strings::asks_Art_thou_here_to_5066]);   /* $ asks, "Art thou here to Pick up or */
        print_text(strings[Strings::Leave_a_companion_or_to_508E]);   /* Leave a companion, or to Rest for the night?" */
        for (;;) {
            unsigned c = (unsigned)key() & 0xFF;
            if (c == ' ') {
                done = 1;
                break;
            }
            if (c == 'L') {
                int r = inn_leave_companion(result, sex, intel);
                if (r > -2) result = r;
                if (result == -1) done = 1;
                break;
            }
            if (c == 'P') {
                result = inn_pick_up_companion(result, intel);
                if (result == -1) done = 1;
                break;
            }
            if (c == 'R') {
                int r = inn_rest(result, sex, intel);
                if (r > -2) {
                    result = r;
                    done = 1;
                }
                break;
            }
        }
        if (!done) print_text(strings[Strings::Is_there_anything_more_I_50BD]);   /* "Is there anything more I can do for thee?" */
    }
    shop_farewell(result);
}
