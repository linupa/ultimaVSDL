/* endings.c - BLCKTHRN.OVL and ENDGAME.OVL. See endings.h. */
#include <stdio.h>
#include <string.h>
#include "commands.h"
#include "data.h"
#include "sound.h"
#include "endings.h"
#include "gfx.h"
#include "input.h"
#include "map.h"
#include "misc.h"
#include "objects.h"
#include "picture.h"
#include "stats.h"
#include "status.h"
#include "story.h"
#include "text.h"
#include "tiles.h"
#include "town.h"
#include "video.h"
#include "view.h"

#define TEXT_BUFFER ((uint16_t)offsetof(struct DGroup, text_buffer))
#define T(off)      ((const char *)ds_at(TEXT_BUFFER + (off)))   /* text loaded from a .DAT file */
#define S(off)      ds_str(off)

static int key(void)   /* a key, or Return once the window is closed */
{
    int k = get_command_key();
    return platform_quit_requested() ? 0x0D : k;
}

static int yes_no(uint16_t yes_text, uint16_t no_text)
{
    for (;;) {
        int k = key();
        if (platform_quit_requested()) k = 'N';
        if (k == 'N') { print_text(S(no_text)); return 'N'; }
        if (k == 'Y') { print_text(S(yes_text)); return 'Y'; }
    }
}

/* The scene map: rows of 16 bytes (as loaded from MISCMAPS.DAT into view_tiles) to local_map's
 * rows of 32. */
static void load_scene(uint32_t offset, const char *file)
{
    load_file_part(offset, 0xB0, dg.view_tiles, file);
    for (int r = 0; r < 11; r++) memcpy(&dg.local_map[r * 0x20], &dg.view_tiles[r * 0x10], 11);
}

static uint8_t class_tile(uint8_t cls) { return ds_u8((uint16_t)(0x1ADE + str_index(cls, S(0x701A)))); }   /* "AMBFDTPRS" */

/* ---------------------------------------------------------------- main program helpers */

void flash_square(int y, int x)
{
    if (dg.location < 0x80) {
        x -= dg.party_x - 5;
        y -= dg.party_y - 5;
    }
    draw_tile_at(y, x, 0);
    sound_noise(2000, 3000, 10);
    world_tick();
}

/* The view (pixels 8-183) is copied to the back page, where 5-row strips are shifted 2 pixels
 * down, shown, then shifted back up, eight times, while the speaker rumbles at a new random
 * pitch (19-150 Hz) for every strip. The original is paced only by the time the scrolling takes;
 * the port gives each strip a little over a millisecond. */
static void shake_rumble(void)
{
    speaker_on((unsigned)random_between(0x96, 0x13));
    sound_crackle(1200);
}

void screen_shake(void)
{
    copy_page(1, 0);
    set_draw_page(1);
    for (int n = 0; n < 8; n++) {
        for (int y = 8; y < 0xB4; y += 3) {
            video_scroll_rect(8, y, 0xB7, y + 4, 2);
            shake_rumble();
        }
        for (int y = 8; y < 0xB4; y += 3) {
            copy_page_rect(y + 4, 0xB7, y, 8, 0, 1);
            shake_rumble();
        }
        delay_loop(1, 1);
        for (int y = 0xB3; y > 7; y -= 3) {
            video_scroll_rect(8, y, 0xB7, y + 4, -2);
            shake_rumble();
        }
        for (int y = 0xB3; y > 7; y -= 3) {
            copy_page_rect(y + 4, 0xB7, y, 8, 0, 1);
            shake_rumble();
        }
        delay_loop(1, 1);
    }
    set_draw_page(0);
    speaker_off();
}

int str_index(uint8_t c, const char *s)
{
    int i = 0;
    while (s[i] != 0 && (uint8_t)s[i] != c) i++;
    return i;
}

/* ---------------------------------------------------------------- BLCKTHRN: cutscenes */

void cutscene_wait(int n)
{
    for (; n > 0; n--) {
        step_sound();
        wait_turns(2);
    }
}

/* Actor in the high bits: 0x10 / 0x14 the two guards (objects 6, 7), 0x18 Blackthorn (8),
 * 0x1C the Avatar (0), 0x20 the companion (1); low 2 bits: 0 north, 1 east, 2 south, 3 west. */
