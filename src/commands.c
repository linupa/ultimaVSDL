/* commands.c - command input, passability and moving between Britannia and towns. */
#include <string.h>
#include "commands.h"
#include "data.h"
#include "input.h"
#include "map.h"
#include "objects.h"
#include "text.h"
#include "view.h"
#include "world.h"

unsigned to_upper(uint8_t c)
{
    return (c >= 'a' && c <= 'z') ? (unsigned)(c - 0x20) : c;
}

/* Waits with the blinking cursor; every empty poll runs world_tick (outdoors, in towns and in
 * combat). Keys come back upper case. Arrow keys stay 1-4 (poll_key sets numpad_shift for them);
 * Shift + keypad digits become directions 1-4 and diagonals 0xD3-0xD6; codes 1-4 typed without
 * numpad_shift (Ctrl+A to Ctrl+D) become 0xFB-0xFE. */
int get_command_key(void)
{
    unsigned key;
    if (dg.video_type == 2) dg.anim_flag = 0xFF;
    do {
        key = to_upper((uint8_t)wait_key_cursor());
        if (key == 0 && (dg.location < 0x21 || dg.location > 0x7F)) world_tick();
        if (platform_quit_requested()) return 0x1B;
    } while (key == 0);
    if (dg.numpad_shift == 0) {
        if (key < 5) key += 0xFA;
    } else {
        switch (key) {
        case '1': key = 0xD4; break;
        case '2': key = 4; break;
        case '3': key = 0xD6; break;
        case '4': key = 1; break;
        case '6': key = 2; break;
        case '7': key = 0xD3; break;
        case '8': key = 3; break;
        case '9': key = 0xD5; break;
        }
    }
    return (int)key;
}

/* Walkable terrain: one bit per tile at ds:54D4 (bit clear = walkable). Furniture (0x90-0x93)
 * only for the party on foot (0x1C/0x1D) and people (0x40-0x4F). */
int land_passable(int tile, uint8_t mover)
{
    int ok = (ds_u8(0x54D4 + (tile >> 3)) & (0x80 >> (tile & 7))) == 0;
    if ((mover & 0xFE) != 0x1C && (mover & 0xF0) != 0x40 && (tile & 0xFC) == 0x90) ok = 0;
    return ok;
}

int water_passable(int tile)
{
    return tile < 4 || (tile & 0xF0) == 0x60;
}

/* The mover's class comes from ds:54F4 indexed by mover / 4. */
int tile_passable(int tile, int mover)
{
    switch (ds_u8(0x54F4 + (mover >> 2))) {
    case 0: return land_passable(tile, (uint8_t)mover);
    case 1: return water_passable(tile);
    case 2:
        if ((tile & 0xF0) == 0x60 || water_passable(tile)) return 1;
        return land_passable(tile, (uint8_t)mover);
    case 3: return land_passable(tile, (uint8_t)mover) && tile != 0x8F && tile != 4;
    case 4: return !water_passable(tile);
    case 5:   /* ships: shore tiles say which sides can be sailed onto */
        if ((tile & 0xFC) == 0x34) return (8 >> (mover & 3) & ds_u8(0x5510 + tile)) != 0;
        if (!water_passable(tile)) return 0;
        if (tile < 0x60) return 1;
        return (ds_u8(0x54D4 + tile) & 8 >> (mover & 3)) != 0;
    case 6: return tile <= 2;
    case 7: return tile == 4;
    case 8: return tile == 5;
    case 9: return tile == 1;
    case 10: return tile == 7;
    default: return 0;
    }
}

/* The town whose entrance is the party's square (town_exit_x/y[1..0x20]). Prints the name,
 * centred, except for locations 14-18. */
