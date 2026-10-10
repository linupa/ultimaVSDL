/* test_game.c - the main loop and the commands with scripted keys, from the game's SAVED.GAM
 * (the start of the game, in Iolo's hut). Usage: test_game <game dir> <save dir>.
 * Writes test_game_*.ppm in the save dir. */
#include <stdio.h>
#include <string.h>
#include "cmds.h"
#include "combat.h"
#include "comsubs.h"
#include "dungeon.h"
#include "files.h"
#include "game.h"
#include "intro.h"
#include "look.h"
#include "shrine.h"
#include "magic.h"
#include "gfx.h"
#include "map.h"
#include "objects.h"
#include "outdoor.h"
#include "picture.h"
#include "platform.h"
#include "sjog.h"
#include "stats.h"
#include "text.h"
#include "tiles.h"
#include "town.h"
#include "video.h"
#include "view.h"
#include "world.h"

void platform_script_keys(const int *keys, int n);

enum { W = 1, E = 2, N = 3, S_ = 4, RET = 0x0D, ESC = 0x1B, CTRL_E = 5, CTRL_K = 0x0B };

static char transcript[65536], flat[65536];
static size_t tlen;
static void echo(uint8_t c, int rune)
{
    (void)rune;
    if (tlen + 1 < sizeof transcript) transcript[tlen++] = (char)c;
}

static int fails;
static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

/* The transcript with newlines and runs of spaces as one space, so wrapped text can be matched. */
static int has(const char *s)
{
    size_t j = 0;
    for (size_t i = 0; i < tlen && j + 1 < sizeof flat; i++) {
        char c = transcript[i] == '\n' ? ' ' : transcript[i];
        if (c == ' ' && j && flat[j - 1] == ' ') continue;
        flat[j++] = c;
    }
    flat[j] = 0;
    return strstr(flat, s) != NULL;
}

static char save[600];
static const char *save_dir;

/* Fresh state: SAVED.GAM, and SAVED.OOL split into the working object files as Journey Onward does. */
static void setup(void)
{
    static uint8_t ool[0x200];
    dgroup_load_game(save);
    dg.path_grid = (uint16_t)offsetof(struct DGroup, text_buffer);
    load_file_part(0, 0x200, ool, "SAVED.OOL");
    save_file_part(0x100, ool, "BRIT.OOL");
    save_file_part(0x100, ool + 0x100, "UNDER.OOL");
    reset_text_windows();
    select_text_window(0);
    video_clear();
    draw_screen_frame();
    define_text_window(9, 0x27, 1, 0x18, 1);
    define_text_window(0x17, 0x27, 0x0B, 0x18, 2);
    select_text_window(2);
    dg.world_tick_enabled = 1;
    game_clear_exit();
    tlen = 0;
}

/* Runs main's loop on the keys; the last key is never read if the game exits by itself. */
static void play(const int *keys, int n)
{
    platform_script_keys(keys, n);
    game_run();
    transcript[tlen] = 0;
}

static void shot(const char *name)
{
    char path[700];
    snprintf(path, sizeof path, "%s/test_game_%s.ppm", save_dir, name);
    if (video_save_ppm(path) == 0) printf("wrote %s\n", path);
}

static void print_transcript(const char *title)
{
    printf("---- %s\n", title);
    for (size_t i = 0; i < tlen; i++) {
        uint8_t c = (uint8_t)transcript[i];
        putchar(c == '\n' || (c >= 0x20 && c < 0x7F) ? c : '~');
    }
    printf("\n----\n");
}

static void outdoors_at(unsigned x, unsigned y)
{
    dg.location = 0;
    dg.z_level = 0;
    dg.party_x = (uint8_t)x;
    dg.party_y = (uint8_t)y;
    memset(&dg.objects[1], 0, sizeof dg.objects - sizeof dg.objects[0]);
    save_file_part(0x100, (const uint8_t *)dg.objects, "BRIT.OOL");
}

/* A simple player for combat, as a key source: Esc once the battle is won; otherwise attack when
 * an enemy is next to the fighter, else walk towards the nearest; while aiming, move the cursor
 * onto the enemy and fire. */
extern int (*platform_script_next_key)(void);
extern void (*platform_script_on_tick)(long ticks);
static int bot_keys, bot_shot;

static int nearest_enemy(const Combatant *me, int *ex, int *ey)
{
    int best = 999;
    for (int i = 0; i < 32; i++) {
        const Combatant *c = &dg.combatants[i];
        if (!(c->flags & 0x40) || (c->flags & 0x20) || combatant_side(i) == 0) continue;
        int d = distance(c->y, c->x, me->y, me->x);
        if (d < best) { best = d; *ex = c->x; *ey = c->y; }
    }
    return best;
}

static int combat_bot(void)
{
    if (++bot_keys > 6000 || dg.location != 0xFF) return -1;
    const Combatant *me = &dg.combatants[dg.combat_turn & 31];
    int ex = 0, ey = 0, d = nearest_enemy(me, &ex, &ey);
    if (!bot_shot && d < 999) {
        bot_shot = 1;
        world_tick();
        shot("combat");
    }
    if (dg.aim_mode) {
        if (d == 999) return ESC;
        if (dg.aim_x == ex && dg.aim_y == ey) return RET;
        if (dg.aim_x != ex) return dg.aim_x < ex ? E : W;
        return dg.aim_y < ey ? S_ : N;
    }
    if (dg.combat_victory || d == 999) return ESC;
    if (d <= 1) return 'A';
    static int last_turn = -1, last_x, last_y;
    if (last_turn == dg.combat_turn && last_x == me->x && last_y == me->y) {   /* the last step was blocked */
        last_turn = -1;
        return ' ';
    }
    last_turn = dg.combat_turn;
    last_x = me->x;
    last_y = me->y;
    if (me->x != ex && (me->y == ey || (bot_keys & 1))) return me->x < ex ? E : W;
    if (me->y != ey) return me->y < ey ? S_ : N;
    return ' ';
}

static int map_shot_key(void)
{
    shot("dungeon_map");
    return ' ';
}

static void strong_party(void)
{
    for (unsigned i = 0; i < dg.party_size; i++) dg.party[i].hp = dg.party[i].max_hp = 400;
}

/* Iolo's hut: the party sits at (15,15); wall torch at (17,13), barrel (14,12), chair (17,14),
 * bed (12,14), door (15,19). */
