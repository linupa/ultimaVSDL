/* test_game.cpp - the main loop and the commands with scripted keys, from the game's SAVED.GAM
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
#include "gamedata.h"
#include "gametime.h"
#include "intro.h"
#include "map.h"
#include "look.h"
#include "npc.h"
#include "party.h"
#include "platform_script.h"
#include "savegame.h"
#include "shrine.h"
#include "magic.h"
#include "gfx.h"
#include "objects.h"
#include "outdoor.h"
#include "picture.h"
#include "platform.h"
#include "sjog.h"
#include "spells.h"
#include "stats.h"
#include "strings.h"
#include "text.h"
#include "tiles.h"
#include "town.h"
#include "video.h"
#include "view.h"
#include "world.h"


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
    savegame.load_path(save);
    {   /* the saved game, taken apart into the classes, goes back together unchanged */
        static uint8_t file[SaveGame::SIZE], image[SaveGame::SIZE];
        FILE *f = fopen(save, "rb");
        size_t n = f ? fread(file, 1, sizeof file, f) : 0;
        if (f) fclose(f);
        savegame.pack(image);
        static int checked;
        if (!checked++) check(n == sizeof file && !memcmp(file, image, sizeof file), "the saved game packs back byte for byte");
        if (checked == 1) {
            /* Every byte of the file, whatever it holds: a made-up image goes in and comes out
             * unchanged; and with SaveGame's copy of the rest wiped afterwards, the bytes the
             * classes hold still come out, so they come from the classes. */
            static uint8_t pattern[SaveGame::SIZE], held[SaveGame::SIZE];
            for (unsigned i = 0; i < sizeof pattern; i++) pattern[i] = (uint8_t)(i * 7 + (i >> 8) * 13 + 1);
            savegame.unpack(pattern);
            savegame.pack(image);
            check(!memcmp(pattern, image, sizeof pattern), "a made-up saved game goes in and comes out unchanged");
            unsigned covered = 0;
            for (int p = 0; p < savegame.count(); p++) {
                unsigned off, size;
                const char *name;
                savegame.piece(p, &off, &size, &name);
                printf("  piece ds:%04X %4u %s\n", off, size, name);
                memset(held + (off - SaveGame::DS_OFFSET), 1, size);
                covered += size;
            }
            savegame.clear_rest();
            savegame.pack(image);
            int ok = 1;
            for (unsigned i = 0; i < sizeof pattern; i++)
                if (image[i] != (held[i] ? pattern[i] : 0)) ok = 0;
            check(ok, "the pieces held by classes are saved from the classes, the rest as loaded");
            printf("  %d pieces hold %u of the %u bytes\n", savegame.count(), covered, (unsigned)SaveGame::SIZE);
            savegame.load_path(save);   /* back to the real saved game */
        }
    }
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
    view.world_tick_enabled = 1;
    game_clear_exit();
    tlen = 0;
}

/* Runs main's loop on the keys; the last key is never read if the game exits by itself. */
static void play(const int *keys, int n)
{
    script_platform.keys(keys, n);
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
    map.set(Map::BRITANNIA);
    map.z = 0;
    map.x = (uint8_t)x;
    map.y = (uint8_t)y;
    memset(&map.objects[1], 0, sizeof map.objects.items - sizeof map.objects[0]);
    save_file_part(0x100, map.objects.bytes(), "BRIT.OOL");
}

/* A simple player for combat, as a key source: Esc once the battle is won; otherwise attack when
 * an enemy is next to the fighter, else walk towards the nearest; while aiming, move the cursor
 * onto the enemy and fire. */
static int bot_keys, bot_shot;

static int nearest_enemy(const Combatant *me, int *ex, int *ey)
{
    int best = 999;
    for (int i = 0; i < 32; i++) {
        const Combatant *c = &combat.combatants[i];
        if (!(c->flags & 0x40) || (c->flags & 0x20) || combat.combatant_side(i) == 0) continue;
        int d = distance(c->y, c->x, me->y, me->x);
        if (d < best) { best = d; *ex = c->x; *ey = c->y; }
    }
    return best;
}