int enter_town_from_map(const char *prompt)
{
    print_text(prompt);
    int i;
    for (i = 0; i < 0x20; i++)
        if (dg.town_exit_x[1 + i] == dg.party_x && dg.town_exit_y[1 + i] == dg.party_y) break;
    if (i == 0x20) {
        print_text(ds_str(0x39A8));   /* "\nWhat town?\n" */
        return 1;
    }
    if (i < 0x0D || i > 0x11) {
        print_text(ds_str(0x399C));   /* "\n\n" */
        put_char(0xFC);
        print_text(ds_str(ds_u16(0x1E3A + i * 2)));
        put_char(0xFB);
    }
    put_char('\n');
    save_file_part(0x100, (const uint8_t *)dg.objects, objects_file_name());   /* Britannia's objects wait in BRIT.OOL / UNDER.OOL */
    dg.location = (uint8_t)(i + 1);
    dg.z_level = 0;
    dg.party_x = 0x0F;
    dg.party_y = 0x1E;
    return 0;
}

/* Accepts only the four directions and Space ("Pass"). */
int ask_direction(void)
{
    int key;
    dg.dir_x = 0;
    dg.dir_y = 0;
    do {
        key = get_command_key() & 0xFF;
        if (platform_quit_requested()) return 0;
    } while (!(key == ' ' || (key >= 1 && key <= 4)));
    switch (key) {
    case 1: print_text(ds_str(MSG_WEST)); dg.dir_x--; break;
    case 2: print_text(ds_str(MSG_EAST)); dg.dir_x++; break;
    case 3: print_text(ds_str(MSG_NORTH)); dg.dir_y--; break;
    case 4: print_text(ds_str(MSG_SOUTH)); dg.dir_y++; break;
    default: print_text(ds_str(0xA2A0)); return 0;   /* "Pass" */
    }
    return 1;
}

void restore_open_door(uint8_t y, uint8_t x, uint8_t tile)
{
    if (dg.location != 0 && dg.location < 0x21 && tile != 0) *get_map_tile_ptr(y, x) = tile;
}

/* Like read_name, but through get_command_key (the view keeps animating) and with type-ahead off. */
void read_line(int max_len, char *buf)
{
    uint16_t saved = dg.typeahead_on;
    int key, count = 0;
    dg.typeahead_on = 0;
    do {
        key = get_command_key();
        if ((key == 8 || key == 1) && count != 0) {
            erase_chars(1);
            count--;
        } else if (key == 0x1B && count != 0) {
            erase_chars(count);
            count = 0;
        } else if (key > 0x1F && key < 0x80 && count < max_len) {
            buf[count++] = (char)key;
            put_char((uint8_t)key);
        }
        if (platform_quit_requested()) break;
    } while (key != 0x0D);
    buf[count] = '\0';
    dg.typeahead_on = saved;
}

void wait_turns(int turns)
{
    if (!dg.world_tick_enabled) return;
    for (; turns > 0; turns--) {
        world_tick();
        delay_ticks(1);
    }
}

/* Up to max_digits (5 at most) digits, a leading '-' or '+' allowed. With nothing typed the
 * original reads an uninitialised first byte; the port returns 0. */
int read_number(int max_digits)
{
    static const int16_t pow10[] = {1, 10, 100, 1000, 10000};   /* ds:6A0A */
    uint8_t digits[6] = {0};
    int n = 0;
    if (max_digits > 5) max_digits = 5;
    for (;;) {
        unsigned key = (unsigned)get_command_key();
        if (platform_quit_requested()) key = 0x0D;
        if (key >= '0' && key <= '9') {
            if (n < max_digits) {
                digits[n++] = (uint8_t)key;
                put_char((uint8_t)key);
            }
        } else if ((key == '-' || key == '+') && n == 0) {
            digits[n++] = (uint8_t)key;
            put_char((uint8_t)key);
        } else if ((key == 8 || key == 1) && n != 0) {
            n--;
            erase_chars(1);
        } else if (key == 0x1B && n != 0) {
            erase_chars(n);
            n = 0;
        }
        if (key == 0x0D) {
            int value = 0, place = 0;
            for (int i = n - 1; i > 0; i--) value += (digits[i] - '0') * pow10[place++];
            if (digits[0] == '-') value = -value;
            else if (digits[0] > '/') value += pow10[place] * (digits[0] - '0');
            return (int16_t)value;
        }
    }
}