int cutscene_actor(unsigned cmd)
{
    static const int8_t dx[] = {0, 1, 0, -1}, dy[] = {-1, 0, 1, 0};
    int slot = 0;
    switch (cmd & 0xFC) {
    case 0x10: slot = 6; break;
    case 0x14: slot = 7; break;
    case 0x18: slot = 8; break;
    case 0x1C: slot = 0; break;
    case 0x20: slot = 1; break;
    }
    dg.dir_x = (uint16_t)dx[cmd & 3];
    dg.dir_y = (uint16_t)dy[cmd & 3];
    return slot;
}

/* Script bytes: 0 end; 1 the next move is two actors together; 2 n = repeat the next move n
 * times; 3 / 4 show each step (default) / move at once; 5 n = wait n turns; 6 tile x y = change
 * the scene; 7 redraw; 8 wait; 9 slot = remove an actor; 0x10 and up = move an actor one step. */
void run_cutscene(uint16_t script)
{
    const uint8_t *p = ds_ptr(script);
    int animate = 1;
    for (;;) {
        unsigned repeat = 1, cmd;
        int pair = 0;
        for (;;) {
            cmd = *p++;
            if (cmd == 0) return;
            if (cmd > 0x0F) break;
            switch (cmd) {
            case 1: pair = 1; break;
            case 2: repeat = *p++; break;
            case 3: animate = 1; break;
            case 4: animate = 0; break;
            case 5: wait_turns(*p++); break;
            case 6: dg.local_map[(p[1] + p[2] * 0x20) % sizeof dg.local_map] = p[0]; p += 3; break;
            case 7: world_tick(); break;
            case 8: cutscene_wait((int)repeat); repeat = 1; break;
            case 9: dg.objects[*p & 31].tile = dg.objects[*p & 31].tile2 = 0; p++; break;
            }
            if (platform_quit_requested()) return;
        }
        int a = cutscene_actor(cmd), b = 0;
        int8_t ax = (int8_t)dg.dir_x, ay = (int8_t)dg.dir_y, bx = 0, by = 0;
        if (pair) {
            b = cutscene_actor(*p++);
            bx = (int8_t)dg.dir_x;
            by = (int8_t)dg.dir_y;
        }
        for (; repeat != 0; repeat--) {
            dg.objects[a].x = (uint8_t)(dg.objects[a].x + ax);
            dg.objects[a].y = (uint8_t)(dg.objects[a].y + ay);
            if (pair) {
                dg.objects[b].x = (uint8_t)(dg.objects[b].x + bx);
                dg.objects[b].y = (uint8_t)(dg.objects[b].y + by);
            }
            if (animate) cutscene_wait(1);
        }
    }
}

/* ---------------------------------------------------------------- BLCKTHRN: interrogation */

/* MISCMSG.DAT holds Blackthorn's lines; three ever harsher demands for the shrine's mantra. */
void blackthorn_demand(int shrine, int attempt)
{
    static const uint16_t line[] = {0, 0x2D, 0x52}, tail[] = {0x6F6E, 0x6F72, 0x6F76};
    if (attempt >= 0 && attempt <= 2) {
        print_text(T(line[attempt]));
        print_text(S(dg.virtue_names[shrine]));
        print_text(S(tail[attempt]));
    } else if (attempt == 3) {
        print_text(T(0xAC));
    }
}

/* True if the typed line contains the mantra (any case). */
int check_mantra_answer(int shrine)
{
    char answer[18];
    print_text(S(0x6F7A));   /* "Your response?" */
    read_line(0x0E, answer);
    print_text(S(0x6F8E));
    const char *mantra = S(dg.mantras[shrine]);
    int mlen = (int)strlen(mantra), alen = (int)strlen(answer);
    for (int at = 0; at <= alen - mlen; at++) {
        int i = 0;
        while (i < mlen && to_upper((uint8_t)mantra[i]) == to_upper((uint8_t)answer[at + i])) i++;
        if (i == mlen) return 1;
    }
    return 0;
}

