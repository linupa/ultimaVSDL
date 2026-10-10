/* game.c - main's loop, the command dispatcher and the main program's command handlers. See
 * game.h. */
#include "cmds.h"
#include "combat.h"
#include "commands.h"
#include <string.h>
#include "data.h"
#include "dungeon.h"
#include "game.h"
#include "look.h"
#include "shrine.h"
#include "magic.h"
#include "stats.h"
#include "gfx.h"
#include "map.h"
#include "objects.h"
#include "outdoor.h"
#include "platform.h"
#include "sjog.h"
#include "status.h"
#include "talk.h"
#include "text.h"
#include "town.h"
#include "view.h"
#include "world.h"

#define S(off) ds_str(off)

static int exit_requested;

void exit_game(int code)
{
    (void)code;
    exit_requested = 1;
}

int game_quit(void) { return exit_requested || platform_quit_requested(); }

void game_clear_exit(void) { exit_requested = 0; }

/* ---- the main program's commands ---- */

int cmd_set_active_player(int key)
{
    unsigned member = (unsigned)(key - '1');
    print_text(S(0xA396));   /* "Set Active Plr:\n" */
    if (key == '0') {
        print_text(S(0xA3A8));   /* "None!\n" */
        dg.active_player = 0xFF;
    } else {
        if (dg.party_size <= member || dg.party[member].status == 'D' || dg.party[member].status == 'S') {
            print_text(S(0xA3B0));   /* "Invalid!\n" */
            return 1;
        }
        dg.active_player = (uint8_t)member;
        print_text(dg.party[member].name);
        put_char('\n');
    }
    draw_status_bar();
    return 0;
}

/* On a ship with furled sails: five 5-minute turns (the monsters move), then the hull gains
 * 1-3 at a time until it is at least 10 (99 at most). Otherwise the party camps for 1-9 hours,
 * with a watch if it wants one (camp_rest, on a combat map). The ship test reads
 * the party's object (objects[0]), which the view keeps equal to `transport`. */
void cmd_hole_up(void)
{
    uint8_t tile = dg.objects[0].tile2;
    print_text(S(0xA2C2));   /* "Hole up & " */
    if ((tile & 0xF8) == 0x20) {
        print_text(S(0xA2CE));   /* "\nrepair...\n\n" */
        if (dg.objects[0].tile2 <= 0x23) {
            print_text(S(0xA2DC));   /* "Sails must be\n" */
            print_text(S(0xA2EC));   /* "lowered!\n\n" */
            dg.redraw_flags = 1;
            return;
        }
        for (int i = 0; i < 5; i++) {
            wait_if_object1_near();
            outdoor_monsters_turn();
            if ((dg.transport & 0xFC) != 0x24) return;
            pass_time(5);
        }
        do {
            dg.objects[0].hull = (uint8_t)(dg.objects[0].hull + random_between(3, 1));
            if (dg.objects[0].hull > 99) dg.objects[0].hull = 99;
        } while (dg.objects[0].hull < 10);
        print_text(S(0xA2F8));   /* "Hull now " */
        print_number(' ', 2, dg.objects[0].hull);
        print_text(S(0xA302));   /* "!\n\n" */
        dg.redraw_flags = 1;
        dg.status_dirty = 1;
        return;
    }
    print_text(S(0xA306));   /* "camp!\n\n" */
    if (dg.location < 0x21) tile = *get_map_tile_ptr(dg.party_y, dg.party_x);
    if (dg.location < 0x21 && tile != 0 && tile < 4) {
        print_text(S(0xA30E));   /* "On land or ship!\n\n" */
        dg.redraw_flags = 1;
        return;
    }
    if (dg.location < 0x21 && dg.objects[0].tile2 != 0x1C) {
        print_text(S(0xA322));   /* "On foot!\n" */
        dg.redraw_flags = 1;
        return;
    }
    print_text(S(0xA32C));   /* "For how many hours? (1-9) " */
    uint8_t key;
    do {
        key = (uint8_t)get_command_key();
        if (game_quit()) return;
    } while (key != ' ' && (key < '0' || key > '9'));
    put_char(key);
    put_char('\n');
    if (key == ' ' || key == '0') return;
    int able = 0, guard = -1;
    for (unsigned i = 0; i != dg.party_size; i++)
        if (dg.party[i].status == 'G' || dg.party[i].status == 'P') able++;
    if (able > 1) {
        print_text(S(0xA348));   /* "\nWilt thou set a watch? " */
        char c;
        do {
            c = (char)get_command_key();
            if (game_quit()) return;
        } while (c != 'Y' && c != 'N');
        if (c == 'N') {
            print_text(S(0xA362));   /* "No\n\n" */
        } else {
            print_text(S(0xA368));   /* "Yes\n\n" */
            print_text(S(0xA36E));   /* "Who will stand guard? " */
            guard = ask_member_number();
            put_char('\n');
            if (guard == -1 || dg.party[guard].status != 'G') {
                guard = -1;
                print_text(S(0xA386));   /* "None posted!\n\n" */
            }
        }
    }
    if (dg.location > 0x20) {   /* a dungeon camp is a corridor fight map */
        dg.combat_flags = 6;
        unload_dungeon_graphics();
        setup_corridor_combat();
        run_combat(key - '0', guard, dg.combat_flags);
        load_dungeon_graphics(0);
        memcpy(&dg.objects[1], dg.objects_backup + 8, 8);
        dungeon_spawn_monster(0);
        dg.redraw_flags = 1;
        return;
    }
    camp_ambush_combat(key - '0', guard);
    dg.redraw_flags = 1;
}

