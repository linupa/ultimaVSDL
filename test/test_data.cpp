/* test_data.cpp - checks the text from DATA.OVL and the SAVED.GAM round trip against the real game files. */
#include <stdio.h>
#include <string.h>
#include "files.h"
#include "gamedata.h"
#include "gametime.h"
#include "gfx.h"
#include "map.h"
#include "party.h"
#include "savegame.h"
#include "strings.h"

int main(int argc, char **argv)
{
    const char *save = argc > 1 ? argv[1] : "SAVED.GAM";
    /* DATA.OVL is in the same directory as the saved game */
    char dir[512];
    snprintf(dir, sizeof dir, "%s", save);
    char *slash = strrchr(dir, '/');
    if (slash) *slash = '\0';
    else snprintf(dir, sizeof dir, ".");
    files_set_game_dir(dir);
    if (!Data::instance().load()) return 1;
    /* The text comes from DATA.OVL, not the port's source: it must match the file's bytes. */
    int bad = 0;
    struct { Strings::Id id; const char *text; } known[] = {
        {Strings::Leather_Helm_0042, "Leather Helm"}, {Strings::Hull_now_A2F8, "Hull now "},
        {Strings::CONFLICT_A438, "*** CONFLICT ***\n"}, {Strings::ocked_26D8, "ocked!\n"},
    };
    for (auto &k : known)
        if (strcmp(strings[k.id], k.text) != 0) { printf("FAIL: string %04X is \"%s\"\n", (unsigned)k.id, strings[k.id]); bad++; }
    unsigned owned = 0;
    for (int i = 0; i < Data::instance().count(); i++) {
        unsigned off, size;
        const char *name;
        Data::instance().piece(i, &off, &size, &name);
        owned += size;
    }
    printf("DATA.OVL: %d pieces hold %u of the %u bytes\n", Data::instance().count(), owned, (unsigned)Data::SIZE);
    if (bad) return 1;
    printf("string at ds:41D4: \"%.10s\"   party name bytes before load: \"%s\"\n", strings[Strings::Zzzzzz_41D4], party[0].name);
    printf("color_fire on disk (CGA): %u\n", gfx.color_fire);

    if (savegame.load_path(save) != 0) { printf("cannot read %s\n", save); return 1; }
    for (int i = 0; i < 6; i++) {
        PartyMember *m = &party[i];
        printf("%-9s %c %c str %2u dex %2u int %2u  hp %3u/%3u xp %4u lvl %u%s\n", m->name, m->cls, m->status,
               m->str, m->dex, m->intel, m->hp, m->max_hp, m->xp, m->level, i < party.size() ? "" : "  (not in party)");
    }
    printf("food %u gold %u karma %u keys %u torches %u  date %u-%u-%u %02u:%02u  location %u at (%u,%u)\n",
           party.food, party.gold, party.karma, party.keys, party.torches, game_time.month, game_time.day, game_time.year, game_time.hour, game_time.minute,
           map.id(), map.x, map.y);

    if (savegame.save_path("roundtrip.gam") != 0) return 1;
    FILE *a = fopen(save, "rb"), *b = fopen("roundtrip.gam", "rb");
    static unsigned char x[SaveGame::SIZE], y[SaveGame::SIZE];
    size_t na = fread(x, 1, sizeof x, a), nb = fread(y, 1, sizeof y, b);
    printf("save round trip: %s\n", na == nb && !memcmp(x, y, na) ? "identical" : "DIFFERENT");
    fclose(a); fclose(b);
    return 0;
}
