/* endings.cpp - BLCKTHRN.OVL and ENDGAME.OVL. See endings.h. */
#include <stdio.h>
#include <string.h>
#include "combat.h"
#include "commands.h"
#include "gamedata.h"
#include "gametime.h"
#include "intro.h"
#include "map.h"
#include "party.h"
#include "pictures.h"
#include "sound.h"
#include "endings.h"
#include "gfx.h"
#include "input.h"
#include "misc.h"
#include "objects.h"
#include "picture.h"
#include "stats.h"
#include "status.h"
#include "story.h"
#include "strings.h"
#include "text.h"
#include "textbuf.h"
#include "tiles.h"
#include "town.h"
#include "video.h"
#include "view.h"
#include "world.h"

#define T(off)      ((const char *)text_buffer.at(off))   /* text loaded from a .DAT file */

Endings &Endings::instance()
{
    static Endings the_endings;
    return the_endings;
}

Endings &endings = Endings::instance();

Endings::Endings()
{
    Data &data = Data::instance();   /* not a global: this runs before main, in no set order */
    /* the last two chapter bytes are also number_words' first entry (0, unused) */
    data.add(0x3DA6, 6 * 17, [](const uint8_t *b, unsigned) { Intro::read_pages(pages_, 6, b, 6, 6); }, "endings.pages", Data::SHARED);
    data.add(0x1F0A, prisoner_places_, "endings.prisoner_places");
    data.add(0x1F42, prisoner_x_, "endings.prisoner_x");
    data.add(0x1F48, prisoner_y_, "endings.prisoner_y");
    data.add(0x369E, cutscene_scripts_, "endings.cutscene_scripts");
    data.add(0x3720, death_notes_, "endings.death_notes");
    data.add(0x3E0A, number_words_, "endings.number_words");
    data.add(0x3E5A, rescue_x_, "endings.rescue_x");
    data.add(0x3E60, rescue_y_, "endings.rescue_y");
}

/* The cut-scene scripts (ds:369E-371F; see run_cutscene). */
uint8_t Endings::cutscene_scripts_[0x82];   /* ds:369E */

/* Where the prisoners stand in Blackthorn's chamber: for each number of the living, which places
 * (ds:1F0A), and the places' squares (ds:1F42, 1F48). */
uint8_t Endings::prisoner_places_[7][8];   /* ds:1F0A */
uint8_t Endings::prisoner_x_[6];   /* ds:1F42 */
uint8_t Endings::prisoner_y_[6];   /* ds:1F48 */

/* The six notes heard at the party's death, as sound_sweep's arguments (ds:3720-374F). */
uint16_t Endings::death_notes_[4][6];   /* ds:3720 */

/* The ending's pages (ds:3DA6-3E0B). The original keeps them field by field - six left pairs,
 * six right pairs, six text offsets, then six of each byte field - and Endings puts them
 * together a page at a time (add_pages). */
Endings::Page Endings::pages_[6];

/* Numbers in words for the proclamation, one table (ds:3E0A): 0-20 "One" ... "Twenty", then from
 * 21 the tens "Thirty" ... "Ninety", then from 28 the ordinals "First" ... "Twelfth". */
Strings::Id Endings::number_words_[40];   /* ds:3E0A */

/* Where each member walks to at the rescue of Lord British (ds:3E5A, 3E60). */
uint8_t Endings::rescue_x_[6];   /* ds:3E5A */
uint8_t Endings::rescue_y_[6];   /* ds:3E60 */

static int key(void)   /* a key, or Return once the window is closed */
{
    int k = get_command_key();
    return platform->quit_requested() ? 0x0D : k;
}

static int yes_no(Strings::Id yes_text, Strings::Id no_text)
{
    for (;;) {
        int k = key();
        if (platform->quit_requested()) k = 'N';
        if (k == 'N') { print_text(strings[no_text]); return 'N'; }
        if (k == 'Y') { print_text(strings[yes_text]); return 'Y'; }
    }
}

/* The scene map: rows of 16 bytes (as loaded from MISCMAPS.DAT into view_tiles) to local_map's
 * rows of 32. */
