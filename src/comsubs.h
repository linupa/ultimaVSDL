/* comsubs.h - COMSUBS.OVL: attacks, aiming, missiles and the messages of a hit. Parameter order
 * follows the Ghidra signatures. Weapons are item ids (0xFF = bare hands); ids 0x23 and up count
 * as magic. item_range gives the reach (0 = hand to hand) and item_missile the missile drawn. */
#ifndef U5_COMSUBS_H
#define U5_COMSUBS_H

#include <stdint.h>

int  magic_resist_roll(int missile, int target, int attacker);   /* e1e0: 1 = resisted */
void decay_combat_fields(void);                           /* e236 */
void print_combatant_name(int slot);                      /* e274 */
void print_missed(int slot);                              /* e2b2 */
int  monster_special(int slot);                           /* e2d4: possess, turn invisible, gate in a daemon */
void report_hit(int attacker, int target);                /* e4f2 */
long distance_squared(int y1, int x1, int y2, int x2);    /* e638 */
int  distance(int y1, int x1, int y2, int x2);            /* e66a: square root, rounded down */
int  combatant_distance(int a, int b);                    /* e6b4 */
int  aim_cursor(int range, unsigned slot);                /* e6e4: 0 cancelled, else the distance; aim_x / aim_y */
int  combatant_at(unsigned y, unsigned x);                /* e928: slot or -1 */
void random_near(int y, int x);                           /* e9b4: to dir_x / dir_y */
int  fire_missile(int anim, int hit, unsigned y, unsigned x, int attacker);   /* ea02: slot hit or -1 */
void consume_ammo(int hit, unsigned weapon);              /* eb5c */
int  check_interference(int slot);                        /* ebdc */
void ranged_attack(int anim, int weapon, unsigned slot);  /* ec48 */
void melee_attack(int weapon, unsigned target, int slot); /* edd8 */
void attack_with(int weapon, unsigned slot);              /* ee32 */
void attack_with_item(int weapon_count, int item);        /* ef1c */
void cmd_attack_combat(int weapon_count, int slot);       /* ef76 */
void trace_line(uint8_t *path_y, uint8_t *path_x, int to_y, int to_x, int from_y, int from_x);   /* f006 */
void draw_missile_frame(uint8_t *path_y, uint8_t *path_x, int kind, int frame, uint8_t y, uint8_t x);   /* f12a */
int  animate_missile(int kind, int to_y, int to_x, int from_y, int from_x);   /* f4be: 0 if stopped short (dir_x / dir_y) */

void pixel_to_square(int py, int px);                     /* 3fb4: view square to dir_x / dir_y, 0xFFFF outside */
int  is_square_visible(int y, int x);                     /* 3f6e */

#endif
