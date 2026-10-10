/* outdoor.h - Britannia and the Underworld: MAINOUT.OVL (movement, sailing, wandering monsters, the
 * outdoor command loop) and the rest of OUTSUBS.OVL. Parameter order follows the Ghidra signatures.
 * The map window itself (enter_outdoors, outdoor_step, chunks) is in map.c. */
#ifndef U5_OUTDOOR_H
#define U5_OUTDOOR_H

#include <stdint.h>

void wait_if_object1_near(void);             /* 824a */
int  outdoor_set_facing(int dir);            /* 82aa: dir 0-3 = N E S W; 1 = the turn used the move */
int  outdoor_can_move(int dy, int dx);       /* 83ce */
void terrain_slowdown(void);                 /* 85b0 */
int  outdoor_move(int fast, unsigned key);   /* 8660: key 1-4 = W E N S */
unsigned outdoor_read_key(void);             /* 8768: under sail the ship keeps going */
int  cmd_attack_outdoor(void);               /* 88bc */
int  enter_dungeon(const char *prompt);      /* 8960 */
int  cmd_enter(void);                        /* 8aae */
int  outdoor_idle_check(int ticked);         /* 8bea */
void random_earthquake(void);                /* 8c30 */
void outdoor_main_loop(void);                /* 8c54 */
void outdoor_start(void);                    /* 8ef2 */
int  encounter_chance(void);                 /* 8f5c */
int  weighted_random_index(const uint8_t *weights);   /* 8fd4 */
uint8_t pick_monster_for_terrain(int tile);  /* 901e */
void random_spawn_pos(void);                 /* 911e: to dir_x / dir_y */
void spawn_random_monster(void);             /* 9194 */
int  is_active_monster_tile(int tile);       /* 922c */
void damage_vehicle(void);                   /* 926e */
void pirate_broadside(int dist_y, int dist_x, int object);   /* 9338 */
void monster_contact(int object);            /* 9418 */
int  monster_act(int object);                /* 94ea */
int  monster_can_step(unsigned y, unsigned x, int object);   /* 9652 */
int  not_last_position(unsigned y, unsigned x);              /* 9698 */
uint8_t shadowlord_chase_weight(int object); /* 96ba */
void monster_step(int dy, int dx, int object);               /* 9748 */
void monster_random_walk(int object);        /* 98cc */
void monster_chase(int chase, int object);   /* 99a4 */
void monster_move_outdoor(int object);       /* 9b5c */
int  outdoor_monsters_turn(void);            /* 9c30: number of monsters that acted */
void troll_toll(void);                       /* 9d0e */
void troll_bridge(void);                     /* 9db8 */

void waterfall_fall(void);                   /* OUTSUBS a6e8 */
void place_underworld_specials(void);        /* OUTSUBS a7f6 */
void lava_burn(void);                        /* OUTSUBS a87e */
void swamp_poison(void);                     /* OUTSUBS a88c */
void apparition_level_up(void);              /* OUTSUBS a8e8 */

#endif
