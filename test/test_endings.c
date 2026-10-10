/* test_endings.c - the death scene, Blackthorn's interrogation and the endgame with scripted
 * keys. Saves screenshots (test_endings_*.ppm). Usage: test_endings <game dir> <scratch dir> */
#include <stdio.h>
#include <string.h>
#include "endings.h"
#include "files.h"
#include "gfx.h"
#include "picture.h"
#include "text.h"
#include "tiles.h"
#include "town.h"
#include "video.h"
#include "view.h"

void platform_script_keys(const int *keys, int n);
long platform_script_ticks(void);
extern void (*platform_script_on_tick)(long ticks);

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
    dgroup_load_game(save);
    dg.path_grid = (uint16_t)offsetof(struct DGroup, text_buffer);
    dg.shadowlord_here = 0xFF;
    dg.tile_origin_y = 8;
    select_text_window(0);
    video_clear();
    draw_screen_frame();
    select_text_window(2);
    put_char(0xFF);
    tlen = 0;
    nkeys = 0;
    base = platform_script_ticks();
    shot_tick = tick;
    shot_name = name;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    files_set_game_dir(dir);
    files_set_save_dir(argc > 2 ? argv[2] : ".");
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
    define_text_window(9, 0x27, 1, 0x18, 1);
    define_text_window(0x17, 0x27, 0x0B, 0x18, 2);
    text_echo = echo;
    platform_script_on_tick = on_tick;

    /* 1. Everyone dies (out in Britannia, karma 40). */
    setup("death", 75);
    dg.location = 0;
    dg.karma = 40;
    dg.food = 0;
    for (unsigned i = 0; i < dg.party_size; i++) { dg.party[i].status = 'D'; dg.party[i].hp = 0; }
    keys[nkeys++] = ' ';
    platform_script_keys(keys, nkeys);
    party_death();
    printf("death scene: %ld ticks\n", platform_script_ticks() - base);
    check(said("An unending darkness engulfs thee") && said("FORTIS FORTUNA") && said("Strange words are intoned") && said("Vertigo"),
          "death scene text");
    int all_alive = 1;
    for (unsigned i = 0; i < dg.party_size; i++) all_alive &= dg.party[i].status == 'G' && dg.party[i].hp == dg.party[i].max_hp;
    check(all_alive, "the party is raised");
    check(dg.location == 0x11 && dg.z_level == 1 && dg.hour == 6 && dg.karma == 75 && dg.food == 63,
          "Lord British's castle, floor 2, 6:00, karma 75, food 63");

    /* 2. Captured; refuse the mantra four times: the companion dies. */
    setup("blackthorn", 300);
    int size0 = dg.party_size;
    char second[9];
    strcpy(second, dg.party[1].name);
    keys[nkeys++] = ' ';   /* "an honour to meet thee" */
    keys[nkeys++] = ' ';   /* after his offer */
    type("no");
    keys[nkeys++] = ' ';   /* the threat */
    type("never");
    type("no");
    type("no");
    keys[nkeys++] = ' ';   /* "... is sliced in half!" */
    platform_script_keys(keys, nkeys);
    captured_by_blackthorn();
    printf("capture: %ld ticks\n", platform_script_ticks() - base);
    printf("Blackthorn: party %d -> %u, location 0x%02X, keys %u\n", size0, dg.party_size, dg.location, dg.keys);
    check(said("Thou art subdued and blindfolded") && said("Blackthorn says") && said("Your response?"), "capture and interrogation text");
    check(said("is sliced in half"), "refusing four times costs a companion");
    check(dg.party_size == size0 - 1 && !strcmp(dg.party[15].name, second) && dg.party[15].where == 0x7F, "the second member is gone");
    check(dg.location == 0x12 && dg.keys == 0 && dg.z_level == 0xFF, "left in Blackthorn's dungeon without keys");

    /* 2b. Giving the mantra: the shrine falls. */
    setup(NULL, 0);
    int shrine = 0;
    while (shrine < 8 && dg.shrine_state[shrine]) shrine++;
    uint8_t karma0 = dg.karma;
    keys[nkeys++] = ' ';
    keys[nkeys++] = ' ';
    type(ds_str(dg.mantras[shrine]));
    keys[nkeys++] = ' ';
    platform_script_keys(keys, nkeys);
    captured_by_blackthorn();
    check(dg.shrine_state[shrine] == 0xFF && dg.karma == karma0 - 5, "the mantra destroys the shrine, karma -5");

    /* 3. The endgame, with the sandalwood box. */
    setup("ending", 330);
    dg.location = 0xFF;
    dg.sandalwood_box = 1;
    dg.party[1].status = 'D';
    keys[nkeys++] = ' ';
    keys[nkeys++] = 'Y';
    for (int i = 0; i < 7; i++) keys[nkeys++] = ' ';    /* his speech */
    for (int i = 0; i < 6; i++) keys[nkeys++] = ' ';    /* ending pages */
    platform_script_keys(keys, nkeys);
    rescue_lord_british();
    video_save_ppm("test_endings_proclamation.ppm");
    check(said("lives!") && dg.party[1].status == 'G', "the dead companion is raised");
    check(said("Be it known that on") && said("the Fifth Day of") && said("the Fourth Month") &&
          said("One Hundred") && said("Thirty-Nine"), "proclamation date in words (4-5-139)");
    check(said("to Lord British at Origin Systems"), "proclamation ends");

    printf("%d failure(s)\n", fails);
    return fails != 0;
}