static int combat_bot(void)
{
    if (++bot_keys > 6000 || map != Map::SCENE) return -1;
    const Combatant *me = &combat.combatants[combat.turn & 31];
    int ex = 0, ey = 0, d = nearest_enemy(me, &ex, &ey);
    if (!bot_shot && d < 999) {
        bot_shot = 1;
        world_tick();
        shot("combat");
    }
    if (view.aim_mode) {
        if (d == 999) return ESC;
        if (combat.aim_x == ex && combat.aim_y == ey) return RET;
        if (combat.aim_x != ex) return combat.aim_x < ex ? E : W;
        return combat.aim_y < ey ? S_ : N;
    }
    if (combat.victory || d == 999) return ESC;
    if (d <= 1) return 'A';
    static int last_turn = -1, last_x, last_y;
    if (last_turn == combat.turn && last_x == me->x && last_y == me->y) {   /* the last step was blocked */
        last_turn = -1;
        return ' ';
    }
    last_turn = combat.turn;
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
    for (unsigned i = 0; i < party.size(); i++) party[i].hp = party[i].max_hp = 400;
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
    strcpy(second, party[1].name);
    strcpy(third, party[2].name);
    uint8_t torches = party.torches, hour = game_time.hour;
    check(map == Map::IOLOS_HUT && map.x == 15 && map.y == 15, "the saved game starts in Iolo's hut");
    play(keys, (int)(sizeof keys / sizeof keys[0]));
    print_transcript("town");
    check(has("Z-stats...") && has("Pass") && has("Ignite torch!") && has("D-What?"), "dispatcher prompts");
    check(party.torches == torches - 1, "Ignite used a torch");
    check(has("Set Active Plr: Shamino") && has("Set Active Plr: None!"), "set active player");
    check(has("Get-East Borrowed!"), "Get borrows the wall torch");
    check(has("Search-West") && has("In the barrel thou dost find nothing of note."), "Search the barrel");
    check(has("Push-East Pushed!"), "Push the chair");
    check(has("Hole up- For how many hours? 1") && has("Zzzzzzz..."), "Hole up in bed");
    check(has("New Order") && !strcmp(party[1].name, third) && !strcmp(party[2].name, second), "New Order swaps two members");
    check(has("Open-South Opened!"), "Open the door");
    check(has("Exit to Britannia!"), "leaving the town");
    check(has("Enter hut"), "Enter the hut again from Britannia");
    check(map == Map::IOLOS_HUT && map.x == 15 && map.y == 30, "back in the hut, at its entrance");
    check(game_time.hour >= hour + 1, "at least the hour in bed has passed");
    check(has("Save game? Yes Saving... Done."), "Quit and save");
    check(has("Exit to DOS?") && !platform->quit_requested(), "Ctrl-E Y ends the game (the script was not used up)");
    static uint8_t gam[SaveGame::SIZE], ool[0x200];
    check(load_file_part(0, SaveGame::SIZE, gam, "SAVED.GAM") == 0 && gam[0x5893 - SaveGame::DS_OFFSET] == 13 &&
          gam[0x5896 - SaveGame::DS_OFFSET] == 15 && gam[0x5897 - SaveGame::DS_OFFSET] == 30, "SAVED.GAM was written with the new position");
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
    outdoors_at(map.exit_x(13), (unsigned)map.exit_y(13) + 1);
    unsigned start = game_time.hour * 60u + game_time.minute;
    play(keys, (int)(sizeof keys / sizeof keys[0]));
    print_transcript("outdoors");
    unsigned now = game_time.hour * 60u + game_time.minute;
    check(has("Talk-North Funny, no response!"), "Talk outdoors");
    check(has("Klimb-With what?"), "Klimb without a grapple");
    check(has("Board What?"), "Board nothing");
    check(has("X-it what?"), "X-it on foot");
    check(has("Attack-North Nothing to attack!"), "Attack nothing");
    check(has("Yell what? :FALLAX") && has("A word of power is uttered") && has("No effect!"), "Yell a word of power away from a dungeon");
    check(has("Hole up & camp!") && has("For how many hours? (1-9)"), "Hole up asks for the hours");
    check(has("View a gem! You have none!") && has("W-What?") && has("Invalid!"), "View, W and a bad player number");
    check(map.is_outdoors() && now - start >= 20 && now - start < 120, "each turn outdoors takes at least 2 minutes");
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
            map.enter_outdoors();
            int water = 1;
            for (int dy = -6; dy <= 6; dy++)
                for (int dx = -6; dx <= 6; dx++)
                    if (*map.get_map_tile_ptr(y + (unsigned)dy, x + (unsigned)dx) != 1) water = 0;
            if (water) { sx = x; sy = y; }
        }
    check(sx != 0, "found open sea");
    setup();
    outdoors_at(sx, sy);
    party.transport = 0x24;
    map.objects[0].tile = map.objects[0].tile2 = 0x24;
    map.objects[0].hull = 5;
    map.objects[0].skiffs = 1;
    map.wind_direction = 1;
    game_time.hour = 12;   /* daylight, fewer monsters */
    play(keys, 5);
    check(has("Hole up & repair...") && has("Hull now") && map.objects[0].hull >= 10, "Hole up repairs the hull");
    check(has("Fire-East Fire-North"), "a broadside");
    check(has("Fire-North Fire broadsides only!"), "no firing ahead");
    script_platform.keys(keys + 5, (int)(sizeof keys / sizeof keys[0]) - 5);
    game_run();
    transcript[tlen] = 0;
    print_transcript("ship");
    check(has("Yell HOIST!") && has("X-it Under sail!"), "Yell hoists the sails; no leaving under sail");
    check(has("Head East"), "the first key turns the ship");
    check(map.x >= sx + 3 && map.y == sy, "under sail the ship runs on without keys");
    check(has("Yell FURL!"), "Yell furls the sails");
    check(has("X-it ship!") && has("Board Ship"), "into the skiff and aboard again");
    check((party.transport & 0xFC) == 0x24 && map.objects[0].skiffs == 1, "the skiff is back on board");
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
    party.active_player = 0;
    set_object_entry(5, 20, 0, 14, 15, 1, 1);
    uint8_t keys_before = party.keys, karma = party.karma;
    uint16_t gold = party.gold;
    map.set(Map::IOLOS_HUT);
    play(keys, (int)(sizeof keys / sizeof keys[0]));
    print_transcript("chest");
    check(has("Search-North Thou dost find") && (has("no trap!") || has("a trap!")), "Search a chest for traps");
    check(has("Jimmy-North Key broke!") && (party.keys == keys_before - 1 || has(" key")), "Jimmy on an untrapped chest breaks the key");
    check(has("Open-North") && (has("Found:") || has("Chest empty!")), "Open the chest");
    check(party.karma == karma - 2, "opening a chest in a town costs 2 karma");
    check(!has("Found:") || has("gold!") || has("key") || has("gem") || has("torch") || has("potion!") || has("A scroll:") || has("food!"), "Get picks up what was found");
    check(has("Nothing to get!"), "until nothing is left");
    check(!has("a sack of gold!") || party.gold > gold, "the gold was added");
    check(has("Open-West Nothing to open!"), "Open with nothing there");
    int left = 0;
    for (int i = 1; i < 32; i++) left += map.objects[i].tile != 0 && map.objects[i].tile < 0x10 && map.objects[i].tile != 1;
    check(left == 0, "no loose items remain");
}

static void test_mix(void)
{
    int keys[64], n = 0;
    setup();
    for (int i = 0; i < 8; i++) party.reagents[i] = 10;
    uint8_t recipe = spells.recipe(0), before = party.mixed_spells[0];
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
    for (int i = 0; i < 8; i++) used &= party.reagents[i] == ((recipe & (0x80 >> i)) ? 7 : 10);
    check(has("Mix Reagents") && has("For what spell? :IN XEN") && has("LOR"), "the spell is typed by its letters");
    check(has("Mixing...") && has("Done!"), "mixing");
    check(used, "3 of each reagent in the recipe were used");
    check(party.mixed_spells[0] == before + 3, "3 In Lor spells mixed");
    check(has("None!"), "no spell named: nothing happens");
    shot("mix");
}

