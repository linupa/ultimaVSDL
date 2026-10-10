/* shrine.c - the shrines of virtue and the Codex (CAST2.OVL). See shrine.h. */
#include <string.h>
#include "commands.h"
#include "data.h"
#include "endings.h"
#include "gfx.h"
#include "input.h"
#include "map.h"
#include "misc.h"
#include "objects.h"
#include "platform.h"
#include "shrine.h"
#include "status.h"
#include "text.h"
#include "view.h"
#include "world.h"

#define S(off) ds_str(off)
#define MSG(off) ((const char *)dg.text_buffer + (off))   /* MISCMSG.DAT from 0x3AB, loaded by visit_shrine */

/* 1-8 urns stand around the altar: positions from ds:4B9E (x) / ds:4BC2 (y), starting at
 * ds:4B95[count], as objects 1 up with tile 3. */
void shrine_place_urns(int count)
{
    unsigned pos = ds_u8((uint16_t)(0x4B95 + count));
    for (int i = 0; i < count; i++, pos++)
        set_object_entry(i + 1, 0, dg.z_level, ds_u8((uint16_t)(0x4BC2 + pos)), ds_u8((uint16_t)(0x4B9E + pos)), 3, 3);
}

static void flash_and_hum(int length, int pitch)
{
    set_draw_color(dg.color_text);
    xor_fill_rect(0xB7, 0xB7, 8, 8);
    for (int f = 2000; f < 25000; f += 0x32) sound_sweep(0, f, length, 1, pitch);
    for (int f = 25000; f > 2000; f -= 0x32) sound_sweep(0, f, length, 1, pitch);
}

/* The virtue is matched on its first four letters, the mantra three times. Then:
 *  - not yet answered at the Codex: the quest is given (or given again);
 *  - answered, quest not held: an offering of 100-900 gold buys 1-9 karma;
 *  - answered and the quest held: "WELL DONE!", karma +3 (+6 at Humility) and +1 to the
 *    Avatar's strength, dexterity and intelligence as the virtue's tables at ds:4B7E say. */
void virtue_shrine(void)
{
    int shrine;
    for (shrine = 0; shrine < 8; shrine++)
        if (dg.shrine_x[shrine] == dg.party_x && dg.shrine_y[shrine] == dg.party_y) break;
    if (shrine == 8) shrine = 6;   /* reached through the midnight moongate: Spirituality */
    uint8_t bit = (uint8_t)(1 << shrine);
    dg.objects[0].tile = dg.objects[0].tile2 = 0x6C;   /* kneeling */
    world_tick();
    print_text(MSG(0x36D));   /* "...and thou dost kneel before the Altar." */
    wait_turns(10);
    print_text(MSG(0x398));   /* "Upon what virtue dost thou meditate?" */
    read_line(0x0C, dg.shrine_input);
    if (dg.shrine_input[0] == '\0') return;
    int ok = str_find_nocase(dg.shrine_input, S(ds_u16((uint16_t)(0x4B3E + shrine * 2)))) != -1;
    wait_turns(6);
    put_char('\n');
    for (int i = 0; i < 3; i++) {
        print_text(S(0x958E));   /* "\nMantra:" */
        read_line(0x0C, dg.shrine_input);
        if (dg.shrine_input[0] == '\0') return;
        if (str_find_nocase(dg.shrine_input, S(dg.mantras[shrine])) == -1) ok = 0;
        wait_turns(0x0C);
    }
    if (!ok) {
        print_text(MSG(0x3C0));   /* "Thine thoughts are unfocused." */
        return;
    }
    if (!(dg.codex_answered & bit)) {
        dg.sacred_quest |= bit;
        print_text(MSG(0x3E1));   /* "The Altar speaks and a Quest is ordained!" */
        dg.objects[0].tile = dg.objects[0].tile2 = 0x1C;
        get_command_key();
        print_text(MSG(0x40E));   /* "'Tis now thy sacred Quest to go unto the Codex and learn " */
        print_text(MSG(ds_u16((uint16_t)(0x4B5E + shrine * 2))));
        print_text(S(0x9598));    /* "\"\n" */
        get_command_key();
        print_text(MSG(0x44B));   /* "Return again when thy Quest is done!" */
        for (int i = 0; i < 7; i++)
            sound_sweep((int16_t)ds_u16((uint16_t)(0x4C10 + i * 2)), ds_u16((uint16_t)(0x4C02 + i * 2)), (int16_t)ds_u16((uint16_t)(0x4BF4 + i * 2)), 1, ds_u16((uint16_t)(0x4BE6 + i * 2)));
    } else if (!(dg.sacred_quest & bit)) {
        int n;
        for (;;) {
            print_text(MSG(0x474));   /* "Offer how many hundredweights gold? " */
            unsigned key;
            do {
                key = (unsigned)get_command_key();
                if (platform_quit_requested()) return;
            } while (key < '0' || key > '9');
            put_char((uint8_t)key);
            n = (int)key - '0';
            if (n == 0) {
                print_text(S(0x959C));   /* " gp\n" */
                return;
            }
            print_text(S(0x95A2));   /* "00 gp\n\n" */
            if (n * 100 <= (int16_t)dg.gold) break;
            print_text(MSG(0x49B));  /* "Thou hast not that much gold!" */
        }
        dg.gold = (uint16_t)(dg.gold - n * 100);
        draw_status_bar();
        add_byte_capped(99, (uint8_t)n, &dg.karma);
        select_font(1);
        print_text(S(0x95AA));   /* "ALAKAZAM" in runes */
        select_font(0);
        print_text(S(0x95B4));   /* "!\n" */
        flash_and_hum(200, 0xA8C);
    } else {
        dg.sacred_quest &= (uint8_t)~bit;
        print_text(MSG(0x4B9));   /* "A thunderous voice booms: \"WELL DONE!\"" */
        flash_and_hum(0x96, 0xC1C);
        screen_shake();
        add_byte_capped(99, 3, &dg.karma);
        if (ds_u8((uint16_t)(0x4B7E + shrine)) != 0) {
            add_byte_capped(0x1E, 1, &dg.party[0].str);
            print_text(S(0x95B8));   /* "Strength +1\n" */
        }
        if (ds_u8((uint16_t)(0x4B86 + shrine)) != 0) {
            add_byte_capped(0x1E, 1, &dg.party[0].dex);
            print_text(S(0x95C6));   /* "Dexterity +1\n" */
        }
        if (ds_u8((uint16_t)(0x4B8E + shrine)) != 0) {
            add_byte_capped(0x1E, 1, &dg.party[0].intel);
            print_text(S(0x95D4));   /* "Intelligence +1\n" */
        }
        if (shrine == 7) add_byte_capped(99, 3, &dg.karma);
    }
    wait_turns(10);
}