static void test_town(void)
{
    static const int keys[] = {
        'Z', RET, ' ', ' ', 'I', CTRL_K, '2', '0', 'D',   /* Ztats of the first member and out; Pass; ... */
        N, N, E, 'G', E,                       /* borrow the wall torch */
        W, N, 'S', W, '1', RET,                /* search the barrel */
        S_, S_, E, 'P', E,                     /* push the chair east */
        W, W, W, W, W, 'H', '1',               /* into bed for an hour */
        'N', '2', RET, '3', RET,               /* swap members 2 and 3 */
        E, S_, S_, E, S_, S_, 'O', S_,         /* to the door, open it */
        S_, S_, S_, S_, S_, S_, S_, S_, S_, S_, S_, S_, S_, S_, 'Y',   /* out of the hut and off the map */
        'E',                                   /* Britannia: straight back in */
        'Q', 'Y', CTRL_E, 'Y', 'x',
    };
    setup();
    char second[10], third[10];
    strcpy(second, dg.party[1].name);
    strcpy(third, dg.party[2].name);
    uint8_t torches = dg.torches, hour = dg.hour;
    check(dg.location == 13 && dg.party_x == 15 && dg.party_y == 15, "the saved game starts in Iolo's hut");
    play(keys, (int)(sizeof keys / sizeof keys[0]));
    print_transcript("town");
    check(has("Z-stats...") && has("Pass") && has("Ignite torch!") && has("D-What?"), "dispatcher prompts");
    check(dg.torches == torches - 1, "Ignite used a torch");
    check(has("Set Active Plr: Shamino") && has("Set Active Plr: None!"), "set active player");
    check(has("Get-East Borrowed!"), "Get borrows the wall torch");
    check(has("Search-West") && has("In the barrel thou dost find nothing of note."), "Search the barrel");
    check(has("Push-East Pushed!"), "Push the chair");
    check(has("Hole up- For how many hours? 1") && has("Zzzzzzz..."), "Hole up in bed");
    check(has("New Order") && !strcmp(dg.party[1].name, third) && !strcmp(dg.party[2].name, second), "New Order swaps two members");
    check(has("Open-South Opened!"), "Open the door");
    check(has("Exit to Britannia!"), "leaving the town");
    check(has("Enter hut"), "Enter the hut again from Britannia");
    check(dg.location == 13 && dg.party_x == 15 && dg.party_y == 30, "back in the hut, at its entrance");
    check(dg.hour >= hour + 1, "at least the hour in bed has passed");
    check(has("Save game? Yes Saving... Done."), "Quit and save");
    check(has("Exit to DOS?") && !platform_quit_requested(), "Ctrl-E Y ends the game (the script was not used up)");
    static uint8_t gam[SAVE_SIZE], ool[0x200];
    check(load_file_part(0, SAVE_SIZE, gam, "SAVED.GAM") == 0 && gam[0x5893 - SAVE_OFFSET] == 13 &&
          gam[0x5896 - SAVE_OFFSET] == 15 && gam[0x5897 - SAVE_OFFSET] == 30, "SAVED.GAM was written with the new position");
    check(load_file_part(0, 0x200, ool, "SAVED.OOL") == 0, "SAVED.OOL was written");
    shot("town");
}

static void test_outdoors(void)
{
    static const int keys[] = {
        'T', N, 'K', 'B', 'X', 'A', N, ' ',
        'Y', 'F', 'A', 'L', 'L', 'A', 'X', RET,
        'H', ' ',
        N, N, W, W, S_, E,
        'V', 'W', '9', CTRL_K, 'E',
    };
    setup();
    outdoors_at(dg.town_exit_x[13], (unsigned)dg.town_exit_y[13] + 1);
    unsigned start = dg.hour * 60u + dg.minute;
    play(keys, (int)(sizeof keys / sizeof keys[0]));
    print_transcript("outdoors");
    unsigned now = dg.hour * 60u + dg.minute;
    check(has("Talk-North Funny, no response!"), "Talk outdoors");
    check(has("Klimb-With what?"), "Klimb without a grapple");
    check(has("Board What?"), "Board nothing");
    check(has("X-it what?"), "X-it on foot");
    check(has("Attack-North Nothing to attack!"), "Attack nothing");
    check(has("Yell what? :FALLAX") && has("A word of power is uttered") && has("No effect!"), "Yell a word of power away from a dungeon");
    check(has("Hole up & camp!") && has("For how many hours? (1-9)"), "Hole up asks for the hours");
    check(has("View a gem! You have none!") && has("W-What?") && has("Invalid!"), "View, W and a bad player number");
    check(dg.location == 0 && now - start >= 20 && now - start < 120, "each turn outdoors takes at least 2 minutes");
    shot("outdoors");
}

/* A ship with furled sails on open water, hull 5, one skiff, a north wind. */
static void test_ship(void)
{
    static const int keys[] = {
        'H',                    /* repair */
        'F', E,                 /* facing north: east is abeam */
        'F', N,
        'Y', 'X',               /* hoist; can't leave under sail */
        E, E, 0, 0, 0, 0, 0,    /* turn east, then run on */
        'Y',                    /* furl */
        'X', 'B',               /* into the skiff and aboard again */
        0,
    };
    setup();
    unsigned sx = 0, sy = 0;
    for (unsigned y = 8; y < 248 && !sx; y += 8)
        for (unsigned x = 8; x < 248 && !sx; x += 8) {
            outdoors_at(x, y);
            enter_outdoors();
            int water = 1;
            for (int dy = -6; dy <= 6; dy++)
                for (int dx = -6; dx <= 6; dx++)
                    if (*get_map_tile_ptr(y + (unsigned)dy, x + (unsigned)dx) != 1) water = 0;
            if (water) { sx = x; sy = y; }
        }
    check(sx != 0, "found open sea");
    setup();
    outdoors_at(sx, sy);
    dg.transport = 0x24;
    dg.objects[0].tile = dg.objects[0].tile2 = 0x24;
    dg.objects[0].hull = 5;
    dg.objects[0].skiffs = 1;
    dg.wind_direction = 1;
    dg.hour = 12;   /* daylight, fewer monsters */
    play(keys, 5);
    check(has("Hole up & repair...") && has("Hull now") && dg.objects[0].hull >= 10, "Hole up repairs the hull");
    check(has("Fire-East Fire-North"), "a broadside");
    check(has("Fire-North Fire broadsides only!"), "no firing ahead");
    platform_script_keys(keys + 5, (int)(sizeof keys / sizeof keys[0]) - 5);
    game_run();
    transcript[tlen] = 0;
    print_transcript("ship");
    check(has("Yell HOIST!") && has("X-it Under sail!"), "Yell hoists the sails; no leaving under sail");
    check(has("Head East"), "the first key turns the ship");
    check(dg.party_x >= sx + 3 && dg.party_y == sy, "under sail the ship runs on without keys");
    check(has("Yell FURL!"), "Yell furls the sails");
    check(has("X-it ship!") && has("Board Ship"), "into the skiff and aboard again");
    check((dg.transport & 0xFC) == 0x24 && dg.objects[0].skiffs == 1, "the skiff is back on board");
    shot("ship");
}

/* A chest (level 20, not trapped) north of the party in the hut. */
static void test_chest(void)
{
    static const int keys[] = {
        'S', N, 'J', N, 'O', N,
        'G', N, 'G', N, 'G', N, 'G', N, 'G', N, 'G', N, 'G', N, 'G', N, 'G', N, 'G', N,
        'G', N, 'G', N, 'G', N, 'G', N, 'G', N, 'G', N, 'G', N, 'G', N, 'G', N, 'G', N,
        'O', W, 0,
    };
    setup();
    dg.active_player = 0;
    set_object_entry(5, 20, 0, 14, 15, 1, 1);
    uint8_t keys_before = dg.keys, karma = dg.karma;
    uint16_t gold = dg.gold;
    dg.location = 13;
    play(keys, (int)(sizeof keys / sizeof keys[0]));
    print_transcript("chest");
    check(has("Search-North Thou dost find") && (has("no trap!") || has("a trap!")), "Search a chest for traps");
    check(has("Jimmy-North Key broke!") && (dg.keys == keys_before - 1 || has(" key")), "Jimmy on an untrapped chest breaks the key");
    check(has("Open-North") && (has("Found:") || has("Chest empty!")), "Open the chest");
    check(dg.karma == karma - 2, "opening a chest in a town costs 2 karma");
    check(!has("Found:") || has("gold!") || has("key") || has("gem") || has("torch") || has("potion!") || has("A scroll:") || has("food!"), "Get picks up what was found");
    check(has("Nothing to get!"), "until nothing is left");
    check(!has("a sack of gold!") || dg.gold > gold, "the gold was added");
    check(has("Open-West Nothing to open!"), "Open with nothing there");
    int left = 0;
    for (int i = 1; i < 32; i++) left += dg.objects[i].tile != 0 && dg.objects[i].tile < 0x10 && dg.objects[i].tile != 1;
    check(left == 0, "no loose items remain");
}