static void load_scene(uint32_t offset, const char *file)
{
    load_file_part(offset, 0xB0, view.tiles, file);
    for (int r = 0; r < 11; r++) memcpy(&view.local_map[r * 0x20], &view.tiles[r * 0x10], 11);
}

static uint8_t class_tile(uint8_t cls) { return party.class_tile((unsigned)str_index(cls, strings[Strings::AMBFDTPRS_701A])); }   /* "AMBFDTPRS" */

/* ---------------------------------------------------------------- main program helpers */

void flash_square(int y, int x)
{
    if (!map.in_scene()) {
        x -= map.x - 5;
        y -= map.y - 5;
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

void Endings::cutscene_wait(int n)
{
    for (; n > 0; n--) {
        step_sound();
        wait_turns(2);
    }
}

/* Actor in the high bits: 0x10 / 0x14 the two guards (objects 6, 7), 0x18 Blackthorn (8),
 * 0x1C the Avatar (0), 0x20 the companion (1); low 2 bits: 0 north, 1 east, 2 south, 3 west. */
int Endings::cutscene_actor(unsigned cmd, int8_t *step_x, int8_t *step_y)
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
    *step_x = dx[cmd & 3];
    *step_y = dy[cmd & 3];
    return slot;
}

/* Script bytes: 0 end; 1 the next move is two actors together; 2 n = repeat the next move n
 * times; 3 / 4 show each step (default) / move at once; 5 n = wait n turns; 6 tile x y = change
 * the scene; 7 redraw; 8 wait; 9 slot = remove an actor; 0x10 and up = move an actor one step. */
void Endings::run_cutscene(Cutscene script)
{
    const uint8_t *p = cutscene_scripts_ + script;
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
            case 6: view.local_map[(p[1] + p[2] * 0x20) % sizeof view.local_map] = p[0]; p += 3; break;
            case 7: world_tick(); break;
            case 8: cutscene_wait((int)repeat); repeat = 1; break;
            case 9: map.objects[*p & 31].tile = map.objects[*p & 31].tile2 = 0; p++; break;
            }
            if (platform->quit_requested()) return;
        }
        int8_t ax = 0, ay = 0, bx = 0, by = 0;
        int a = cutscene_actor(cmd, &ax, &ay), b = 0;
        if (pair) b = cutscene_actor(*p++, &bx, &by);
        for (; repeat != 0; repeat--) {
            map.objects[a].x = (uint8_t)(map.objects[a].x + ax);
            map.objects[a].y = (uint8_t)(map.objects[a].y + ay);
            if (pair) {
                map.objects[b].x = (uint8_t)(map.objects[b].x + bx);
                map.objects[b].y = (uint8_t)(map.objects[b].y + by);
            }
            if (animate) cutscene_wait(1);
        }
    }
}

/* ---------------------------------------------------------------- BLCKTHRN: interrogation */

/* MISCMSG.DAT holds Blackthorn's lines; three ever harsher demands for the shrine's mantra. */
void Endings::blackthorn_demand(int shrine, int attempt)
{
    static const uint16_t line[] = {0, 0x2D, 0x52};
    static const Strings::Id tail[] = {Strings::Qm_Quote_6F6E, Strings::Qm_Quote_6F72, Strings::Qm_Quote_6F76};
    if (attempt >= 0 && attempt <= 2) {
        print_text(T(line[attempt]));
        print_text(map.virtue_name((unsigned)shrine));
        print_text(strings[tail[attempt]]);
    } else if (attempt == 3) {
        print_text(T(0xAC));
    }
}

/* True if the typed line contains the mantra (any case). */
int Endings::check_mantra_answer(int shrine)
{
    char answer[18];
    print_text(strings[Strings::Your_response_6F7A]);   /* "Your response?" */
    read_line(0x0E, answer);
    print_text(strings[Strings::NL_NL_6F8E]);
    const char *mantra = map.mantra((unsigned)shrine);
    int mlen = (int)strlen(mantra), alen = (int)strlen(answer);
    for (int at = 0; at <= alen - mlen; at++) {
        int i = 0;
        while (i < mlen && to_upper((uint8_t)mantra[i]) == to_upper((uint8_t)answer[at + i])) i++;
        if (i == mlen) return 1;
    }
    return 0;
}

