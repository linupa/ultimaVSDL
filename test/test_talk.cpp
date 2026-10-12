/* test_talk.cpp - a scripted conversation with Zachariah (Moonglow, TOWNE.TLK script 1), checked
 * against the expected transcript. Usage: test_talk <game dir> */
#include <stdio.h>
#include <string.h>
#include "files.h"
#include "gamedata.h"
#include "gametime.h"
#include "gfx.h"
#include "map.h"
#include "npc.h"
#include "objects.h"
#include "party.h"
#include "platform_script.h"
#include "savegame.h"
#include "talk.h"
#include "text.h"
#include "tiles.h"
#include "town.h"
#include "video.h"
#include "view.h"


static char transcript[8192];
static size_t tlen;
static void echo(uint8_t c, int rune)
{
    (void)rune;
    if (tlen + 1 < sizeof transcript) transcript[tlen++] = (char)c;
}

static int keys[512], nkeys;
static void type(const char *s)
{
    for (; *s; s++) keys[nkeys++] = (unsigned char)*s;
    keys[nkeys++] = 0x0D;
}

static int fails;
static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

/* The transcript as one line of words (window line breaks become spaces). */
static void flatten(char *out, size_t n)
{
    size_t j = 0;
    for (size_t i = 0; i < tlen && j + 1 < n; i++) {
        char c = transcript[i] == '\n' ? ' ' : transcript[i];
        if (c == ' ' && j && out[j - 1] == ' ') continue;
        out[j++] = c;
    }
    out[j] = 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char save[600];
    files_set_game_dir(dir);
    if (!Data::instance().load()) return 1;   /* the game's tables and text */
    snprintf(save, sizeof save, "%s/SAVED.GAM", dir);
    reset_text_windows();
    load_font(0, "ibm.ch");
    load_font(1, "runes.ch");
    select_font(0);
    set_ega_colors();
    gfx.video_type = 1;
    reset_tile_anim_map();
    load_tileset("tiles.16");
    if (savegame.load_path(save) != 0) return 1;
    define_text_window(0x17, 0x27, 0x0B, 0x18, 2);   /* the game's message window */
    select_text_window(2);
    text_echo = echo;

    map.set(Map::MOONGLOW);   /* Moonglow, 10:00 */
    map.z = 0;
    game_time.hour = 10;
    map.x = 15;
    map.y = 30;
    town.enter_town(1);
    int npc = 0;
    for (int i = 1; i < 32; i++)
        if (npcs.type[i] && npcs.state[i].dialogue == 1) npc = i;
    check(npc != 0, "Zachariah (dialogue 1) is in Moonglow");
    conversation.met_bits[1] = 0;

    type("name");
    type("job");
    type("tele");           /* <OR>: same answer as "star" */
    type("damn");           /* swearing: rebuked; the pause after it eats one typed-ahead key */
    keys[nkeys++] = ' ';
    type("xyzzy");          /* unknown */
    type("retu");           /* -> label 0x91: "Art thou the Avatar of legend?" */
    type("y");              /* "Indeed?" then not met: ask the name */
    type(party[0].name); /* "A pleasure!" -> met -> label 0x92 */
    type("dawn");           /* the password: news, two key waits, END */
    keys[nkeys++] = ' ';
    keys[nkeys++] = ' ';
    script_platform.keys(keys, nkeys);
    tlen = 0;
    conversation.talk_to_npc(npc);
    transcript[tlen] = 0;
    printf("---- transcript (message window lines)\n%s\n----\n", transcript);

    char flat[8192];
    flatten(flat, sizeof flat);
    check(strstr(flat, "You see a stately, white-haired man of many years.") != NULL, "description, with table words");
    check(strstr(flat, "My name is Zachariah") != NULL, "NAME");
    check(strstr(flat, "I study the stars.") != NULL, "JOB");
    check(strstr(flat, "I watch the signs amongst the planets!") != NULL, "tele -> <OR> -> star's answer");
    check(strstr(flat, "With language like that") != NULL, "swearing rebuked");
    check(strstr(flat, "I cannot help thee with that.") != NULL, "unknown keyword");
    check(strstr(flat, "Art thou the Avatar of legend?") != NULL, "question label 0x91");
    check(strstr(flat, "What is thy name?") != NULL && strstr(flat, "A pleasure!") != NULL, "asks the name, recognises it");
    check(conversation.talk_has_met(npc), "NPC marked as met");
    check(strstr(flat, "What is the password of our Resistance?") != NULL, "met -> label 0x92");
    check(strstr(flat, "Seek him out in Jhelom.") != NULL, "password answer, key waits, end");
    int long_lines = 0, col = 0;
    for (size_t i = 0; i < tlen; i++) {
        if (transcript[i] == '\n') col = 0;
        else if (++col > 16) long_lines++;
    }
    check(long_lines == 0, "no line longer than the 16-column window");

    /* The T command: stand south of an NPC, Talk, North, then leave with an empty line. */
    int other = 0;
    for (int i = 1; i < 32 && !other; i++)
        if (i != npc && npcs.type[i] && npcs.state[i].z == 0 && npcs.state[i].dialogue && npcs.state[i].dialogue < 0x80 &&
            find_object_at(0, (uint8_t)(npcs.state[i].y + 1), (uint8_t)npcs.state[i].x) == 0)
            other = i;
    map.x = (uint8_t)npcs.state[other].x;
    map.y = (uint8_t)(npcs.state[other].y + 1);
    nkeys = 0;
    keys[nkeys++] = 3;      /* KEY_UP: North */
    keys[nkeys++] = 0x0D;   /* empty input: BYE */
    script_platform.keys(keys, nkeys);
    tlen = 0;
    conversation.cmd_talk();
    transcript[tlen] = 0;
    flatten(flat, sizeof flat);
    printf("T North -> NPC %d: %.120s...\n", other, flat);
    check(other && strstr(flat, "You see") != NULL && strstr(flat, "BYE") != NULL, "T + direction finds the NPC and talks");

    printf("%d failure(s)\n", fails);
    return fails != 0;
}