static void test_mix(void)
{
    int keys[64], n = 0;
    setup();
    for (int i = 0; i < 8; i++) dg.reagents[i] = 10;
    uint8_t recipe = dg.spell_recipes[0], before = dg.spells[0];
    keys[n++] = 'M';
    keys[n++] = 'I';
    keys[n++] = 'X';     /* a slip */
    keys[n++] = 8;       /* backspace */
    keys[n++] = 'L';
    keys[n++] = RET;
    for (int i = 0; i < 8; i++) {
        if (recipe & (0x80 >> i)) keys[n++] = RET;
        keys[n++] = S_;
    }
    keys[n++] = 'M';
    keys[n++] = '3';
    keys[n++] = RET;
    keys[n++] = 'M';     /* and once more, giving up at the spell name */
    keys[n++] = RET;
    keys[n++] = 0;
    play(keys, n);
    print_transcript("mix");
    int used = 1;
    for (int i = 0; i < 8; i++) used &= dg.reagents[i] == ((recipe & (0x80 >> i)) ? 7 : 10);
    check(has("Mix Reagents") && has("For what spell? :IN XEN") && has("LOR"), "the spell is typed by its letters");
    check(has("Mixing...") && has("Done!"), "mixing");
    check(used, "3 of each reagent in the recipe were used");
    check(dg.spells[0] == before + 3, "3 In Lor spells mixed");
    check(has("None!"), "no spell named: nothing happens");
    shot("mix");
}

static void test_moongates(void)
{
    setup();
    int stone = -1;
    for (int i = 0; i < 8; i++)
        if (dg.moonstone_loc[i] == 0 && dg.moonstone_z[i] == 0) { stone = i; break; }
    check(stone >= 0, "a moonstone is buried in Britannia");
    if (stone < 0) return;
    unsigned x = dg.moonstone_x[stone], y = dg.moonstone_y[stone];
    outdoors_at(x, y + 1);
    dg.hour = 22;
    dg.moongate_phase = 0;
    enter_outdoors();
    update_moongates();
    check(*get_map_tile_ptr(y, x) == 0xDC && dg.moongate_phase == 1, "at night a moongate rises over the stone");
    world_tick();
    shot("moongate");
    dg.hour = 12;
    for (int i = 0; i < 3; i++) update_moongates();
    check(*get_map_tile_ptr(y, x) == 5 && dg.moongate_phase == 0, "by day it sinks back into the grass");
    int other = -1;
    for (int i = 0; i < 8; i++)
        if (i != stone && dg.moonstone_loc[i] == 0) { other = i; break; }
    if (other >= 0) {
        check(moongate_travel(other) == 1 && dg.party_x == dg.moonstone_x[other] && dg.party_y == dg.moonstone_y[other] &&
              moonstone_here(other), "moongate travel arrives at the other stone");
    }
    /* the sky bar: sun and moons */
    tlen = 0;
    dg.hour = 12;
    dg.day = 1;
    draw_sky_bar();
    transcript[tlen] = 0;
    check(tlen == 12 && transcript[5] == '*', "the sky bar shows the sun past its height at noon");
    check(dg.moon1_phase == ds_u8(0x1ED8 + 2) && dg.moon2_phase == ds_u8(0x1ED9 + 2), "moon phases come from the day");
}

static int attack_key(void)
{
    if (dg.location != 0xFF) return bot_keys++ == 0 ? N : -1;
    return combat_bot();
}

/* Moonglow (location 1): attack a townsperson. */
static void test_attack(void)
{
    setup();
    dg.location = 1;
    dg.z_level = 0;
    dg.party_x = 15;
    dg.party_y = 30;
    dg.hour = 12;
    enter_town(1);
    int npc = -1;
    for (int i = 1; i < 32; i++)
        if (dg.npc_type[i] >= 0x40 && dg.npc_type[i] < 0x80 && dg.npc_state[i].z == 0 && dg.objects[dg.npc_state[i].object].tile) { npc = i; break; }
    check(npc > 0, "found a townsperson");
    if (npc < 0) return;
    MapObject *o = &dg.objects[dg.npc_state[npc].object];
    uint8_t karma = dg.karma;
    /* stand south of them, wherever that is */
    dg.party_x = o->x;
    dg.party_y = (uint8_t)(o->y + 1);
    strong_party();
    bot_keys = 0;
    bot_shot = 1;
    platform_script_keys(NULL, 0);
    platform_script_next_key = attack_key;
    select_text_window(2);
    tlen = 0;
    int r = dispatch_command('A');
    platform_script_next_key = NULL;
    transcript[tlen] = 0;
    print_transcript("attack");
    check(r == 1 && has("Attack-North"), "Attack in a town");
    check(dg.karma == karma - 5, "attacking a townsperson costs 5 karma");
    check(has("*** CONFLICT ***") && (has("VICTORY!") || has("BATTLE IS LOST!")), "it is a fight, in the town's combat map");
    check((dg.npc_dead_bits[dg.location] >> npc & 1) && dg.npc_type[npc] == 0, "the NPC is gone for good");
    check(dg.location == 1, "back in the town");
}

/* Giant rats north of the party on the grass outside Iolo's hut. */
static void test_combat(void)
{
    setup();
    outdoors_at(dg.town_exit_x[13], (unsigned)dg.town_exit_y[13] + 1);
    enter_outdoors();
    strong_party();
    uint16_t xp = (uint16_t)(dg.party[0].xp + dg.party[1].xp + dg.party[2].xp);
    uint8_t px = dg.party_x, py = dg.party_y;
    set_object_entry(7, 0, 0, (uint8_t)(py - 1), px, 0x90, 0x90);
    static uint8_t before[0x100];
    memcpy(before, dg.objects, 0x100);
    bot_keys = bot_shot = 0;
    platform_script_keys(NULL, 0);
    platform_script_next_key = combat_bot;
    combat_with_object(7);
    platform_script_next_key = NULL;
    transcript[tlen] = 0;
    print_transcript("combat");
    printf("(%d keys)\n", bot_keys);
    check(has("GIANT RATS") && has("*** CONFLICT ***"), "the fight starts");
    check(has("armed with"), "a member's turn names the weapons");
    check(has("Attack-") && has("Aim!"), "Attack asks to aim");
    check(has("VICTORY!") || has("BATTLE IS LOST!"), "the fight ends");
    check(!has("VICTORY!") || has("killed!"), "a won fight has kills");
    check(!has("VICTORY!") || (uint16_t)(dg.party[0].xp + dg.party[1].xp + dg.party[2].xp) > xp, "experience was earned");
    check(dg.location == 0 && dg.party_x == px && dg.party_y == py, "back on the map where the party stood");
    check(dg.objects[7].tile == 0, "the monster is gone from the map");
    before[7 * 8] = before[7 * 8 + 1] = before[7 * 8 + 2] = before[7 * 8 + 3] = before[7 * 8 + 4] = 0;
    check(!memcmp(before + 8, (uint8_t *)dg.objects + 8, 0xF8), "the other map objects are as before");
    check(!platform_quit_requested(), "the player did not run out of keys");
}

