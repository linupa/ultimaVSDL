/* town.h - towns, castles, keeps and dwellings: NPC placement, lamps, horses, stairs and movement.
 * Port of parts of TOWN.OVL; load_town_level is in map.c. */
#ifndef U5_TOWN_H
#define U5_TOWN_H

#include <stdint.h>

int  npc_is_dead(int npc);                   /* 81d0 */
void mark_npc_dead(int npc);                 /* 8222: also used when an NPC joins the party */
void npc_make_hostile(int npc);              /* 8a2e */
void npc_make_fleeing(int npc);              /* 8aa4 */
void alert_town(void);                       /* 8b28: talk op 0x8B, attacking */
void npc_begone(int npc);                    /* 92aa: "Begone, vermin!" */
void remove_npc(int npc);                    /* 8280 */
int  npc_for_object(int object);             /* 82ee: NPC slot of a map object, -1 if none */
void update_lamps(void);                     /* 8340: at hours 5 and 20 */
void take_stairs(int tile, unsigned facing); /* 86fe */
void set_vehicle_facing(int dir);            /* 874c */
int  town_move(int dir);                     /* 87d0: dir 1-4 (W E N S); 1 if the party left the town */
int  is_hitching_tile(unsigned y, unsigned x);   /* 8e1a */
void wander_horses(void);                    /* 8e48 */
void enter_town(int fresh);                  /* 93c0 */
void npc_encounter(int mode);                /* 9522 */
void shadowlord_blight(void);                /* 83e2 */
void check_shadowlord_arrival(void);         /* 847e */
void fight_npc(int npc);                     /* 8b8c */
int  cmd_attack_town(void);                  /* 8bb6 */
int  cmd_klimb_town(void);                   /* 8d52 */
unsigned town_read_key(int redraw);          /* 8f94 */
int  town_number_key(int key);               /* 9004 */
void town_tile_effects(void);                /* 90d2 */
int  npc_swayed_by_shadowlord(int npc);      /* 92c2 */
void apply_shadowlord_influence(void);       /* 9326 */
void print_shadowlord_air(int shadowlord);   /* 9388 */
int  arrest_party(void);                     /* 947e: 1 = the party resists */
void town_main_loop(void);                   /* 95ee */
void place_npcs(void);                       /* 9864 */
void place_npc(unsigned z, unsigned y, unsigned x, int npc);   /* 98f6 */

/* The end of a turn in town_main_loop (9764 on): clock, lamps, door, horses, NPCs, encounters.
 * key = the command key (Space lets a carpet keep pace), result = the command's result (1 normal,
 * 2 = NPC encounter forced, 0 = no turn). */
void town_end_turn(int key, unsigned result);

#endif