/* The second living member is killed: moved to the end of the roster and marked gone (0x7F). */
void companion_executed(int say)
{
    print_text(T(say ? 0xF1 : 0x12D));
    wait_turns(10);
    for (int f = 2000; f < 25000; f += 0x32) sound_sweep(0, (unsigned)f, 200, 1, 0xA50);   /* the blade hums up and down */
    for (int f = 25000; f > 2000; f -= 0x32) sound_sweep(0, (unsigned)f, 200, 1, 0xA50);
    flash_square(dg.objects[1].y, dg.objects[1].x);
    dg.objects[1].tile = dg.objects[1].tile2 = 0;
    dg.local_map[0xE5] = 0x80;
    unsigned m = 0, living = 0;
    for (; m < dg.party_size; m++) {
        if (dg.party[m].status != 'D') living++;
        if (living == 2) break;
    }
    PartyMember tmp = dg.party[m];
    if (m < 15) memmove(&dg.party[m], &dg.party[m + 1], (15 - m) * sizeof tmp);
    dg.party[15] = tmp;
    dg.party[15].where = 0x7F;
    dg.party_size--;
    draw_status_bar();
    if (say) {
        print_text(S(0x6F92));
        print_text(dg.party[15].name);
        print_text(S(0x6F96));   /* " is sliced in half! " */
        key();
        print_text(T(0x1A0));
    }
    put_char('\n');
}

void blackthorn_release(void)
{
    key();
    run_cutscene(0x369E);
}

void blackthorn_threat(void)
{
    print_text(T(0x229));
    run_cutscene(0x36DA);
    print_text(T(0x25F));
    print_text(dg.party[1].name);
    print_text(S(0x6FAC));   /* " die!" " */
    key();
    print_text(S(0x6FB4));
}

/* Giving the mantra destroys that shrine and costs 5 karma (and, with companions, one of them
 * anyway). Refusing: a threat, then the torture device advances each time, and the fourth
 * refusal kills the companion. Alone, a refusal ends it at once. */
void blackthorn_interrogation(int alive, int shrine)
{
    int threatened = 0;
    print_text(S(0x6FB8));
    for (int attempt = 0; attempt <= 3; attempt++) {
        blackthorn_demand(shrine, attempt);
        if (check_mantra_answer(shrine)) {
            dg.shrine_state[shrine] = 0xFF;
            sub_byte_floor0(5, &dg.karma);
            if (alive > 1) companion_executed(0);
            else print_text(T(0x29C));
            blackthorn_release();
            return;
        }
        if (alive < 2) {
            print_text(T(0x2DC));
            blackthorn_release();
            return;
        }
        if (!threatened) {
            threatened = 1;
            blackthorn_threat();
        } else {
            pass_time(2);
            draw_status_bar();
            if (attempt == 0) dg.local_map[0x125] = 0xEA;
            else if (attempt == 1) dg.local_map[0x125] = 0xEB;
            else if (attempt == 2) dg.local_map[0x125] = 0xE8;
            else companion_executed(1);
        }
        if (platform_quit_requested()) return;
    }
}