/* The second living member is killed: moved to the end of the roster and marked gone (0x7F). */
void Endings::companion_executed(int say)
{
    print_text(T(say ? 0xF1 : 0x12D));
    wait_turns(10);
    for (int f = 2000; f < 25000; f += 0x32) sound_sweep(0, (unsigned)f, 200, 1, 0xA50);   /* the blade hums up and down */
    for (int f = 25000; f > 2000; f -= 0x32) sound_sweep(0, (unsigned)f, 200, 1, 0xA50);
    flash_square(map.objects[1].y, map.objects[1].x);
    map.objects[1].tile = map.objects[1].tile2 = 0;
    view.local_map[0xE5] = 0x80;
    unsigned m = 0, living = 0;
    for (; m < party.size(); m++) {
        if (!party[m].dead()) living++;
        if (living == 2) break;
    }
    party.move_out(m).where = 0x7F;
    draw_status_bar();
    if (say) {
        print_text(strings[Strings::NL_NL_6F92]);
        print_text(party[15].name);
        print_text(strings[Strings::is_sliced_in_half_6F96]);   /* " is sliced in half! " */
        key();
        print_text(T(0x1A0));
    }
    put_char('\n');
}

void Endings::blackthorn_release(void)
{
    key();
    run_cutscene(SCENE_RELEASE);
}

void Endings::blackthorn_threat(void)
{
    print_text(T(0x229));
    run_cutscene(SCENE_THREAT);
    print_text(T(0x25F));
    print_text(party[1].name);
    print_text(strings[Strings::die_6FAC]);   /* " die!" " */
    key();
    print_text(strings[Strings::NL_NL_6FB4]);
}

/* Giving the mantra destroys that shrine and costs 5 karma (and, with companions, one of them
 * anyway). Refusing: a threat, then the torture device advances each time, and the fourth
 * refusal kills the companion. Alone, a refusal ends it at once. */
void Endings::blackthorn_interrogation(int alive, int shrine)
{
    int threatened = 0;
    print_text(strings[Strings::NL_NL_6FB8]);
    for (int attempt = 0; attempt <= 3; attempt++) {
        blackthorn_demand(shrine, attempt);
        if (check_mantra_answer(shrine)) {
            map.shrine_state[shrine] = 0xFF;
            sub_byte_floor0(5, &party.karma);
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
            if (attempt == 0) view.local_map[0x125] = 0xEA;
            else if (attempt == 1) view.local_map[0x125] = 0xEB;
            else if (attempt == 2) view.local_map[0x125] = 0xE8;
            else companion_executed(1);
        }
        if (platform->quit_requested()) return;
    }
}