/* The rules, with fixed pieces: a glass sword always hits for 99 and then shatters. */
static void test_combat_rules(void)
{
    setup();
    outdoors_at(dg.town_exit_x[13], (unsigned)dg.town_exit_y[13] + 1);
    enter_outdoors();
    dg.prev_location = 0;
    dg.location = 0xFF;
    load_combat_map(2);
    place_monster_group(0x20, 1);   /* orcs */
    int orcs = 0, orc = -1;
    for (int i = 6; i < 32; i++)
        if (dg.combatants[i].flags & 0x40) { orcs++; orc = i; }
    count_combat_sides();
    check(orcs >= 1 && orcs <= 10 && dg.dir_x == orcs && dg.dir_y == 3, "a group of 1-10 orcs against the three members");
    check(dg.combatants[0].flags == 0x80 && dg.combatants[0].speed == dg.party[0].dex &&
          dg.objects[dg.combatants[0].object].tile == 0x4C, "the Avatar is combatant 0");
    check(dg.combatants[orc].hp == 10 && dg.objects[dg.combatants[orc].object].tile == 0xC0, "an orc has 10 hit points and tile 0xC0");
    check(combatant_at(dg.combatants[orc].y, dg.combatants[orc].x) == orc, "combatant_at finds it");
    check(!combat_tile_free(dg.combatants[orc].y, dg.combatants[orc].x, 0x4C), "its square is not free");
    check(distance(0, 0, 3, 4) == 5 && distance(2, 2, 3, 3) == 1, "distance is the rounded-down square root");
    dg.party[0].left_hand = 0x27;
    dg.attack_item = 0x27;
    dg.combat_turn = 0;
    uint16_t xp = dg.party[0].xp;
    tlen = 0;
    melee_attack(0x27, (unsigned)orc, 0);
    transcript[tlen] = 0;
    check(has("Thy sword hath shattered!") && has("Orc killed!"), "the glass sword kills and shatters");
    check(dg.party[0].left_hand == 0xFF && dg.party[0].xp == xp + 3, "it is gone from the hand; 10 / 4 + 1 experience");
    uint8_t left = dg.objects[dg.combatants[orc].object].tile;
    check((dg.combatants[orc].flags & 0x40) == 0 && (left == 0x1F || left == 1), "the orc leaves a body or a chest");
    /* a sleeping member and the hazards */
    sleep_combatant(1);
    check(dg.party[1].status == 'S' && (dg.combatants[1].flags & 8), "sleep");
    wake_combatant(1);
    check(dg.party[1].status == 'G' && !(dg.combatants[1].flags & 8), "wake");
    dg.party[2].hp = 50;
    dg.local_map[dg.combatants[2].y * 0x20 + dg.combatants[2].x] = 0x8F;
    for (int i = 0; i < 20 && dg.party[2].hp == 50; i++) apply_tile_hazard(2);
    check(dg.party[2].hp < 50 && dg.party[2].hp >= 40, "lava burns for up to 10");
    apply_damage(99, 2);
    check(dg.party[2].status == 'D' && (dg.combatants[2].flags & 0x20) && dg.objects[dg.combatants[2].object].tile == 0x1E, "99 damage kills a member");
    count_combat_sides();
    check(dg.dir_y == 2, "two members left on the party's side");
    dg.location = 0;
}

static int magic_key(void);
static const int *magic_keys;
static int magic_n, magic_pos;

/* Keys from a list; when it runs out (or while aiming) the combat player takes over. */
static int magic_key(void)
{
    if (!dg.aim_mode && magic_pos < magic_n) return magic_keys[magic_pos++];
    if (dg.location == 0xFF && dg.aim_mode) return combat_bot();
    return -1;
}

static void cast(const int *keys, int n)
{
    magic_keys = keys;
    magic_n = n;
    magic_pos = 0;
    bot_keys = 0;
    bot_shot = 1;
    platform_script_keys(NULL, 0);
    platform_script_next_key = magic_key;
    select_text_window(2);
    tlen = 0;
}

static void end_cast(const char *title)
{
    platform_script_next_key = NULL;
    transcript[tlen] = 0;
    print_transcript(title);
}

static void test_magic(void)
{
    /* in the hut */
    setup();
    enter_town(0);
    dg.active_player = 0;
    for (int i = 0; i < 48; i++) dg.spells[i] = 0;
    dg.spells[0] = 3;     /* In Lor */
    dg.spells[4] = 2;     /* Mani */
    dg.spells[13] = 1;    /* Vas Flam */
    dg.spells[42] = 1;    /* In Mani Corp: 8th circle */
    dg.party[0].mp = 20;
    dg.party[0].level = 3;
    dg.light_spell = 0;
    static const int lor[] = {'L', 'I', RET};
    cast(lor, 3);
    int r = dispatch_command('C');
    (void)r;
    end_cast("In Lor");
    check(has("Cast...") && has("Spell name: :LOR IN") && dg.light_spell == 100 && dg.spells[0] == 2 && dg.party[0].mp == 19,
          "In Lor: light for 100 turns, one spell and one magic point used");
    dg.party[1].hp = 3;
    static const int mani[] = {'M', RET, '2', RET};
    cast(mani, 4);
    cmd_cast();
    end_cast("Mani");
    check(has("On who: ") && has("Success!") && dg.party[1].hp > 3 && dg.party[1].hp <= 33 && dg.spells[4] == 1, "Mani heals 1-30");
    static const int flam[] = {'V', 'F', RET};
    cast(flam, 3);
    cmd_cast();
    end_cast("Vas Flam in town");
    check(has("VAS FLAM") && has("Not here!") && dg.spells[13] == 1, "a combat spell can't be cast in town; it isn't used up");
    static const int grav[] = {'A', 'N', RET};   /* An Nox */
    cast(grav, 3);
    cmd_cast();
    end_cast("none mixed");
    check(has("None mixed!"), "a spell that isn't mixed");
    static const int corp[] = {'I', 'M', 'C', RET};
    uint8_t mp = dg.party[0].mp;
    cast(corp, 4);
    cmd_cast();
    end_cast("too high");
    check(has("Failed!") && dg.spells[42] == 0 && dg.party[0].mp == mp - 8, "above the caster's level: the spell and the points are lost");
    static const int bad[] = {'Z', 'Z', RET};
    cast(bad, 3);
    cmd_cast();
    end_cast("no such spell");
    check(has("No effect!"), "letters that are no spell");

    /* in a fight with orcs */
    setup();
    outdoors_at(dg.town_exit_x[13], (unsigned)dg.town_exit_y[13] + 1);
    enter_outdoors();
    strong_party();
    dg.prev_location = 0;
    dg.location = 0xFF;
    load_combat_map(2);
    place_monster_group(0x20, 1);
    world_tick();
    dg.combat_turn = 0;
    dg.active_player = 0xFF;
    dg.party[0].mp = 30;
    dg.party[0].level = 8;
    for (int i = 0; i < 48; i++) dg.spells[i] = 2;
    int orcs = 0;
    for (int i = 6; i < 32; i++) orcs += (dg.combatants[i].flags & 0x40) != 0;
    cast(flam, 3);
    cmd_cast();
    end_cast("Vas Flam");
    check(has("Attack-") == 0 && has("Aim!") && (has("Orc") || has("Failed!")), "Vas Flam is aimed like a missile");
    check(dg.spells[13] == 1 && dg.party[0].mp == 27, "third circle: 3 magic points");
    static const int zu[] = {'I', 'Z', RET, N};
    cast(zu, 4);
    cmd_cast();
    end_cast("In Zu");
    int asleep = 0;
    for (int i = 6; i < 32; i++) asleep += (dg.combatants[i].flags & 0x48) == 0x48;
    printf("(%d asleep)\n", asleep);
    check(asleep == 0 || has("slept!"), "In Zu puts monsters to sleep");
    static const int hur[] = {'F', 'H', 'I', RET, N};
    cast(hur, 5);
    cmd_cast();
    end_cast("In Flam Hur");
    shot("firecloud");
    int left = 0;
    for (int i = 6; i < 32; i++) left += (dg.combatants[i].flags & 0x40) != 0;
    check(has("Direction-North") && dg.party[0].mp == 14, "In Flam Hur: a direction, 8 points");
    printf("(orcs %d -> %d)\n", orcs, left);
    static const int lorw[] = {'L', 'S', RET};
    cast(lorw, 3);
    cmd_cast();
    end_cast("Sanct Lor");
    check((dg.combatants[0].flags & 0x10) && dg.objects[dg.combatants[0].object].tile2 == 0x1D && has("Success!"), "Sanct Lor: the caster is invisible");
    static const int tym[] = {'A', 'T', RET};
    dg.party[0].mp = 30;
    cast(tym, 3);
    cmd_cast();
    end_cast("An Tym");
    check(dg.active_spell == 'T' && dg.active_spell_turns == 10, "An Tym stops time for 10 turns");
    static const int kal[] = {'K', 'X', RET};
    cast(kal, 3);
    cmd_cast();
    end_cast("Kal Xen");
    int allies = 0;
    for (int i = 6; i < 32; i++) allies += (dg.combatants[i].flags & 0x41) == 0x41;
    check(!has("Success!") || allies == 1, "Kal Xen summons a creature on the party's side");
    dg.location = 0;
    dg.active_spell = 0;
}