void captured_by_blackthorn(void)
{
    int alive = 0, shrine;
    for (unsigned i = 0; i < dg.party_size; i++) alive += dg.party[i].status != 'D';
    dg.transport = 0x1C;
    world_tick();
    print_text(S(0x6FBC));   /* "Thou art subdued and blindfolded!" */
    for (shrine = 0; shrine < 8; shrine++)
        if (dg.shrine_state[shrine] == 0) break;
    if (shrine < 8) {   /* a shrine is still whole: Blackthorn wants its mantra */
        wait_turns(2);
        set_draw_color(0);
        fill_rect(0xB7, 0xB7, 8, 8);
        dg.z_level = 0xFF;
        for (int i = 5; i != 0; i--) {
            delay_ticks(5);
            step_sound();
        }
        print_text(S(0x6FE0));   /* "Strong guards drag thee away!" */
        for (int i = 0; i < (dg.sound_enabled == 0 ? 3 : 0x12); i++) {   /* a long march, short with sound off */
            delay_ticks(5);
            step_sound();
        }
        for (int i = 0; i < 0x20; i++) set_object_entry(i, 0, 0, 0, 0, 0, 0);
        dg.location = 0xFF;
        dg.combat_turn = 0xFF;
        load_scene(0, S(0x7000));                                      /* MISCMAPS.DAT: the torture chamber */
        load_file_part(0, 1000, ds_ptr(TEXT_BUFFER), S(0x700E));      /* MISCMSG.DAT */
        for (int i = 0; i < alive; i++) {   /* where each prisoner stands, by party size */
            unsigned pos = ds_u8((uint16_t)(0x1F0A + alive * 8 + i));
            MapObject *o = &dg.objects[i];
            o->x = ds_u8((uint16_t)(0x1F42 + pos));
            o->y = ds_u8((uint16_t)(0x1F48 + pos));
            o->tile = o->tile2 = class_tile((uint8_t)dg.party[i].cls);
        }
        wait_turns(0x10);
        print_text(S(0x7024));   /* "Thou hast been chained and manacled!" */
        wait_turns(0x32);
        print_text(S(0x704C));   /* "Footsteps!" */
        cutscene_wait(8);
        set_object_entry(6, 0, 0, 10, 4, 0x70, 0x70);
        set_object_entry(7, 0, 0, 10, 6, 0x70, 0x70);
        run_cutscene(0x3702);
        sound_sweep(5, 100, 13000, 1, 0xAF0);
        set_object_entry(8, 0, 0, 5, 5, 0x16, 0x16);
        dissolve_tile(5, 5, 0x178);   /* Blackthorn appears */
        set_object_entry(8, 0, 0, 5, 5, 0x78, 0x78);
        wait_turns(8);
        print_text(S(0x705A));   /* Blackthorn says: "Ah, */
        print_text(dg.party[0].name);
        print_text(S(0x7074));   /* 'Tis indeed an honour to meet thee at last! */
        key();
        print_text(S(0x70A4));   /* GUARD! Release this good */
        if (dg.party[0].sex == 0x0C) print_text(S(0x70C0));
        else if (dg.party[0].sex == 0x0B) print_text(S(0x70C8));
        print_text(S(0x70CE));   /* at once!" */
        run_cutscene(0x370E);
        print_text(T(0x32C));
        key();
        blackthorn_interrogation(alive, shrine);
        if (dg.objects[8].tile != 0) run_cutscene(0x3716);
    }
    dg.z_level = 0xFF;
    dg.party_x = 10;
    dg.party_y = 7;
    dg.keys = 0;
    dg.location = 0x12;
}

/* Everyone is dead: darkness, a voice, three figures appear (0x5E, 0x5F and Lord British's
 * shade 0x174), a judgement of the party's karma (KARMA.DAT), and all are raised again in Lord
 * British's castle at 6:00, with karma at least 75 and some food. */
