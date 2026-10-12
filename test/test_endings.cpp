/* test_endings.cpp - the death scene, Blackthorn's interrogation and the endgame with scripted
 * keys. Saves screenshots (test_endings_*.ppm). Usage: test_endings <game dir> <scratch dir> */
#include <stdio.h>
#include <string.h>
#include "endings.h"
#include "files.h"
#include "gamedata.h"
#include "gametime.h"
#include "gfx.h"
#include "map.h"
#include "npc.h"
#include "party.h"
#include "picture.h"
#include "platform_script.h"
#include "savegame.h"
#include "strings.h"
#include "text.h"
#include "tiles.h"
#include "town.h"
#include "video.h"
#include "view.h"


static int fails;
static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

static char transcript[32768];
static size_t tlen;
static void echo(uint8_t c, int rune)
{
    (void)rune;
    if (tlen + 1 < sizeof transcript) transcript[tlen++] = c == '\n' ? ' ' : (char)c;
}
static int said(const char *s)
{
    static char flat[32768];
    size_t j = 0;
    for (size_t i = 0; i < tlen; i++) {
        if (transcript[i] == ' ' && j && flat[j - 1] == ' ') continue;
        flat[j++] = transcript[i];
    }
    flat[j] = 0;
    return strstr(flat, s) != NULL;
}

static long base, shot_tick;
static const char *shot_name;
static void on_tick(long t)
{
    if (shot_name && t - base == shot_tick) {
        char path[80];
        snprintf(path, sizeof path, "test_endings_%s.ppm", shot_name);
        video_save_ppm(path);
    }
}

static char save[600];
static int keys[256], nkeys;
static void type(const char *s) { for (; *s; s++) keys[nkeys++] = (unsigned char)*s; keys[nkeys++] = 0x0D; }

static void setup(const char *name, long tick)
{
    savegame.load_path(save);
    town.shadowlord_here = 0xFF;
    tiles.origin_y = 8;
    select_text_window(0);
    video_clear();
    draw_screen_frame();
    select_text_window(2);
    put_char(0xFF);
    tlen = 0;
    nkeys = 0;
    base = script_platform.ticks();
    shot_tick = tick;
    shot_name = name;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    files_set_game_dir(dir);
    if (!Data::instance().load()) return 1;   /* the game's tables and text */
    files_set_save_dir(argc > 2 ? argv[2] : ".");
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
    define_text_window(9, 0x27, 1, 0x18, 1);
    define_text_window(0x17, 0x27, 0x0B, 0x18, 2);
    text_echo = echo;
    script_platform.on_tick = on_tick;

    /* 1. Everyone dies (out in Britannia, karma 40). */
    setup("death", 75);
    map.set(Map::BRITANNIA);
    party.karma = 40;
    party.food = 0;
    for (unsigned i = 0; i < party.size(); i++) { party[i].status = 'D'; party[i].hp = 0; }
    keys[nkeys++] = ' ';
    script_platform.keys(keys, nkeys);
    endings.party_death();
    printf("death scene: %ld ticks\n", script_platform.ticks() - base);
    check(said("An unending darkness engulfs thee") && said("FORTIS FORTUNA") && said("Strange words are intoned") && said("Vertigo"),
          "death scene text");
    int all_alive = 1;
    for (unsigned i = 0; i < party.size(); i++) all_alive &= party[i].status == 'G' && party[i].hp == party[i].max_hp;
    check(all_alive, "the party is raised");
    check(map == Map::LORD_BRITISHS_CASTLE && map.z == 1 && game_time.hour == 6 && party.karma == 75 && party.food == 63,
          "Lord British's castle, floor 2, 6:00, karma 75, food 63");

    /* 2. Captured; refuse the mantra four times: the companion dies. */
    setup("blackthorn", 300);
    int size0 = party.size();
    char second[9];
    strcpy(second, party[1].name);
    keys[nkeys++] = ' ';   /* "an honour to meet thee" */
    keys[nkeys++] = ' ';   /* after his offer */
    type("no");
    keys[nkeys++] = ' ';   /* the threat */
    type("never");
    type("no");
    type("no");
    keys[nkeys++] = ' ';   /* "... is sliced in half!" */
    script_platform.keys(keys, nkeys);
    endings.captured_by_blackthorn();
    printf("capture: %ld ticks\n", script_platform.ticks() - base);
    printf("Blackthorn: party %d -> %u, location 0x%02X, keys %u\n", size0, party.size(), map.id(), party.keys);
    check(said("Thou art subdued and blindfolded") && said("Blackthorn says") && said("Your response?"), "capture and interrogation text");
    check(said("is sliced in half"), "refusing four times costs a companion");
    check(party.size() == size0 - 1 && !strcmp(party[15].name, second) && party[15].where == 0x7F, "the second member is gone");
    check(map == Map::PALACE_OF_BLACKTHORN && party.keys == 0 && map.z == 0xFF, "left in Blackthorn's dungeon without keys");

    /* 2b. Giving the mantra: the shrine falls. */
    setup(NULL, 0);
    int shrine = 0;
    while (shrine < 8 && map.shrine_state[shrine]) shrine++;
    uint8_t karma0 = party.karma;
    keys[nkeys++] = ' ';
    keys[nkeys++] = ' ';
    type(map.mantra((unsigned)shrine));
    keys[nkeys++] = ' ';
    script_platform.keys(keys, nkeys);
    endings.captured_by_blackthorn();
    check(map.shrine_state[shrine] == 0xFF && party.karma == karma0 - 5, "the mantra destroys the shrine, karma -5");

    /* 3. The endgame, with the sandalwood box. */
    setup("ending", 330);
    map.set(Map::SCENE);
    party.sandalwood_box = 1;
    party[1].status = 'D';
    keys[nkeys++] = ' ';
    keys[nkeys++] = 'Y';
    for (int i = 0; i < 7; i++) keys[nkeys++] = ' ';    /* his speech */
    for (int i = 0; i < 6; i++) keys[nkeys++] = ' ';    /* ending pages */
    script_platform.keys(keys, nkeys);
    endings.rescue_lord_british();
    video_save_ppm("test_endings_proclamation.ppm");
    check(said("lives!") && party[1].status == 'G', "the dead companion is raised");
    check(said("Be it known that on") && said("the Fifth Day of") && said("the Fourth Month") &&
          said("One Hundred") && said("Thirty-Nine"), "proclamation date in words (4-5-139)");
    check(said("to Lord British at Origin Systems"), "proclamation ends");

    printf("%d failure(s)\n", fails);
    return fails != 0;
}