static void test_use(void)
{
    setup();
    outdoors_at(dg.town_exit_x[13], (unsigned)dg.town_exit_y[13] + 1);
    enter_outdoors();
    world_tick();
    memset(dg.scrolls, 0, 8);
    memset(dg.potions, 0, 8);
    memset(dg.moonstone_loc, 0, 8);
    dg.magic_carpets = dg.odd_keys = dg.lb_amulet = dg.lb_crown = dg.lb_sceptre = 0;
    dg.shards[0] = dg.shards[1] = dg.shards[2] = 0;
    dg.quest_item_57ba = dg.quest_item_57bc = dg.quest_item_57be = dg.hms_cape_plans = dg.sandalwood_box = 0;
    ds_set_u8(0x57BD, 0);
    static const int none[] = {0};
    cast(none, 0);
    cmd_use();
    end_cast("nothing to use");
    check(has("No usable items!"), "Use with nothing");
    dg.potions[1] = 2;            /* yellow */
    dg.scrolls[2] = 1;            /* protection */
    dg.moonstone_loc[3] = 0xFF;   /* carried */
    ds_set_u8(0x57BD, 0xFF);      /* pocket watch */
    dg.party[0].hp = 10;
    static const int scroll[] = {RET};
    cast(scroll, 1);
    cmd_use();
    end_cast("scroll");
    shot("use");
    check(has("Item: ") && has("Scroll") && has("Protection!") && dg.active_spell == 'P' && dg.active_spell_turns == 100 && dg.scrolls[2] == 0,
          "the first item, a Protection scroll");
    static const int potion[] = {RET, '1', RET};
    cast(potion, 3);
    cmd_use();
    end_cast("potion");
    check(has("Potion") && dg.potions[1] == 1, "a potion is drunk");
    check(!has("Healed!") || dg.party[0].hp > 10, "the yellow potion heals (unless it turned out to be another)");
    static const int stone[] = {S_, RET};
    cast(stone, 2);
    cmd_use();
    end_cast("moonstone");
    check(has("Moonstone buried!") && dg.moonstone_loc[3] == 0 && dg.moonstone_x[3] == dg.party_x && dg.moonstone_y[3] == dg.party_y,
          "a moonstone is buried where the party stands");
    static const int watch[] = {S_, S_, S_, S_, RET};
    cast(watch, 5);
    cmd_use();
    end_cast("watch");
    check(has("The pocket watch reads") && (has(" AM.") || has(" PM.")), "the pocket watch (last of the list)");
    static const int esc[] = {ESC};
    cast(esc, 1);
    cmd_use();
    end_cast("esc");
    check(has("None!"), "Esc leaves the list");
}

static void test_ztats_ready(void)
{
    setup();
    enter_town(0);
    dg.active_player = 0xFF;
    world_tick();
    /* Ztats: member 1's two pages, then on through the party's pages, a jump to member 3 */
    static const int z[] = {RET, E, '0', E, E, E, E, S_, '3', W, ' '};
    cast(z, 11);
    cmd_zstats();
    end_cast("ztats");
    check(has("Player: ") && has("Status: "), "Ztats asks for the player");
    check(has("Lv-") && has("Avatar") && has("Good Health") && has("Str=20") && has("HP: 60") && has("Dex=24") && has("Magic:"), "the stats page");
    check(has("Arms") && has("Long Sword"), "the arms page");
    check(has("Equipment") && has("Food: 63") && has("Gold: 150") && has("Keys....... 2") && has("Torches.... 4"), "the equipment page");
    check(has("Reagents") && has("Spells") && has("Items") && has("Armaments"), "the four list pages");
    check(has("Iolo") && has("Bard") && has("Done"), "a jump to the third member, and out");

    /* Ready: the Avatar puts the long sword away and takes it up again */
    uint8_t sword = dg.party[0].left_hand, had = dg.inventory[sword];
    check(sword != 0xFF && is_equipped_by(sword, 0), "the Avatar holds a weapon");
    int row = 0;   /* the sword's row in the list */
    for (int i = list_next_item(0, dg.inventory, 0x30, -1); i != -1 && i != sword; i = list_next_item(0, dg.inventory, 0x30, i)) row++;
    static int r[40];
    int n = 0;
    r[n++] = '1';
    r[n++] = RET;
    for (int i = 0; i < row; i++) r[n++] = S_;
    r[n++] = RET;
    r[n++] = ESC;
    cast(r, n);
    cmd_ready();
    end_cast("unready");
    shot("ready");
    check(has("Item: ") && dg.party[0].left_hand == 0xFF && dg.inventory[sword] == had + 1 && has("Done"), "picking a held weapon puts it away");
    cast(r, n);
    cmd_ready();
    end_cast("ready");
    check((dg.party[0].left_hand == sword || dg.party[0].right_hand == sword) && dg.inventory[sword] == had, "picking it again readies it");

    /* the rules, directly */
    tlen = 0;
    dg.party[0].left_hand = dg.party[0].right_hand = 0xFF;
    dg.inventory[0x21] = 1;   /* a two-handed sword */
    dg.party[0].str = 30;
    check(free_hands(0) == 2, "both hands free");
    ready_item(0x21, 0);
    check(dg.party[0].left_hand == 0x21 && dg.inventory[0x21] == 0 && free_hands(0) == 0xFF, "a two-handed sword takes both hands");
    dg.inventory[0x10] = 1;   /* a dagger */
    ready_item(0x10, 0);
    transcript[tlen] = 0;
    check(has("Thou must free one of thy hands first!") && dg.inventory[0x10] == 1, "no hand left for a dagger");
    ready_item(0x21, 0);
    dg.party[0].str = 1;
    tlen = 0;
    ready_item(0x21, 0);
    transcript[tlen] = 0;
    check(has("Thou art not strong enough!") && dg.party[0].left_hand == 0xFF, "too heavy for strength 1");
    dg.inventory[0x1A] = 1;   /* a bow, no arrows */
    dg.inventory[0x1B] = 0;
    dg.party[0].str = 30;
    tlen = 0;
    ready_item(0x1A, 0);
    transcript[tlen] = 0;
    check(has("Thou hast no ammunition for that weapon!"), "a bow needs arrows");
}

