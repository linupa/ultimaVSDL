/* game.cpp - main's loop, the command dispatcher and the main program's command handlers. See
 * game.h. */
#include "cmds.h"
#include "combat.h"
#include "commands.h"
#include <string.h>
#include "dungeon.h"
#include "game.h"
#include "input.h"
#include "map.h"
#include "look.h"
#include "misc.h"
#include "party.h"
#include "shrine.h"
#include "magic.h"
#include "stats.h"
#include "gfx.h"
#include "objects.h"
#include "outdoor.h"
#include "platform.h"
#include "sjog.h"
#include "status.h"
#include "strings.h"
#include "talk.h"
#include "text.h"
#include "textbuf.h"
#include "town.h"
#include "view.h"
#include "world.h"


static int exit_requested;

void exit_game(int code)
{
    (void)code;
    exit_requested = 1;
}

int game_quit(void) { return exit_requested || platform->quit_requested(); }

void game_clear_exit(void) { exit_requested = 0; }

/* ---- the main program's commands ---- */

int cmd_set_active_player(int key)
{
    unsigned member = (unsigned)(key - '1');
    print_text(strings[Strings::Set_Active_Plr_A396]);   /* "Set Active Plr:\n" */
    if (key == '0') {
        print_text(strings[Strings::None_A3A8]);   /* "None!\n" */
        party.active_player = 0xFF;
    } else {
        if (party.size() <= member || party[member].dead() || party[member].asleep()) {
            print_text(strings[Strings::Invalid_A3B0]);   /* "Invalid!\n" */
            return 1;
        }
        party.active_player = (uint8_t)member;
        print_text(party[member].name);
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
    uint8_t tile = map.objects[0].tile2;
    print_text(strings[Strings::Hole_up_A2C2]);   /* "Hole up & " */
    if ((tile & 0xF8) == 0x20) {
        print_text(strings[Strings::repair_A2CE]);   /* "\nrepair...\n\n" */
        if (map.objects[0].tile2 <= 0x23) {
            print_text(strings[Strings::Sails_must_be_A2DC]);   /* "Sails must be\n" */
            print_text(strings[Strings::lowered_A2EC]);   /* "lowered!\n\n" */
            view.rebuild = true;
            return;
        }
        for (int i = 0; i < 5; i++) {
            wait_if_object1_near();
            outdoor_monsters_turn();
            if ((party.transport & 0xFC) != 0x24) return;
            pass_time(5);
        }
        do {
            map.objects[0].hull = (uint8_t)(map.objects[0].hull + random_between(3, 1));
            if (map.objects[0].hull > 99) map.objects[0].hull = 99;
        } while (map.objects[0].hull < 10);
        print_text(strings[Strings::Hull_now_A2F8]);   /* "Hull now " */
        print_number(' ', 2, map.objects[0].hull);
        print_text(strings[Strings::Bang_NL_NL_A302]);   /* "!\n\n" */
        view.rebuild = true;
        status.dirty = true;
        return;
    }
    print_text(strings[Strings::camp_A306]);   /* "camp!\n\n" */
    if (map.on_tile_map()) tile = *map.get_map_tile_ptr(map.y, map.x);
    if (map.on_tile_map() && tile != 0 && tile < 4) {
        print_text(strings[Strings::On_land_or_ship_A30E]);   /* "On land or ship!\n\n" */
        view.rebuild = true;
        return;
    }
    if (map.on_tile_map() && map.objects[0].tile2 != 0x1C) {
        print_text(strings[Strings::On_foot_A322]);   /* "On foot!\n" */
        view.rebuild = true;
        return;
    }
    print_text(strings[Strings::For_how_many_hours_1_A32C]);   /* "For how many hours? (1-9) " */
    uint8_t key;
    do {
        key = (uint8_t)get_command_key();
        if (game_quit()) return;
    } while (key != ' ' && (key < '0' || key > '9'));
    put_char(key);
    put_char('\n');
    if (key == ' ' || key == '0') return;
    int able = 0, guard = -1;
    for (unsigned i = 0; i != party.size(); i++)
        if (party[i].good() || party[i].poisoned()) able++;
    if (able > 1) {
        print_text(strings[Strings::Wilt_thou_set_a_watch_A348]);   /* "\nWilt thou set a watch? " */
        char c;
        do {
            c = (char)get_command_key();
            if (game_quit()) return;
        } while (c != 'Y' && c != 'N');
        if (c == 'N') {
            print_text(strings[Strings::No_A362]);   /* "No\n\n" */
        } else {
            print_text(strings[Strings::Yes_A368]);   /* "Yes\n\n" */
            print_text(strings[Strings::Who_will_stand_guard_A36E]);   /* "Who will stand guard? " */
            guard = ask_member_number();
            put_char('\n');
            if (guard == -1 || !party[guard].good()) {
                guard = -1;
                print_text(strings[Strings::None_posted_A386]);   /* "None posted!\n\n" */
            }
        }
    }
    if (!map.on_tile_map()) {   /* a dungeon camp is a corridor fight map */
        combat.flags = 6;
        dungeon.unload_dungeon_graphics();
        dungeon.setup_corridor_combat();
        combat.run_combat(key - '0', guard, combat.flags);
        dungeon.load_dungeon_graphics(0);
        memcpy(&map.objects[1], map.objects_backup + 8, 8);
        dungeon.dungeon_spawn_monster(0);
        view.rebuild = true;
        return;
    }
    combat.camp_ambush_combat(key - '0', guard);
    view.rebuild = true;
}

/* SAVED.GAM is the saved part of the data segment; SAVED.OOL is BRIT.OOL followed by UNDER.OOL as
 * they are on disk (the objects of the map the party is on are inside SAVED.GAM). */
void cmd_quit_save(void)
{
    print_text(strings[Strings::Save_game_9658]);   /* "\nSave game? " */
    char c;
    do {
        c = (char)get_command_key();
        if (game_quit()) return;
    } while (c != 'Y' && c != 'N');
    if (c == 'N') {
        print_text(strings[Strings::No_9666]);   /* "No\n" */
        return;
    }
    print_text(strings[Strings::Yes_Saving_966A]);   /* "Yes\nSaving...\n" */
    load_file_part(0, 0x100, text_buffer.data + 0x100, strings[Strings::UNDER_OOL_967A]);   /* UNDER.OOL */
    load_file_part(0, 0x100, text_buffer.data, strings[Strings::BRIT_OOL_9684]);           /* BRIT.OOL */
    save_game_file(strings[Strings::SAVED_GAM_9698]);        /* SAVED.GAM */
    save_file_part(0x200, text_buffer.data, strings[Strings::SAVED_OOL_96A2]);              /* SAVED.OOL */
    print_text(strings[Strings::Done_96AC]);   /* "Done.\n" */
}

int dispatch_command(int key)
{
    switch (key) {
    case ' ':
        if (map.is_outdoors() && party.sail_direction != 0) {
            print_text(strings[Strings::Sheets_in_irons_A122]);   /* "Sheets in irons!\n" */
            party.sail_direction = 0;
            return 1;
        }
        print_text(strings[Strings::Pass_A134]);   /* "Pass\n" */
        return 1;
    case 'A':
        if (map.is_outdoors()) return cmd_attack_outdoor();
        if (!map.on_tile_map()) return dungeon.cmd_attack_dungeon();
        return town.cmd_attack_town();
    case 'B':
        print_text(strings[Strings::Board_A13A]);   /* "Board " */
        return cmd_board();
    case 'C':
        print_text(strings[Strings::Cast_A142]);   /* "Cast...\n" */
        return cmd_cast();
    case 'D':
        print_text(strings[Strings::D_What_A14C]);   /* "D-What?\n" */
        return 0;
    case 'E':
        if (map.is_outdoors()) return cmd_enter();
        print_text(strings[Strings::Enter_what_A156]);   /* "Enter what?\n" */
        return 1;
    case 'F':
        print_text(strings[Strings::Fire_A164]);   /* "Fire-" */
        cmd_fire();
        return 1;
    case 'G':
        if (map.on_tile_map()) print_text(strings[Strings::Get_A16A]);   /* "Get-" */
        cmd_get();
        return 1;
    case 'H':
        if (map.is_outdoors() || !map.on_tile_map()) {
            cmd_hole_up();
            return 1;
        }
        {
            uint8_t tile = *map.get_map_tile_ptr(map.y, map.x);
            print_text(strings[Strings::Hole_up_A170]);   /* "Hole up- " */
            if (tile == 0xAB) cmd_hole_up_bed();
            else print_text(strings[Strings::Only_in_bed_A17A]);   /* "Only in bed!\n" */
        }
        return 1;
    case 'I':
        print_text(strings[Strings::Ignite_torch_A188]);   /* "Ignite torch!\n" */
        cmd_ignite_torch();
        return 1;
    case 'J':
        print_text(strings[Strings::Jimmy_A198]);   /* "Jimmy-" */
        cmd_jimmy();
        return 1;
    case 'K':
        if (map.is_outdoors()) {
            print_text(strings[Strings::Klimb_A1A0]);   /* "Klimb-" */
            cmd_klimb_grapple();
            return 1;
        }
        if (!map.on_tile_map()) return dungeon.cmd_klimb_dungeon();
        return town.cmd_klimb_town();
    case 'L':
        print_text(strings[Strings::Look_A1A8]);   /* "Look" */
        if (map.is_dungeon()) {
            print_text(strings[Strings::Dot_Dot_Dot_NL_A1AE]);   /* "...\n" */
            dungeon.cmd_look_dungeon();
            return 1;
        }
        put_char('-');
        cmd_look();
        return 1;
    case 'M':
        print_text(strings[Strings::Mix_Reagents_A1B4]);   /* "Mix Reagents\n\n" */
        cmd_mix_reagents();
        return 1;
    case 'N':
        print_text(strings[Strings::New_Order_A1C4]);   /* "New Order" */
        cmd_new_order();
        return 1;
    case 'O':
        print_text(strings[Strings::Open_A1CE]);   /* "Open-" */
        cmd_open();
        return 1;
    case 'P':
        if (!map.is_dungeon()) {
            print_text(strings[Strings::Push_A1E4]);   /* "Push-" */
            cmd_push();
            return 1;
        }
        print_text(strings[Strings::Push_Not_here_A1D4]);   /* "Push\nNot here!\n" */
        return 0;
    case 'Q':
        print_text(strings[Strings::Quit_A1EA]);   /* "Quit:" */
        cmd_quit_save();
        return 0;
    case 'R':
        print_text(strings[Strings::Ready_A1F0]);   /* "Ready...\n\n" */
        cmd_ready();
        return 1;
    case 'S':
        print_text(strings[map.on_tile_map() ? Strings::Search_A1FC : Strings::Search_A204]);   /* "Search-" / "Search...\n" */
        cmd_search();
        return 1;
    case 'T':
        if (map.is_outdoors()) {
            print_text(strings[Strings::Talk_A210]);   /* "Talk-" */
            if (ask_direction() == 0) return 1;
            print_text(strings[Strings::Funny_no_response_A216]);   /* "Funny, no response!\n" */
            return 1;
        }
        if (map.on_tile_map()) {
            print_text(strings[Strings::Talk_A246]);   /* "Talk-" */
            return conversation.cmd_talk() != 0 ? 2 : 1;
        }
        print_text(strings[Strings::Talk_Funny_no_response_A22C]);   /* "Talk-Funny, no response!\n" */
        return 1;
    case 'U':
        print_text(strings[Strings::Use_item_A24C]);   /* "Use item\n\n" */
        cmd_use();
        return 1;
    case 'V':
        print_text(strings[Strings::View_a_gem_A258]);   /* "View a gem!\n" */
        if (party.gems == 0) {
            print_text(strings[Strings::You_have_none_A266]);   /* "You have none!\n" */
            return 1;
        }
        party.gems--;
        if (!map.on_tile_map()) dungeon.show_dungeon_map();
        else show_area_map(map.y, map.x);
        return 1;
    case 'W':
        print_text(strings[Strings::W_What_A276]);   /* "W-What?\n" */
        return 0;
    case 'X':
        print_text(strings[Strings::X_it_A280]);   /* "X-it " */
        cmd_exit();
        return 1;
    case 'Y':
        print_text(strings[Strings::Yell_A286]);   /* "Yell " */
        return cmd_yell();
    case 'Z':
        print_text(strings[Strings::Z_stats_A28C]);   /* "Z-stats...\n" */
        cmd_zstats();
        return 1;
    case 0xFC:   /* the type-ahead buffer toggle */
        print_text(strings[Strings::Buffer_O_A110]);   /* "Buffer O" */
        input.typeahead_on = input.typeahead_on == 0;
        print_text(strings[input.typeahead_on == 0 ? Strings::n_A11E : Strings::ff_A11A]);   /* per the original: "n\n" when off, "ff\n" when on */
        return 0;
    }
    print_text(strings[Strings::What_A298]);   /* "What?\n" */
    return 0;
}

/* main's loop. Coming from Britannia (or a dungeon) a town is entered fresh: its NPCs are loaded
 * and placed; a saved game inside a town or dungeon carries on with what was saved. After a town
 * or dungeon Britannia's objects come back from BRIT.OOL / UNDER.OOL. */
void game_run(void)
{
    exit_requested = 0;
    draw_status_bar();
    int from_dungeon = 0;
    if (map.in_scene()) {   /* can't be: combat isn't saved */
        map.set(Map::BRITANNIA);
        load_file_part(0, 0x100, map.objects.bytes(), objects_file_name());
    }
    for (;;) {
        int outdoors = map.is_outdoors();
        if (outdoors) {
            outdoor_start();
            from_dungeon = 0;
            if (game_quit() || map.is_outdoors()) return;
        }
        if (map.on_tile_map()) {
            town.enter_town(outdoors || from_dungeon);
            town.town_main_loop();
            from_dungeon = 0;
        } else {
            dungeon.dungeon_main_loop(outdoors);
            from_dungeon = 1;
        }
        if (game_quit()) return;
        load_file_part(0, 0x100, map.objects.bytes(), objects_file_name());
    }
}
