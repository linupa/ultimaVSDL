/* sdl_main.c - the port in an SDL window.
 * Usage: u5sdl [-v ega|cga|herc] [-p green|amber|white] [-4] [-s scale] [-j | -m] [-w save dir] [-x scene] <game dir>
 *   -v the graphics driver to stand in for: EGA.DRV (the default), CGA.DRV or HER.DRV (Hercules;
 *      "hercraw" shows its dots unblended)
 *   -p green|amber|white: the Hercules monitor's phosphor (default green)
 *   -4 stretch to 4:3 like a PC monitor    -j skip the intro (Journey Onward)    -m start at the menu
 *   -w where SAVED.GAM etc. are written (default ./u5save; the game directory is never modified)
 *   -x death | blackthorn | ending: play that story scene from the saved game
 * Keys are the game's: arrows (or Shift + keypad) move, letters are the commands (A-ttack, B-oard,
 * ... Z-tats), 0-6 set the active player, Q saves, Ctrl-E exits. Closing the window quits.
 * In dungeons: left / right turn, up advances, down backs up, Return turns around. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "commands.h"
#include "endings.h"
#include "files.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "intro.h"
#include "map.h"
#include "picture.h"
#include "status.h"
#include "talk.h"
#include "text.h"
#include "town.h"
#include "tiles.h"
#include "video.h"
#include "view.h"

int  platform_init(const char *title, int scale, int aspect_4_3);   /* sdl_platform.c */
void platform_shutdown(void);

enum { WIN_STATUS = 1, WIN_MESSAGE = 2 };

int main(int argc, char **argv)
{
    int scale = 3, aspect = 0, skip_intro = 0, skip_title = 0, video = VIDEO_EGA;
    const char *dir = ".", *save_dir = "u5save", *scene = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-4")) aspect = 1;
        else if (!strcmp(argv[i], "-j")) skip_intro = 1;
        else if (!strcmp(argv[i], "-m")) skip_title = 1;
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-v") && i + 1 < argc) {
            const char *v = argv[++i];
            if (!strcmp(v, "ega")) video = VIDEO_EGA;
            else if (!strcmp(v, "cga")) video = VIDEO_CGA;
            else if (!strcmp(v, "hercraw")) { video = VIDEO_HERC; video_herc_raw = 1; }
            else if (!strncmp(v, "her", 3)) video = VIDEO_HERC;
            else {
                fprintf(stderr, "-v %s: the drivers are ega, cga and herc\n", v);
                return 1;
            }
        }
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            static const struct { const char *name; uint8_t rgb[3]; } screens[] = {
                {"green", {0x40, 0xFF, 0x60}}, {"amber", {0xFF, 0xB0, 0x00}}, {"white", {0xE0, 0xE0, 0xE0}},
            };
            const char *v = argv[++i];
            int k = 0;
            while (k < 3 && strcmp(v, screens[k].name)) k++;
            if (k == 3) {
                fprintf(stderr, "-p %s: the screens are green, amber and white\n", v);
                return 1;
            }
            memcpy(video_herc_phosphor, screens[k].rgb, 3);
        }
        else if (!strcmp(argv[i], "-w") && i + 1 < argc) save_dir = argv[++i];
        else if (!strcmp(argv[i], "-x") && i + 1 < argc) { scene = argv[++i]; skip_intro = 1; }
        else dir = argv[i];
    }
    if (scale < 1) scale = 1;

    files_set_game_dir(dir);
    mkdir(save_dir, 0777);   /* saves go here; the game directory is only read */
    files_set_save_dir(save_dir);
    dg.video_type = (uint16_t)video;   /* the original detects the card, or takes it from the command line */
    if (!video_start() || !load_tileset(ds_str(dg.tileset_file))) {
        fprintf(stderr, "cannot read the fonts or %s in %s\n", ds_str(dg.tileset_file), dir);
        return 1;
    }
    dg.path_grid = (uint16_t)offsetof(struct DGroup, text_buffer);   /* as main() does */
    reset_tile_anim_map();
    reset_text_windows();
    reset_text_attrs();
    select_font(0);
    if (platform_init("Ultima V port", scale, aspect) != 0) return 1;

    if (skip_intro) {   /* -j: straight into the saved game, like holding J at start-up */
        alloc_back_page();
        dg.sound_enabled = 1;   /* as intro_main does */
        if (load_file_part(0, SAVE_SIZE, ds_ptr(SAVE_OFFSET), "SAVED.GAM") != 0 || dg.party[0].name[0] == 0) {
            fprintf(stderr, "no saved game in %s or %s\n", save_dir, dir);
            platform_shutdown();
            return 1;
        }
        {   /* as Journey Onward does: the saved objects become the working files */
            static uint8_t ool[0x200];
            if (load_file_part(0, 0x200, ool, "SAVED.OOL") == 0) {
                save_file_part(0x100, ool, "BRIT.OOL");
                save_file_part(0x100, ool + 0x100, "UNDER.OOL");
            }
        }
        select_text_window(0);
        draw_screen_frame();
        define_text_window(9, 0x27, 1, 0x18, WIN_STATUS);
        define_text_window(0x17, 0x27, 0x0B, 0x18, WIN_MESSAGE);
    } else if (!intro_main(skip_title)) {   /* title, menu; returns after Journey Onward */
        platform_shutdown();
        return 0;
    }
    select_text_window(WIN_MESSAGE);
    set_text_color((uint8_t)dg.color_text);

    if (scene) {   /* -x: play a story scene straight away, from the saved game */
        dg.world_tick_enabled = 1;
        if (!strcmp(scene, "death")) {
            for (unsigned i = 0; i < dg.party_size; i++) { dg.party[i].status = 'D'; dg.party[i].hp = 0; }
            draw_status_bar();
            party_death();
        } else if (!strcmp(scene, "blackthorn")) {
            captured_by_blackthorn();
        } else if (!strcmp(scene, "ending")) {
            dg.location = 0xFF;
            dg.sandalwood_box = 1;
            rescue_lord_british();
        }
        if (platform_quit_requested()) {
            platform_shutdown();
            return 0;
        }
        select_text_window(0);
        draw_screen_frame();
        select_text_window(WIN_MESSAGE);
    }
    dg.world_tick_enabled = 1;
    game_run();   /* main's loop: outdoors / towns, until Exit to DOS (Ctrl-E) or the window is closed */
    platform_shutdown();
    return 0;
}