static void test_look(void)
{
    setup();
    enter_town(0);
    dg.active_player = 0;
    world_tick();
    /* the party sits at (15,15) in the hut: plates to the east, the bed further west */
    static const int e[] = {E};
    cast(e, 1);
    cmd_look();
    end_cast("look east");
    check(has("Thou dost see") && tlen > 20, "Look describes the tile");
    static char first[64];
    strncpy(first, flat, 63);
    static const int n[] = {N};
    cast(n, 1);
    cmd_look();
    end_cast("look north");
    check(has("Thou dost see") && has("cobble"), "the cobbles to the north");
    /* an object: a chest */
    set_object_entry(5, 20, 0, 14, 15, 1, 1);
    cast(n, 1);
    cmd_look();
    end_cast("look at a chest");
    check(has("chest"), "an object is described from the second table");
    set_object_entry(5, 0, 0, 0, 0, 0, 0);
    /* a clock */
    *get_map_tile_ptr(14, 15) = 0xFA;
    dg.hour = 15;
    dg.minute = 7;
    cast(n, 1);
    cmd_look();
    end_cast("look at a clock");
    check(has("3:07 PM."), "a clock tells the time");
    /* a sign with no entry: the default */
    *get_map_tile_ptr(14, 15) = 0xA0;
    cast(n, 1);
    cmd_look();
    end_cast("look at a sign");
    check(has("LIVE@BY@[E") && has("EIGHT@LAWS"), "a sign without a text reads LIVE BY THE EIGHT LAWS (in runes)");
    /* a real sign: the first entry of Moonglow's */
    setup();
    dg.location = 1;
    dg.party_x = 15;
    dg.party_y = 30;
    enter_town(1);
    static uint8_t idx[0x42], ent[16];
    load_file_part(0, 0x42, idx, "signs.dat");
    load_file_part((uint32_t)(idx[2] | idx[3] << 8), 16, ent, "signs.dat");
    dg.z_level = ent[1];
    tlen = 0;
    select_text_window(2);
    read_sign(ent[3], ent[2], ent[1]);
    transcript[tlen] = 0;
    print_transcript("a sign in Moonglow");
    check(tlen > 40 && !has("LIVE@BY"), "a sign's own text");
    /* the gem map and the night sky */
    setup();
    enter_town(0);
    world_tick();
    static const int key[] = {0, 0, 0, 0, 0, 0, ' '};
    cast(key, 7);
    dg.gems = 2;
    dispatch_command('V');
    end_cast("view");
    check(dg.gems == 1 && has("View a gem!"), "View uses a gem");
    cast(key, 0);
    platform_script_next_key = NULL;
    static const int key2[] = {0, 0, ' '};
    platform_script_keys(key2, 3);
    show_area_map(dg.party_y, dg.party_x);
    set_draw_page(1);   /* the map is still on the back page */
    video_copy_rect(0, 0, 319, 199, 1, 0);
    set_draw_page(0);
    shot("map");
    world_tick();
    dg.hour = 23;
    static const int key3[] = {0, ' '};
    platform_script_keys(key3, 2);
    tlen = 0;
    look_sky();
    transcript[tlen] = 0;
    check(has("the night sky!"), "the night sky");
    shot("sky");
    dg.hour = 12;
    dg.active_player = 0;
    uint16_t hp = dg.party[0].hp;
    tlen = 0;
    look_sky();
    transcript[tlen] = 0;
    check(has("the sun!") && dg.party[0].hp == hp - 1, "looking at the sun hurts");
}

static const int *shrine_keys;
static int shrine_n, shrine_pos;
static int shrine_key(void)
{
    if (shrine_pos < shrine_n) return shrine_keys[shrine_pos++];
    return ' ';   /* the pauses */
}

static void visit(const int *keys, int n)
{
    shrine_keys = keys;
    shrine_n = n;
    shrine_pos = 0;
    platform_script_keys(NULL, 0);
    platform_script_next_key = shrine_key;
    select_text_window(2);
    tlen = 0;
    visit_shrine();
    platform_script_next_key = NULL;
    transcript[tlen] = 0;
}

/* The shrine of Honesty, the Codex and back. */
static void test_shrines(void)
{
    setup();
    outdoors_at(dg.shrine_x[0], dg.shrine_y[0]);
    enter_outdoors();
    world_tick();
    dg.sacred_quest = dg.codex_answered = 0;
    uint8_t karma = dg.karma = 50, str = dg.party[0].str, dex = dg.party[0].dex, intel = dg.party[0].intel;
    unsigned t0 = dg.hour * 60u + dg.minute;
    static int k[64];
    int n = 0;
    const char *mantra = ds_str(dg.mantras[0]);
    for (const char *p = "honesty"; *p; p++) k[n++] = *p;
    k[n++] = RET;
    for (int i = 0; i < 3; i++) {
        for (const char *p = mantra; *p; p++) k[n++] = *p;
        k[n++] = RET;
    }
    check(*get_map_tile_ptr(dg.party_y, dg.party_x) == 0x19, "the party stands on the shrine of Honesty");
    visit(k, n);
    print_transcript("shrine: the quest");
    shot("shrine");
    check(has("Thou dost approach the tranquil Shrine") && has("kneel before the Altar") && has("Mantra:"), "the shrine scene");
    check(has("a Quest is ordained!") && dg.sacred_quest == 1, "the right virtue and mantra give the quest");
    check(dg.location == 0 && dg.hour * 60u + dg.minute >= t0 + 16, "back outside, 16 minutes later");
    /* the Codex answers it */
    *get_map_tile_ptr(dg.party_y, dg.party_x) = 0x11;
    visit(k, 0);
    print_transcript("codex");
    check(has("The Codex of Ultimate Wisdom lies before thee") && has("Upon the hallowed page thou dost read:") && dg.codex_answered == 1,
          "the Codex answers the quest held");
    /* back at the shrine: the reward */
    *get_map_tile_ptr(dg.party_y, dg.party_x) = 0x19;
    visit(k, n);
    print_transcript("shrine: the reward");
    check(has("WELL DONE!") && dg.sacred_quest == 0 && dg.karma == karma + 3, "the return: WELL DONE, karma +3");
    check(dg.party[0].str + dg.party[0].dex + dg.party[0].intel > str + dex + intel, "and an attribute point");
    /* once more: an offering */
    k[n] = '2';
    dg.gold = 500;
    visit(k, n + 1);
    check(has("Offer how many hundredweights gold? 2") && dg.gold == 300 && dg.karma == karma + 5 && has("ALAKAZAM"), "later visits take offerings: 200 gold for 2 karma");
    /* a wrong mantra */
    k[n - 2] = 'x';
    visit(k, n);
    check(has("Thine thoughts are unfocused."), "a wrong mantra");
}

/* Deceit: in by Enter, a torch, a walk to the pit and the sign, Look, Search, the map, and out
 * by the ladder. Movement keys in a dungeon: left / right turn, up advances, down backs up. */