void party_death(void)
{
    uint8_t was = dg.location;
    dg.transport = 0x1E;
    if (dg.location < 0x21) world_tick();
    delay_ticks(10);
    dg.location = 0xFF;
    dg.combat_turn = 0xFF;
    print_text(S(0x70E2));   /* "An unending darkness engulfs thee..." */
    set_draw_page(1);
    set_draw_color(0);
    fill_rect(0xB7, 0xB7, 8, 8);
    set_draw_page(0);
    copy_back_rect(0xB7, 0xB7, 8, 8);
    for (int r = 0; r < 11; r++) memset(&dg.local_map[r * 0x20], 0xFF, 11);
    for (int i = 0; i < 0x20; i++) set_object_entry(i, 0, 0, 0, 0, 0, 0);
    world_tick();
    print_text(S(0x7108));   /* "Thou hast found refuge." */
    delay_ticks(0x0E);
    print_text(S(0x7122));   /* "No evil lives here, only peace and darkness." */
    delay_ticks(0x1C);
    print_text(S(0x7152));   /* "But thy slumber is disturbed!" */
    dg.local_map[0xA5] = 0;
    dg.objects[0].tile = dg.objects[0].tile2 = 0x1C;
    dg.objects[0].x = dg.objects[0].y = 5;
    world_tick();
    for (int i = 0; i < 6; i++)   /* six notes */
        sound_sweep((int16_t)ds_u16((uint16_t)(0x3744 + i * 2)), ds_u16((uint16_t)(0x3738 + i * 2)), (int16_t)ds_u16((uint16_t)(0x372C + i * 2)), 1, ds_u16((uint16_t)(0x3720 + i * 2)));
    print_text(S(0x7172));   /* Someone shouts "FORTIS FORTUNA AVENTARI" */
    delay_ticks(6);
    dg.objects[1].tile = dg.objects[1].tile2 = 0x16;
    dg.objects[1].x = 2;
    dg.objects[1].y = 7;
    dg.local_map[0xE2] = 0;
    dissolve_tile(7, 2, 0x5E);
    dg.local_map[0xE2] = 0x5E;
    dg.objects[1].tile = 0;
    world_tick();
    delay_ticks(4);
    dg.objects[1].tile = 0x16;
    dg.objects[1].x = 8;
    dg.local_map[0xE8] = 0;
    dissolve_tile(7, 8, 0x5F);
    dg.local_map[0xE8] = 0x5F;
    dg.objects[1].tile = 0;
    world_tick();
    delay_ticks(4);
    print_text(S(0x719E));   /* "There is a peal of thunder!" */
    screen_shake();
    screen_shake();
    dg.local_map[0x45] = 0;
    dg.objects[1].tile = dg.objects[1].tile2 = 0x16;
    dg.objects[1].x = 5;
    dg.objects[1].y = 2;
    dissolve_tile(2, 5, 0x174);
    dg.objects[1].tile = dg.objects[1].tile2 = 0x74;
    world_tick();
    print_text(S(0x71BE));
    load_file_part(ds_u16((uint16_t)(0x1A74 + (dg.karma / 0x14) * 2)), 2000, ds_ptr(TEXT_BUFFER), S(0x71C2));   /* KARMA.DAT */
    print_text(T(0));
    put_char('"');
    key();
    print_text(S(0x71CC));   /* "Strange words are intoned." */
    delay_ticks(4);
    for (unsigned i = 0; i < dg.party_size; i++) {
        sound_sweep(2, 2000, 30000, 1, 0x8E30u / (i + 7));   /* a lower note for each */
        resurrect_member(0xFF, (int)i);
        dg.party[i].hp = dg.party[i].max_hp;
        draw_status_bar();
    }
    print_text(S(0x71EA));   /* "Vertigo..." */
    delay_ticks(4);
    set_draw_page(1);
    set_draw_color(0);
    fill_rect(0xB7, 0xB7, 8, 8);
    draw_tile_at(5, 5, 0x11C);
    set_draw_page(0);
    copy_back_rect(0xB7, 0xB7, 8, 8);
    if (dg.karma < 0x4B) dg.karma = 0x4B;
    dg.location = 0x11;
    dg.z_level = 1;
    dg.transport = 0x1C;
    dg.party_x = dg.party_y = 10;
    dg.active_spell_turns = 0;
    dg.active_spell = 0;
    while (dg.hour != 6) pass_time(9);
    dg.light_spell = 0;
    dg.torch_light = 0;
    if (dg.food == 0) dg.food = 0x3F;
    if (was != 0 && was < 0x21) enter_town(1);
}

/* ---------------------------------------------------------------- ENDGAME */