static void test_moongates(void)
{
    setup();
    int stone = -1;
    for (int i = 0; i < 8; i++)
        if (map.moonstone_loc[i] == 0 && map.moonstone_z[i] == 0) { stone = i; break; }
    check(stone >= 0, "a moonstone is buried in Britannia");
    if (stone < 0) return;
    unsigned x = map.moonstone_x[stone], y = map.moonstone_y[stone];
    outdoors_at(x, y + 1);
    game_time.hour = 22;
    game_time.moongate_phase = 0;
    map.enter_outdoors();
    update_moongates();
    check(*map.get_map_tile_ptr(y, x) == 0xDC && game_time.moongate_phase == 1, "at night a moongate rises over the stone");
    world_tick();
    shot("moongate");
    game_time.hour = 12;
    for (int i = 0; i < 3; i++) update_moongates();
    check(*map.get_map_tile_ptr(y, x) == 5 && game_time.moongate_phase == 0, "by day it sinks back into the grass");
    int other = -1;
    for (int i = 0; i < 8; i++)
        if (i != stone && map.moonstone_loc[i] == 0) { other = i; break; }
    if (other >= 0) {
        check(moongate_travel(other) == 1 && map.x == map.moonstone_x[other] && map.y == map.moonstone_y[other] &&
              moonstone_here(other), "moongate travel arrives at the other stone");
    }
    /* the sky bar: sun and moons */
    tlen = 0;
    game_time.hour = 12;
    game_time.day = 1;
    draw_sky_bar();
    transcript[tlen] = 0;
    check(tlen == 12 && transcript[5] == '*', "the sky bar shows the sun past its height at noon");
    check(game_time.moon1_phase == game_time.moon_phase(1, 0) && game_time.moon2_phase == game_time.moon_phase(1, 1) &&
          game_time.moon1_phase == '0' && game_time.moon2_phase == '0', "moon phases come from the day (both new on day 1)");
}

static int attack_key(void)
{
    if (map != Map::SCENE) return bot_keys++ == 0 ? N : -1;
    return combat_bot();
}

/* Moonglow (location 1): attack a townsperson. */
static void test_attack(void)
{
    setup();
    map.set(Map::MOONGLOW);
    map.z = 0;
    map.x = 15;
    map.y = 30;
    game_time.hour = 12;
    town.enter_town(1);
    int npc = -1;
    for (int i = 1; i < 32; i++)
        if (npcs.type[i] >= 0x40 && npcs.type[i] < 0x80 && npcs.state[i].z == 0 && map.objects[npcs.state[i].object].tile) { npc = i; break; }
    check(npc > 0, "found a townsperson");
    if (npc < 0) return;
    MapObject *o = &map.objects[npcs.state[npc].object];
    uint8_t karma = party.karma;
    /* stand south of them, wherever that is */
    map.x = o->x;
    map.y = (uint8_t)(o->y + 1);
    strong_party();
    bot_keys = 0;
    bot_shot = 1;
    script_platform.keys(NULL, 0);
    script_platform.next_key = attack_key;
    select_text_window(2);
    tlen = 0;
    int r = dispatch_command('A');
    script_platform.next_key = NULL;
    transcript[tlen] = 0;
    print_transcript("attack");
    check(r == 1 && has("Attack-North"), "Attack in a town");
    check(party.karma == karma - 5, "attacking a townsperson costs 5 karma");
    check(has("*** CONFLICT ***") && (has("VICTORY!") || has("BATTLE IS LOST!")), "it is a fight, in the town's combat map");
    check((npcs.dead_bits[map.id()] >> npc & 1) && npcs.type[npc] == 0, "the NPC is gone for good");
    check(map == Map::MOONGLOW, "back in the town");
}

/* Giant rats north of the party on the grass outside Iolo's hut. */
static void test_combat(void)
{
    setup();
    outdoors_at(map.exit_x(13), (unsigned)map.exit_y(13) + 1);
    map.enter_outdoors();
    strong_party();
    uint16_t xp = (uint16_t)(party[0].xp + party[1].xp + party[2].xp);
    uint8_t px = map.x, py = map.y;
    set_object_entry(7, 0, 0, (uint8_t)(py - 1), px, 0x90, 0x90);
    static uint8_t before[0x100];
    memcpy(before, map.objects.bytes(), 0x100);
    bot_keys = bot_shot = 0;
    script_platform.keys(NULL, 0);
    script_platform.next_key = combat_bot;
    combat.combat_with_object(7);
    script_platform.next_key = NULL;
    transcript[tlen] = 0;
    print_transcript("combat");
    printf("(%d keys)\n", bot_keys);
    check(has("GIANT RATS") && has("*** CONFLICT ***"), "the fight starts");
    check(has("armed with"), "a member's turn names the weapons");
    check(has("Attack-") && has("Aim!"), "Attack asks to aim");
    check(has("VICTORY!") || has("BATTLE IS LOST!"), "the fight ends");
    check(!has("VICTORY!") || has("killed!"), "a won fight has kills");
    check(!has("VICTORY!") || (uint16_t)(party[0].xp + party[1].xp + party[2].xp) > xp, "experience was earned");
    check(map.is_outdoors() && map.x == px && map.y == py, "back on the map where the party stood");
    check(map.objects[7].tile == 0, "the monster is gone from the map");
    before[7 * 8] = before[7 * 8 + 1] = before[7 * 8 + 2] = before[7 * 8 + 3] = before[7 * 8 + 4] = 0;
    check(!memcmp(before + 8, map.objects.bytes() + 8, 0xF8), "the other map objects are as before");
    check(!platform->quit_requested(), "the player did not run out of keys");
}

