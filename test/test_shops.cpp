/* test_shops.cpp - each shop type with scripted keys; checks the effects and prints the transcripts.
 * Usage: test_shops <game dir> */
#include <stdio.h>
#include <string.h>
#include "files.h"
#include "gamedata.h"
#include "gametime.h"
#include "gfx.h"
#include "map.h"
#include "npc.h"
#include "party.h"
#include "platform_script.h"
#include "savegame.h"
#include "shops.h"
#include "strings.h"
#include "talk.h"
#include "text.h"
#include "tiles.h"
#include "town.h"
#include "video.h"
#include "view.h"


static char transcript[16384];
static size_t tlen;
static void echo(uint8_t c, int rune)
{
    (void)rune;
    if (tlen + 1 < sizeof transcript) transcript[tlen++] = (char)c;
}

static int fails;
static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

static char save[600];

/* Fresh state from SAVED.GAM, in the first town that has this shop type. */
static int setup(int type)
{
    savegame.load_path(save);
    map.set(Map::Id(Town::table_byte(0x23CA + type * 16 + (type == 6))));   /* healers: skip location 5 (free) */
    map.z = 0;
    game_time.hour = 12;
    party.gold = 1000;
    party.active_player = 0xFF;
    town.shadowlord_here = 0xFF;
    map.x = 15;
    map.y = 15;
    town.enter_town(1);
    select_text_window(2);
    return map.id();
}

static void run(int type, const int *keys, int n)
{
    static int script[256];
    int m = 0;
    for (int i = 0; i < n; i++) script[m++] = keys[i];
    script_platform.keys(script, m);
    tlen = 0;
    conversation.open_shop(0x81 + type);
    transcript[tlen] = 0;
    int odd = 0;
    for (size_t i = 0; i < tlen; i++) odd += (uint8_t)transcript[i] < 0x20 && transcript[i] != '\n';
    printf("---- type %d at location %u, shop %s, keeper %s:\n%s\n----\n", type, map.id(),
           strings[Strings::Id(town.shop_name)], strings[Strings::Id(town.shopkeeper_name)], transcript);
    check(odd == 0, "no control characters in the shop text");
}

static int has(const char *s)
{
    static char flat[16384];
    size_t j = 0;
    for (size_t i = 0; i < tlen && j + 1 < sizeof flat; i++) {
        char c = transcript[i] == '\n' ? ' ' : transcript[i];
        if (c == ' ' && j && flat[j - 1] == ' ') continue;
        flat[j++] = c;
    }
    flat[j] = 0;
    return strstr(flat, s) != NULL;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
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
    define_text_window(9, 0x27, 1, 0x18, 1);
    define_text_window(0x17, 0x27, 0x0B, 0x18, 2);
    text_echo = echo;

    /* 5 provisioner: Yes, a) keys, Yes, then leave the menu */
    { setup(5); uint8_t k0 = party.keys; uint16_t g0 = party.gold;
      int keys[] = {'Y', 'A', 'Y', ' '}; run(5, keys, 4);
      check(party.keys == k0 + 3 && party.gold < g0, "provisioner: 3 keys bought");
      check(has("Sold!"), "provisioner: \"Sold!\""); }

    /* 4 reagents: Yes, A, Yes, Esc */
    { setup(4); uint8_t r[8]; memcpy(r, party.reagents, 8); uint16_t g0 = party.gold;
      int keys[] = {'Y', 'A', 'Y', 0x1B}; run(4, keys, 4);
      int more = 0; for (int i = 0; i < 8; i++) more += party.reagents[i] > r[i];
      check(more == 1 && party.gold < g0, "reagents: one reagent bought"); }

    /* 0 armoury: key, Buy, a, Yes, Space */
    { setup(0); uint8_t inv[48]; memcpy(inv, party.inventory, 48); uint16_t g0 = party.gold;
      int keys[] = {' ', 'B', 'A', 'Y', ' '}; run(0, keys, 5);
      int more = 0; for (int i = 0; i < 48; i++) more += party.inventory[i] != inv[i];
      check(more == 1 && party.gold < g0, "armoury: one item bought"); }

    /* 0 armoury: sell the first item */
    { setup(0); uint8_t inv[48]; memcpy(inv, party.inventory, 48); uint16_t g0 = party.gold;
      int keys[] = {' ', 'S', 0x0D, 'Y', 0x1B}; run(0, keys, 5);
      int less = 0; for (int i = 0; i < 48; i++) less += party.inventory[i] < inv[i];
      check((less == 1 && party.gold > g0) || has("We don't deal in used ammunition"), "armoury: sold an item (or refused ammunition)"); }

    /* 6 healer: Yes, Heal, member 2, Return, Yes (pay), then No */
    { setup(6); party[1].hp = 5; uint16_t g0 = party.gold;
      int keys[] = {'Y', 'H', '2', 0x0D, 'Y', 'N'}; run(6, keys, 6);
      check(party[1].hp == party[1].max_hp && party.gold < g0, "healer: member 2 healed for gold"); }

    /* 1 tavern: Yes, food letter, then No to "anything else" */
    { setup(1); uint16_t f0 = party.food, g0 = party.gold;
      int food_key = Town::table_byte(0x4C1E + Town::table_byte(0x4D4C + 0));
      int keys[] = {'Y', food_key, 'N'}; run(1, keys, 3);
      check(party.food > f0 && party.gold < g0, "tavern: a meal (food per diner)"); }

    /* 7 inn: Yes, Rest, Yes -> morning */
    { setup(7); party[0].hp = 1; uint16_t g0 = party.gold;
      int keys[] = {'Y', 'R', 'Y'}; run(7, keys, 3);
      check(game_time.hour == 6 && party[0].hp == party[0].max_hp && party.gold < g0, "inn: rested until 6:00, healed");
      check(has("Morning!"), "inn: \"Morning!\""); }

    /* 2 stables: Yes, Yes */
    { setup(2); uint16_t g0 = party.gold; int horses0 = 0, horses = 0;
      for (int i = 0; i < 32; i++) horses0 += map.objects[i].tile == 0x10;
      int keys[] = {'Y', 'Y'}; run(2, keys, 2);
      for (int i = 0; i < 32; i++) horses += map.objects[i].tile == 0x10;
      check((horses == horses0 + 1 && party.gold < g0) || has("The stables are closed"), "stables: a horse outside (or closed here)"); }

    /* 3 shipwright: Yes, Skiff, Yes, then No */
    { setup(3); town.stored_ship = 0; uint16_t g0 = party.gold;
      int keys[] = {'Y', 'S', 'Y', 'N'}; run(3, keys, 4);
      check(town.stored_ship == 0x40 && party.gold < g0, "shipwright: a skiff at the dock"); }

    printf("%d failure(s)\n", fails);
    return fails != 0;
}