void Endings::captured_by_blackthorn(void)
{
    int alive = (int)party.living(), shrine;
    party.transport = 0x1C;
    world_tick();
    print_text(strings[Strings::Thou_art_subdued_and_6FBC]);   /* "Thou art subdued and blindfolded!" */
    for (shrine = 0; shrine < 8; shrine++)
        if (map.shrine_state[shrine] == 0) break;
    if (shrine < 8) {   /* a shrine is still whole: Blackthorn wants its mantra */
        wait_turns(2);
        set_draw_color(0);
        fill_rect(0xB7, 0xB7, 8, 8);
        map.z = 0xFF;
        draw_sky_bar();
        set_wind_direction(-1);
        for (int i = 5; i != 0; i--) {
            delay_ticks(5);
            step_sound();
        }
        print_text(strings[Strings::Strong_guards_drag_thee_6FE0]);   /* "Strong guards drag thee away!" */
        for (int i = 0; i < (sound.enabled == 0 ? 3 : 0x12); i++) {   /* a long march, short with sound off */
            delay_ticks(5);
            step_sound();
        }
        for (int i = 0; i < 0x20; i++) set_object_entry(i, 0, 0, 0, 0, 0, 0);
        map.set(Map::SCENE);
        combat.turn = 0xFF;
        load_scene(0, strings[Strings::MISCMAPS_DAT_7000]);                                      /* MISCMAPS.DAT: the torture chamber */
        load_file_part(0, 1000, text_buffer.data, strings[Strings::MISCMSG_DAT_700E]);      /* MISCMSG.DAT */
        for (int i = 0; i < alive; i++) {   /* where each prisoner stands, by party size */
            unsigned pos = prisoner_places_[alive % 7][i & 7];
            MapObject *o = &map.objects[i];
            o->x = prisoner_x_[pos % 6];
            o->y = prisoner_y_[pos % 6];
            o->tile = o->tile2 = class_tile((uint8_t)party[i].cls);
        }
        wait_turns(0x10);
        print_text(strings[Strings::Thou_hast_been_chained_7024]);   /* "Thou hast been chained and manacled!" */
        wait_turns(0x32);
        print_text(strings[Strings::Footsteps_704C]);   /* "Footsteps!" */
        cutscene_wait(8);
        set_object_entry(6, 0, 0, 10, 4, 0x70, 0x70);
        set_object_entry(7, 0, 0, 10, 6, 0x70, 0x70);
        run_cutscene(SCENE_GUARDS_ENTER);
        sound_sweep(5, 100, 13000, 1, 0xAF0);
        set_object_entry(8, 0, 0, 5, 5, 0x16, 0x16);
        dissolve_tile(5, 5, 0x178);   /* Blackthorn appears */
        set_object_entry(8, 0, 0, 5, 5, 0x78, 0x78);
        wait_turns(8);
        print_text(strings[Strings::Blackthorn_says_Ah_705A]);   /* Blackthorn says: "Ah, */
        print_text(party[0].name);
        print_text(strings[Strings::Tis_indeed_an_honour_to_7074]);   /* 'Tis indeed an honour to meet thee at last! */
        key();
        print_text(strings[Strings::GUARD_Release_this_good_70A4]);   /* GUARD! Release this good */
        if (party[0].sex == 0x0C) print_text(strings[Strings::lady_70C0]);
        else if (party[0].sex == 0x0B) print_text(strings[Strings::man_70C8]);
        print_text(strings[Strings::at_once_70CE]);   /* at once!" */
        run_cutscene(SCENE_GUARDS_UNCHAIN);
        print_text(T(0x32C));
        key();
        blackthorn_interrogation(alive, shrine);
        if (map.objects[8].tile != 0) run_cutscene(SCENE_BLACKTHORN_LEAVES);
    }
    map.z = 0xFF;
    draw_sky_bar();
    set_wind_direction(-1);
    map.x = 10;
    map.y = 7;
    party.keys = 0;
    map.set(Map::PALACE_OF_BLACKTHORN);
}

/* Everyone is dead: darkness, a voice, three figures appear (0x5E, 0x5F and Lord British's
 * shade 0x174), a judgement of the party's karma (KARMA.DAT), and all are raised again in Lord
 * British's castle at 6:00, with karma at least 75 and some food. */