static void test_dungeon(void)
{
    static const int keys[] = {
        'E',
        'L', '1', RET, N,           /* in the dark */
        'I',
        E, N,                       /* turn right (south), advance to (1,2) */
        'L', '1', RET, N,           /* the pit ahead */
        N,                          /* onto the pit */
        'S', '1', RET, N,           /* the wall with the sign ahead */
        'V', ' ',                   /* the map */
        'A',                        /* nothing to attack */
        RET, N, N,                  /* turn around, back to the ladder */
        'K',                        /* up and out */
        CTRL_E, 'Y', 0,
    };
    setup();
    outdoors_at(dg.town_exit_x[0x21], dg.town_exit_y[0x21]);
    dg.dungeon_seals[0] = 0x80;   /* unsealed, as the word of power FALLAX does */
    dg.gems = 1;
    dg.torch_light = dg.light_spell = 0;
    dg.active_player = 0xFF;
    uint8_t torches = dg.torches;
    play(keys, (int)(sizeof keys / sizeof keys[0]));
    print_transcript("dungeon");
    check(has("Enter dungeon") && has("DECEIT"), "Enter the dungeon Deceit");
    check(has("You see: darkness."), "nothing to see without light");
    check(has("Ignite torch!") && dg.torches == torches - 1, "a torch");
    check(has("Turn right") && has("Advance"), "turning and walking");
    check(has("Dir-Ahead You see: a pit."), "Look ahead: the pit");
    check(has("You find: Nothing hidden on the wall."), "Search the wall ahead");
    check(has("View a gem!") && dg.gems == 0, "the gem shows the dungeon map");
    check(has("Attack What?"), "Attack with no monster ahead");
    check(has("Turn around.") && has("Klimb-Up!") && has("Exit to Britannia!"), "back up the ladder and out");
    check(dg.location == 0 && dg.z_level == 0 && dg.party_x == dg.town_exit_x[0x21] && dg.party_y == dg.town_exit_y[0x21], "outside at the entrance again");
    check(!platform_quit_requested(), "the script was not used up");

    /* pieces, directly: in Deceit on level 1 at (1,2) facing south, with light */
    setup();
    load_file_part(0, 0x200, ds_ptr((uint16_t)offsetof(struct DGroup, dungeon_map)), "DUNGEON.DAT");
    dg.location = 0x21;
    dg.z_level = 0;
    dg.party_x = 1;
    dg.party_y = 2;
    dg.facing = 2;
    dg.torch_light = 200;
    dg.active_player = 0;
    dg.dungeon_style = 3;
    dg.corridor_wall_tile = 0x4F;
    dg.corridor_floor_tile = 0x45;
    dg.dungeon_gfx_flags = 2;
    load_dungeon_graphics(1);
    draw_dungeon_frame();
    redraw_dungeon();
    shot("dungeon");
    platform_script_keys(NULL, 0);
    platform_script_next_key = map_shot_key;
    show_dungeon_map();
    platform_script_next_key = NULL;
    /* a fountain of healing ahead */
    DCELL(0, 3, 1) = 0x51;
    dg.party[0].hp = 5;
    static const int drink[] = {N, 'Y'};
    cast(drink, 2);
    cmd_look_dungeon();
    end_cast("fountain");
    check(has("a fountain.") && has("Will you drink?") && has("Healed!") && dg.party[0].hp == dg.party[0].max_hp, "a healing fountain");
    /* a trapped, locked chest under the party */
    DCELL(0, 2, 1) = 0x43;
    dg.keys = 5;
    static const int none[] = {0};
    cast(none, 0);
    cmd_get();
    end_cast("get closed");
    check(has("Must open first!"), "Get on a closed chest");
    cast(none, 0);
    cmd_open();
    end_cast("open chest");
    check(has("Chest opened") && (DCELL(0, 2, 1) & 0xF0) == 0x70, "Open the chest (its trap goes off)");
    uint16_t gold = dg.gold;
    cast(none, 0);
    cmd_get();
    end_cast("get");
    (void)gold;
    check(has("contents of chest You find:") && DCELL(0, 2, 1) == 0, "Get empties it");
    /* a hidden pit ahead is found by searching */
    DCELL(0, 3, 1) = 0x61;
    static const int ahead[] = {N};
    cast(ahead, 1);
    cmd_search();
    end_cast("search pit");
    check(has("A pit!") && DCELL(0, 3, 1) == 0x60 && (DCELL(1, 3, 1) & 8), "Search finds the hidden pit; the level below gets its hole");
    /* down the pit and up again by the spell's rule */
    dg.party_y = 3;
    check(dungeon_change_level(0, 1) == 0 && dg.z_level == 1, "down a level");
    check(dungeon_change_level(0, -1) == 0 && dg.z_level == 0, "and up");
    dg.z_level = 7;
    check(dungeon_change_level(0, 1) == 1, "below level 8 is the way out");
    dg.z_level = 0;
    /* a corridor fight with giant rats ahead */
    dg.party_y = 2;
    DCELL(0, 3, 1) = 0;
    strong_party();
    dg.objects[1].tile = dg.objects[1].tile2 = 0;
    dg.objects[1].hull = 0x14;
    dg.objects[1].x = 1;
    dg.objects[1].y = 3;
    dg.objects[1].z = 0;
    dg.dungeon_monster_pic = load_picture("MON0.16");
    draw_dungeon_view(1);
    copy_page_rect(0xB2, 0xAF, 0x0E, 0x10, 0, 1);
    shot("dungeon_monster");
    bot_keys = 0;
    bot_shot = 0;
    platform_script_keys(NULL, 0);
    platform_script_next_key = combat_bot;
    select_text_window(2);
    tlen = 0;
    cmd_attack_dungeon();
    platform_script_next_key = NULL;
    transcript[tlen] = 0;
    print_transcript("corridor fight");
    printf("(%d keys)\n", bot_keys);
    check(has("Attack") && has("armed with") && has("Giant Rat"), "a corridor fight is fought (the bot may fail to catch fleeing rats)");
    check(dg.location == 0x21 || dg.location == 0, "back in the dungeon (or out by a ladder)");
    /* a room's set-up: Destard level 1, room 0 */
    setup();
    load_file_part(2 * 0x200, 0x200, ds_ptr((uint16_t)offsetof(struct DGroup, dungeon_map)), "DUNGEON.DAT");
    dg.location = 0x23;
    dg.z_level = 0;
    dg.facing = 0;
    load_file_part((uint32_t)(1 * 0x1600 + 0 * 0x160), 0x160, dg.local_map, "dungeon.cbt");
    dg.prev_location = dg.location;
    dg.location = 0xFF;
    place_combatants(3, 0xF0);
    int monsters = 0, things = 0;
    for (int i = 6; i < 32; i++) monsters += (dg.combatants[i].flags & 0x40) != 0;
    for (int i = 0; i < 32; i++) things += dg.objects[i].tile != 0;
    count_combat_sides();
    check(dg.dir_y == 3 && monsters > 0 && things >= monsters + 3, "a room is set up with the party and its monsters");
    dg.combat_turn = 0xFF;
    world_tick();
    shot("room");
    check(is_room_cleared(0) == 0, "the room is not cleared yet");
    mark_room_cleared(0);
    check(is_room_cleared(0) == 1, "a cleared room is remembered");
    dg.location = 0;
}

