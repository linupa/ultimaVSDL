/* shops.c - merchants (SHOPPES, SHOPPES2, SHOPPES3 overlays). See shops.h. String and table
 * addresses are ds offsets from the original; the comments quote the strings. */
#include <stdio.h>
#include <string.h>
#include "commands.h"
#include "data.h"
#include "sound.h"
#include "gfx.h"
#include "input.h"
#include "map.h"
#include "misc.h"
#include "objects.h"
#include "shops.h"
#include "stats.h"
#include "status.h"
#include "text.h"
#include "town.h"
#include "view.h"

#define TEXT_BUFFER ((uint16_t)offsetof(struct DGroup, text_buffer))
#define S(off)      ds_str(off)
#define W(off)      ds_u16(off)
#define B(off)      ds_u8(off)

/* get_command_key, but when the window is closed it cycles through keys that end every shop
 * prompt, so the original's wait-for-Y/N loops can't hang. */
static int key(void)
{
    static const int quit_keys[] = {'N', ' ', 0x1B, 0x0D};
    static int q;
    int k = get_command_key();
    if (platform_quit_requested()) k = quit_keys[q++ & 3];
    return k;
}

static int yes_no_key(void)
{
    int k;
    do k = key(); while (k != 'Y' && k != 'N');
    return k;
}

static int intel_of(int member) { return dg.party[member].intel; }

/* price + price * (100 - 3 * INT) / 100, the division unsigned as in the original (MSC
 * __aNuldiv): from INT 34 the negative product wraps. */
static uint16_t adjust_price_u(uint16_t price, int intel)
{
    int32_t prod = (int32_t)(int16_t)price * (int32_t)(100 - 3 * intel);
    return (uint16_t)(price + (uint16_t)((uint32_t)prod / 100u));
}

/* The inn's version divides signed. */
static uint16_t adjust_price_s(uint16_t price, int intel)
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

void shop_print(const char *text)
{
    char *buf = (char *)dg.shop_text_buf, *dst = buf;
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
            dst = copy_end(S(W((uint16_t)(0x23EA + b * 2))), dst + 1);
            if (*p != 0 && (uint8_t)*p < 0x80) *dst++ = ' ';
            continue;
        }
        switch (b) {
        case '%': snprintf(num, sizeof num, "%d", (int16_t)dg.shop_price); src = num; break;
        case '^': snprintf(num, sizeof num, "%d", (int16_t)dg.shop_quantity); src = num; break;
        case '#': src = S(dg.shop_name); break;
        case '$': src = S(dg.shopkeeper_name); break;
        case '&': src = S(dg.shop_item_name); break;
        case '*': src = S(dg.shop_item_name2); break;
        default:  src = dg.hour < 12 ? S(0x7826) : dg.hour < 18 ? S(0x782E) : S(0x7838); break;   /* @ */
        }
        dst = copy_end(src, dst);
    }
    strcpy(dst, run);
    print_text(buf);
}

/* The original reads 1500 bytes into text_buffer, which runs on to shop_text_buf. */
void shop_say(uint16_t text_offset)
{
    load_file_part(text_offset, 0x5DC, ds_ptr(TEXT_BUFFER), S(0x7840));   /* SHOPPE.DAT */
    shop_print((const char *)ds_ptr(TEXT_BUFFER));
}

void shop_take_gold(void)
{
    if (dg.shadowlord_here == 0) sub_word_floor0((uint16_t)random_between(0x40, 1), &dg.gold);
}

void shop_greeting(void)
{
    put_char('"');
    shop_say(W((uint16_t)(0x3B2A + dg.shop_type * 8 + rnd(3) * 2)));
    if (get_cursor_col() < 0x0C) {
        if (get_cursor_col() != 0) {
            put_char(' ');
            return;
        }
        print_text(S(0x7850));
    } else {
        print_text(S(0x784C));
    }
}

void shop_farewell(int kind)
{
    if (kind == 0) {
        print_text(S(0x7854));
        shop_say(W((uint16_t)(0x3B6A + dg.shop_type * 8 + rnd(3) * 2)));
    } else if (kind == 1) {
        print_text(S(0x7858));
        shop_say(W((uint16_t)(0x3BAA + dg.shop_type * 8 + rnd(3) * 2)));
    } else {
        return;
    }
    if (get_cursor_col() != 0) put_char('\n');
    shop_print(S(0x785C));   /* "says $." */
}

int ask_yes_no(void)
{
    int k = yes_no_key();
    print_text(k == 'N' ? S(0x7866) : S(0x786A));
    return k;
}

/* ---------------------------------------------------------------- provisioner (type 5) */

int provisioner_offer(int result, int item, int member)
{
    dg.shop_price = adjust_price_u(W((uint16_t)(0x3BEA + dg.shop_town * 8 + item * 2)), intel_of(member));
    print_text(S(0x786E));
    shop_say(W((uint16_t)(0x3C0A + item * 2)));
    print_text(S(0x7872));   /* "Interested?" */
    int k = yes_no_key();
    if (k == 'N') {
        print_text(S(0x7882));   /* "No ... What else, then?" */
        return result;
    }
    print_text(S(0x789A));
    if ((int16_t)dg.shop_price > (int16_t)dg.gold) {
        shop_say(0x21E6);
        return -1;
    }
    dg.gold = (uint16_t)(dg.gold - dg.shop_price);
    shop_take_gold();
    draw_status_bar();
    if (item == 0) add_byte_capped(99, 3, &dg.keys);
    else if (item == 1) add_byte_capped(99, 4, &dg.gems);
    else if (item == 2) add_byte_capped(99, 5, &dg.torches);
    shop_print(S(0x78A0));   /* "Sold!" says $. "What else, " */
    print_text(dg.party[member].sex == 0x0C ? S(0x78C0) : S(0x78C8));   /* m'lady / m'lord */
    print_text(S(0x78D0));
    return 1;
}

