/* sjog.h - Search, Jimmy, Open and Get outside the dungeons and combat (SJOG.OVL c7xx-daxx).
 * Parameter order follows the Ghidra signatures. The dungeon versions are in dungeon.c and the
 * combat half of the overlay in combat.c.
 *
 * Small objects on the map are MapObjects whose tile is the kind and whose `hull` is the amount
 * or id: 1 chest (hull = level, bit 7 = trapped), 2 gold, 3 potion, 4 scroll, 5/6 and 9-0xC
 * equipment (hull = item id), 7 keys, 8 gems, 0xD torches, 0xE sandalwood box, 0xF food, 0x19
 * moonstone, 0x1B carpet, 0x1E/0x1F bodies, 0xB4-0xB7 shards, crown, sceptre and amulet. */
#ifndef U5_SJOG_H
#define U5_SJOG_H

#include <stdint.h>

int  find_free_object_slot(void);                    /* bf80: highest free slot, 0 if none */
void print_found_item(unsigned kind);                /* c0aa */
void search_body(int member, int object);            /* c172 */
void search_chest_trap(int member, int object);      /* c26a */
int  search_moonstone(unsigned z, unsigned y, unsigned x);   /* c328 */
int  search_reagent_spot(unsigned y, unsigned x);    /* c3da */
void search_hidden_item(unsigned y, unsigned x);     /* c494 */
void cmd_search(void);                               /* c8dc */
void jimmy_chest(int object);                        /* cb2a */
void cmd_jimmy(void);                                /* ccca */
void spawn_chest_item(int *found, int level, uint8_t z, uint8_t y, uint8_t x, int amount, int kind);   /* cf08 */
void chest_roll_items(int *found, unsigned z, unsigned y, unsigned x, unsigned level);   /* cfc0 */
void chest_roll_equipment(int *found, uint8_t z, uint8_t y, uint8_t x, unsigned level); /* d038 */
void open_chest(unsigned z, unsigned y, unsigned x); /* d0ac */
void cmd_open(void);                                 /* d2f4 */
void get_item(int slot, unsigned amount, int kind);  /* d3d8 */
void cmd_get(void);                                  /* d84e */

#endif