void Endings::party_death(void)
{
    uint8_t was = map.id();
    party.transport = 0x1E;
    if (map.on_tile_map()) world_tick();
    delay_ticks(10);
    map.set(Map::SCENE);
    combat.turn = 0xFF;
    print_text(strings[Strings::An_unending_darkness_70E2]);   /* "An unending darkness engulfs thee..." */
    set_draw_page(1);
    set_draw_color(0);
    fill_rect(0xB7, 0xB7, 8, 8);
    set_draw_page(0);
    copy_back_rect(0xB7, 0xB7, 8, 8);
    for (int r = 0; r < 11; r++) memset(&view.local_map[r * 0x20], 0xFF, 11);
    for (int i = 0; i < 0x20; i++) set_object_entry(i, 0, 0, 0, 0, 0, 0);
    world_tick();
    print_text(strings[Strings::Thou_hast_found_refuge_7108]);   /* "Thou hast found refuge." */
    delay_ticks(0x0E);
    print_text(strings[Strings::No_evil_lives_here_only_7122]);   /* "No evil lives here, only peace and darkness." */
    delay_ticks(0x1C);
    print_text(strings[Strings::But_thy_slumber_is_7152]);   /* "But thy slumber is disturbed!" */
    view.local_map[0xA5] = 0;
    map.objects[0].tile = map.objects[0].tile2 = 0x1C;
    map.objects[0].x = map.objects[0].y = 5;
    world_tick();
    for (int i = 0; i < 6; i++)   /* six notes */
        sound_sweep((int16_t)death_notes_[3][i], death_notes_[2][i], (int16_t)death_notes_[1][i], 1, death_notes_[0][i]);
    print_text(strings[Strings::Someone_shouts_FORTIS_7172]);   /* Someone shouts "FORTIS FORTUNA AVENTARI" */
    delay_ticks(6);
    map.objects[1].tile = map.objects[1].tile2 = 0x16;
    map.objects[1].x = 2;
    map.objects[1].y = 7;
    view.local_map[0xE2] = 0;
    dissolve_tile(7, 2, 0x5E);
    view.local_map[0xE2] = 0x5E;
    map.objects[1].tile = 0;
    world_tick();
    delay_ticks(4);
    map.objects[1].tile = 0x16;
    map.objects[1].x = 8;
    view.local_map[0xE8] = 0;
    dissolve_tile(7, 8, 0x5F);
    view.local_map[0xE8] = 0x5F;
    map.objects[1].tile = 0;
    world_tick();
    delay_ticks(4);
    print_text(strings[Strings::There_is_a_peal_of_719E]);   /* "There is a peal of thunder!" */
    screen_shake();
    screen_shake();
    view.local_map[0x45] = 0;
    map.objects[1].tile = map.objects[1].tile2 = 0x16;
    map.objects[1].x = 5;
    map.objects[1].y = 2;
    dissolve_tile(2, 5, 0x174);
    map.objects[1].tile = map.objects[1].tile2 = 0x74;
    world_tick();
    print_text(strings[Strings::NL_Quote_71BE]);
    load_file_part(party.karma_text(party.karma / 0x14), 2000, text_buffer.data, strings[Strings::KARMA_DAT_71C2]);   /* KARMA.DAT */
    print_text(T(0));
    put_char('"');
    key();
    print_text(strings[Strings::Strange_words_are_71CC]);   /* "Strange words are intoned." */
    delay_ticks(4);
    for (unsigned i = 0; i < party.size(); i++) {
        sound_sweep(2, 2000, 30000, 1, 0x8E30u / (i + 7));   /* a lower note for each */
        resurrect_member(0xFF, (int)i);
        party[i].hp = party[i].max_hp;
        draw_status_bar();
    }
    print_text(strings[Strings::Vertigo_71EA]);   /* "Vertigo..." */
    delay_ticks(4);
    set_draw_page(1);
    set_draw_color(0);
    fill_rect(0xB7, 0xB7, 8, 8);
    draw_tile_at(5, 5, 0x11C);
    set_draw_page(0);
    copy_back_rect(0xB7, 0xB7, 8, 8);
    if (party.karma < 0x4B) party.karma = 0x4B;
    map.set(Map::LORD_BRITISHS_CASTLE);
    map.z = 1;
    party.transport = 0x1C;
    map.x = map.y = 10;
    party.active_spell_turns = 0;
    party.active_spell = 0;
    while (game_time.hour != 6) pass_time(9);
    party.light_spell = 0;
    party.torch_light = 0;
    if (party.food == 0) party.food = 0x3F;
    if (was != 0 && was < 0x21) town.enter_town(1);
}

/* ---------------------------------------------------------------- ENDGAME */

