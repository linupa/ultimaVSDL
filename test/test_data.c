/* test_data.c - checks data.c/data.h against the real game files. */
#include <stdio.h>
#include <string.h>
#include "data.h"

int main(int argc, char **argv)
{
    const char *save = argc > 1 ? argv[1] : "SAVED.GAM";
    /* raw-offset access to the initial image */
    printf("string at ds:41D4: \"%.10s\"   party name bytes before load: \"%s\"\n", ds_str(0x41D4), dg.party[0].name);
    printf("color_fire on disk (CGA): %u\n", dg.color_fire);

    if (dgroup_load_game(save) != 0) { printf("cannot read %s\n", save); return 1; }
    for (int i = 0; i < 6; i++) {
        PartyMember *m = &dg.party[i];
        printf("%-9s %c %c str %2u dex %2u int %2u  hp %3u/%3u xp %4u lvl %u%s\n", m->name, m->cls, m->status,
               m->str, m->dex, m->intel, m->hp, m->max_hp, m->xp, m->level, i < dg.party_size ? "" : "  (not in party)");
    }
    printf("food %u gold %u karma %u keys %u torches %u  date %u-%u-%u %02u:%02u  location %u at (%u,%u)\n",
           dg.food, dg.gold, dg.karma, dg.keys, dg.torches, dg.month, dg.day, dg.year, dg.hour, dg.minute,
           dg.location, dg.party_x, dg.party_y);
    printf("ds_u16(0x57A8) == dg.food: %s\n", ds_u16(0x57A8) == dg.food ? "yes" : "NO");

    if (dgroup_save_game("roundtrip.gam") != 0) return 1;
    FILE *a = fopen(save, "rb"), *b = fopen("roundtrip.gam", "rb");
    static unsigned char x[SAVE_SIZE], y[SAVE_SIZE];
    size_t na = fread(x, 1, sizeof x, a), nb = fread(y, 1, sizeof y, b);
    printf("save round trip: %s\n", na == nb && !memcmp(x, y, na) ? "identical" : "DIFFERENT");
    fclose(a); fclose(b);
    return 0;
}
