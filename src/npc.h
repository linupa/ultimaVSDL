/* npc.h - town NPCs: schedules, movement and path finding. Port of NPC.OVL (NPC::a290-b5bf).
 *
 * Each town has up to 31 NPCs (slot 0 unused). From the .NPC file: npc_schedules[n] (three
 * entries of activity and x/y/z, four change times), npc_type[n] (its tile, 0 = none) and the
 * dialogue number (npc_state[n].dialogue). npc_state[n].state:
 *   0 none   1 at its spot / doing its activity   2 walking a path   3 walking to stairs
 *   4/5 arriving from the floor above/below      6/7 must go up/down to reach its spot
 *   8 spot is on another floor and so is the NPC (just moved there)
 * Paths are stored run-length in npc_paths[n * 32] as (count, direction) pairs. Directions for
 * step_in_dir: 1 east, 2 north, 3 west, 4 south. Parameter order follows the Ghidra signatures. */
#ifndef U5_NPC_H
#define U5_NPC_H

#include <stdint.h>
#include "strings.h"

/* A square of a settlement's level, as the original's words. */
struct Pos {
    uint16_t x, y;
};

/* An NPC's day: 16 bytes, as in the .NPC files and SAVED.GAM. */
#pragma pack(push, 1)
struct NpcSchedule {
    uint8_t activity[3];   /* AI per schedule slot */
    uint8_t x[3], y[3], z[3];
    uint8_t times[4];      /* hours when the slot changes */
};
#pragma pack(pop)
static_assert(sizeof(NpcSchedule) == 16, "NpcSchedule size");

/* What an NPC is doing: 16 bytes, as in SAVED.GAM. */
#pragma pack(push, 1)
struct NpcState {
    uint16_t state;        /* 0 none, 1 at spot, 2 walking, 3 stairs, 4-8 changing level */
    uint16_t x, y, z;
    uint16_t type;         /* copy of npc_type */
    uint16_t dialogue;     /* TLK script id / merchant / 0xFD-0xFF */
    uint16_t object;       /* index into objects */
    uint16_t sched_slot;
};
#pragma pack(pop)
static_assert(sizeof(NpcState) == 16, "NpcState size");

/* The people of the settlement the party is in: their schedules, movement and path finding
 * (NPC.OVL). `npcs` is the one Npcs object.
 *
 * All of it is part of the saved game: the members are registered with SaveGame (savegame.h),
 * which puts them at their old places in SAVED.GAM. (The path search grid is the first 32 x 32
 * bytes of the text buffer, textbuf.h, as in the original.) */
class Npcs {
public:
    static Npcs &instance();

    /* Part of the saved game (registered with SaveGame). Slot 0 is unused; a town has up to 31 NPCs. */
    uint8_t type[32] = {};           /* ds:659E  the NPC's tile; 0 = no NPC in this slot */
    NpcSchedule schedules[32] = {};  /* ds:5D5E  three places and activities, four change times */
    NpcState state[32] = {};         /* ds:5F5E */
    uint8_t paths[1024] = {};        /* ds:615E  32 bytes each: (count, direction) pairs */
    uint16_t path_pos[32] = {};      /* ds:655E */
    uint16_t blocked_turns[32] = {}; /* ds:65C2 */
    char event = 0;                  /* ds:65BE  'a' = an NPC is attacking */
    uint8_t event_slot = 0;          /* ds:65BF  the NPC that reached the party */
    /* A bit per NPC that is dead or gone, by location 1-0x20 (ds:5B56); entry 0 is unused. That
     * makes 33 words: the original declares 32 and lets Serpent's Hold (0x20) use the word after
     * them, which is entry 0 of the "met" table (see Conversation::met_bits). */
    uint32_t dead_bits[33] = {};

    /* ---- schedules, movement and path finding (npc.cpp) ---- */

    void load_town_npcs(void);                                  /* a290 */
    void init_npc_states(unsigned hour);                        /* a366 */
    int  npc_path_to_stairs(int npc, int up, unsigned y, unsigned x, Pos *stairs = nullptr);   /* a430: 1 if found, the stairs in *stairs */
    void build_path_grid(int npc, int mode, int dest_y, int dest_x, int start_y, int start_x);   /* a462 */
    int  npc_find_path(int npc, int mode, int dest_y, int dest_x, int start_y, int start_x, Pos *goal = nullptr);   /* a5bc: 1 if found, the square reached in *goal */
    int  npc_store_path(int dest_y, int dest_x, int mode, int npc);     /* a73c */
    void step_in_dir(int dir, Pos *pos);                        /* a8c2: moves *pos one square; 5 and up leave it */
    int  manhattan_distance(int y1, int x1, int y2, int x2);    /* a930 */
    void npc_follow_party(int slot, int npc);                   /* a974 */
    int  npc_check_schedule(unsigned hour, int npc);            /* abc8 */
    int  npc_on_stairs(int slot, int npc);                      /* acda */
    uint8_t npc_square_walkable(int slot, int npc, unsigned z, unsigned y, unsigned x);   /* ad6c */
    int  npc_can_step(int slot, int npc, unsigned y, unsigned x);       /* ae2e */
    void npc_wander(NpcSchedule *sched, int npc, int slot, int radius, NpcState *state); /* aee0 */
    void npc_do_activity(int slot, int npc);                    /* af90 */
    void update_npcs(unsigned hour);                            /* b044: once per town turn */
    uint8_t current_schedule_slot(uint8_t hour, int npc);       /* b570 */

    Npcs(const Npcs &) = delete;
    Npcs &operator=(const Npcs &) = delete;

private:
    Npcs();

    uint8_t walkable_bits_[32] = {};   /* ds:367E  a bit per tile, set = an NPC can't step there */

    int sx(int npc);
    int sy(int npc);
};

extern Npcs &npcs;

#endif