/* Six pages like the introduction's (tables at ds:3DA6-3E06, text in END.DAT), then ENDSC.16. */
void Endings::ending_pictures(void)
{
    uint16_t font = load_bitmap(pictures.name(Pictures::STORY_FONT));
    uint16_t titles = load_picture(pictures.name(Pictures::STORY_BACKGROUND));
    uint16_t picture = load_picture(pictures.ending(0));
    unsigned set = 0x31;
    copy_back_rect(199, 0x13F, 0, 0);
    select_text_window(0);
    for (int page = 0; page < 6; page++) {
        const Page &pg = pages_[page];
        if (pg.set != set) {
            dos_free_mem(picture);
            set = pg.set;
            picture = load_picture(pictures.ending(set));
        }
        set_draw_page(1);
        put_char(0xFF);
        if (pg.chapter == 1) {   /* a chapter title */
            if (page == 0) {
                draw_picture_frame(0, 0, 0xD8, 0, titles);
                draw_picture_frame(0, 0x1C, 0x98, 4, titles);
            } else if (page == 3) {
                draw_picture_frame(0, 0, 0xE0, 5, titles);
                draw_picture_frame(0, 0, 0xB0, 0, titles);
            }
        }
        draw_picture_frame(0, pg.py, pg.px, pg.frame, picture);
        intro.story_left[0] = pg.left[0];
        intro.story_left[1] = pg.left[1];
        intro.story_right[0] = pg.right[0];
        intro.story_right[1] = pg.right[1];
        intro.story_line_min = pg.line_min;
        intro.story_line_max = pg.line_max;
        intro.story_x = pg.x;
        intro.story_y = pg.y;
        load_file_part(pg.text, 2000, text_buffer.data, strings[Strings::END_DAT_81FE]);   /* END.DAT */
        intro.draw_story_text(text_buffer.data, font);
        if (page != 0) intro.wait_any_key();
        copy_page(0, 1);
        if (platform->quit_requested()) break;
    }
    dos_free_mem(picture);
    dos_free_mem(titles);
    dos_free_mem2(font);
    intro.wait_any_key();
    set_draw_page(1);
    put_char(0xFF);
    uint16_t last = load_picture(pictures.name(Pictures::END_SCROLL));   /* ENDSC.16 */
    draw_picture_frame(0, 0, 0x28, 0, last);
    dos_free_mem(last);
    copy_page(0, 1);
    set_draw_page(0);
}

/* Words are collected into a line (39 columns at most) and the line is printed, centred, when
 * it ends with a new line. */
void Endings::print_ending_word(const char *word)
{
    char *line = (char *)endings.proclamation_line;
    for (; *word != 0 && endings.proclamation_col < 0x27; endings.proclamation_col++) line[endings.proclamation_col] = *word++;
    line[endings.proclamation_col] = 0;
    if (endings.proclamation_col > 0 && line[endings.proclamation_col - 1] == '\n') {
        print_text(line);
        endings.proclamation_col = 0;
    }
}

void Endings::print_number_words(int n)
{
    if (n > 0x14) {
        print_ending_word(strings[number_words_[(18 + n / 10) % 40]]);
        n %= 10;
        if (n == 0) return;
        print_ending_word(strings[Strings::Dash_82C6]);   /* "-" */
    }
    print_ending_word(strings[number_words_[n % 40]]);
}

void Endings::print_ordinal_words(int n)
{
    if (n < 0x0D) {
        print_ending_word(strings[number_words_[(27 + n) % 40]]);
    } else if (n < 0x14) {
        print_number_words(n);
        print_ending_word(strings[Strings::th_831E]);   /* "th" */
    } else {
        print_ending_word(strings[Strings::Twent_8322]);   /* "Twent" */
        if (n == 0x14) {
            print_ending_word(strings[Strings::ieth_8328]);   /* "ieth" */
        } else {
            print_ending_word(strings[Strings::y_832E]);   /* "y-" */
            print_ending_word(strings[number_words_[(7 + n) % 40]]);
        }
    }
}

/* "Be it known that on the <day> Day of the <month> Month of the Year <year> <name> the Avatar
 * saved the life of our sovereign ..." and how long the quest took, counted from 4-5-139. */