/* Six pages like the introduction's (tables at ds:3DA6-3E06, text in END.DAT), then ENDSC.16. */
void ending_pictures(void)
{
    uint16_t font = load_bitmap(S(ds_u16(0x25EA)));
    uint16_t titles = load_picture(S(dg.story_bg_pic));
    uint16_t picture = load_picture(S(dg.ending_pics[0]));
    unsigned set = 0x31;
    copy_back_rect(199, 0x13F, 0, 0);
    select_text_window(0);
    for (int page = 0; page < 6; page++) {
        if (ds_u8((uint16_t)(0x3DF4 + page)) != set) {
            dos_free_mem(picture);
            set = ds_u8((uint16_t)(0x3DF4 + page));
            picture = load_picture(S(dg.ending_pics[set & 1]));
        }
        set_draw_page(1);
        put_char(0xFF);
        if (ds_u8((uint16_t)(0x3E06 + page)) == 1) {   /* a chapter title */
            if (page == 0) {
                draw_picture_frame(0, 0, 0xD8, 0, titles);
                draw_picture_frame(0, 0x1C, 0x98, 4, titles);
            } else if (page == 3) {
                draw_picture_frame(0, 0, 0xE0, 5, titles);
                draw_picture_frame(0, 0, 0xB0, 0, titles);
            }
        }
        draw_picture_frame(0, ds_u8((uint16_t)(0x3E00 + page)), ds_u8((uint16_t)(0x3DFA + page)),
                           ds_u8((uint16_t)(0x3DEE + page)), picture);
        ds_set_u16(STORY_LEFT(0), ds_u8((uint16_t)(0x3DA6 + page * 2)));
        ds_set_u16(STORY_LEFT(1), ds_u8((uint16_t)(0x3DA7 + page * 2)));
        ds_set_u16(STORY_RIGHT(0), ds_u16((uint16_t)(0x3DB2 + page * 4)));
        ds_set_u16(STORY_RIGHT(1), ds_u16((uint16_t)(0x3DB4 + page * 4)));
        dg.story_line_min = ds_u8((uint16_t)(0x3DD6 + page));
        dg.story_line_max = ds_u8((uint16_t)(0x3DDC + page));
        dg.story_x = ds_u8((uint16_t)(0x3DE2 + page));
        dg.story_y = ds_u8((uint16_t)(0x3DE8 + page));
        load_file_part(ds_u16((uint16_t)(0x3DCA + page * 2)), 2000, ds_ptr(TEXT_BUFFER), S(0x81FE));   /* END.DAT */
        draw_story_text(ds_at(TEXT_BUFFER), font);
        if (page != 0) wait_any_key();
        copy_page(0, 1);
        if (platform_quit_requested()) break;
    }
    dos_free_mem(picture);
    dos_free_mem(titles);
    dos_free_mem2(font);
    wait_any_key();
    set_draw_page(1);
    put_char(0xFF);
    uint16_t last = load_picture(S(ds_u16(0x2622)));   /* ENDSC.16 */
    draw_picture_frame(0, 0, 0x28, 0, last);
    dos_free_mem(last);
    copy_page(0, 1);
    set_draw_page(0);
}

/* Words are collected into a line (39 columns at most) and the line is printed, centred, when
 * it ends with a new line. */
void print_ending_word(const char *word)
{
    char *line = (char *)dg.proclamation_line;
    for (; *word != 0 && dg.proclamation_col < 0x27; dg.proclamation_col++) line[dg.proclamation_col] = *word++;
    line[dg.proclamation_col] = 0;
    if (dg.proclamation_col > 0 && line[dg.proclamation_col - 1] == '\n') {
        print_text(line);
        dg.proclamation_col = 0;
    }
}

void print_number_words(int n)
{
    if (n > 0x14) {
        print_ending_word(S(ds_u16((uint16_t)(0x3E2E + (n / 10) * 2))));
        n %= 10;
        if (n == 0) return;
        print_ending_word(S(0x82C6));   /* "-" */
    }
    print_ending_word(S(ds_u16((uint16_t)(0x3E0A + n * 2))));
}

void print_ordinal_words(int n)
{
    if (n < 0x0D) {
        print_ending_word(S(ds_u16((uint16_t)(0x3E40 + n * 2))));
    } else if (n < 0x14) {
        print_number_words(n);
        print_ending_word(S(0x831E));   /* "th" */
    } else {
        print_ending_word(S(0x8322));   /* "Twent" */
        if (n == 0x14) {
            print_ending_word(S(0x8328));   /* "ieth" */
        } else {
            print_ending_word(S(0x832E));   /* "y-" */
            print_ending_word(S(ds_u16((uint16_t)(0x3E18 + n * 2))));
        }
    }
}

/* "Be it known that on the <day> Day of the <month> Month of the Year <year> <name> the Avatar
 * saved the life of our sovereign ..." and how long the quest took, counted from 4-5-139. */