/* Camp for 8 hours with a guard, on the same grass; whatever comes is fought. */
static void test_camp(void)
{
    static const int keys[] = {'8', 'Y', '1', RET};
    static int pos;
    setup();
    outdoors_at(dg.town_exit_x[13], (unsigned)dg.town_exit_y[13] + 1);
    enter_outdoors();
    strong_party();
    dg.party[1].hp = 100;
    dg.rest_cooldown = 0;
    unsigned start = dg.hour;
    bot_keys = 0;
    bot_shot = 1;
    pos = 0;
    platform_script_keys(keys, 4);
    platform_script_next_key = NULL;
    /* the first four keys answer the prompts, the rest is the combat player */
    extern int camp_key(void);
    (void)pos;
    platform_script_next_key = camp_key;
    select_text_window(2);
    world_tick();   /* puts the party's object (objects[0]) in step with the transport */
    dispatch_command('H');
    platform_script_next_key = NULL;
    transcript[tlen] = 0;
    print_transcript("camp");
    check(has("Hole up & camp!") && has("Wilt thou set a watch? Yes") && has("Who will stand guard?"), "the camp's questions");
    check(has("Zzzzzz..."), "the party sleeps");
    check(has("Party rested!") || has("Ambushed!"), "rested or ambushed");
    check(!has("Party rested!") || (dg.party[1].hp > 100 && dg.rest_cooldown == 14 && dg.hour == (start + 8) % 24), "a full rest heals and takes the hours");
    check(!has("Ambushed!") || has("*** CONFLICT ***"), "an ambush is a fight");
    check(dg.location == 0 && dg.party[0].status != 'S' && dg.party[2].status != 'S', "awake on the map again");
    check(!platform_quit_requested(), "the player did not run out of keys");
    shot("camp");
}

int camp_key(void)
{
    static const int answers[] = {'8', 'Y', '1', RET};
    if (dg.location != 0xFF || dg.combat_turn == 0xFF) {
        if (bot_keys < 4) return answers[bot_keys++];
        return ++bot_keys > 6000 ? -1 : ' ';   /* the apparition's pauses */
    }
    return combat_bot();
}

/* A made-up Ultima IV PARTY.SAV: "Mariah", a female mage with 950 experience, strength 16,
 * dexterity 33, intelligence 50, all eight virtues at 0 (an Avatar). */
static void write_party_sav(int avatar, int bad)
{
    static uint8_t sav[0x1F6];
    char path[700];
    memset(sav, 0, sizeof sav);
    uint8_t *c = sav + 8;
    c[0] = 300 & 0xFF; c[1] = 300 >> 8;     /* hp */
    c[2] = 400 & 0xFF; c[3] = 400 >> 8;     /* max hp */
    c[4] = 950 & 0xFF; c[5] = 950 >> 8;     /* experience */
    c[6] = bad ? 99 : 16;
    c[8] = 33;
    c[10] = 50;
    c[12] = 40;
    memcpy(c + 0x14, "Mariah", 6);
    c[0x24] = 0x0C;
    c[0x25] = 0;
    for (int i = 0; i < 8; i++) sav[0x146 + i * 2] = avatar ? 0 : (uint8_t)(50 + i);
    snprintf(path, sizeof path, "%s/PARTY.SAV", save_dir);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(sav, 1, sizeof sav, f);
        fclose(f);
    }
}

static int transfer_shot_key(void)
{
    static int n;
    static const int keys[] = {' ', 'N', 'Z', 'o', 'e', RET, 'N', ' ', ' ', ' ', ' ', ' ', ' '};
    if (dg.location == 0xFE) {   /* reset */
        dg.location = 0x41;
        n = 0;
    }
    if (n == 9) shot("transfer");
    return n < (int)(sizeof keys / sizeof keys[0]) ? keys[n++] : -1;
}

static void test_transfer(void)
{
    char path[700];
    snprintf(path, sizeof path, "%s/PARTY.SAV", save_dir);
    check(convert_ultima4_stat(9) == 9 && convert_ultima4_stat(16) == 13 && convert_ultima4_stat(29) == 20 &&
          convert_ultima4_stat(33) == 20 && convert_ultima4_stat(50) == 25 && convert_ultima4_stat(70) == 30, "attributes 0-70 are scaled to 0-30");
    /* no file: the prompt, Esc */
    setup();
    remove(path);
    static const int esc[] = {ESC, 0};
    cast(esc, 1);
    select_text_window(0);
    transfer_from_ultima4();
    end_cast("transfer: no disk");
    check(has("Please insert the Ultima IV Player Disk") && has("<Esc> to abort"), "without PARTY.SAV the disk is asked for; Esc gives up");
    /* bad data */
    write_party_sav(1, 1);
    static const int any[] = {' ', 0};
    cast(any, 1);
    select_text_window(0);
    transfer_from_ultima4();
    end_cast("transfer: bad data");
    check(has("Error: Your Ultima IV game contains bad data."), "an attribute above 70 is bad data");
    /* the real thing: rename to Zoe, become male */
    write_party_sav(1, 0);
    dg.location = 0xFE;
    platform_script_keys(NULL, 0);
    platform_script_next_key = transfer_shot_key;
    tlen = 0;
    video_clear();
    select_text_window(0);
    transfer_from_ultima4();
    platform_script_next_key = NULL;
    transcript[tlen] = 0;
    print_transcript("transfer");
    check(has("Found:") && has("Mariah") && has("a level 4 Female") && has("is an Avatar."), "the Ultima IV character is shown");
    check(has("Keep this name?") && has("Enter new name: Zoe") && has("Keep same sex?"), "name and sex can be changed");
    check(has("Thou art now an Avatar:") && has("Experience has been converted") && has("Level has been converted"), "the steps of the conversion");
    check(has("Strength: was 16(50), now 20(30)") && has("Dexterity: was 33(50), now 20(30)") && has("Intellect: was 50(50), now 25(30)"), "the attributes, strength raised to the minimum of 20");
    static uint8_t gam[SAVE_SIZE];
    check(has("Conversion complete, saving...") && load_file_part(0, SAVE_SIZE, gam, "SAVED.GAM") == 0, "SAVED.GAM is written");
    const PartyMember *m = (const PartyMember *)(gam + 2);
    check(!strcmp(m->name, "Zoe") && m->sex == 0x0B && m->cls == 'A' && m->status == 'G', "Zoe, male, Avatar");
    check(m->xp == 95 && m->level == 1 && m->hp == 30 && m->max_hp == 30, "experience 950 -> 95, level 1, 30 hit points");
    check(m->str == 20 && m->dex == 20 && m->intel == 25 && m->mp == 25, "strength 20, dexterity 20, intelligence and magic 25");
    check(!strcmp(((const PartyMember *)(gam + 2 + 32))->name, "Shamino"), "the rest of the new game is INIT.GAM's");
    /* not an Avatar: the class stays */
    write_party_sav(0, 0);
    static const int keep[] = {' ', 'Y', 'Y', ' ', ' ', ' ', ' ', ' ', ' ', 0};
    cast(keep, 9);
    select_text_window(0);
    transfer_from_ultima4();
    end_cast("transfer: not an Avatar");
    check(has("is not an Avatar") && has("Class remains intact") && dg.party[0].cls == 'M' && dg.party[0].sex == 0x0C && !strcmp(dg.party[0].name, "Mariah"),
          "a character who is no Avatar keeps the class");
    remove(path);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    save_dir = argc > 2 ? argv[2] : ".";
    files_set_game_dir(dir);
    files_set_save_dir(save_dir);
    snprintf(save, sizeof save, "%s/SAVED.GAM", dir);
    reset_text_windows();
    load_font(0, "ibm.ch");
    load_font(1, "runes.ch");
    select_font(0);
    set_ega_colors();
    dg.video_type = 1;
    reset_tile_anim_map();
    load_tileset("tiles.16");
    alloc_back_page();
    text_echo = echo;

    test_town();
    test_outdoors();
    test_ship();
    test_chest();
    test_mix();
    test_moongates();
    test_combat_rules();
    test_combat();
    test_camp();
    test_magic();
    test_use();
    test_ztats_ready();
    test_look();
    test_shrines();
    test_dungeon();
    test_attack();
    test_transfer();
    printf("%d failure(s)\n", fails);
    return fails != 0;
}