void Endings::print_proclamation(void)
{
    char num[8];
    set_cursor(1, 0);
    put_char(0xFD);
    put_char(0xFC);
    print_text(strings[Strings::Be_it_known_that_on_8332]);   /* "Be it known that on" */
    endings.proclamation_col = 0;
    print_ending_word(strings[Strings::the_8348]);
    print_ordinal_words(game_time.day);
    print_ending_word(strings[Strings::Day_of_834E]);   /* " Day of" */
    print_ending_word(strings[Strings::the_8358]);
    print_ordinal_words(game_time.month);
    print_ending_word(strings[Strings::Month_835E]);   /* " Month" */
    print_text(strings[Strings::of_the_Year_8366]);          /* "of the Year" */
    print_number_words((int16_t)game_time.year / 100);
    print_ending_word(strings[Strings::Hundred_8374]);   /* " Hundred" */
    print_number_words((int16_t)game_time.year % 100);
    print_ending_word(strings[Strings::NL_NL_837E]);
    print_ending_word(party[0].name);
    print_ending_word(strings[Strings::the_Avatar_8382]);   /* " the Avatar" */
    print_text(strings[Strings::saved_the_life_8390]);          /* "saved the life" */
    print_text(strings[Strings::of_our_sovereign_83A0]);          /* "of our sovereign" */
    print_text(strings[Strings::Lord_British_thereby_83B2]);          /* "Lord British, thereby" */
    print_text(strings[Strings::saving_our_people_83CA]);          /* "saving our people" */
    print_text(strings[Strings::and_our_land_83DE]);          /* "and our land." */
    select_font(1);
    print_text(strings[Strings::E_QUE_OF_E_AVATAR_83EE]);          /* runes: THE QUEST OF THE AVATAR */
    print_text(strings[Strings::IS_FOREVER_8404]);          /* IS FOREVER */
    select_font(0);
    put_char(0xFD);
    print_text(strings[Strings::Report_now_thy_Quest_8414]);          /* "Report now, thy Quest compleat in" */
    int years = (int16_t)game_time.year - 0x8B, months = game_time.month - 4, days = game_time.day - 5;
    if (days < 0) { days = game_time.day + 0x17; months = game_time.month - 5; }
    if (months < 0) { months += 0x0D; years = (int16_t)game_time.year - 0x8C; }
    if (years != 0) {
        snprintf(num, sizeof num, "%d", years);
        print_ending_word(num);
        print_ending_word(strings[Strings::year_8438]);   /* " year" */
        if (years > 1) print_ending_word(strings[Strings::s_843E]);
        if (months != 0 || days != 0) print_ending_word(strings[Strings::Comma_SP_8440]);
    }
    if (months != 0) {
        snprintf(num, sizeof num, "%d", months);
        print_ending_word(num);
        print_ending_word(strings[Strings::month_8444]);   /* " month" */
        if (months > 1) print_ending_word(strings[Strings::s_844C]);
        if (days != 0) print_ending_word(strings[Strings::Comma_SP_844E]);
    }
    if (days != 0) {
        snprintf(num, sizeof num, "%d", days);
        print_ending_word(num);
        print_ending_word(strings[Strings::day_8452]);   /* " day" */
        if (days > 1) print_ending_word(strings[Strings::s_8458]);
    }
    print_ending_word(strings[Strings::NL_845A]);
    print_text(strings[Strings::to_Lord_British_at_845C]);          /* "to Lord British at Origin Systems" */
    while (!platform->quit_requested()) {   /* the original stops here for good */
        poll_key();
        delay_ticks(1);
    }
}

void Endings::ending_tick(void)
{
    wait_turns(2);
    step_sound();
    wait_turns(3);
}

/* One step toward (x, y), along the longer axis first. */
int Endings::walk_ending_actor(unsigned y, unsigned x, int slot)
{
    MapObject *o = &map.objects[slot];
    if (o->tile == 0 || (o->x == x && o->y == y) || platform->quit_requested()) return 0;
    int dx = o->x - (int)x, dy = o->y - (int)y;
    if ((dx < 0 ? -dx : dx) < (dy < 0 ? -dy : dy)) o->y = (uint8_t)(o->y + (y < o->y ? -1 : 1));
    else o->x = (uint8_t)(o->x + (x < o->x ? -1 : 1));
    ending_tick();
    return 1;
}

