/* test_npc.cpp - town NPCs from the real .NPC files: loading, placement, schedules and walking.
 * Usage: test_npc <game dir>. Writes test_npc_*.ppm. */
#include <stdio.h>
#include <string.h>
#include "files.h"
#include "gamedata.h"
#include "gametime.h"
#include "gfx.h"
#include "map.h"
#include "npc.h"
#include "party.h"
#include "savegame.h"
#include "text.h"
#include "tiles.h"
#include "town.h"
#include "video.h"
#include "view.h"

static int fails;
static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

static void list_npcs(void)
{
    printf(" npc type dlg act    x,y,z (now)  slot state obj  path\n");
    for (int i = 1; i < 32; i++) {
        if (!npcs.type[i]) continue;
        NpcState *s = &npcs.state[i];
        NpcSchedule *sc = &npcs.schedules[i];
        int slot = npcs.current_schedule_slot(game_time.hour, i);
        printf("  %2d  %02X %3u  %u%u%u  %2u,%2u,%u (%2u,%2u,%u)  %d   %u     %2u  %s\n", i, npcs.type[i],
               s->dialogue, sc->activity[0], sc->activity[1], sc->activity[2], sc->x[slot], sc->y[slot],
               sc->z[slot], s->x, s->y, s->z, slot, s->state, s->object,
               npcs.path_pos[i] == 0xFFFF ? "-" : "yes");
    }
}

static int at_spot_count(void)
{
    int n = 0;
    for (int i = 1; i < 32; i++) {
        if (!npcs.type[i]) continue;
        NpcSchedule *sc = &npcs.schedules[i];
        int slot = npcs.current_schedule_slot(game_time.hour, i);
        NpcState *s = &npcs.state[i];
        n += s->x == sc->x[slot] && s->y == sc->y[slot] && s->z == sc->z[slot];
    }
    return n;
}

static void shot(const char *name)
{
    char path[64];
    video_clear();
    draw_screen_frame();
    world_tick();
    snprintf(path, sizeof path, "test_npc_%s.ppm", name);
    if (video_save_ppm(path) == 0) printf("wrote %s\n", path);
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
    select_font(0);
    set_ega_colors();
    gfx.video_type = 1;
    reset_tile_anim_map();
    load_tileset("tiles.16");
    check(savegame.load_path(save) == 0, "SAVED.GAM");

    /* docs/conversations.md: TOWNE.NPC, first town, NPC 1 */
    map.set(Map::MOONGLOW);
    npcs.load_town_npcs();
    NpcSchedule *sc = &npcs.schedules[1];
    check(sc->activity[2] == 1 && sc->x[1] == 23 && sc->y[2] == 29 && sc->z[0] == 1 && sc->times[0] == 21 &&
          npcs.type[1] == 0x54 && npcs.state[1].dialogue == 130, "Moonglow NPC 1 matches the documented record");
    game_time.hour = 10;
    check(npcs.current_schedule_slot(10, 1) == 1, "10:00 -> entry 1 (times 21 9 17 18)");
    check(npcs.current_schedule_slot(17, 1) == 2 && npcs.current_schedule_slot(18, 1) == 1 && npcs.current_schedule_slot(3, 1) == 0,
          "17 -> 2, 18 -> 1, 3 -> 0");

    /* Britain at 8:40, ground floor. */
    map.set(Map::BRITAIN);
    map.z = 0;
    game_time.hour = 8;
    game_time.minute = 40;
    map.x = 15;
    map.y = 30;
    party.transport = 0x1C;
    town.enter_town(1);
    printf("Britain at %02u:%02u\n", game_time.hour, game_time.minute);
    list_npcs();
    int placed = 0, wrong = 0;
    for (int i = 1; i < 32; i++) {
        NpcState *s = &npcs.state[i];
        if (!npcs.type[i]) continue;
        if (s->z == map.z) {
            placed += s->object != 0;
            MapObject *o = &map.objects[s->object];
            wrong += !(o->x == s->x && o->y == s->y && o->tile == npcs.type[i]);
        } else {
            wrong += s->object != 0;
        }
    }
    printf("NPCs on this floor with objects: %d\n", placed);
    check(placed > 0 && wrong == 0, "NPCs on the party's floor have map objects at their spots");
    shot("britain_0840");

    /* An hour of turns, across the 9:00 schedule changes. */
    int before = at_spot_count(), moved = 0;
    uint16_t x0[32];
    for (int i = 0; i < 32; i++) x0[i] = (uint16_t)(npcs.state[i].x | npcs.state[i].y << 8);
    for (int t = 0; t < 60; t++) {
        town.town_end_turn('x', 1);
        world_tick();
    }
    for (int i = 1; i < 32; i++) moved += npcs.type[i] && x0[i] != (uint16_t)(npcs.state[i].x | npcs.state[i].y << 8);
    printf("Britain at %02u:%02u: %d NPCs moved, at their spot %d -> %d\n", game_time.hour, game_time.minute, moved, before, at_spot_count());
    list_npcs();
    check(moved > 0, "NPCs move");
    shot("britain_0940");

    /* Path search on its own: from the party's square to an NPC's spot. */
    int npc = 0;
    for (int i = 1; i < 32 && !npc; i++)
        if (npcs.type[i] && npcs.state[i].z == 0) npc = i;
    int slot = npcs.current_schedule_slot(game_time.hour, npc);
    int ty = npcs.schedules[npc].y[slot], tx = npcs.schedules[npc].x[slot];
    npcs.state[npc].x = 15;
    npcs.state[npc].y = 28;
    int found = npcs.npc_find_path(npc, 0, ty, tx, 28, 15);
    int n = found ? npcs.npc_store_path(ty, tx, 0, npc) : 0;
    int px = 15, py = 28;
    printf("path for NPC %d from (15,28) to (%d,%d):", npc, tx, ty);
    for (int k = 0; k < n; k += 2) {
        int c = npcs.paths[npc * 32 + k], d = npcs.paths[npc * 32 + k + 1];
        printf(" %d%c", c, " ENWS"[d]);
        px += (d == 1) * c - (d == 3) * c;
        py += (d == 4) * c - (d == 2) * c;
    }
    printf("  -> ends at (%d,%d)\n", px, py);
    check(found && n < 32 ? (px == tx && py == ty) : found, "stored path leads to the spot");

    printf("%d failure(s)\n", fails);
    return fails != 0;
}
