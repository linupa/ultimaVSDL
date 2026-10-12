/* shrine.cpp - the shrines of virtue and the Codex (CAST2.OVL). See shrine.h. */
#include <string.h>
#include "combat.h"
#include "commands.h"
#include "endings.h"
#include "gamedata.h"
#include "gfx.h"
#include "input.h"
#include "map.h"
#include "misc.h"
#include "objects.h"
#include "party.h"
#include "platform.h"
#include "shrine.h"
#include "status.h"
#include "strings.h"
#include "text.h"
#include "textbuf.h"
#include "view.h"
#include "world.h"

#define MSG(off) ((const char *)text_buffer.data + (off))   /* MISCMSG.DAT from 0x3AB, loaded by visit_shrine */

/* The shrines' read-only tables, from DATA.OVL (gamedata.h). By shrine 0-7: the virtue to meditate
 * on (ds:4B3E), where its quest text is in MISCMSG.DAT (ds:4B5E), and whether finishing the
 * quest raises strength, dexterity or intelligence (ds:4B7E, ds:4B86, ds:4B8E); by Codex
 * question 0-7, where its text is (ds:4B6E). The urns: for 1-8 of them, the first of their
 * places (ds:4B96), and the places' x and y (ds:4B9E, ds:4BC2). The tune the shrine plays:
 * seven sweeps (sound_sweep's increment ds:4BE6, count 4BF4, from 4C02 and step 4C10). */
static Strings::Id virtue_words[8];
static uint16_t quest_texts[8], codex_texts[8];
static uint8_t gives_str[8], gives_dex[8], gives_int[8];
static uint8_t urn_first[8], urn_x[36], urn_y[36];
static uint16_t tune_increment[7], tune_from[7];
static int16_t tune_count[7], tune_step[7];
[[maybe_unused]] static const bool shrine_tables_added = (Data::instance().add(0x4B3E, virtue_words, "shrine.virtue_words"),
                                                          Data::instance().add(0x4B5E, quest_texts, "shrine.quest_texts"),
                                                          Data::instance().add(0x4B6E, codex_texts, "shrine.codex_texts"),
                                                          Data::instance().add(0x4B7E, gives_str, "shrine.gives_str"),
                                                          Data::instance().add(0x4B86, gives_dex, "shrine.gives_dex"),
                                                          Data::instance().add(0x4B8E, gives_int, "shrine.gives_int"),
                                                          Data::instance().add(0x4B96, urn_first, "shrine.urn_first"),
                                                          Data::instance().add(0x4B9E, urn_x, "shrine.urn_x"),
                                                          Data::instance().add(0x4BC2, urn_y, "shrine.urn_y"),
                                                          Data::instance().add(0x4BE6, tune_increment, "shrine.tune_increment"),
                                                          Data::instance().add(0x4BF4, tune_count, "shrine.tune_count"),
                                                          Data::instance().add(0x4C02, tune_from, "shrine.tune_from"),
                                                          Data::instance().add(0x4C10, tune_step, "shrine.tune_step"), true);

/* 1-8 urns stand around the altar, at urn_x / urn_y from urn_first[count - 1] on, as objects 1
 * up with tile 3. */
void shrine_place_urns(int count)
{
    if (count < 1) return;
    unsigned pos = urn_first[(count - 1) & 7];
    for (int i = 0; i < count && pos < 36; i++, pos++)
        set_object_entry(i + 1, 0, map.z, urn_y[pos], urn_x[pos], 3, 3);
}

static void flash_and_hum(int length, int pitch)
{
    set_draw_color(gfx.color_text);
    xor_fill_rect(0xB7, 0xB7, 8, 8);
    for (int f = 2000; f < 25000; f += 0x32) sound_sweep(0, f, length, 1, pitch);
    for (int f = 25000; f > 2000; f -= 0x32) sound_sweep(0, f, length, 1, pitch);
}

