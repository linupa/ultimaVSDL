/* view.h - the 11x11 map view: line of sight, light, objects, drawing and animation
 * (main program 0000:44b8-0000:7040).
 *
 * view_mask (11 rows of 32 bytes) holds, per square, the terrain tile to draw, 0xFF if the square
 * can't be seen, or 0 if an object is there; view_tiles (11 rows of 16) then holds the object's
 * tile, drawn as tile + 0x100. local_map holds the light map: per square of the 32x32 map window,
 * non-zero if a light source (tiles at ds:6A9A) reaches it. The party is at view square (5,5). */
#ifndef U5_VIEW_H
#define U5_VIEW_H

#include <stdint.h>

uint8_t view_distance_level(int y, int x);    /* 6ff0: distance from the centre, table ds:6AA8 */
int  is_transparent(int level, uint8_t tile); /* 5dfe (was blocks_sight): 1 if sight passes */
void compute_line_of_sight(uint16_t mask, uint16_t stride, int col, int row, int x0, int y0, int radius); /* 5a28 */
void compute_view_mask(uint16_t mode, int y, int x, int radius);   /* 5d0a */
void update_light_map(void);                  /* 5e4a */
void update_view_objects(void);               /* 5394 */
void view_object_fixup(int tile, unsigned y, unsigned x, int row, int col);   /* 51b8 */
void draw_view(void);                         /* 56ac */
int  random_anim_frame(void);                 /* 51a0 */
void cycle_animated_tiles(void);              /* 44b8 */
void animate_objects(void);                   /* 4552 */
void world_tick(void);                        /* 5910: animate, rebuild the view, draw it */

void reset_tile_anim_map(void);               /* intro_main: tile_anim_map[i] = i */

#endif