int provisioner_menu(int member)
{
    int result = 0;
    for (;;) {
        print_text(S(0x78D4));   /* a.........Keys */
        print_text(S(0x78E4));
        print_text(S(0x78F4));
        print_text(S(0x7906));   /* Thy concern? */
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

void shop_provisioner(int member)
{
    int kind = 0;
    shop_greeting();
    for (;;) {
        unsigned k = (unsigned)key() & 0xFF;
        if (k == ' ' || k == 'N') {
            print_text(S(0x7928));
            break;
        }
        if (k == 'Y') {
            print_text(S(0x7916));   /* "We sell:" */
            kind = provisioner_menu(member);
            break;
        }
    }
    shop_farewell(kind);
}

/* ---------------------------------------------------------------- reagents (type 4) */

int reagent_offer(int result, int reagent, int member)
{
    int i, n = 0;
    for (i = 0; i < 8; i++)   /* the reagent-th one this shop sells */
        if (B((uint16_t)(0x3A32 + dg.shop_town * 8 + i)) != 0) {
            if (n == reagent) break;
            n++;
        }
    if (i == 8) return -2;
    if (dg.reagents[i] == 99) {
        print_text(S(0x792C));   /* "Thou canst not carry any more!" */
        key();
        return result;
    }
    dg.shop_price = adjust_price_u(B((uint16_t)(0x3A32 + dg.shop_town * 8 + i)), intel_of(member));
    dg.shop_quantity = B((uint16_t)(0x3A5A + dg.shop_town * 8 + i));
    print_text(S(0x7952));
    shop_print((const char *)ds_at(TEXT_BUFFER + W((uint16_t)(0x3C10 + i * 2)) - 0x1A67));   /* text loaded by reagent_menu */
    print_text(S(0x7956));   /* " Is this thy need?" */
    if (get_cursor_col() > 0x0C) print_text(S(0x796C));
    int k = yes_no_key();
    if (k == 'N') {
        print_text(S(0x7970));
        return result;
    }
    print_text(S(0x7982));
    if ((int16_t)dg.shop_price > (int16_t)dg.gold) {
        shop_print((const char *)ds_at(0xB6E2));   /* in the loaded text: "not enough gold" */
        return -1;
    }
    dg.gold = (uint16_t)(dg.gold - dg.shop_price);
    shop_take_gold();
    draw_status_bar();
    add_byte_capped(99, (uint8_t)dg.shop_quantity, &dg.reagents[i]);
    shop_print(S(0x7988));   /* "I thank thee!" says $. */
    print_text(S(0x79A2));   /* "Anything else?" */
    return 1;
}

int reagent_menu(int member)
{
    int result = 0;
    load_file_part(0x1A67, 0x5DC, ds_ptr(TEXT_BUFFER), S(0x7A0A));   /* SHOPPE.DAT */
    for (;;) {
        uint8_t letter = 'A';
        for (int i = 0; i < 8; i++)
            if (B((uint16_t)(0x3A32 + dg.shop_town * 8 + i)) != 0) {
                put_char(letter++);
                print_text(S(0x7A16));   /* "..." */
                print_text(S(W((uint16_t)(0x3C20 + i * 2))));
                put_char('\n');
            }
        print_text(S(0x7A1A));   /* "Thy interest?" */
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

void shop_reagents(int member)
{
    int kind = 0;
    shop_greeting();
    for (;;) {
        unsigned k = (unsigned)key() & 0xFF;
        if (k == ' ' || k == 'N') {
            print_text(S(0x7A44));
            break;
        }
        if (k == 'Y') {
            print_text(S(0x7A2C));   /* "Fine! We sell:" */
            kind = reagent_menu(member);
            break;
        }
    }
    shop_farewell(kind);
}

/* ---------------------------------------------------------------- stables (type 2) */

/* The horse appears on a free floor (0x44, 0x45) or grass (5) square at one of four offsets
 * (ds:3C38 / ds:3C40) from the party. */
void shop_stables(int member)
{
    int slot = 0;
    while (slot < 32 && dg.objects[slot].tile != 0) slot++;
    int k4;
    uint8_t x = 0, y = 0;
    for (k4 = 0; k4 < 4; k4++) {
        int dx = (int16_t)W((uint16_t)(0x3C38 + k4 * 2)), dy = (int16_t)W((uint16_t)(0x3C40 + k4 * 2));
        uint8_t t = *get_map_tile_ptr((unsigned)(dy + dg.party_y), (unsigned)(dx + dg.party_x));
        y = (uint8_t)(dy + dg.party_y);
        x = (uint8_t)(dx + dg.party_x);
        if (find_object_at(dg.z_level, y, x) == 0 && (t == 0x44 || t == 0x45 || t == 5)) break;
    }
    if (k4 == 4 || slot == 32) {
        print_text(S(0x7A48));   /* "The stables are closed." */
        return;
    }
    int result = 0;
    dg.shop_price = adjust_price_u(W((uint16_t)(0x3C30 + dg.shop_town * 2)), intel_of(member));
    shop_greeting();
    for (;;) {
        unsigned k = (unsigned)key() & 0xFF;
        if (k == ' ' || k == 'N') {
            print_text(S(0x7AB2));
            break;
        }
        if (k != 'Y') continue;
        print_text(S(0x7A62));
        shop_say(0x1643);
        print_text(S(0x7A6A));   /* "Deal?" */
        if (yes_no_key() == 'N') {
            print_text(S(0x7A74));
            break;
        }
        print_text(S(0x7A78));   /* "Yes!" */
        if ((int16_t)dg.gold < (int16_t)dg.shop_price) {
            print_text(S(0x7A7E));   /* "Thou couldst not afford to " */
            shop_print(S(0x7A9E));   /* "feed it!" yells $. */
            result = -1;
        } else {
            dg.gold = (uint16_t)(dg.gold - dg.shop_price);
            shop_take_gold();
            MapObject *o = &dg.objects[slot];
            o->hull = o->skiffs = o->flags = 0;
            o->tile2 = o->tile = 0x10;
            o->x = x;
            o->y = y;
            o->z = dg.z_level;
            world_tick();
            draw_status_bar();
            result = 1;
        }
        break;
    }
    shop_farewell(result);
}

/* ---------------------------------------------------------------- armoury (type 0) */

int armoury_offer(int result, int item, int member)
{
    dg.shop_price = adjust_price_u(W((uint16_t)(0x3A82 + item * 2)), intel_of(member));
    print_text(S(0x7B5E));
    shop_say(W((uint16_t)(0x3C48 + item * 2)));
    print_text(S(0x7B62));
    print_text(S(W((uint16_t)(0x3CA6 + rnd(3) * 2))));
    print_text(S(0x7B66));
    int k = yes_no_key();
    if (k == 'N') {
        print_text(S(0x7B6A));
    } else {
        print_text(S(0x7B70));
        if (dg.inventory[item] == 99) {
            print_text(S(0x7B76));   /* "Thou canst not carry any more!" */
            shop_print(S(0x7B9A));
            key();
        } else if ((int16_t)dg.gold < (int16_t)dg.shop_price) {
            print_text(S(0x7BA4));
            print_text(S(W((uint16_t)(0x3CAE + rnd(3) * 2))));
            shop_print(S(0x7BA8));   /* yells $. */
            result = -1;
        } else {
            dg.gold = (uint16_t)(dg.gold - dg.shop_price);
            shop_take_gold();
            draw_status_bar();
            if (item == 0x1B || item == 0x1D) dg.inventory[item] = 99;   /* a full quiver */
            else add_byte_capped(99, 1, &dg.inventory[item]);
            shop_print(S(0x7BB4));   /* "Sold!" */
            result = 1;
        }
    }
    if (result != -1) {
        print_text(S(0x7BBC));   /* "Anything else, " */
        print_text(result == 0 ? S(0x7BDC) : dg.party[member].sex == 0x0C ? S(0x7BCE) : S(0x7BD6));
    }
    return result;
}

int armoury_buy_menu(int member)
{
    int result = 0;
    for (;;) {
        print_text(S(0x7C44));
        int n;
        for (n = 0; n < 8; n++) {   /* this shop's stock, ds:3AE2, 0xFF ends */
            unsigned id = B((uint16_t)(0x3AE2 + dg.shop_town * 8 + n));
            if (id == 0xFF) break;
            put_char((uint8_t)('a' + n));
            print_text(S(0x7C48));   /* "..." */
            const char *name = S(W((uint16_t)(offsetof(struct DGroup, item_names) + id * 2)));
            if (strlen(name) >= 0x0D) name = S(W((uint16_t)(offsetof(struct DGroup, item_short_names) + id * 2)));
            print_text(name);
            if (get_cursor_col() != 0) put_char('\n');
        }
        put_char('\n');
        print_text(S(W((uint16_t)(0x3CB6 + rnd(3) * 2))));
        print_text(S(0x7C4C));
        for (;;) {
            unsigned k = (unsigned)key();
            int idx = (int)(k & 0xFF) - 'A';
            if (idx >= 0 && idx < n) {
                put_char((uint8_t)(k + 0x20));
                result = armoury_offer(result, B((uint16_t)(0x3AE2 + dg.shop_town * 8 + idx)), member);
                if (result == -1) return result;
                break;
            }
            if ((k & 0xFF) == 0x1B || (k & 0xFF) == ' ') return result;
        }
    }
}

int sell_list_init(void)
{
    for (int i = 0; i < 0x30; i++)
        if (dg.inventory[i]) return 1;
    return 0;
}

#define NEXT(i) list_next_item(0xFF, dg.inventory, 0x30, (i))
#define PREV(i) list_prev_item(0xFF, dg.inventory, 0x30, (i))

/* Four rows of owned items in the panel box: *cursor is highlighted (0xFD inverse), *top the
 * first row shown. Arrows (0x18 up, 0x19 down, 0x12 both) show if there is more. Returns the
 * cursor's screen row. */
uint8_t armoury_sell_list(int *cursor, int *top)
{
    uint8_t cursor_row = 0;   /* uninitialised in the original if the cursor isn't shown */
    if (dg.inventory[*cursor] == 0) {   /* the item was sold out: move to a neighbour */
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
            print_list_entry(0x2D, (uint16_t)offsetof(struct DGroup, item_short_names), dg.inventory, item);
            if (item == *cursor) put_char(0xFD);
            set_cursor(get_cursor_row(), 1);
        } while (get_cursor_row() != 5 && (item = NEXT(item)) != -1);
    }
    for (int r = get_cursor_row(); r != 5; r++) {
        set_cursor(r, 1);
        print_text(S(0x7C50));
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

int armoury_sell_item(int item, int member)
{
    if (item == 0x1B || item == 0x1D) {
        shop_print(S(0x7D32));   /* "We don't deal in used ammunition!" */
        return 1;
    }
    print_text(S(0x7D64));
    uint16_t base = W((uint16_t)(0x3A82 + item * 2));
    if (base == 0) {
        shop_print(S(0x7D8C));   /* "That, I cannot buy from thee." */
    } else {
        uint16_t v = (uint16_t)(intel_of(member) * base);
        dg.shop_price = (uint16_t)((uint32_t)v * 3 / 100 + 1);   /* INT * price * 3 / 100 + 1 */
        dg.shop_item_name = W((uint16_t)(0x3CCE + item * 2));
        if (dg.shop_item_name == 0) dg.shop_item_name = W((uint16_t)(offsetof(struct DGroup, item_names) + item * 2));
        shop_print((const char *)ds_at(TEXT_BUFFER + W((uint16_t)(0x3CBE + rnd(7) * 2)) - 0x0BC0));   /* loaded by armoury_sell */
        print_text(S(0x7D68));   /* "Deal?" */
        if (yes_no_key() == 'N') {
            print_text(S(0x7D72));
        } else {
            shop_print(S(0x7D76));   /* "Done!" says $. */
            add_word_capped(9999, dg.shop_price, &dg.gold);
            sub_byte_floor0(1, &dg.inventory[item]);
        }
        draw_gold();
    }
    return 0;
}

static void draw_panel_box(int inner_rows)
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

void armoury_sell(int member)
{
    if (!sell_list_init()) {
        shop_print(S(0x7F20));   /* "Thou hast nothing to sell!" growls $. */
        return;
    }
    print_text(S(W((uint16_t)(0x3D2E + rnd(3) * 2))));
    print_text(S(0x7EF0));
    draw_panel_title(S(0x7EF4));
    select_text_window(1);
    define_text_window(6, 0x26, 1, 0x18, 1);
    put_char(0xFF);
    define_text_window(9, 0x27, 1, 0x18, 1);
    draw_panel_box(4);
    int cursor = NEXT(-1), top = cursor;
    int row = armoury_sell_list(&cursor, &top);
    int result = 0, done = 0;
    load_file_part(0x0BC0, 0x5DC, ds_ptr(TEXT_BUFFER), S(0x7EFA));   /* SHOPPE.DAT: sell phrases */
    while (!done) {
        unsigned k = (unsigned)key() & 0xFF;
        if (k == 0x0D || k == ' ') {
            result = armoury_sell_item(cursor, member);
            if (result == 0 && sell_list_init()) {
                row = armoury_sell_list(&cursor, &top);
                print_text(S(0x7F06));
                print_text(S(W((uint16_t)(0x3D3E + rnd(3) * 2))));
                print_text(S(0x7F0A));
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
    print_text(S(0x7F0E));
    print_text(S(W((uint16_t)(0x3D36 + rnd(3) * 2))));
    print_text(S(0x7F12));
    if (sell_list_init()) shop_print(S(0x7F16));   /* says $. */
}

void shop_armoury(int member)
{
    int kind = 0;
    shop_print(S(0x8018));   /* "Good @, and welcome to #!" */
    key();
    shop_print(S(0x8036));   /* $ says, " */
    print_text(S(W((uint16_t)(0x3D46 + rnd(1) * 2))));
    print_text(S(0x8042));
    for (;;) {
        unsigned k = (unsigned)key() & 0xFF;
        if (k == ' ') {
            print_text(S(0x8056));
            break;
        }
        if (k == 'B') {
            print_text(S(0x8046));   /* "Buy" */
            print_text(S(W((uint16_t)(0x3D4A + rnd(3) * 2))));
            print_text(S(W((uint16_t)(0x3D52 + rnd(3) * 2))));
            kind = armoury_buy_menu(member);
            break;
        }
        if (k == 'S') {
            print_text(S(0x804E));   /* "Sell" */
            armoury_sell(member);
            kind = -1;
            break;
        }
    }
    shop_farewell(kind);
}

/* ---------------------------------------------------------------- healer (type 6) */

int healer_ask_who(void)
{
    if (dg.party_size == 1) return 0;
    print_text(S(0x805A));   /* "Who needs my aid?" */
    int m = ask_member_number();
    if (m == -1) print_text(S(0x8072));   /* "No one" */
    return m;
}

/* XOR flashes over the view; the original's sound sweeps give them their length. */
void healer_effect(void)
{
    set_draw_color(dg.color_fire);
    xor_fill_rect(0xB7, 0xB7, 8, 8);
    sound_sweep(1, 5000, 0x57E4, 1, 0x100E);
    sound_sweep(-1, 0x6B6C, 0x57E4, 1, 0x100E);
    set_draw_color(dg.color_text);
    xor_fill_rect(0xB7, 0xB7, 8, 8);
    sound_sweep(1, 1, -0x63C0, 1, 0x11B2);
    sound_sweep(-1, 40000, -0x63C0, 1, 0x11B2);
    xor_fill_rect(0xB7, 0xB7, 8, 8);
    sound_sweep(2, 1, 18000, 1, 0x8FC);
    sound_sweep(-2, 36000, 18000, 1, 0x8FC);
}

/* At location 7, a price up to 100 that the party can't pay is waived. */
int healer_ask_pay(void)
{
    shop_print(S(0x807A));   /* "for % gold. Wilt thou pay?" */
    int k = yes_no_key();
    print_text(k == 'Y' ? S(0x8098) : S(0x809C));
    int refused = k == 'N';
    if (!refused && (int16_t)dg.gold < (int16_t)dg.shop_price && ((int16_t)dg.shop_price > 100 || dg.location != 7)) {
        shop_say(0x23AB);
        refused = 1;
    }
    if (!refused && (int16_t)dg.shop_price <= (int16_t)dg.gold) {
        dg.gold = (uint16_t)(dg.gold - dg.shop_price);
        shop_take_gold();
    }
    return refused;
}

void shop_healer(void)
{
    int done = 0;
    shop_greeting();
    while (!done) {
        int k = yes_no_key();
        print_text(k == 'N' ? S(0x80A0) : S(0x80A4));
        if (k == 'N') break;
        print_text(S(0x80AA));   /* "We have powers to Cure, Heal, or Resurrect." */
        shop_print(S(0x80DA));   /* says $. "What is the nature of thy need?" */
        unsigned c;
        do c = (unsigned)key() & 0xFF; while (c != 'C' && c != 'H' && c != 'R' && c != ' ' && c != 0x0D);
        if (c == 0x0D || c == ' ') {
            print_text(S(0x81C0));   /* "Nothing" */
            done = 1;
        } else if (c == 'C') {
            print_text(S(0x8106));   /* "Curing" */
            int m = healer_ask_who();
            if (m != -1) {
                if (dg.party[m].status == 'P') {
                    print_text(S(0x810E));
                    int refused = 0;
                    if (dg.location == 5) {
                        print_text(S(0x8112));   /* "Receive now the Light!" */
                    } else {
                        print_text(S(0x812A));   /* "I can cure thy poisoned body " */
                        dg.shop_price = B((uint16_t)(0x3D8E + dg.shop_town));
                        refused = healer_ask_pay();
                    }
                    if (!refused) {
                        healer_effect();
                        dg.party[m].status = 'G';
                    }
                } else {
                    shop_print(S(0x3D5A));   /* "Thou hast no need of this art!" says $. */
                }
            }
        } else if (c == 'H') {
            print_text(S(0x8148));   /* "Healing" */
            int m = healer_ask_who();
            if (m != -1) {
                PartyMember *p = &dg.party[m];
                if (p->status == 'D' || p->hp == p->max_hp) {
                    shop_print(S(0x3D5A));
                } else {
                    print_text(S(0x8150));
                    int refused = 0;
                    if (dg.location == 5) {
                        print_text(S(0x8154));
                    } else {
                        print_text(S(0x816C));   /* "I can heal thee " */
                        dg.shop_price = B((uint16_t)(0x3D86 + dg.shop_town));
                        refused = healer_ask_pay();
                    }
                    if (!refused) {
                        healer_effect();
                        p->hp = p->max_hp;
                    }
                }
            }
        } else {
            print_text(S(0x817E));   /* "Resurrect" */
            int m = healer_ask_who();
            if (m != -1) {
                if (dg.party[m].status != 'D') {
                    shop_print(S(0x3D5A));
                } else {
                    print_text(S(0x8188));
                    print_text(S(0x818C));   /* "I can raise this unfortunate person from " */
                    print_text(S(0x81B6));   /* "the dead " */
                    dg.shop_price = W((uint16_t)(0x3D96 + dg.shop_town * 2));
                    if (!healer_ask_pay()) {
                        healer_effect();
                        resurrect_member(0xFF, m);
                        dg.party[m].hp = dg.party[m].max_hp;
                        draw_status_bar();
                    }
                }
            }
        }
        if (!done) {
            draw_status_bar();
            print_text(S(0x81C8));   /* "Is there any other way in which I may aid thee?" */
            print_text(S(0x81F2));
        }
    }
    shop_farewell(1);
}

/* ---------------------------------------------------------------- SHOPPES2: tavern (type 1) */

/* The tavern's letters for food, drink, rations and rumours are per tavern (ds:4C1E, 4C24,
 * 4C2A, 4C30, indexed by tavern_index from ds:4D4C). */
#define TAVERN_KEY(base) ((char)B((uint16_t)((base) + dg.tavern_index)))

void tavern_price_for_party(int price_each)
{
    dg.shop_price = 0;
    ds_set_u16(0xBD1C, 0);   /* tavern_diners2 and tavern_diners are words in the original */
    ds_set_u16(0xBD1A, 0);
    for (unsigned i = 0; i < dg.party_size; i++)
        if (dg.party[i].status != 'D') {
            dg.tavern_diners2++;
            dg.tavern_diners++;
            dg.shop_price = (uint16_t)(dg.shop_price + price_each);
        }
    put_char('\n');
    put_char('\n');
}

void number_word(void)
{
    static const uint16_t words[] = {0x9AA8, 0x9AAC, 0x9AB2, 0x9AB8, 0x9ABE};   /* two .. six */
    if (dg.tavern_diners2 >= 2 && dg.tavern_diners2 <= 6) print_text(S(words[dg.tavern_diners2 - 2]));
}

void sir_or_milady(void)
{
    dg.active_player = 0xFF;
    print_text(dg.party[0].sex == 0x0B ? S(0x9AC2) : S(0x9AC6));
}

/* Food goes onto the table next to the party (0x95 -> 0x9B above / 0x9A below). */
int tavern_serve_food(void)
{
    put_char('"');
    print_text(S(0x9ACE));   /* "That will be " */
    print_number(' ', 1, (int16_t)dg.shop_price);
    print_text(S(0x9ADC));   /* " gold for the " */
    number_word();
    print_text(S(0x9AEC));   /* " of ye," */
    sir_or_milady();
    put_char('.');
    if ((int16_t)dg.gold < (int16_t)dg.shop_price) {
        print_text(S(0x9AF6));   /* "CAN'T PAY? Beat it!" yells */
        print_text(S(dg.shopkeeper_name));
        put_char('.');
        put_char('\n');
        return 1;
    }
    dg.gold = (uint16_t)(dg.gold - dg.shop_price);
    shop_take_gold();
    if (dg.tavern_diners == 0) {
        dg.tavern_drinks++;
    } else {
        add_word_capped(9999, dg.tavern_diners, &dg.food);
        uint8_t *above = get_map_tile_ptr((unsigned)(dg.party_y - 1), dg.party_x);
        uint8_t *below = get_map_tile_ptr((unsigned)(dg.party_y + 1), dg.party_x);
        if (*above == 0x95) {
            *above = 0x9B;
            dg.tavern_food_tile = 0x9B;
            world_tick();
        } else if (*below == 0x95) {
            *below = 0x9A;
            dg.tavern_food_tile = 0x9A;
            world_tick();
        }
    }
    print_text(S(0x9B16));   /* "Enjoy!" */
    return 0;
}

int tavern_order_food(void)
{
    put_char((uint8_t)TAVERN_KEY(0x4C1E));
    tavern_price_for_party((int16_t)W((uint16_t)(0x4C36 + dg.shop_town * 2)));
    return tavern_serve_food();
}

/* The fourth drink can be refused ("haven't ye had enough?"); insisting makes the party drunk
 * (25 turns of "Hic!") and costs 1 karma. Wine taverns ('W') have a wine list. Returns 1 if the
 * party was thrown out, 2 if it chose nothing. */
int tavern_wine(void)
{
    put_char((uint8_t)TAVERN_KEY(0x4C24));
    if (dg.tavern_drinks == 3) {
        print_text(S(0x9B22));   /* "I beg thy pardon, " */
        sir_or_milady();
        print_text(S(0x9B38));
        print_text(S(dg.shopkeeper_name));
        print_text(S(0x9B42));   /* "But haven't ye had enough to drink?" */
        if (yes_no_key() == 'Y') {
            print_text(S(0x9B6C));
            return 0;
        }
        print_text(S(0x9B72));   /* "No!" */
        dg.drunk_turns = 0x19;
        sub_byte_floor0(1, &dg.karma);
    }
    tavern_price_for_party(1);
    if (TAVERN_KEY(0x4C24) != 'W') {
        ds_set_u16(0xBD1A, 0);   /* a drink, not a meal */
        return tavern_serve_food();
    }
    print_text(S(0x9B76));   /* "Our wine list, " */
    sir_or_milady();
    print_text(S(0x9B88));
    for (uint16_t o = 0x9B8C; o <= 0x9BE6; o += 0x12) print_text(S(o));   /* a) Rose ... f) Chablis */
    print_text(S(0x9BFA));   /* "Thy choice?" */
    unsigned k;
    do k = (unsigned)key() & 0xFF; while (k != ' ' && (k < 'A' || k > 'F'));
    if (k != ' ') {
        put_char((uint8_t)k);
        print_text(S(0x9C08));   /* "Ah, a fine choice, " */
        sir_or_milady();
        put_char('.');
        int16_t price = (int16_t)W((uint16_t)(0x4C48 + (k - 'A') * 2));
        if ((int16_t)dg.gold < price) {
            print_text(S(0x9C20));
            print_text(S(dg.shopkeeper_name));
            put_char('.');
            put_char('\n');
            return 1;
        }
        dg.gold = (uint16_t)(dg.gold - price);
        shop_take_gold();
        dg.tavern_drinks++;
        print_text(S(0x9C40));   /* "Enjoy!" */
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
int tavern_buy_rations(int member)
{
    put_char((uint8_t)TAVERN_KEY(0x4C2A));
    put_char('\n');
    put_char('\n');
    dg.shop_price = adjust_price_u(W((uint16_t)(0x4C54 + dg.shop_town * 2)), intel_of(member));
    put_char('"');
    shop_say(W((uint16_t)(0x4C66 + random_between(6, 0) * 2)));
    print_text(S(0x9C4A));   /* "How many wouldst thou like?" */
    int want = read_number(2);
    if (want == 0) {
        print_text(S(0x9C6A));   /* "Hrumph." */
        return 2;
    }
    for (int left = want; left != 0; left--) {
        if ((int16_t)dg.gold < (int16_t)dg.shop_price) {
            print_text(S(0x9C76));
            if (want - left == 0) {
                if ((int16_t)dg.food < 3) {   /* pity on the starving */
                    add_word_capped(9999, (uint16_t)(random_between(1, 0) + 1), &dg.food);
                    shop_say(0x143F);
                } else {
                    print_text(S(0x9C7A));   /* "Thou hast neither gold nor need! Out!" */
                    print_text(S(0x9CA4));
                    print_text(S(dg.shopkeeper_name));
                    print_text(S(0x9CAC));
                }
                return 1;
            }
            print_text(S(0x9CB0));   /* "Thou canst afford only " */
            print_number(' ', 1, want - left);
            print_text(S(0x9CCA));
            return 0;
        }
        sub_word_floor0(dg.shop_price, &dg.gold);
        add_word_capped(9999, 0x19, &dg.food);
        if (dg.food == 9999) break;
    }
    print_text(S(0x9CD0));
    shop_take_gold();
    return 0;
}

/* 26 topics (ds:4C74); each has a price (ds:4D10) and fills & / * for the answer text. */
int tavern_rumours(void)
{
    int topic = -1;
    put_char((uint8_t)TAVERN_KEY(0x4C30));
    print_text(S(0x9EFC));
    for (;;) {
        print_text(S(0x9F00));   /* "Of what wouldst thou hear my lore, " */
        sir_or_milady();
        print_text(S(0x9F24));   /* "?" You respond: */
        read_line(0x0F, dg.talk_input);
        print_text(S(0x9F36));
        if (dg.talk_input[0] == 0) return 0;
        for (int i = 0; i < 0x1A; i++) {
            int pos = str_find_nocase(dg.talk_input, S(W((uint16_t)(0x4C74 + i * 2))));
            if (pos == 0 || (pos > 0 && dg.talk_input[pos - 1] == ' ')) {
                topic = i;
                break;
            }
        }
        if (topic != -1) break;
        print_text(S(0x9F3A));   /* "That, I cannot help thee with." */
    }
    dg.shop_price = W((uint16_t)(0x4D10 + topic * 2));
    shop_say(0x134E);
    print_text(S(0x9F5C));   /* "Fair 'nuff?" */
    if (yes_no_key() == 'N') {
        print_text(S(0x9F6C));
        return 0;
    }
    print_text(S(0x9F72));
    if ((int16_t)dg.gold < (int16_t)dg.shop_price) {
        print_text(S(0x9F78));   /* "Sorry, " */
        sir_or_milady();
        shop_say(0x146A);
        return 1;
    }
    dg.gold = (uint16_t)(dg.gold - dg.shop_price);
    draw_status_bar();
    dg.shop_item_name = W((uint16_t)(0x4CA8 + topic * 2));
    dg.shop_item_name2 = W((uint16_t)(0x4CF6 + B((uint16_t)(0x4CDC + topic)) * 2));
    shop_say(W((uint16_t)(0x4D44 + rnd(3) * 2)));
    print_text(S(0x9F82));   /* "says " */
    print_text(S(dg.shopkeeper_name));
    print_text(S(0x9F8A));
    return 0;
}

void tavern_farewell(void) { shop_farewell(dg.tavern_farewell_kind); }

/* Rumours are only offered once the party has bought something. */
void shop_tavern(int member)
{
    dg.tavern_index = B((uint16_t)(0x4D4C + dg.shop_town));
    ds_set_u16(0xBD18, 0);   /* tavern_farewell_kind */
    ds_set_u16(0xBD1E, 0);   /* tavern_food_tile */
    shop_greeting();
    int k;
    do {
        k = key();
        if (k == 'N' || k == ' ') {
            print_text(S(0x9F8E));
            tavern_farewell();
            return;
        }
    } while (k != 'Y');
    print_text(S(0x9F92));
    shop_say(W((uint16_t)(0x4D56 + dg.tavern_index * 2)));
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
            if (TAVERN_KEY(0x4C1E) == c && tavern_order_food() != 0) return;
            if (TAVERN_KEY(0x4C24) == c && (r = tavern_wine()) == 1) return;
            if (TAVERN_KEY(0x4C2A) == c && (r = tavern_buy_rations(member)) == 1) return;
            if (TAVERN_KEY(0x4C30) == c) {
                if (dg.tavern_farewell_kind == 0) continue;
                if (tavern_rumours() != 0) return;
            }
            if (TAVERN_KEY(0x4C1E) == c || TAVERN_KEY(0x4C24) == c || TAVERN_KEY(0x4C2A) == c || TAVERN_KEY(0x4C30) == c)
                break;
        }
        if (r != 2) ds_set_u16(0xBD18, 1);
        draw_status_bar();
        print_text(S(0x9F9A));   /* "Anything else for thee?" */
        if (yes_no_key() == 'N') {
            print_text(S(0x9FB6));
            tavern_farewell();
            return;
        }
        print_text(S(0x9FBA));
        shop_say(W((uint16_t)(0x4D5E + dg.tavern_index * 2)));
        put_char('"');
        put_char(' ');
    }
}

/* ---------------------------------------------------------------- SHOPPES2: shipwright (type 3) */

int can_afford(int price)
{
    if (price <= (int16_t)dg.gold) return 1;
    shop_say(0x198E);
    shop_print(S(0x9FC2));   /* yells $. */
    ds_set_u16(0xBD22, 1);   /* shipwright_can_buy: skips the farewell */
    return 0;
}

/* pay: charge shop_price; place: put the ship at this town's dock (ds:4D76 / 4D7A). The "milady"
 * test compares the sex byte (0x0B/0x0C) with 'F', so everyone is "sir". Returns 1 to stop. */
int shipwright_offer(int member, int pay, int place)
{
    if (place) {
        shop_say(0x19AB);
        dg.stored_ship_x = B((uint16_t)(0x4D76 + dg.shop_town));
        dg.stored_ship_y = B((uint16_t)(0x4D7A + dg.shop_town));
    }
    if (pay) {
        dg.gold = (uint16_t)(dg.gold - dg.shop_price);
        ds_set_u16(0xBD24, 1);   /* shipwright_bought */
        shop_take_gold();
    }
    draw_status_bar();
    shop_say(0x19DA);
    print_text(dg.party[member].sex == 'F' ? S(0x9FCC) : S(0x9FD4));
    print_text(S(0x9FD8));
    int k = yes_no_key();
    print_text(k == 'Y' ? S(0x9FDC) : S(0x9FE0));
    return k != 'Y';
}

/* stored_ship: 0x80 a frigate, 0x40 a skiff, low bits extra skiffs. */
void shipwright_menu(int member)
{
    int stop = 0;
    do {
        shop_say(0x18EB);
        int k = key();
        if (k == 0x1B || k == ' ') {
            stop = 1;
        } else if (k == 'F') {
            print_text(S(0x9FE4));
            stop = 1;
            if ((dg.stored_ship & 0xC0) == 0) {
                dg.shop_price = adjust_price_u(W((uint16_t)(0x4D66 + dg.shop_town * 2)), intel_of(member));
                shop_say(0x183E);
                shop_say(0x1A50);
                if (yes_no_key() != 'Y') {
                    print_text(S(0x9FF6));
                    return;
                }
                print_text(S(0x9FF0));
                if (!can_afford((int16_t)dg.shop_price)) return;
                dg.stored_ship = 0x82;
                stop = shipwright_offer(member, 1, 1);
            } else {   /* already has a ship: trade it in? (costs 10000, never affordable) */
                shop_say(0x19F2);
                if (yes_no_key() == 'Y') {
                    print_text(S(0x9FE8));
                    can_afford(10000);
                } else {
                    print_text(S(0x9FEC));
                    shipwright_menu(member);
                }
            }
        } else if (k == 'S') {
            print_text(S(0x9FFA));
            dg.shop_price = adjust_price_u(W((uint16_t)(0x4D6E + dg.shop_town * 2)), intel_of(member));
            shop_say(0x188C);
            shop_say(0x1A50);
            int yes = yes_no_key() == 'Y';
            stop = 1;
            if (!yes) {
                print_text(S(0xA004));
                return;
            }
            print_text(S(0x9FFE));
            if (!can_afford((int16_t)dg.shop_price)) return;
            if ((dg.stored_ship & 0xC0) == 0) {
                dg.stored_ship = 0x40;
                stop = shipwright_offer(member, 1, 1);
            } else if (dg.stored_ship & 0x80) {   /* a skiff for the frigate */
                shop_say(0x193B);
                dg.stored_ship++;
                stop = shipwright_offer(member, 1, 0);
            } else if (dg.stored_ship & 0x40) {
                shop_say(0x195F);
                stop = shipwright_offer(member, 0, 0);
            }
        }
    } while (stop == 0);
}

void shop_shipwright(int member)
{
    ds_set_u16(0xBD22, 0);
    ds_set_u16(0xBD24, 0);
    shop_greeting();
    for (;;) {
        int k = key();
        if (k == ' ' || k == 'N') {
            print_text(S(0xA00C));
            break;
        }
        if (k == 'Y') {
            print_text(S(0xA008));
            shipwright_menu(member);
            break;
        }
    }
    if (dg.shipwright_can_buy == 0) shop_farewell(dg.shipwright_bought);
}

/* ---------------------------------------------------------------- SHOPPES3: inn (type 7) */

int count_left_companions(void)
{
    int n = 0;
    for (int i = 0; i < 16; i++) n += dg.party[i].where == dg.location;
    return n;
}

int inn_sir_or_milady(uint8_t sex)
{
    print_text(S(0x4D84));
    if (count_left_companions() < B((uint16_t)(0x4DC4 + dg.shop_town))) return 1;
    print_text(S(0x4D87));   /* "I am sorry, " */
    print_text(sex == 0x0C ? S(0x4D95) : S(0x4D9C));
    print_text(S(0x4DA0));   /* ", but we have no room available." */
    return 0;
}

static void restore_mp(PartyMember *p)
{
    if (p->cls == 'A' || p->cls == 'M') p->mp = p->intel;
    else if (p->cls == 'B') p->mp = p->intel >> 1;
}

/* A night's rest: the party sleeps in the inn's bed (ds:4E7A / 4E80) until 6:00, then is
 * healed; anyone still poisoned dies. Returns -1 after resting, -2 if no room. */
int inn_rest(int result, uint8_t sex, int intel)
{
    put_char('R');
    if (!inn_sir_or_milady(sex)) return -2;
    put_char('"');
    dg.shop_price = adjust_price_s((uint16_t)(B((uint16_t)(0x4D7E + dg.shop_town)) * dg.party_size), intel);
    shop_say(W((uint16_t)(0x4E6E + dg.shop_town * 2)));
    print_text(S(0x4DCA));   /* "Wilt thou take it?" */
    int k = ask_yes_no();
    print_text(S(0x4DE0));
    if (k != 'Y') return result;
    if ((int16_t)dg.gold < (int16_t)dg.shop_price) {
        print_text(S(0x4DE3));   /* "Highwaymen! Cheap, at that! OUT!" */
        shop_print(S(0x4E07));   /* screams $. */
        return result;
    }
    dg.gold = (uint16_t)(dg.gold - dg.shop_price);
    shop_take_gold();
    draw_status_bar();
    print_text(S(0x4E13));   /* "Have a pleasant night, " */
    print_text(sex == 0x0C ? S(0x4E2C) : S(0x4E33));
    shop_print(S(0x4E37));
    wait_turns(10);
    restore_open_door(dg.open_door_y, dg.open_door_x, dg.open_door_tile);
    uint8_t active = dg.active_player;
    dg.party_x = B((uint16_t)(0x4E7A + dg.shop_town));
    dg.party_y = B((uint16_t)(0x4E80 + dg.shop_town));
    dg.redraw_flags = 1;
    for (unsigned i = 0; i < dg.party_size; i++)
        if (dg.party[i].status == 'G') dg.party[i].status = 'S';
    draw_status_bar();
    world_tick();
    print_text(S(0x4E44));   /* "Zzzzzz...." */
    for (int i = 0; i < 12; i++) pass_time(5);
    while (dg.hour != 6 && !platform_quit_requested()) {
        wait_turns(1);
        regen_ring_tick();
        draw_status_bar();
        pass_time(9);
        if (dg.hour == 0x14 || dg.hour == 5) update_lamps();
    }
    print_text(S(0x4E51));   /* "Morning!" */
    place_npcs();
    for (unsigned i = 0; i < dg.party_size; i++) {
        PartyMember *p = &dg.party[i];
        if (p->status == 'D') continue;
        p->hp = p->max_hp;
        restore_mp(p);
        if (p->status == 'P') {
            p->status = 'D';
            p->hp = 0;
            put_char('\n');
            print_text(p->name);
            print_text(S(0x4E5B));   /* " has passed away." */
        } else if (p->status == 'S') {
            p->status = 'G';
        }
    }
    dg.party_x++;
    dg.redraw_flags = 1;
    dg.active_player = active;
    draw_status_bar();
    return -1;
}

/* A member (not the Avatar) stays at the inn: marked with this location and moved to the end of
 * the 16-member roster; billed per month at pick-up. */
int inn_leave_companion(int result, uint8_t sex, int intel)
{
    put_char('L');
    if (!inn_sir_or_milady(sex)) return result;
    if (dg.party_size == 1) {
        shop_say(0x26E8);
        return -1;
    }
    for (;;) {
        shop_print(S(0x4E86));   /* $ asks, "Who will stay?" */
        int m = ask_member_number();
        if (m == -1) {
            print_text(S(0x4EA0));   /* "Nobody" */
            return -2;
        }
        print_text(S(0x4EA9));
        if (m == 0) {
            print_text(S(0x4EAC));   /* "Thy friend" */
            if (dg.party_size > 2) put_char('s');
            print_text(S(0x4EB7));   /* " will not leave thee!" */
            continue;
        }
        if (dg.party[m].status == 'D') {
            shop_say(0x2723);
            return -1;
        }
        dg.shop_price = adjust_price_s((uint16_t)(B((uint16_t)(0x4D7E + dg.shop_town)) * 10), intel);
        print_text(S(0x4ECF));   /* "The rate for our most comfortable room will be " */
        shop_print(S(0x4F00));   /* "% gold per month, due at check-out." */
        print_text(S(0x4F24));
        int k = ask_yes_no();
        print_text(S(0x4F3A));
        if (k == 'Y') {
            result = 1;
            if (m == dg.active_player) dg.active_player = 0xFF;
            else if (m < dg.active_player && dg.active_player != 0xFF) dg.active_player--;
            dg.party[m].where = dg.location;
            dg.party[m].inn_months = 0;
            PartyMember tmp = dg.party[m];
            memmove(&dg.party[m], &dg.party[m + 1], (size_t)(15 - m) * sizeof tmp);
            dg.party[15] = tmp;
            dg.party_size--;
            draw_status_bar();
            shop_print(S(0x4F3D));   /* "I thank thee." says $. */
        }
        return result;
    }
}

int prev_inn_guest(int slot)
{
    do {
        if (--slot == 0) return 0;
    } while (dg.party[slot].where != dg.location);
    return slot;
}

int next_inn_guest(int slot)
{
    do {
        if (++slot > 0x0F) break;
    } while (dg.party[slot].where != dg.location);
    return slot == 0x10 ? 0 : slot;
}

int inn_pick_up_companion(int result, int intel)
{
    int guests = count_left_companions(), sel;
    if (dg.party_size == 6) {
        print_text(S(0x4F57));   /* "One must first be left behind!" */
        return result;
    }
    if (guests == 0) {
        shop_print(S(0x4F7A));   /* "No one here is from thy party!" says $. */
        return result;
    }
    if (guests < 2) {
        sel = next_inn_guest(0);
    } else {   /* the guest register in the side panel, a bar to choose with */
        print_text(S(0x4FA7));   /* "Who will check out?" */
        select_text_window(1);
        clear_side_panel();
        define_text_window(9, 0x26, 1, 0x18, 1);
        put_char(0xFF);
        define_text_window(9, 0x27, 1, 0x18, 1);
        draw_panel_box(7);
        set_cursor(1, 1);
        print_text(S(0x4FC0));   /* "    GUEST" */
        set_cursor(2, 1);
        print_text(S(0x4FCA));   /* "  REGISTER:" */
        for (int i = 1; i < 16; i++)
            if (dg.party[i].where == dg.location) {
                set_cursor(get_cursor_row(), 4);
                print_text(dg.party[i].name);
                put_char('\n');
            }
        select_text_window(2);
        sel = next_inn_guest(0);
        int y = 0x28, done = 0;
        set_draw_color(dg.color_text);
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
                        print_text(S(0x4FD8));   /* "No one" */
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
    unsigned months = dg.party[sel].inn_months ? dg.party[sel].inn_months : 1;
    dg.shop_price = (uint16_t)(months * adjust_price_s((uint16_t)(B((uint16_t)(0x4D7E + dg.shop_town)) * 10), intel));
    shop_print(S(0x4FE1));   /* "That will be % gold, please." */
    if ((int16_t)dg.gold < (int16_t)dg.shop_price) {
        shop_say(0x275A);
        alert_town();
        return -1;
    }
    dg.gold = (uint16_t)(dg.gold - dg.shop_price);
    shop_take_gold();
    draw_status_bar();
    PartyMember tmp = dg.party[sel];
    if (dg.party_size < sel)   /* make room: move the roster between up by one */
        memmove(&dg.party[dg.party_size + 1], &dg.party[dg.party_size], (size_t)(sel - dg.party_size) * sizeof tmp);
    PartyMember *p = &dg.party[dg.party_size];
    *p = tmp;
    dg.party_size++;
    p->where = 0;
    const char *text;
    if (p->status == 'P') {   /* poison kills while away */
        p->status = 'D';
        p->hp = 0;
        text = S(0x5005);   /* "Thy friend has died, by the way." */
    } else {
        p->hp = p->max_hp;
        restore_mp(p);
        text = S(0x5028);   /* "I hope thou hast found thy stay enjoyable," */
    }
    print_text(text);
    shop_print(S(0x5055));   /* says $. */
    draw_status_bar();
    return 1;
}

void shop_inn(int member)
{
    int result = 0, done = 0;
    int intel = intel_of(member);
    uint8_t sex = dg.party[member].sex;
    dg.active_spell_turns = 0;
    dg.active_spell = 0;
    draw_status_bar();
    shop_greeting();
    while (!done) {
        int k;
        do {
            k = key();
            if (k == ' ') k = 'N';
        } while (k != 'N' && k != 'Y');
        print_text(k == 'N' ? S(0x505F) : S(0x5062));
        if (k == 'N') break;
        shop_print(S(0x5066));   /* $ asks, "Art thou here to Pick up or */
        print_text(S(0x508E));   /* Leave a companion, or to Rest for the night?" */
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
        if (!done) print_text(S(0x50BD));   /* "Is there anything more I can do for thee?" */
    }
    shop_farewell(result);
}