/* The virtue is matched on its first four letters, the mantra three times. Then:
 *  - not yet answered at the Codex: the quest is given (or given again);
 *  - answered, quest not held: an offering of 100-900 gold buys 1-9 karma;
 *  - answered and the quest held: "WELL DONE!", karma +3 (+6 at Humility) and +1 to the
 *    Avatar's strength, dexterity and intelligence as gives_str / gives_dex / gives_int say. */
void virtue_shrine(void)
{
    int shrine;
    for (shrine = 0; shrine < 8; shrine++)
        if (map.shrine_x((unsigned)shrine) == map.x && map.shrine_y((unsigned)shrine) == map.y) break;
    if (shrine == 8) shrine = 6;   /* reached through the midnight moongate: Spirituality */
    uint8_t bit = (uint8_t)(1 << shrine);
    char typed[13];   /* the virtue, then each mantra (the original's buffer at ds:BD08) */
    map.objects[0].tile = map.objects[0].tile2 = 0x6C;   /* kneeling */
    world_tick();
    print_text(MSG(0x36D));   /* "...and thou dost kneel before the Altar." */
    wait_turns(10);
    print_text(MSG(0x398));   /* "Upon what virtue dost thou meditate?" */
    read_line(0x0C, typed);
    if (typed[0] == '\0') return;
    int ok = str_find_nocase(typed, strings[virtue_words[shrine]]) != -1;
    wait_turns(6);
    put_char('\n');
    for (int i = 0; i < 3; i++) {
        print_text(strings[Strings::Mantra_958E]);   /* "\nMantra:" */
        read_line(0x0C, typed);
        if (typed[0] == '\0') return;
        if (str_find_nocase(typed, map.mantra((unsigned)shrine)) == -1) ok = 0;
        wait_turns(0x0C);
    }
    if (!ok) {
        print_text(MSG(0x3C0));   /* "Thine thoughts are unfocused." */
        return;
    }
    if (!(party.codex_answered & bit)) {
        party.sacred_quest |= bit;
        print_text(MSG(0x3E1));   /* "The Altar speaks and a Quest is ordained!" */
        map.objects[0].tile = map.objects[0].tile2 = 0x1C;
        get_command_key();
        print_text(MSG(0x40E));   /* "'Tis now thy sacred Quest to go unto the Codex and learn " */
        print_text(MSG(quest_texts[shrine]));
        print_text(strings[Strings::Quote_NL_9598]);    /* "\"\n" */
        get_command_key();
        print_text(MSG(0x44B));   /* "Return again when thy Quest is done!" */
        for (int i = 0; i < 7; i++)
            sound_sweep(tune_step[i], tune_from[i], tune_count[i], 1, tune_increment[i]);
    } else if (!(party.sacred_quest & bit)) {
        int n;
        for (;;) {
            print_text(MSG(0x474));   /* "Offer how many hundredweights gold? " */
            unsigned key;
            do {
                key = (unsigned)get_command_key();
                if (platform->quit_requested()) return;
            } while (key < '0' || key > '9');
            put_char((uint8_t)key);
            n = (int)key - '0';
            if (n == 0) {
                print_text(strings[Strings::gp_959C]);   /* " gp\n" */
                return;
            }
            print_text(strings[Strings::N00_gp_95A2]);   /* "00 gp\n\n" */
            if (n * 100 <= (int16_t)party.gold) break;
            print_text(MSG(0x49B));  /* "Thou hast not that much gold!" */
        }
        party.gold = (uint16_t)(party.gold - n * 100);
        draw_status_bar();
        add_byte_capped(99, (uint8_t)n, &party.karma);
        select_font(1);
        print_text(strings[Strings::ALAKAZAM_95AA]);   /* "ALAKAZAM" in runes */
        select_font(0);
        print_text(strings[Strings::Bang_NL_95B4]);   /* "!\n" */
        flash_and_hum(200, 0xA8C);
    } else {
        party.sacred_quest &= (uint8_t)~bit;
        print_text(MSG(0x4B9));   /* "A thunderous voice booms: \"WELL DONE!\"" */
        flash_and_hum(0x96, 0xC1C);
        screen_shake();
        add_byte_capped(99, 3, &party.karma);
        if (gives_str[shrine] != 0) {
            add_byte_capped(0x1E, 1, &party[0].str);
            print_text(strings[Strings::Strength_1_95B8]);   /* "Strength +1\n" */
        }
        if (gives_dex[shrine] != 0) {
            add_byte_capped(0x1E, 1, &party[0].dex);
            print_text(strings[Strings::Dexterity_1_95C6]);   /* "Dexterity +1\n" */
        }
        if (gives_int[shrine] != 0) {
            add_byte_capped(0x1E, 1, &party[0].intel);
            print_text(strings[Strings::Intelligence_1_95D4]);   /* "Intelligence +1\n" */
        }
        if (shrine == 7) add_byte_capped(99, 3, &party.karma);
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
        if (party.sacred_quest & (1 << q)) break;
    if (q == 8) {
        print_text(MSG(0x53E));   /* "HOW DID YOU GET HERE?" */
        return;
    }
    party.codex_answered |= (uint8_t)(1 << q);
    put_char('"');
    print_text(MSG(codex_texts[q]));
    print_text(strings[Strings::Quote_NL_NL_95E6]);   /* "\"\n\n" */
    get_command_key();
    if (party.codex_answered != 0xFF) return;
    for (int i = 0; i < 3; i++) {
        set_draw_color((i & 1) ? gfx.color_text : gfx.color_fire);
        xor_fill_rect(0xB7, 0xB7, 8, 8);
        screen_shake();
    }
    print_text(MSG(0x555));   /* "A STRANGE WIND CAUSES THE PAGE TO TURN!" */
    get_command_key();
    print_text(strings[Strings::Thou_dost_read_95EA]);    /* "Thou dost read:\n\n" */
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
    int codex = *map.get_map_tile_ptr(map.y, map.x) == 0x11;
    view.rebuild = true;
    map.saved_location = map.id();
    map.set(Map::SCENE);
    combat.turn = 0xFF;
    memcpy(map.objects_backup, map.objects.bytes(), 0x100);
    for (int i = 0; i < 0x20; i++) map.objects[i].tile = 0;
    load_file_part(codex ? 0x160 : 0xB0, 0xB0, view.tiles, strings[Strings::MISCMAPS_DAT_95FC]);   /* "MISCMAPS.DAT" */
    memset(text_buffer.data, 0, 2000);
    load_file_part(0x3AB, 2000, text_buffer.data, strings[Strings::MISCMSG_DAT_9618]);                 /* "MISCMSG.DAT" */
    for (int r = 0; r < 11; r++) memcpy(&view.local_map[r * 0x20], &view.tiles[r * 0x10], 11);
    world_tick();
    int urns = 0;
    for (int m = 1; m < 16; m++)
        if (party[m].where == 0x7F) urns++;
    if (urns > 0) shrine_place_urns(urns > 8 ? 8 : urns);
    print_text(MSG(codex ? 0x6DB : 0x6AE));   /* "The Codex of Ultimate Wisdom lies before thee..." / "Thou dost approach the tranquil Shrine..." */
    for (int i = 4; i != 0; i--) shrine_walk_step();
    MapObject *me = &map.objects[0];
    me->tile = me->tile2 = 0x1C;
    me->x = 5;
    me->y = 10;
    me->z = map.z;
    shrine_walk_step();
    if (codex && urns > 0) {
        print_text(strings[Strings::Thou_dost_see_9624]);   /* "\n\nThou dost see\n" */
        print_text(strings[urns == 1 ? Strings::an_urn_marked_9636 : Strings::urns_marked_9648]);   /* "an urn marked:" / "urns marked:" */
        for (int m = 1; m < 16; m++) {
            if (party[m].where != 0x7F) continue;
            for (const char *p = party[m].name; *p; p++) put_char((uint8_t)to_upper((uint8_t)*p));
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
    memcpy(map.objects.bytes(), map.objects_backup, 0x100);
    map.set(Map::Id(map.saved_location));
    pass_time(0x10);
    update_light_map();
}
