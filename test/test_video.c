/* test_video.c - the CGA and Hercules drivers: runs the title sequence, the menu and a little of
 * the game headlessly under the driver named on the command line, saving screenshots
 * (test_video_<driver>_*.ppm) and checking that what is drawn fits the driver's pixels.
 * Usage: test_video <game dir> <save dir> ega|cga|herc */
#include <stdio.h>
#include <string.h>
#include "data.h"
#include "dungeon.h"
#include "files.h"
#include "game.h"
#include "gfx.h"
#include "intro.h"
#include "map.h"
#include "picture.h"
#include "platform.h"
#include "text.h"
#include "tiles.h"
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
    stage_start = platform_script_ticks();
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
    files_set_save_dir(argv[2]);
    driver = argv[3];
    int type = !strcmp(driver, "cga") ? VIDEO_CGA : !strcmp(driver, "herc") ? VIDEO_HERC : VIDEO_EGA;
    int two_bit = type != VIDEO_EGA;

    dg.video_type = (uint16_t)type;
    reset_text_windows();
    reset_text_attrs();
    check(video_start(), "fonts load");
    check(load_tileset(ds_str(dg.tileset_file)), ds_str(dg.tileset_file));
    check(!two_bit || !strcmp(ds_str(dg.tileset_file), "tiles.4") || !strcmp(ds_str(dg.tileset_file), "TILES.4"), "four-colour tile file");
    dg.path_grid = (uint16_t)offsetof(struct DGroup, text_buffer);
    reset_tile_anim_map();
    select_font(0);
    platform_script_on_tick = on_tick;

    /* The title sequence with no key pressed, then Journey Onward from the menu. */
    int n = 0;
    memset(keys, 0, sizeof keys);
    n = 20000;
    platform_script_keys(keys, n);
    begin("title", 60);
    int went = intro_main(0);
    printf("title ran %ld ticks, returned %d\n", platform_script_ticks() - stage_start, went);
    check(max_pixel() <= (two_bit ? 3 : 15), "pixels within the driver's colours (title)");
    check(type != VIDEO_HERC || lit_rows(200, 300) > 20, "Hercules uses the rows below 200");
    check(type == VIDEO_HERC || lit_rows(200, 300) == 0, "nothing below row 200");
    shot("menu");

    n = 0;
    keys[n++] = 'J';
    for (int i = 0; i < 40; i++) keys[n++] = 0;
    static const int moves[] = {3, 3, 4, 2, 2, 'Z', 0, 0, 0x1B, 'L', 2, 0, 0, 3, 1};   /* walk, Ztats, Look */
    platform_script_keys(keys, n);
    begin("", 0);
    went = intro_main(1);
    check(went == 1, "Journey Onward");
    shot("game0");
    n = 0;
    for (unsigned i = 0; i < sizeof moves / sizeof moves[0]; i++) {
        keys[n++] = moves[i];
        for (int j = 0; j < 6; j++) keys[n++] = 0;
    }
    platform_script_keys(keys, n);
    select_text_window(2);
    set_text_color((uint8_t)dg.color_text);
    dg.world_tick_enabled = 1;
    begin("game", 4);
    game_run();
    shot("game_end");
    check(max_pixel() <= (two_bit ? 3 : 15), "pixels within the driver's colours (game)");
    check(type != VIDEO_HERC || lit_rows(200, 300) > 50, "Hercules: the game screen fills 300 rows");

    /* The dungeon view: walls drawn on the left and mirrored on the right, and a monster. */
    game_clear_exit();
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
    begin("", 0);
    load_dungeon_graphics(1);
    draw_dungeon_frame();
    redraw_dungeon();
    shot("dungeon");
    dg.objects[1].tile = dg.objects[1].tile2 = 0;
    dg.objects[1].hull = 0x14;
    dg.objects[1].x = 1;
    dg.objects[1].y = 3;
    dg.objects[1].z = 0;
    dg.dungeon_monster_pic = load_picture(two_bit ? "MON0.4" : "MON0.16");
    draw_dungeon_view(1);
    copy_page_rect(0xB2, 0xAF, 0x0E, 0x10, 0, 1);
    shot("dungeon_monster");
    check(max_pixel() <= (two_bit ? 3 : 15), "pixels within the driver's colours (dungeon)");

    /* Back at the title after creating a character: the game draws the banner frames again from
     * the title picture it has just freed. */
    select_text_window(0);
    put_char(0xFF);
    set_draw_page(1);
    put_char(0xFF);
    show_title_screen(0);
    draw_menu_banner();
    set_draw_page(0);
    wait_reveal_strips();
    shot("title_again");
    check(lit_rows(type == VIDEO_HERC ? 97 : 65, type == VIDEO_HERC ? 171 : 114) > 40, "the banner is still there after the picture is freed");

    printf("%d failure(s)\n", fails);
    return fails != 0;
}