/* The book opens at the page of the first quest held and answers it. With all eight answered
 * the page turns once more: the four rune texts that tell how to enter Doom. */
void codex_shrine(void)
{
    get_command_key();
    print_text(MSG(0x4E5));   /* "The book is open to the page thou dost seek!" */
    get_command_key();
    print_text(MSG(0x515));   /* "Upon the hallowed page thou dost read:" */
    get_command_key();
    int q;
    for (q = 0; q < 8; q++)
        if (dg.sacred_quest & (1 << q)) break;
    if (q == 8) {
        print_text(MSG(0x53E));   /* "HOW DID YOU GET HERE?" */
        return;
    }
    dg.codex_answered |= (uint8_t)(1 << q);
    put_char('"');
    print_text(MSG(ds_u16((uint16_t)(0x4B6E + q * 2))));
    print_text(S(0x95E6));   /* "\"\n\n" */
    get_command_key();
    if (dg.codex_answered != 0xFF) return;
    for (int i = 0; i < 3; i++) {
        set_draw_color((i & 1) ? dg.color_text : dg.color_fire);
        xor_fill_rect(0xB7, 0xB7, 8, 8);
        screen_shake();
    }
    print_text(MSG(0x555));   /* "A STRANGE WIND CAUSES THE PAGE TO TURN!" */
    get_command_key();
    print_text(S(0x95EA));    /* "Thou dost read:\n\n" */
    static const uint16_t pages[4] = {0x57F, 0x5D1, 0x60C, 0x680};
    for (int i = 0; i < 4; i++) {
        select_font(1);
        print_text(MSG(pages[i]));
        select_font(0);
        get_command_key();
    }
}

void shrine_walk_step(void)
{
    wait_turns(1);
    step_sound();
    wait_turns(4);
}

/* The Avatar walks in from the bottom of the scene to the altar (4 steps) or the Codex (7),
 * and out again; 16 minutes pass. At the Codex the urns' names are read on the way. */
void visit_shrine(void)
{
    int codex = *get_map_tile_ptr(dg.party_y, dg.party_x) == 0x11;
    dg.redraw_flags = 1;
    dg.saved_location = dg.location;
    dg.location = 0xFF;
    dg.combat_turn = 0xFF;
    memcpy(dg.objects_backup, dg.objects, 0x100);
    for (int i = 0; i < 0x20; i++) dg.objects[i].tile = 0;
    load_file_part(codex ? 0x160 : 0xB0, 0xB0, dg.view_tiles, S(0x95FC));   /* "MISCMAPS.DAT" */
    memset(dg.text_buffer, 0, 2000);
    load_file_part(0x3AB, 2000, dg.text_buffer, S(0x9618));                 /* "MISCMSG.DAT" */
    for (int r = 0; r < 11; r++) memcpy(&dg.local_map[r * 0x20], &dg.view_tiles[r * 0x10], 11);
    world_tick();
    int urns = 0;
    for (int m = 1; m < 16; m++)
        if (dg.party[m].where == 0x7F) urns++;
    if (urns > 0) shrine_place_urns(urns > 8 ? 8 : urns);
    print_text(MSG(codex ? 0x6DB : 0x6AE));   /* "The Codex of Ultimate Wisdom lies before thee..." / "Thou dost approach the tranquil Shrine..." */
    for (int i = 4; i != 0; i--) shrine_walk_step();
    MapObject *me = &dg.objects[0];
    me->tile = me->tile2 = 0x1C;
    me->x = 5;
    me->y = 10;
    me->z = dg.z_level;
    shrine_walk_step();
    if (codex && urns > 0) {
        print_text(S(0x9624));   /* "\n\nThou dost see\n" */
        print_text(S(urns == 1 ? 0x9636 : 0x9648));   /* "an urn marked:" / "urns marked:" */
        for (int m = 1; m < 16; m++) {
            if (dg.party[m].where != 0x7F) continue;
            for (const char *p = dg.party[m].name; *p; p++) put_char((uint8_t)to_upper((uint8_t)*p));
            put_char('\n');
        }
    }
    for (int i = codex ? 7 : 4; i != 0; i--) {
        me->y--;
        shrine_walk_step();
    }
    if (codex) codex_shrine();
    else virtue_shrine();
    me->tile = me->tile2 = 0x1C;
    wait_turns(1);
    while (me->y < 10) {
        me->y++;
        shrine_walk_step();
    }
    me->y = me->x = me->tile = 0;
    for (int i = 4; i != 0; i--) shrine_walk_step();
    wait_turns(10);
    memcpy(dg.objects, dg.objects_backup, 0x100);
    dg.location = dg.saved_location;
    pass_time(0x10);
    update_light_map();
}
