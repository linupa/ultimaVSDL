/* world.h - per-turn helpers of the main program shared by the town and outdoor loops: damage and
 * poison, moongates, wind, the sky bar, the lighthouse beams. Parameter order follows the Ghidra
 * signatures.
 */
#ifndef U5_WORLD_H
#define U5_WORLD_H

#include <stdint.h>
#include "sound.h"

void per_turn_status(void);              /* 2ae8: poison, food, spell timer, regeneration */
void damage_party(void);                 /* 2aa8: 1-8 to every living member */
void damage_member(int damage, int member);   /* 2a52 */
void poison_member(unsigned member);     /* 2fa6 */
int  random_stat_roll(void);             /* 3abe: 1-30 */
void spring_trap(unsigned member);       /* 2fd0: acid, poison, bomb or gas */

int  check_moongate(void);               /* 48a8: 1 = the midnight gate (to a shrine) */
int  moongate_travel(int moonstone);     /* 47f4 */
int  moonstone_here(int moonstone);      /* 4702 */
void update_moongates(void);             /* 475a: gates rise at night where a moonstone is buried */

void set_wind_direction(int dir);        /* 2e96: -1 = only redraw the label */
void random_wind_change(void);           /* 2f62 */
void draw_sky_bar(void);                 /* 4a84: sun and moons along the top of the view */
void clear_location_label(void);         /* 4be8 */
int  ask_which_member(void);             /* 4988: active player, or asks; -1 none */

void stamp_view_shape(int y, int x, int set, int shape);   /* 7040 */
void animate_light_beams(void);          /* 70a6: the lighthouse beam */

const char *objects_file_name(void);     /* OUTSUBS a5f8: "BRIT.OOL" or "UNDER.OOL" */

#endif
