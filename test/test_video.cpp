/* test_video.cpp - the CGA and Hercules drivers: runs the title sequence, the menu and a little of
 * the game headlessly under the driver named on the command line, saving screenshots
 * (test_video_<driver>_*.ppm) and checking that what is drawn fits the driver's pixels.
 * Usage: test_video <game dir> <save dir> ega|cga|herc */
#include <stdio.h>
#include <string.h>
#include "dungeon.h"
#include "files.h"
#include "game.h"
#include "gamedata.h"
#include "gfx.h"
#include "intro.h"
#include "map.h"
#include "npc.h"
#include "party.h"
#include "picture.h"
#include "pictures.h"
#include "platform.h"
#include "platform_script.h"
#include "strings.h"
#include "text.h"
#include "tiles.h"
#include "video.h"
#include "view.h"


static int fails;
static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

static const char *driver = "ega";
static const char *stage = "";
static long every = 1, shots, stage_start;

static void shot(const char *name)
{
    char path[96];
    snprintf(path, sizeof path, "test_video_%s_%s.ppm", driver, name);
    video_save_ppm(path);
}

static void on_tick(long t)
{
    if (every && (t - stage_start) % every == 0 && shots < 12) {
        char name[32];
        snprintf(name, sizeof name, "%s%02ld", stage, shots++);
        shot(name);
    }
}

static void begin(const char *name, long interval)
{
    stage = name;
    every = interval;
    shots = 0;
    stage_start = script_platform.ticks();
}

static int max_pixel(void)
{
    int m = 0;
    for (int y = 0; y < VIDEO_ROWS; y++)
        for (int x = 0; x < SCREEN_W; x++) {
            if (video_fb[y][x] > m) m = video_fb[y][x];
            if (video_back[y][x] > m) m = video_back[y][x];
        }
    return m;
}

static int lit_rows(int from, int to)
{
    int n = 0;
    for (int y = from; y < to; y++)
        for (int x = 0; x < SCREEN_W; x++)
            if (video_fb[y][x]) { n++; break; }
    return n;
}

static int keys[60000];

int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    files_set_game_dir(argv[1]);
    if (!Data::instance().load()) return 1;   /* the game's tables and text */
    files_set_save_dir(argv[2]);
    driver = argv[3];
    int type = !strcmp(driver, "cga") ? VIDEO_CGA : !strcmp(driver, "herc") ? VIDEO_HERC : VIDEO_EGA;
    int two_bit = type != VIDEO_EGA;

    gfx.video_type = (uint16_t)type;
    reset_text_windows();
    reset_text_attrs();
    check(video_start(), "fonts load");
    check(load_tileset(pictures.name(Pictures::TILES)), pictures.name(Pictures::TILES));
    check(!two_bit || !strcmp(pictures.name(Pictures::TILES), "tiles.4") || !strcmp(pictures.name(Pictures::TILES), "TILES.4"), "four-colour tile file");
    reset_tile_anim_map();
    select_font(0);
    script_platform.on_tick = on_tick;

    /* The title sequence with no key pressed, then Journey Onward from the menu. */
    int n = 0;
    memset(keys, 0, sizeof keys);
    n = 20000;
    script_platform.keys(keys, n);
    begin("title", 60);
    int went = intro.intro_main(0);
    printf("title ran %ld ticks, returned %d\n", script_platform.ticks() - stage_start, went);
    check(max_pixel() <= (two_bit ? 3 : 15), "pixels within the driver's colours (title)");
    check(type != VIDEO_HERC || lit_rows(200, 300) > 20, "Hercules uses the rows below 200");
    check(type == VIDEO_HERC || lit_rows(200, 300) == 0, "nothing below row 200");
    shot("menu");

    n = 0;
    keys[n++] = 'J';
    for (int i = 0; i < 40; i++) keys[n++] = 0;
    static const int moves[] = {3, 3, 4, 2, 2, 'Z', 0, 0, 0x1B, 'L', 2, 0, 0, 3, 1};   /* walk, Ztats, Look */
    script_platform.keys(keys, n);
    begin("", 0);
    went = intro.intro_main(1);
    check(went == 1, "Journey Onward");
    shot("game0");
    n = 0;
    for (unsigned i = 0; i < sizeof moves / sizeof moves[0]; i++) {
        keys[n++] = moves[i];
        for (int j = 0; j < 6; j++) keys[n++] = 0;
    }
    script_platform.keys(keys, n);
    select_text_window(2);
    set_text_color((uint8_t)gfx.color_text);
    view.world_tick_enabled = 1;
    begin("game", 4);
    game_run();
    shot("game_end");
    check(max_pixel() <= (two_bit ? 3 : 15), "pixels within the driver's colours (game)");
    check(type != VIDEO_HERC || lit_rows(200, 300) > 50, "Hercules: the game screen fills 300 rows");

    /* The dungeon view: walls drawn on the left and mirrored on the right, and a monster. */
    game_clear_exit();
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
    begin("", 0);
    dungeon.load_dungeon_graphics(1);
    dungeon.draw_dungeon_frame();
    dungeon.redraw_dungeon();
    shot("dungeon");
    map.objects[1].tile = map.objects[1].tile2 = 0;
    map.objects[1].hull = 0x14;
    map.objects[1].x = 1;
    map.objects[1].y = 3;
    map.objects[1].z = 0;
    dungeon.monster_pic = load_picture(two_bit ? "MON0.4" : "MON0.16");
    dungeon.draw_dungeon_view(1);
    copy_page_rect(0xB2, 0xAF, 0x0E, 0x10, 0, 1);
    shot("dungeon_monster");
    check(max_pixel() <= (two_bit ? 3 : 15), "pixels within the driver's colours (dungeon)");

    /* Back at the title after creating a character: the game draws the banner frames again from
     * the title picture it has just freed. */
    select_text_window(0);
    put_char(0xFF);
    set_draw_page(1);
    put_char(0xFF);
    intro.show_title_screen(0);
    intro.draw_menu_banner();
    set_draw_page(0);
    wait_reveal_strips();
    shot("title_again");
    check(lit_rows(type == VIDEO_HERC ? 97 : 65, type == VIDEO_HERC ? 171 : 114) > 40, "the banner is still there after the picture is freed");

    printf("%d failure(s)\n", fails);
    return fails != 0;
}
