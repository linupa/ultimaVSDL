/* objects.h - the map object table helpers of the main program (0000:3702-0000:3aae). */
#ifndef U5_OBJECTS_H
#define U5_OBJECTS_H

#include <stdint.h>

void set_object_entry(int index, uint8_t hull, uint8_t z, uint8_t y, uint8_t x, uint8_t tile2, uint8_t tile); /* 3a74 */
int  find_object_in_tile_range(int far_only, unsigned max_tile, unsigned min_tile);  /* 3868 */
int  alloc_object_slot(void);                    /* 38e4 */
uint8_t object_tile_at(unsigned z, unsigned y, unsigned x);   /* 3702: tile there, 0 if none; dir_x = index */
int  find_object_at(uint8_t z, uint8_t y, uint8_t x);        /* 368e: tile there (objects 1-31), dir_x = index */
int  random_upto(int max);                       /* 3aae: random_between(max, 0) */

#endif