void print_proclamation(void)
{
    char num[8];
    set_cursor(1, 0);
    put_char(0xFD);
    put_char(0xFC);
    print_text(S(0x8332));   /* "Be it known that on" */
    dg.proclamation_col = 0;
    print_ending_word(S(0x8348));
    print_ordinal_words(dg.day);
    print_ending_word(S(0x834E));   /* " Day of" */
    print_ending_word(S(0x8358));
    print_ordinal_words(dg.month);
    print_ending_word(S(0x835E));   /* " Month" */
    print_text(S(0x8366));          /* "of the Year" */
    print_number_words((int16_t)dg.year / 100);
    print_ending_word(S(0x8374));   /* " Hundred" */
    print_number_words((int16_t)dg.year % 100);
    print_ending_word(S(0x837E));
    print_ending_word(dg.party[0].name);
    print_ending_word(S(0x8382));   /* " the Avatar" */
    print_text(S(0x8390));          /* "saved the life" */
    print_text(S(0x83A0));          /* "of our sovereign" */
    print_text(S(0x83B2));          /* "Lord British, thereby" */
    print_text(S(0x83CA));          /* "saving our people" */
    print_text(S(0x83DE));          /* "and our land." */
    select_font(1);
    print_text(S(0x83EE));          /* runes: THE QUEST OF THE AVATAR */
    print_text(S(0x8404));          /* IS FOREVER */
    select_font(0);
    put_char(0xFD);
    print_text(S(0x8414));          /* "Report now, thy Quest compleat in" */
    int years = (int16_t)dg.year - 0x8B, months = dg.month - 4, days = dg.day - 5;
    if (days < 0) { days = dg.day + 0x17; months = dg.month - 5; }
    if (months < 0) { months += 0x0D; years = (int16_t)dg.year - 0x8C; }
    if (years != 0) {
        snprintf(num, sizeof num, "%d", years);
        print_ending_word(num);
        print_ending_word(S(0x8438));   /* " year" */
        if (years > 1) print_ending_word(S(0x843E));
        if (months != 0 || days != 0) print_ending_word(S(0x8440));
    }
    if (months != 0) {
        snprintf(num, sizeof num, "%d", months);
        print_ending_word(num);
        print_ending_word(S(0x8444));   /* " month" */
        if (months > 1) print_ending_word(S(0x844C));
        if (days != 0) print_ending_word(S(0x844E));
    }
    if (days != 0) {
        snprintf(num, sizeof num, "%d", days);
        print_ending_word(num);
        print_ending_word(S(0x8452));   /* " day" */
        if (days > 1) print_ending_word(S(0x8458));
    }
    print_ending_word(S(0x845A));
    print_text(S(0x845C));          /* "to Lord British at Origin Systems" */
    while (!platform_quit_requested()) {   /* the original stops here for good */
        poll_key();
        delay_ticks(1);
    }
}

void ending_tick(void)
{
    wait_turns(2);
    step_sound();
    wait_turns(3);
}

/* One step toward (x, y), along the longer axis first. */
int walk_ending_actor(unsigned y, unsigned x, int slot)
{
    MapObject *o = &dg.objects[slot];
    if (o->tile == 0 || (o->x == x && o->y == y) || platform_quit_requested()) return 0;
    int dx = o->x - (int)x, dy = o->y - (int)y;
    if ((dx < 0 ? -dx : dx) < (dy < 0 ? -dy : dy)) o->y = (uint8_t)(o->y + (y < o->y ? -1 : 1));
    else o->x = (uint8_t)(o->x + (x < o->x ? -1 : 1));
    ending_tick();
    return 1;
}

/* Half the time, a random step onto floor (0x44). */
void ending_actor_leave(int slot)
{
    MapObject *o = &dg.objects[slot];
    if (o->tile != 0 && random_between(1, 0) != 0) {
        for (int tries = 0; tries < 8; tries++) {
            unsigned x = o->x, y = o->y;
            switch (random_between(3, 0)) {
            case 0: x++; break;
            case 1: x--; break;
            case 2: y++; break;
            case 3: y--; break;
            }
            if (*ds_at(offsetof(struct DGroup, view_mask) + y * 0x20 + x) == 0x44) {
                o->x = (uint8_t)x;
                o->y = (uint8_t)y;
                break;
            }
        }
    }
    wait_turns(1);
}

/* Lord British (0x7C) steps out of the mirror; the party gathers, the dead are raised. With
 * the sandalwood box and a "yes", he speaks, opens a moongate home, and the ending follows;
 * otherwise everyone just drifts away, forever. */