/* The rules, with fixed pieces: a glass sword always hits for 99 and then shatters. */
static void test_combat_rules(void)
{
    setup();
    outdoors_at(map.exit_x(13), (unsigned)map.exit_y(13) + 1);
    map.enter_outdoors();
    combat.prev_location = 0;
    map.set(Map::SCENE);
    combat.load_combat_map(2);
    combat.place_monster_group(0x20, 1);   /* orcs */
    int orcs = 0, orc = -1;
    for (int i = 6; i < 32; i++)
        if (combat.combatants[i].flags & 0x40) { orcs++; orc = i; }
    Combat::Sides sides = combat.count_combat_sides();
    check(orcs >= 1 && orcs <= 10 && (int)sides.others == orcs && sides.party == 3, "a group of 1-10 orcs against the three members");
    check(combat.combatants[0].flags == 0x80 && combat.combatants[0].speed == party[0].dex &&
          map.objects[combat.combatants[0].object].tile == 0x4C, "the Avatar is combatant 0");
    check(combat.combatants[orc].hp == 10 && map.objects[combat.combatants[orc].object].tile == 0xC0, "an orc has 10 hit points and tile 0xC0");
    check(combat.combatant_at(combat.combatants[orc].y, combat.combatants[orc].x) == orc, "combatant_at finds it");
    check(!combat.combat_tile_free(combat.combatants[orc].y, combat.combatants[orc].x, 0x4C), "its square is not free");
    check(distance(0, 0, 3, 4) == 5 && distance(2, 2, 3, 3) == 1, "distance is the rounded-down square root");
    party[0].left_hand = 0x27;
    combat.attack_item = 0x27;
    combat.turn = 0;
    uint16_t xp = party[0].xp;
    tlen = 0;
    combat.melee_attack(0x27, (unsigned)orc, 0);
    transcript[tlen] = 0;
    check(has("Thy sword hath shattered!") && has("Orc killed!"), "the glass sword kills and shatters");
    check(party[0].left_hand == 0xFF && party[0].xp == xp + 3, "it is gone from the hand; 10 / 4 + 1 experience");
    uint8_t left = map.objects[combat.combatants[orc].object].tile;
    check((combat.combatants[orc].flags & 0x40) == 0 && (left == 0x1F || left == 1), "the orc leaves a body or a chest");
    /* a sleeping member and the hazards */
    combat.sleep_combatant(1);
    check(party[1].status == 'S' && (combat.combatants[1].flags & 8), "sleep");
    combat.wake_combatant(1);
    check(party[1].status == 'G' && !(combat.combatants[1].flags & 8), "wake");
    party[2].hp = 50;
    view.local_map[combat.combatants[2].y * 0x20 + combat.combatants[2].x] = 0x8F;
    for (int i = 0; i < 20 && party[2].hp == 50; i++) combat.apply_tile_hazard(2);
    check(party[2].hp < 50 && party[2].hp >= 40, "lava burns for up to 10");
    combat.apply_damage(99, 2);
    check(party[2].status == 'D' && (combat.combatants[2].flags & 0x20) && map.objects[combat.combatants[2].object].tile == 0x1E, "99 damage kills a member");
    check(combat.count_combat_sides().party == 2, "two members left on the party's side");
    map.set(Map::BRITANNIA);
}

static int magic_key(void);
static const int *magic_keys;
static int magic_n, magic_pos;

/* Keys from a list; when it runs out (or while aiming) the combat player takes over. */
static int magic_key(void)
{
    if (!view.aim_mode && magic_pos < magic_n) return magic_keys[magic_pos++];
    if (map == Map::SCENE && view.aim_mode) return combat_bot();
    return -1;
}

static void cast(const int *keys, int n)
{
    magic_keys = keys;
    magic_n = n;
    magic_pos = 0;
    bot_keys = 0;
    bot_shot = 1;
    script_platform.keys(NULL, 0);
    script_platform.next_key = magic_key;
    select_text_window(2);
    tlen = 0;
}

static void end_cast(const char *title)
{
    script_platform.next_key = NULL;
    transcript[tlen] = 0;
    print_transcript(title);
}

