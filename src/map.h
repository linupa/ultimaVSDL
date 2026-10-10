/* map.h - the map in memory: outdoor chunks and town levels (map_buffer), and moving around them.
 * Ports of get_map_tile_ptr (main program) and of the loaders in the OUTSUBS, MAINOUT and TOWN
 * overlays. Coordinates are absolute map coordinates (outdoors 0-255, towns 0-31). */
#ifndef U5_MAP_H
#define U5_MAP_H

#include <stdint.h>

uint8_t *get_map_tile_ptr(unsigned y, unsigned x);                              /* 4402 */
int  load_file_part(uint32_t offset, unsigned size, uint8_t *buf, const char *filename);   /* 256e */
int  save_file_part(unsigned size, const uint8_t *buf, const char *filename);   /* writes the file from offset 0 */

/* OUTSUBS */
int  dungeon_entrance_sealed(unsigned chunk);                                  /* a290 */
int  shrine_destroyed(unsigned chunk);                                         /* a2da */
void load_outdoor_chunk(unsigned chunk, int slot, const char *filename);      /* a328 */
void load_outdoor_chunks(int dy, int dx);                                     /* a444 */
void shift_outdoor_chunks(int dy, int dx);                                    /* a558 */

/* MAINOUT, map parts only */
void enter_outdoors(void);                     /* 81d0 */
void outdoor_step(int dy, int dx);             /* 8524 */

/* TOWN, map parts only */
void load_town_level(int place_npcs_too);      /* 85d8: 1 = also place the NPCs */

/* pass_time (4f7c): the clock, torch and light spell countdown, rest cooldown, the Shadowlords'
 * daily moves, the calendar, then the light radius. Quickness halves time, time stop freezes it. */
void pass_time(int minutes);
void update_light_radius(void);   /* the light-radius part of pass_time */

#endif