void rescue_lord_british(void)
{
    dg.combat_turn = 0xFF;
    draw_status_bar();
    patch_tiles(1);
    load_scene(0x210, S(0x8480));                                  /* MISCMAPS.DAT */
    load_file_part(0, 1000, ds_ptr(TEXT_BUFFER), S(0x848E));      /* ENDMSG.DAT */
    for (int i = 0; i < 32; i++) dg.objects[i].tile = dg.objects[i].tile2 = 0;
    MapObject *lb = &dg.objects[0x1F];
    lb->tile = lb->tile2 = 0x7C;
    lb->x = 5;
    lb->y = 8;
    lb->flags = 0;
    wait_turns(0x28);
    while (walk_ending_actor(3, 5, 0x1F)) {}
    for (unsigned i = 0; i < dg.party_size; i++) {
        PartyMember *p = &dg.party[i];
        if (p->status == 'D') {
            put_char('\n');
            print_text(p->name);
            print_text(S(0x849A));   /* " lives!" */
            p->status = 'G';
            p->hp = p->max_hp;
            set_draw_color(dg.color_text);
            xor_fill_rect(0xB7, 0xB7, 8, 8);
            sound_sweep(1, 5000, -0x63C0, 1, 0x2260);
            draw_status_bar();
        }
        MapObject *o = &dg.objects[i];
        o->tile = o->tile2 = ds_u8((uint16_t)(0x1ADE + str_index((uint8_t)p->cls, S(0x84A4))));
        o->x = 5;
        o->y = 9;
        o->flags = 0;
        wait_turns(1);
        while (walk_ending_actor(ds_u8((uint16_t)(0x3E60 + i)), ds_u8((uint16_t)(0x3E5A + i)), (int)i)) {}
    }
    wait_turns(0x28);
    print_text(T(0));
    print_text(dg.party[0].name);
    print_text(S(0x84AE));
    key();
    print_text(T(0x21));
    int answer = yes_no(0x84B4, 0x84BA);
    if (answer == 'N') {
        print_text(T(0x49));
        answer = yes_no(0x84C0, 0x84C6);
    }
    if (answer == 'Y' && dg.sandalwood_box != 0) {
        wait_turns(8);
        while (walk_ending_actor(4, 5, 0)) {}
        while (walk_ending_actor(5, 5, 0)) {}
        wait_turns(4);
        MapObject *box = &dg.objects[6];
        box->tile = box->tile2 = 0x0E;
        box->x = 5;
        box->y = 4;
        box->flags = 0;
        print_text(T(0xAB));
        wait_turns(0x28);
        print_text(S(0x84CC));   /* "He says:" */
        static const uint16_t speech[] = {0xD3, 0x128, 0x167, 0x1C9, 0x211};
        key();
        for (unsigned i = 0; i < 5; i++) {
            print_text(T(speech[i]));
            key();
        }
        print_text(T(0x24B));
        box->tile = box->tile2 = 8;
        key();
        sound_sweep(1, 10000, -0x3CB0, 1, 0x1450);
        box->tile = box->tile2 = 0;
        dg.local_map[0x85] = 0xDC;   /* a moongate rises */
        for (dg.moongate_phase = 1; dg.moongate_phase < 0x10; dg.moongate_phase++) wait_turns(1);
        wait_turns(4);
        while (walk_ending_actor(4, 5, 0x1F)) {}
        lb->tile = lb->tile2 = 0;
        wait_turns(1);
        for (unsigned i = 0; i < dg.party_size; i++) {
            while (walk_ending_actor(4, 5, (int)i)) {}
            dg.objects[i].tile = dg.objects[i].tile2 = 0;
            wait_turns(1);
        }
        for (dg.moongate_phase = 0x0F; dg.moongate_phase != 0; dg.moongate_phase--) wait_turns(1);
        draw_tile_at(4, 5, 0x44);
        set_draw_page(1);
        set_draw_color(0);
        fill_rect(199, 0x13F, 0, 0);
        set_draw_page(0);
        ending_pictures();
        print_proclamation();
        return;
    }
    print_text(S(0x84DA));   /* "I see..." */
    wait_turns(0x28);
    print_text(T(0x2D5));
    dg.objects[0].y--;
    ending_tick();
    while (walk_ending_actor(6, 8, 2) + walk_ending_actor(1, 4, 0x1F) + walk_ending_actor(4, 8, 0) != 0) {}
    while (!platform_quit_requested()) {   /* the original loops here for good */
        ending_actor_leave(1);
        ending_actor_leave(3);
        ending_actor_leave(4);
        ending_actor_leave(5);
    }
}