static void test_magic(void)
{
    /* in the hut */
    setup();
    town.enter_town(0);
    party.active_player = 0;
    for (int i = 0; i < 48; i++) party.mixed_spells[i] = 0;
    party.mixed_spells[0] = 3;     /* In Lor */
    party.mixed_spells[4] = 2;     /* Mani */
    party.mixed_spells[13] = 1;    /* Vas Flam */
    party.mixed_spells[42] = 1;    /* In Mani Corp: 8th circle */
    party[0].mp = 20;
    party[0].level = 3;
    party.light_spell = 0;
    static const int lor[] = {'L', 'I', RET};
    cast(lor, 3);
    int r = dispatch_command('C');
    (void)r;
    end_cast("In Lor");
    check(has("Cast...") && has("Spell name: :LOR IN") && party.light_spell == 100 && party.mixed_spells[0] == 2 && party[0].mp == 19,
          "In Lor: light for 100 turns, one spell and one magic point used");
    party[1].hp = 3;
    static const int mani[] = {'M', RET, '2', RET};
    cast(mani, 4);
    cmd_cast();
    end_cast("Mani");
    check(has("On who: ") && has("Success!") && party[1].hp > 3 && party[1].hp <= 33 && party.mixed_spells[4] == 1, "Mani heals 1-30");
    static const int flam[] = {'V', 'F', RET};
    cast(flam, 3);
    cmd_cast();
    end_cast("Vas Flam in town");
    check(has("VAS FLAM") && has("Not here!") && party.mixed_spells[13] == 1, "a combat spell can't be cast in town; it isn't used up");
    static const int grav[] = {'A', 'N', RET};   /* An Nox */
    cast(grav, 3);
    cmd_cast();
    end_cast("none mixed");
    check(has("None mixed!"), "a spell that isn't mixed");
    static const int corp[] = {'I', 'M', 'C', RET};
    uint8_t mp = party[0].mp;
    cast(corp, 4);
    cmd_cast();
    end_cast("too high");
    check(has("Failed!") && party.mixed_spells[42] == 0 && party[0].mp == mp - 8, "above the caster's level: the spell and the points are lost");
    static const int bad[] = {'Z', 'Z', RET};
    cast(bad, 3);
    cmd_cast();
    end_cast("no such spell");
    check(has("No effect!"), "letters that are no spell");

    /* in a fight with orcs */
    setup();
    outdoors_at(map.exit_x(13), (unsigned)map.exit_y(13) + 1);
    map.enter_outdoors();
    strong_party();
    combat.prev_location = 0;
    map.set(Map::SCENE);
    combat.load_combat_map(2);
    combat.place_monster_group(0x20, 1);
    world_tick();
    combat.turn = 0;
    party.active_player = 0xFF;
    party[0].mp = 30;
    party[0].level = 8;
    for (int i = 0; i < 48; i++) party.mixed_spells[i] = 2;
    int orcs = 0;
    for (int i = 6; i < 32; i++) orcs += (combat.combatants[i].flags & 0x40) != 0;
    cast(flam, 3);
    cmd_cast();
    end_cast("Vas Flam");
    check(has("Attack-") == 0 && has("Aim!") && (has("Orc") || has("Failed!")), "Vas Flam is aimed like a missile");
    check(party.mixed_spells[13] == 1 && party[0].mp == 27, "third circle: 3 magic points");
    static const int zu[] = {'I', 'Z', RET, N};
    cast(zu, 4);
    cmd_cast();
    end_cast("In Zu");
    int asleep = 0;
    for (int i = 6; i < 32; i++) asleep += (combat.combatants[i].flags & 0x48) == 0x48;
    printf("(%d asleep)\n", asleep);
    check(asleep == 0 || has("slept!"), "In Zu puts monsters to sleep");
    static const int hur[] = {'F', 'H', 'I', RET, N};
    cast(hur, 5);
    cmd_cast();
    end_cast("In Flam Hur");
    shot("firecloud");
    int left = 0;
    for (int i = 6; i < 32; i++) left += (combat.combatants[i].flags & 0x40) != 0;
    check(has("Direction-North") && party[0].mp == 14, "In Flam Hur: a direction, 8 points");
    printf("(orcs %d -> %d)\n", orcs, left);
    static const int lorw[] = {'L', 'S', RET};
    cast(lorw, 3);
    cmd_cast();
    end_cast("Sanct Lor");
    check((combat.combatants[0].flags & 0x10) && map.objects[combat.combatants[0].object].tile2 == 0x1D && has("Success!"), "Sanct Lor: the caster is invisible");
    static const int tym[] = {'A', 'T', RET};
    party[0].mp = 30;
    cast(tym, 3);
    cmd_cast();
    end_cast("An Tym");
    check(party.active_spell == 'T' && party.active_spell_turns == 10, "An Tym stops time for 10 turns");
    static const int kal[] = {'K', 'X', RET};
    cast(kal, 3);
    cmd_cast();
    end_cast("Kal Xen");
    int allies = 0;
    for (int i = 6; i < 32; i++) allies += (combat.combatants[i].flags & 0x41) == 0x41;
    check(!has("Success!") || allies == 1, "Kal Xen summons a creature on the party's side");
    map.set(Map::BRITANNIA);
    party.active_spell = 0;
}