/* SAVED.GAM is the saved part of the data segment; SAVED.OOL is BRIT.OOL followed by UNDER.OOL as
 * they are on disk (the objects of the map the party is on are inside SAVED.GAM). Underground,
 * UNDER.OOL is written back first (in the original it lives on another disk). */
void cmd_quit_save(void)
{
    print_text(S(0x9658));   /* "\nSave game? " */
    char c;
    do {
        c = (char)get_command_key();
        if (game_quit()) return;
    } while (c != 'Y' && c != 'N');
    if (c == 'N') {
        print_text(S(0x9666));   /* "No\n" */
        return;
    }
    print_text(S(0x966A));   /* "Yes\nSaving...\n" */
    uint8_t disk = dg.cur_disk;
    load_file_part(0, 0x100, dg.text_buffer + 0x100, S(0x967A));   /* UNDER.OOL */
    load_file_part(0, 0x100, dg.text_buffer, S(0x9684));           /* BRIT.OOL */
    if (disk != 1) save_file_part(0x100, dg.text_buffer + 0x100, S(0x968E));
    save_file_part(0x1060, ds_ptr(SAVE_OFFSET), S(0x9698));        /* SAVED.GAM */
    save_file_part(0x200, dg.text_buffer, S(0x96A2));              /* SAVED.OOL */
    print_text(S(0x96AC));   /* "Done.\n" */
}

