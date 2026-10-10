/* test_intro.c - the title sequence, menu, introduction story, demo and character creation, run
 * with scripted keys; saves screenshots (test_intro_*.ppm) and checks the results.
 * Usage: test_intro <game dir> <scratch save dir> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "files.h"
#include "intro.h"
#include "picture.h"
#include "story.h"
#include "text.h"
#include "tiles.h"
#include "video.h"

void platform_script_keys(const int *keys, int n);
long platform_script_ticks(void);
extern void (*platform_script_on_tick)(long ticks);

static int fails;
static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

static void shot(const char *name)
{
    char path[80];
    snprintf(path, sizeof path, "test_intro_%s.ppm", name);
    video_save_ppm(path);
}

static int lit(int x1, int y1, int x2, int y2)   /* non-black pixels in a screen area */
{
    int n = 0;
    for (int y = y1; y <= y2; y++)
        for (int x = x1; x <= x2; x++) n += video_fb[y][x] != 0;
    return n;
}

static long base_ticks;
static const long *shot_at;
static const char *const *shot_name;
static uint8_t snap[VIDEO_ROWS][SCREEN_W];   /* the back page at the last tick */
static void on_tick(long t)
{
    memcpy(snap, video_back, sizeof snap);
    for (int i = 0; shot_at && shot_at[i]; i++)
        if (t - base_ticks == shot_at[i]) shot(shot_name[i]);
}

static int keys[40000];

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    files_set_game_dir(dir);
    files_set_save_dir(argc > 2 ? argv[2] : ".");
    reset_text_windows();
    reset_text_attrs();
    check(load_font(0, "ibm.ch") > 0 && load_font(1, "runes.ch") > 0, "fonts");
    select_font(0);
    dg.video_type = 1;
    platform_script_on_tick = on_tick;

    /* File loading: compressed by table membership. */
    uint16_t title = load_bitmap("TITLE.BIT"), wd = load_bitmap("WD.BIT"), pic = load_picture("ULTIMA.16");
    int w = 0, h = 0;
    check(title && wd && pic, "TITLE.BIT (compressed), WD.BIT (raw), ULTIMA.16 load");
    check(picture_frame_size(pic, 0, &w, &h) && w == 319 && h == 61, "ULTIMA.16 frame 0 is 319 x 61");
    dos_free_mem2(title); dos_free_mem2(wd); dos_free_mem(pic);

    /* 1. Title sequence, title screen, reveal, then the demo, with no key pressed. */
    static const long at1[] = {30, 75, 150, 215, 330, 420, 600, 900, 1300, 0};
    static const char *const names1[] = {"1_logo", "2_presents", "3_signature", "4_british", "5_title", "6_reveal", "7_demo_a", "8_demo_b", "9_demo_c"};
    memset(keys, 0, sizeof keys);
    platform_script_keys(keys, 40000);
    base_ticks = platform_script_ticks();
    shot_at = at1;
    shot_name = names1;
    int r = intro_main(0);
    printf("title + demo ran %ld ticks, location 0x%02X\n", platform_script_ticks() - base_ticks, dg.location);
    check(r == 0 && dg.location == 0x42, "the idle title sequence ends in the demo");
    shot_at = NULL;

    /* 2. Menu: U (introduction), 21 pages, back to the menu, then quit. */
    int n = 0;
    keys[n++] = 'U';
    for (int i = 0; i < 22; i++) { keys[n++] = 0; keys[n++] = ' '; }
    platform_script_keys(keys, n);
    static const long at2[] = {3, 0};
    static const char *const names2[] = {"menu"};
    base_ticks = platform_script_ticks();
    shot_at = at2;
    shot_name = names2;
    dg.location = 0x40;
    intro_main(1);
    shot("story_end");
    shot_at = NULL;

    /* Story pages on their own, to look at them. */
    uint16_t font = load_bitmap("PROPORT.PCS");
    check(font != 0, "PROPORT.PCS");
    dos_free_mem2(font);
    for (int page = 0; page < 3; page++) {
        int k2[64], m = 0;
        for (int i = 0; i < page; i++) { k2[m++] = 0; k2[m++] = ' '; }
        for (int i = 0; i < 4; i++) k2[m++] = 0;
        platform_script_keys(k2, m);   /* runs out while page `page` waits: it is in the back page */
        define_text_window(0x18, 0x27, 0, 0, 0);
        select_text_window(0);
        intro_story();
        char nm[32];
        snprintf(nm, sizeof nm, "story_page%d", page);
        memcpy(video_fb, snap, sizeof video_fb);
        check(lit(0, 0, 319, 199) > 3000, nm);
        shot(nm);
    }

    /* 3. Create a character: name, M, then A for every question. */
    n = 0;
    keys[n++] = 'C';
    for (const char *s = "Tester"; *s; s++) keys[n++] = *s;
    keys[n++] = 0x0D;
    keys[n++] = 'M';
    keys[n++] = ' ';
    for (int i = 0; i < 7; i++) keys[n++] = 'A';
    keys[n++] = ' ';
    keys[n++] = 'J';   /* then Journey Onward with the new character */
    platform_script_keys(keys, n);
    dg.location = 0x40;
    r = intro_main(1);
    check(r == 1, "Journey Onward after creating a character");
    PartyMember *a = &dg.party[0];
    printf("new Avatar: %s sex %02X str %u dex %u int %u mp %u, party size %u, location %u\n", a->name, a->sex,
           a->str, a->dex, a->intel, a->mp, dg.party_size, dg.location);
    check(!strcmp(a->name, "Tester") && a->sex == 0x0B, "name and sex");
    check(a->str + a->dex + a->intel > 45 && a->str >= 20 && a->mp == a->intel, "virtue answers raised the stats");
    FILE *f = game_fopen("SAVED.GAM", "rb");
    char nm[9] = {0};
    if (f) { fseek(f, 2, SEEK_SET); if (fread(nm, 1, 8, f) != 8) nm[0] = 0; fclose(f); }
    check(!strcmp(nm, "Tester"), "SAVED.GAM written to the save directory");
    shot("journey");

    printf("%d failure(s)\n", fails);
    return fails != 0;
}
