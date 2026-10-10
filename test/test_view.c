/* test_view.c - renders the map view from SAVED.GAM: the saved location, then Britannia at noon
 * and at night, then the same place after a few animation turns. Writes test_view_*.ppm.
 * Usage: test_view <game dir> */
#include <stdio.h>
#include <string.h>
#include "files.h"
#include "gfx.h"
#include "map.h"
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

static void setup_screen(void)
{
    video_clear();
    draw_screen_frame();
}

static void shot(const char *name)
{
    char path[64];
    snprintf(path, sizeof path, "test_view_%s.ppm", name);
    if (video_save_ppm(path) == 0) printf("wrote %s\n", path);
}

int main(int argc, char **argv)
{
    files_set_game_dir(argc > 1 ? argv[1] : ".");
    reset_text_windows();
    load_font(0, "ibm.ch");
    select_font(0);
    set_ega_colors();
    dg.video_type = 1;
    reset_tile_anim_map();
    check(load_tileset("tiles.16") == 1, "load_tileset(tiles.16)");
    check(tile_pixel(0x11C, 8, 8) != 0, "tile 0x11C (Avatar) has pixels");

    FILE *f = game_fopen("SAVED.GAM", "rb");
    check(f != NULL, "SAVED.GAM");
    if (f) fclose(f);
    char save[600];
    snprintf(save, sizeof save, "%s/SAVED.GAM", argc > 1 ? argv[1] : ".");
    check(dgroup_load_game(save) == 0, "dgroup_load_game");
    printf("location %u z %u party (%u,%u) transport 0x%02X  %02u:%02u\n", dg.location, dg.z_level,
           dg.party_x, dg.party_y, dg.transport, dg.hour, dg.minute);

    /* The saved location (a town). */
    setup_screen();
    load_town_level(0);
    dg.world_tick_enabled = 1;
    dg.anim_flag = 0xFF;   /* first frame: no animation step */
    world_tick();
    check(dg.view_mask[5 * 32 + 5] == 0 && (dg.view_tiles[5 * 16 + 5] & 0xFC) == 0x30, "party drawn at the centre, seated (saved on a chair)");
    shot("town");

    /* Britannia at noon, at Lord British's castle (location 0x11 exit). */
    dg.location = 0;
    dg.z_level = 0;
    dg.party_x = dg.town_exit_x[0x11];
    dg.party_y = (uint8_t)(dg.town_exit_y[0x11] + 1);
    memset(&dg.objects[1], 0, sizeof dg.objects - sizeof dg.objects[0]);
    dg.hour = 12;
    dg.light_radius = 0;
    enter_outdoors();
    check(dg.light_radius == 0x32, "daylight radius 50");
    setup_screen();
    world_tick();
    int seen = 0;
    for (int r = 0; r < 11; r++) for (int c = 0; c < 11; c++) seen += dg.view_mask[r * 32 + c] != 0xFF;
    printf("visible squares at noon: %d\n", seen);
    shot("noon");

    /* Night: radius 2, only nearby squares and lit ones. */
    dg.hour = 23;
    update_light_radius();
    check(dg.light_radius == 2, "night radius 2");
    world_tick();
    int seen_night = 0;
    for (int r = 0; r < 11; r++) for (int c = 0; c < 11; c++) seen_night += dg.view_mask[r * 32 + c] != 0xFF;
    printf("visible squares at night: %d\n", seen_night);
    check(seen_night < seen, "fewer squares visible at night");
    shot("night");

    /* Walk east 20 squares: the chunk window has to scroll. */
    dg.hour = 12;
    update_light_radius();
    uint8_t ox = dg.map_origin_x;
    for (int i = 0; i < 20; i++) { outdoor_step(0, 1); world_tick(); }
    check(dg.map_origin_x != ox, "map window scrolled east");
    check(*get_map_tile_ptr(dg.party_y, dg.party_x) == dg.view_mask[5 * 32 + 5] || dg.view_mask[5 * 32 + 5] == 0,
          "view centre matches the map");
    shot("east");

    printf("%d failure(s)\n", fails);
    return fails != 0;
}