static void test_use(void)
{
    setup();
    outdoors_at(map.exit_x(13), (unsigned)map.exit_y(13) + 1);
    map.enter_outdoors();
    world_tick();
    memset(party.scrolls, 0, 8);
    memset(party.potions, 0, 8);
    memset(map.moonstone_loc, 0, 8);
    party.magic_carpets = party.odd_keys = party.lb_amulet = party.lb_crown = party.lb_sceptre = 0;
    party.shards[0] = party.shards[1] = party.shards[2] = 0;
    party.spyglass = party.sextant = party.black_badge = party.hms_cape_plans = party.sandalwood_box = 0;
    party.pocket_watch = 0;
    static const int none[] = {0};
    cast(none, 0);
    cmd_use();
    end_cast("nothing to use");
    check(has("No usable items!"), "Use with nothing");
    party.potions[1] = 2;            /* yellow */
    party.scrolls[2] = 1;            /* protection */
    map.moonstone_loc[3] = 0xFF;   /* carried */
    party.pocket_watch = 0xFF;      /* pocket watch */
    party[0].hp = 10;
    static const int scroll[] = {RET};
    cast(scroll, 1);
    cmd_use();
    end_cast("scroll");
    shot("use");
    check(has("Item: ") && has("Scroll") && has("Protection!") && party.active_spell == 'P' && party.active_spell_turns == 100 && party.scrolls[2] == 0,
          "the first item, a Protection scroll");
    static const int potion[] = {RET, '1', RET};
    cast(potion, 3);
    cmd_use();
    end_cast("potion");
    check(has("Potion") && party.potions[1] == 1, "a potion is drunk");
    check(!has("Healed!") || party[0].hp > 10, "the yellow potion heals (unless it turned out to be another)");
    static const int stone[] = {S_, RET};
    cast(stone, 2);
    cmd_use();
    end_cast("moonstone");
    check(has("Moonstone buried!") && map.moonstone_loc[3] == 0 && map.moonstone_x[3] == map.x && map.moonstone_y[3] == map.y,
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
    town.enter_town(0);
    party.active_player = 0xFF;
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
    uint8_t sword = party[0].left_hand, had = party.inventory[sword];
    check(sword != 0xFF && is_equipped_by(sword, 0), "the Avatar holds a weapon");
    int row = 0;   /* the sword's row in the list */
    for (int i = list_next_item(0, party.inventory, 0x30, -1); i != -1 && i != sword; i = list_next_item(0, party.inventory, 0x30, i)) row++;
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
    check(has("Item: ") && party[0].left_hand == 0xFF && party.inventory[sword] == had + 1 && has("Done"), "picking a held weapon puts it away");
    cast(r, n);
    cmd_ready();
    end_cast("ready");
    check((party[0].left_hand == sword || party[0].right_hand == sword) && party.inventory[sword] == had, "picking it again readies it");

    /* the rules, directly */
    tlen = 0;
    party[0].left_hand = party[0].right_hand = 0xFF;
    party.inventory[0x21] = 1;   /* a two-handed sword */
    party[0].str = 30;
    check(free_hands(0) == 2, "both hands free");
    ready_item(0x21, 0);
    check(party[0].left_hand == 0x21 && party.inventory[0x21] == 0 && free_hands(0) == 0xFF, "a two-handed sword takes both hands");
    party.inventory[0x10] = 1;   /* a dagger */
    ready_item(0x10, 0);
    transcript[tlen] = 0;
    check(has("Thou must free one of thy hands first!") && party.inventory[0x10] == 1, "no hand left for a dagger");
    ready_item(0x21, 0);
    party[0].str = 1;
    tlen = 0;
    ready_item(0x21, 0);
    transcript[tlen] = 0;
    check(has("Thou art not strong enough!") && party[0].left_hand == 0xFF, "too heavy for strength 1");
    party.inventory[0x1A] = 1;   /* a bow, no arrows */
    party.inventory[0x1B] = 0;
    party[0].str = 30;
    tlen = 0;
    ready_item(0x1A, 0);
    transcript[tlen] = 0;
    check(has("Thou hast no ammunition for that weapon!"), "a bow needs arrows");
}

static void test_look(void)
{
    setup();
    town.enter_town(0);
    party.active_player = 0;
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
    *map.get_map_tile_ptr(14, 15) = 0xFA;
    game_time.hour = 15;
    game_time.minute = 7;
    cast(n, 1);
    cmd_look();
    end_cast("look at a clock");
    check(has("3:07 PM."), "a clock tells the time");
    /* a sign with no entry: the default */
    *map.get_map_tile_ptr(14, 15) = 0xA0;
    cast(n, 1);
    cmd_look();
    end_cast("look at a sign");
    check(has("LIVE@BY@[E") && has("EIGHT@LAWS"), "a sign without a text reads LIVE BY THE EIGHT LAWS (in runes)");
    /* a real sign: the first entry of Moonglow's */
    setup();
    map.set(Map::MOONGLOW);
    map.x = 15;
    map.y = 30;
    town.enter_town(1);
    static uint8_t idx[0x42], ent[16];
    load_file_part(0, 0x42, idx, "signs.dat");
    load_file_part((uint32_t)(idx[2] | idx[3] << 8), 16, ent, "signs.dat");
    map.z = ent[1];
    tlen = 0;
    select_text_window(2);
    read_sign(ent[3], ent[2], ent[1]);
    transcript[tlen] = 0;
    print_transcript("a sign in Moonglow");
    check(tlen > 40 && !has("LIVE@BY"), "a sign's own text");
    /* the gem map and the night sky */
    setup();
    town.enter_town(0);
    world_tick();
    static const int key[] = {0, 0, 0, 0, 0, 0, ' '};
    cast(key, 7);
    party.gems = 2;
    dispatch_command('V');
    end_cast("view");
    check(party.gems == 1 && has("View a gem!"), "View uses a gem");
    cast(key, 0);
    script_platform.next_key = NULL;
    static const int key2[] = {0, 0, ' '};
    script_platform.keys(key2, 3);
    show_area_map(map.y, map.x);
    set_draw_page(1);   /* the map is still on the back page */
    video_copy_rect(0, 0, 319, 199, 1, 0);
    set_draw_page(0);
    shot("map");
    world_tick();
    game_time.hour = 23;
    static const int key3[] = {0, ' '};
    script_platform.keys(key3, 2);
    tlen = 0;
    look_sky();
    transcript[tlen] = 0;
    check(has("the night sky!"), "the night sky");
    shot("sky");
    game_time.hour = 12;
    party.active_player = 0;
    uint16_t hp = party[0].hp;
    tlen = 0;
    look_sky();
    transcript[tlen] = 0;
    check(has("the sun!") && party[0].hp == hp - 1, "looking at the sun hurts");
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
    script_platform.keys(NULL, 0);
    script_platform.next_key = shrine_key;
    select_text_window(2);
    tlen = 0;
    visit_shrine();
    script_platform.next_key = NULL;
    transcript[tlen] = 0;
}

/* The shrine of Honesty, the Codex and back. */
static void test_shrines(void)
{
    setup();
    outdoors_at(map.shrine_x(0), map.shrine_y(0));
    map.enter_outdoors();
    world_tick();
    party.sacred_quest = party.codex_answered = 0;
    uint8_t karma = party.karma = 50, str = party[0].str, dex = party[0].dex, intel = party[0].intel;
    unsigned t0 = game_time.hour * 60u + game_time.minute;
    static int k[64];
    int n = 0;
    const char *mantra = map.mantra(0);
    for (const char *p = "honesty"; *p; p++) k[n++] = *p;
    k[n++] = RET;
    for (int i = 0; i < 3; i++) {
        for (const char *p = mantra; *p; p++) k[n++] = *p;
        k[n++] = RET;
    }
    check(*map.get_map_tile_ptr(map.y, map.x) == 0x19, "the party stands on the shrine of Honesty");
    visit(k, n);
    print_transcript("shrine: the quest");
    shot("shrine");
    check(has("Thou dost approach the tranquil Shrine") && has("kneel before the Altar") && has("Mantra:"), "the shrine scene");
    check(has("a Quest is ordained!") && party.sacred_quest == 1, "the right virtue and mantra give the quest");
    check(map.is_outdoors() && game_time.hour * 60u + game_time.minute >= t0 + 16, "back outside, 16 minutes later");
    /* the Codex answers it */
    *map.get_map_tile_ptr(map.y, map.x) = 0x11;
    visit(k, 0);
    print_transcript("codex");
    check(has("The Codex of Ultimate Wisdom lies before thee") && has("Upon the hallowed page thou dost read:") && party.codex_answered == 1,
          "the Codex answers the quest held");
    /* back at the shrine: the reward */
    *map.get_map_tile_ptr(map.y, map.x) = 0x19;
    visit(k, n);
    print_transcript("shrine: the reward");
    check(has("WELL DONE!") && party.sacred_quest == 0 && party.karma == karma + 3, "the return: WELL DONE, karma +3");
    check(party[0].str + party[0].dex + party[0].intel > str + dex + intel, "and an attribute point");
    /* once more: an offering */
    k[n] = '2';
    party.gold = 500;
    visit(k, n + 1);
    check(has("Offer how many hundredweights gold? 2") && party.gold == 300 && party.karma == karma + 5 && has("ALAKAZAM"), "later visits take offerings: 200 gold for 2 karma");
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
    outdoors_at(map.exit_x(0x21), map.exit_y(0x21));
    map.dungeon_seals[0] = 0x80;   /* unsealed, as the word of power FALLAX does */
    party.gems = 1;
    party.torch_light = party.light_spell = 0;
    party.active_player = 0xFF;
    uint8_t torches = party.torches;
    play(keys, (int)(sizeof keys / sizeof keys[0]));
    print_transcript("dungeon");
    check(has("Enter dungeon") && has("DECEIT"), "Enter the dungeon Deceit");
    check(has("You see: darkness."), "nothing to see without light");
    check(has("Ignite torch!") && party.torches == torches - 1, "a torch");
    check(has("Turn right") && has("Advance"), "turning and walking");
    check(has("Dir-Ahead You see: a pit."), "Look ahead: the pit");
    check(has("You find: Nothing hidden on the wall."), "Search the wall ahead");
    check(has("View a gem!") && party.gems == 0, "the gem shows the dungeon map");
    check(has("Attack What?"), "Attack with no monster ahead");
    check(has("Turn around.") && has("Klimb-Up!") && has("Exit to Britannia!"), "back up the ladder and out");
    check(map.is_outdoors() && map.z == 0 && map.x == map.exit_x(0x21) && map.y == map.exit_y(0x21), "outside at the entrance again");
    check(!platform->quit_requested(), "the script was not used up");

    /* pieces, directly: in Deceit on level 1 at (1,2) facing south, with light */
    setup();
    load_file_part(0, 0x200, dungeon.grid, "DUNGEON.DAT");
    map.set(Map::DECEIT);
    map.z = 0;
    map.x = 1;
    map.y = 2;
    dungeon.facing = 2;
    party.torch_light = 200;
    party.active_player = 0;
    dungeon.style = 3;
    dungeon.corridor_wall_tile = 0x4F;
    dungeon.corridor_floor_tile = 0x45;
    dungeon.gfx_flags = 2;
    dungeon.load_dungeon_graphics(1);
    dungeon.draw_dungeon_frame();
    dungeon.redraw_dungeon();
    shot("dungeon");
    script_platform.keys(NULL, 0);
    script_platform.next_key = map_shot_key;
    dungeon.show_dungeon_map();
    script_platform.next_key = NULL;
    /* a fountain of healing ahead */
    DCELL(0, 3, 1) = 0x51;
    party[0].hp = 5;
    static const int drink[] = {N, 'Y'};
    cast(drink, 2);
    dungeon.cmd_look_dungeon();
    end_cast("fountain");
    check(has("a fountain.") && has("Will you drink?") && has("Healed!") && party[0].hp == party[0].max_hp, "a healing fountain");
    /* a trapped, locked chest under the party */
    DCELL(0, 2, 1) = 0x43;
    party.keys = 5;
    static const int none[] = {0};
    cast(none, 0);
    cmd_get();
    end_cast("get closed");
    check(has("Must open first!"), "Get on a closed chest");
    cast(none, 0);
    cmd_open();
    end_cast("open chest");
    check(has("Chest opened") && (DCELL(0, 2, 1) & 0xF0) == 0x70, "Open the chest (its trap goes off)");
    uint16_t gold = party.gold;
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
    map.y = 3;
    check(dungeon.dungeon_change_level(0, 1) == 0 && map.z == 1, "down a level");
    check(dungeon.dungeon_change_level(0, -1) == 0 && map.z == 0, "and up");
    map.z = 7;
    check(dungeon.dungeon_change_level(0, 1) == 1, "below level 8 is the way out");
    map.z = 0;
    /* a corridor fight with giant rats ahead */
    map.y = 2;
    DCELL(0, 3, 1) = 0;
    strong_party();
    map.objects[1].tile = map.objects[1].tile2 = 0;
    map.objects[1].hull = 0x14;
    map.objects[1].x = 1;
    map.objects[1].y = 3;
    map.objects[1].z = 0;
    dungeon.monster_pic = load_picture("MON0.16");
    dungeon.draw_dungeon_view(1);
    copy_page_rect(0xB2, 0xAF, 0x0E, 0x10, 0, 1);
    shot("dungeon_monster");
    bot_keys = 0;
    bot_shot = 0;
    script_platform.keys(NULL, 0);
    script_platform.next_key = combat_bot;
    select_text_window(2);
    tlen = 0;
    dungeon.cmd_attack_dungeon();
    script_platform.next_key = NULL;
    transcript[tlen] = 0;
    print_transcript("corridor fight");
    printf("(%d keys)\n", bot_keys);
    check(has("Attack") && has("armed with") && has("Giant Rat"), "a corridor fight is fought (the bot may fail to catch fleeing rats)");
    check(map == Map::DECEIT || map.is_outdoors(), "back in the dungeon (or out by a ladder)");
    /* a room's set-up: Destard level 1, room 0 */
    setup();
    load_file_part(2 * 0x200, 0x200, dungeon.grid, "DUNGEON.DAT");
    map.set(Map::DESTARD);
    map.z = 0;
    dungeon.facing = 0;
    load_file_part((uint32_t)(1 * 0x1600 + 0 * 0x160), 0x160, view.local_map, "dungeon.cbt");
    combat.prev_location = map.id();
    map.set(Map::SCENE);
    dungeon.place_combatants(3, 0xF0);
    int monsters = 0, things = 0;
    for (int i = 6; i < 32; i++) monsters += (combat.combatants[i].flags & 0x40) != 0;
    for (int i = 0; i < 32; i++) things += map.objects[i].tile != 0;
    check(combat.count_combat_sides().party == 3 && monsters > 0 && things >= monsters + 3, "a room is set up with the party and its monsters");
    combat.turn = 0xFF;
    world_tick();
    shot("room");
    check(dungeon.is_room_cleared(0) == 0, "the room is not cleared yet");
    dungeon.mark_room_cleared(0);
    check(dungeon.is_room_cleared(0) == 1, "a cleared room is remembered");
    map.set(Map::BRITANNIA);
}

/* Camp for 8 hours with a guard, on the same grass; whatever comes is fought. */
static void test_camp(void)
{
    static const int keys[] = {'8', 'Y', '1', RET};
    static int pos;
    setup();
    outdoors_at(map.exit_x(13), (unsigned)map.exit_y(13) + 1);
    map.enter_outdoors();
    strong_party();
    party[1].hp = 100;
    game_time.rest_cooldown = 0;
    unsigned start = game_time.hour;
    bot_keys = 0;
    bot_shot = 1;
    pos = 0;
    script_platform.keys(keys, 4);
    script_platform.next_key = NULL;
    /* the first four keys answer the prompts, the rest is the combat player */
    extern int camp_key(void);
    (void)pos;
    script_platform.next_key = camp_key;
    select_text_window(2);
    world_tick();   /* puts the party's object (objects[0]) in step with the transport */
    dispatch_command('H');
    script_platform.next_key = NULL;
    transcript[tlen] = 0;
    print_transcript("camp");
    check(has("Hole up & camp!") && has("Wilt thou set a watch? Yes") && has("Who will stand guard?"), "the camp's questions");
    check(has("Zzzzzz..."), "the party sleeps");
    check(has("Party rested!") || has("Ambushed!"), "rested or ambushed");
    check(!has("Party rested!") || (party[1].hp > 100 && game_time.rest_cooldown == 14 && game_time.hour == (start + 8) % 24), "a full rest heals and takes the hours");
    check(!has("Ambushed!") || has("*** CONFLICT ***"), "an ambush is a fight");
    check(map.is_outdoors() && party[0].status != 'S' && party[2].status != 'S', "awake on the map again");
    check(!platform->quit_requested(), "the player did not run out of keys");
    shot("camp");
}

int camp_key(void)
{
    static const int answers[] = {'8', 'Y', '1', RET};
    if (map != Map::SCENE || combat.turn == 0xFF) {
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
    if (map == Map::Id(0xFE)) {   /* reset */
        map.set(Map::TITLE_MENU);
        n = 0;
    }
    if (n == 9) shot("transfer");
    return n < (int)(sizeof keys / sizeof keys[0]) ? keys[n++] : -1;
}

static void test_transfer(void)
{
    char path[700];
    snprintf(path, sizeof path, "%s/PARTY.SAV", save_dir);
    check(intro.convert_ultima4_stat(9) == 9 && intro.convert_ultima4_stat(16) == 13 && intro.convert_ultima4_stat(29) == 20 &&
          intro.convert_ultima4_stat(33) == 20 && intro.convert_ultima4_stat(50) == 25 && intro.convert_ultima4_stat(70) == 30, "attributes 0-70 are scaled to 0-30");
    /* no file: the prompt, Esc */
    setup();
    remove(path);
    static const int esc[] = {ESC, 0};
    cast(esc, 1);
    select_text_window(0);
    intro.transfer_from_ultima4();
    end_cast("transfer: no disk");
    check(has("Please insert the Ultima IV Player Disk") && has("<Esc> to abort"), "without PARTY.SAV the disk is asked for; Esc gives up");
    /* bad data */
    write_party_sav(1, 1);
    static const int any[] = {' ', 0};
    cast(any, 1);
    select_text_window(0);
    intro.transfer_from_ultima4();
    end_cast("transfer: bad data");
    check(has("Error: Your Ultima IV game contains bad data."), "an attribute above 70 is bad data");
    /* the real thing: rename to Zoe, become male */
    write_party_sav(1, 0);
    map.set(Map::Id(0xFE));
    script_platform.keys(NULL, 0);
    script_platform.next_key = transfer_shot_key;
    tlen = 0;
    video_clear();
    select_text_window(0);
    intro.transfer_from_ultima4();
    script_platform.next_key = NULL;
    transcript[tlen] = 0;
    print_transcript("transfer");
    check(has("Found:") && has("Mariah") && has("a level 4 Female") && has("is an Avatar."), "the Ultima IV character is shown");
    check(has("Keep this name?") && has("Enter new name: Zoe") && has("Keep same sex?"), "name and sex can be changed");
    check(has("Thou art now an Avatar:") && has("Experience has been converted") && has("Level has been converted"), "the steps of the conversion");
    check(has("Strength: was 16(50), now 20(30)") && has("Dexterity: was 33(50), now 20(30)") && has("Intellect: was 50(50), now 25(30)"), "the attributes, strength raised to the minimum of 20");
    static uint8_t gam[SaveGame::SIZE];
    check(has("Conversion complete, saving...") && load_file_part(0, SaveGame::SIZE, gam, "SAVED.GAM") == 0, "SAVED.GAM is written");
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
    intro.transfer_from_ultima4();
    end_cast("transfer: not an Avatar");
    check(has("is not an Avatar") && has("Class remains intact") && party[0].cls == 'M' && party[0].sex == 0x0C && !strcmp(party[0].name, "Mariah"),
          "a character who is no Avatar keeps the class");
    remove(path);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    save_dir = argc > 2 ? argv[2] : ".";
    files_set_game_dir(dir);
    if (!Data::instance().load()) return 1;   /* the game's tables and text */
    {
        /* how much of DATA.OVL has an owner (with every class linked in) */
        static uint8_t taken[Data::SIZE];
        unsigned owned = 0, off, size;
        const char *name;
        for (int i = 0; i < Data::instance().count(); i++) {
            Data::instance().piece(i, &off, &size, &name);
            for (unsigned j = off; j < off + size; j++) owned += !taken[j]++;
        }
        printf("DATA.OVL: %d pieces hold %u of the %u bytes\n", Data::instance().count(), owned, (unsigned)Data::SIZE);
    }
    files_set_save_dir(save_dir);
    snprintf(save, sizeof save, "%s/SAVED.GAM", dir);
    reset_text_windows();
    load_font(0, "ibm.ch");
    load_font(1, "runes.ch");
    select_font(0);
    set_ega_colors();
    gfx.video_type = 1;
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