/* Half the time, a random step onto floor (0x44). */
void Endings::ending_actor_leave(int slot)
{
    MapObject *o = &map.objects[slot];
    if (o->tile != 0 && random_between(1, 0) != 0) {
        for (int tries = 0; tries < 8; tries++) {
            unsigned x = o->x, y = o->y;
            switch (random_between(3, 0)) {
            case 0: x++; break;
            case 1: x--; break;
            case 2: y++; break;
            case 3: y--; break;
            }
            if (*view.at(View::MASK + y * 0x20 + x) == 0x44) {
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
void Endings::rescue_lord_british(void)
{
    combat.turn = 0xFF;
    draw_status_bar();
    patch_tiles(1);
    load_scene(0x210, strings[Strings::MISCMAPS_DAT_8480]);                                  /* MISCMAPS.DAT */
    load_file_part(0, 1000, text_buffer.data, strings[Strings::ENDMSG_DAT_848E]);      /* ENDMSG.DAT */
    for (int i = 0; i < 32; i++) map.objects[i].tile = map.objects[i].tile2 = 0;
    MapObject *lb = &map.objects[0x1F];
    lb->tile = lb->tile2 = 0x7C;
    lb->x = 5;
    lb->y = 8;
    lb->flags = 0;
    wait_turns(0x28);
    while (walk_ending_actor(3, 5, 0x1F)) {}
    for (unsigned i = 0; i < party.size(); i++) {
        PartyMember *p = &party[i];
        if (p->dead()) {
            put_char('\n');
            print_text(p->name);
            print_text(strings[Strings::lives_849A]);   /* " lives!" */
            p->status = 'G';
            p->hp = p->max_hp;
            set_draw_color(gfx.color_text);
            xor_fill_rect(0xB7, 0xB7, 8, 8);
            sound_sweep(1, 5000, -0x63C0, 1, 0x2260);
            draw_status_bar();
        }
        MapObject *o = &map.objects[i];
        o->tile = o->tile2 = party.class_tile((unsigned)str_index((uint8_t)p->cls, strings[Strings::AMBFDTPRS_84A4]));
        o->x = 5;
        o->y = 9;
        o->flags = 0;
        wait_turns(1);
        while (walk_ending_actor(rescue_y_[i % 6], rescue_x_[i % 6], (int)i)) {}
    }
    wait_turns(0x28);
    print_text(T(0));
    print_text(party[0].name);
    print_text(strings[Strings::Bang_Quote_NL_NL_84AE]);
    key();
    print_text(T(0x21));
    int answer = yes_no(Strings::Yes_84B4, Strings::No_84BA);
    if (answer == 'N') {
        print_text(T(0x49));
        answer = yes_no(Strings::Yes_84C0, Strings::No_84C6);
    }
    if (answer == 'Y' && party.sandalwood_box != 0) {
        wait_turns(8);
        while (walk_ending_actor(4, 5, 0)) {}
        while (walk_ending_actor(5, 5, 0)) {}
        wait_turns(4);
        MapObject *box = &map.objects[6];
        box->tile = box->tile2 = 0x0E;
        box->x = 5;
        box->y = 4;
        box->flags = 0;
        print_text(T(0xAB));
        wait_turns(0x28);
        print_text(strings[Strings::He_says_84CC]);   /* "He says:" */
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
        view.local_map[0x85] = 0xDC;   /* a moongate rises */
        for (game_time.moongate_phase = 1; game_time.moongate_phase < 0x10; game_time.moongate_phase++) wait_turns(1);
        wait_turns(4);
        while (walk_ending_actor(4, 5, 0x1F)) {}
        lb->tile = lb->tile2 = 0;
        wait_turns(1);
        for (unsigned i = 0; i < party.size(); i++) {
            while (walk_ending_actor(4, 5, (int)i)) {}
            map.objects[i].tile = map.objects[i].tile2 = 0;
            wait_turns(1);
        }
        for (game_time.moongate_phase = 0x0F; game_time.moongate_phase != 0; game_time.moongate_phase--) wait_turns(1);
        draw_tile_at(4, 5, 0x44);
        set_draw_page(1);
        set_draw_color(0);
        fill_rect(199, 0x13F, 0, 0);
        set_draw_page(0);
        ending_pictures();
        print_proclamation();
        return;
    }
    print_text(strings[Strings::I_see_84DA]);   /* "I see..." */
    wait_turns(0x28);
    print_text(T(0x2D5));
    map.objects[0].y--;
    ending_tick();
    while (walk_ending_actor(6, 8, 2) + walk_ending_actor(1, 4, 0x1F) + walk_ending_actor(4, 8, 0) != 0) {}
    while (!platform->quit_requested()) {   /* the original loops here for good */
        ending_actor_leave(1);
        ending_actor_leave(3);
        ending_actor_leave(4);
        ending_actor_leave(5);
    }
}
