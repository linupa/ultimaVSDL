/* cmds.h - command handlers from CMDS.OVL, plus select_spell (CAST2.OVL), which Mix needs.
 * Parameter order follows the Ghidra signatures. */
#ifndef U5_CMDS_H
#define U5_CMDS_H

#include <stdint.h>

int  camp_rest(int hours, unsigned guard, unsigned flags);   /* bf80: 1 = ambushed, a fight follows */
int  combat_escape(void);                    /* d76c: 1 = refused */
void cmd_hole_up_bed(void);                  /* c4d2: H on a bed */
int  board_check_on_foot(void);              /* c66e */
int  board_check_ship_ok(void);              /* c68c */
int  is_land_adjacent(void);                 /* c6be */
int  is_land_tile(int x, int y);             /* c708: view square (col x, row y) */
int  cmd_board(void);                        /* c776 */
void fire_ship_cannons(void);                /* c8e2 */
void cmd_fire(void);                         /* ca6a */
void cmd_ignite_torch(void);                 /* cd18 */
void cmd_new_order(void);                    /* cd5c */
void cmd_exit(void);                         /* ce34 */
void yell_summon_shadowlord(const char *word);            /* cfb0 */
void meditate_restore_shrine(unsigned y, unsigned x, int shrine);   /* d182 */
void yell_word_of_power(const char *word);   /* d248 */
int  cmd_yell(void);                         /* d398 */
int  is_pushable_tile(uint8_t tile);         /* d43a */
unsigned rotate_pushed_tile(int pulled, unsigned tile, int dy, int dx);   /* d484 */
void push_object(int dy, int dx, unsigned to_y, unsigned to_x, unsigned from_y, unsigned from_x, uint8_t dest_tile, uint8_t tile);   /* d4c8 */
void pull_object(int dy, int dx, unsigned to_y, unsigned to_x, unsigned from_y, unsigned from_x, uint8_t dest_tile, uint8_t tile);   /* d530 */
void cmd_push(void);                         /* d59a */
unsigned mix_select_reagents(void);          /* d83e: bit mask (0x80 = reagent 0), 0xFFFF = Esc */
unsigned mix_ask_amount(unsigned reagent_mask);   /* d9f0 */
void cmd_mix_reagents(void);                 /* da58 */
void cmd_klimb_grapple(void);                /* dba0 */

int  select_spell(void);                     /* CAST2 e2be: index 0-47, -1 nothing typed, -2 no such spell */

#endif