int dispatch_command(int key)
{
    switch (key) {
    case ' ':
        if (dg.location == 0 && dg.sail_direction != 0) {
            print_text(S(0xA122));   /* "Sheets in irons!\n" */
            dg.sail_direction = 0;
            return 1;
        }
        print_text(S(0xA134));   /* "Pass\n" */
        return 1;
    case 'A':
        if (dg.location == 0) return cmd_attack_outdoor();
        if (dg.location > 0x20) return cmd_attack_dungeon();
        return cmd_attack_town();
    case 'B':
        print_text(S(0xA13A));   /* "Board " */
        return cmd_board();
    case 'C':
        print_text(S(0xA142));   /* "Cast...\n" */
        return cmd_cast();
    case 'D':
        print_text(S(0xA14C));   /* "D-What?\n" */
        return 0;
    case 'E':
        if (dg.location == 0) return cmd_enter();
        print_text(S(0xA156));   /* "Enter what?\n" */
        return 1;
    case 'F':
        print_text(S(0xA164));   /* "Fire-" */
        cmd_fire();
        return 1;
    case 'G':
        if (dg.location < 0x21) print_text(S(0xA16A));   /* "Get-" */
        cmd_get();
        return 1;
    case 'H':
        if (dg.location == 0 || dg.location > 0x20) {
            cmd_hole_up();
            return 1;
        }
        {
            uint8_t tile = *get_map_tile_ptr(dg.party_y, dg.party_x);
            print_text(S(0xA170));   /* "Hole up- " */
            if (tile == 0xAB) cmd_hole_up_bed();
            else print_text(S(0xA17A));   /* "Only in bed!\n" */
        }
        return 1;
    case 'I':
        print_text(S(0xA188));   /* "Ignite torch!\n" */
        cmd_ignite_torch();
        return 1;
    case 'J':
        print_text(S(0xA198));   /* "Jimmy-" */
        cmd_jimmy();
        return 1;
    case 'K':
        if (dg.location == 0) {
            print_text(S(0xA1A0));   /* "Klimb-" */
            cmd_klimb_grapple();
            return 1;
        }
        if (dg.location > 0x20) return cmd_klimb_dungeon();
        return cmd_klimb_town();
    case 'L':
        print_text(S(0xA1A8));   /* "Look" */
        if (dg.location > 0x20 && dg.location < 0x29) {
            print_text(S(0xA1AE));   /* "...\n" */
            cmd_look_dungeon();
            return 1;
        }
        put_char('-');
        cmd_look();
        return 1;
    case 'M':
        print_text(S(0xA1B4));   /* "Mix Reagents\n\n" */
        cmd_mix_reagents();
        return 1;
    case 'N':
        print_text(S(0xA1C4));   /* "New Order" */
        cmd_new_order();
        return 1;
    case 'O':
        print_text(S(0xA1CE));   /* "Open-" */
        cmd_open();
        return 1;
    case 'P':
        if (dg.location < 0x21 || dg.location > 0x28) {
            print_text(S(0xA1E4));   /* "Push-" */
            cmd_push();
            return 1;
        }
        print_text(S(0xA1D4));   /* "Push\nNot here!\n" */
        return 0;
    case 'Q':
        print_text(S(0xA1EA));   /* "Quit:" */
        cmd_quit_save();
        return 0;
    case 'R':
        print_text(S(0xA1F0));   /* "Ready...\n\n" */
        cmd_ready();
        return 1;
    case 'S':
        print_text(S(dg.location < 0x21 ? 0xA1FC : 0xA204));   /* "Search-" / "Search...\n" */
        cmd_search();
        return 1;
    case 'T':
        if (dg.location == 0) {
            print_text(S(0xA210));   /* "Talk-" */
            if (ask_direction() == 0) return 1;
            print_text(S(0xA216));   /* "Funny, no response!\n" */
            return 1;
        }
        if (dg.location < 0x21) {
            print_text(S(0xA246));   /* "Talk-" */
            return cmd_talk() != 0 ? 2 : 1;
        }
        print_text(S(0xA22C));   /* "Talk-Funny, no response!\n" */
        return 1;
    case 'U':
        print_text(S(0xA24C));   /* "Use item\n\n" */
        cmd_use();
        return 1;
    case 'V':
        print_text(S(0xA258));   /* "View a gem!\n" */
        if (dg.gems == 0) {
            print_text(S(0xA266));   /* "You have none!\n" */
            return 1;
        }
        dg.gems--;
        if (dg.location > 0x20) show_dungeon_map();
        else show_area_map(dg.party_y, dg.party_x);
        return 1;
    case 'W':
        print_text(S(0xA276));   /* "W-What?\n" */
        return 0;
    case 'X':
        print_text(S(0xA280));   /* "X-it " */
        cmd_exit();
        return 1;
    case 'Y':
        print_text(S(0xA286));   /* "Yell " */
        return cmd_yell();
    case 'Z':
        print_text(S(0xA28C));   /* "Z-stats...\n" */
        cmd_zstats();
        return 1;
    case 0xFC:   /* the type-ahead buffer toggle */
        print_text(S(0xA110));   /* "Buffer O" */
        dg.typeahead_on = dg.typeahead_on == 0;
        print_text(S(dg.typeahead_on == 0 ? 0xA11E : 0xA11A));   /* per the original: "n\n" when off, "ff\n" when on */
        return 0;
    }
    print_text(S(0xA298));   /* "What?\n" */
    return 0;
}

/* main's loop. Coming from Britannia (or a dungeon) a town is entered fresh: its NPCs are loaded
 * and placed; a saved game inside a town or dungeon carries on with what was saved. After a town
 * or dungeon Britannia's objects come back from BRIT.OOL / UNDER.OOL (and, arriving in the
 * Underworld, are written straight back, a leftover of the disk swap). */
void game_run(void)
{
    exit_requested = 0;
    draw_status_bar();
    int from_dungeon = 0;
    if (dg.location >= 0x80) {   /* can't be: combat isn't saved */
        dg.location = 0;
        load_file_part(0, 0x100, (uint8_t *)dg.objects, objects_file_name());
    }
    for (;;) {
        int outdoors = dg.location == 0;
        if (outdoors) {
            outdoor_start();
            from_dungeon = 0;
            if (game_quit() || dg.location == 0) return;
        }
        if (dg.location < 0x21) {
            enter_town(outdoors || from_dungeon);
            town_main_loop();
            from_dungeon = 0;
        } else {
            dungeon_main_loop(outdoors);
            from_dungeon = 1;
        }
        if (game_quit()) return;
        load_file_part(0, 0x100, (uint8_t *)dg.objects, objects_file_name());
        if (dg.location == 0 && dg.z_level != 0) save_file_part(0x100, (const uint8_t *)dg.objects, objects_file_name());
    }
}
