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
#include "data.h"

void load_town_npcs(void);                                  /* a290 */
void init_npc_states(unsigned hour);                        /* a366 */
int  npc_path_to_stairs(int npc, int up, unsigned y, unsigned x);   /* a430 */
void build_path_grid(int npc, int mode, int dest_y, int dest_x, int start_y, int start_x);   /* a462 */
int  npc_find_path(int npc, int mode, int dest_y, int dest_x, int start_y, int start_x);     /* a5bc */
int  npc_store_path(int dest_y, int dest_x, int mode, int npc);     /* a73c */
void step_in_dir(int dir);                                  /* a8c2: on dg.dir_x / dg.dir_y */
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

#endif
