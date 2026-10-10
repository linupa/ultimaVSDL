/* tiles.h - the 16x16 tile set (TILES.16) and the driver's tile functions.
 *
 * Tiles 0-255 are terrain; 256-511 are objects, vehicles and creatures. The game stores object
 * tiles as a byte and draws them as tile + 0x100 (draw_view).
 *
 * The tile set is kept in EGA.DRV's planar layout (drv 0x48 converts it at load): 128 bytes per
 * tile, byte (row * 8 + half * 4 + plane) holds 8 pixels (bit 7 = left) of one colour plane, half 0
 * = left 8 pixels. The per-turn animation (drv 0x60) edits these bytes directly, so the port keeps
 * the same layout and ports that code byte for byte.
 *
 * CGA.DRV and HER.DRV use TILES.4 unchanged: 64 bytes per tile, byte (row * 4 + x / 4) holds four
 * pixels of two bits. Their animation works on those bytes. */
#ifndef U5_TILES_H
#define U5_TILES_H

#include <stdint.h>

#define TILE_COUNT 512
#define TILE_BYTES 128

extern uint8_t tileset[TILE_COUNT * TILE_BYTES];

int  load_tileset(const char *filename);           /* 0ff4 + drv 0x48: 1 ok, 0 error */
void draw_tile_at(int row, int col, int tile);     /* 10e0 + drv 0x51: view square (row, col) */
void animate_tiles_alt(void);                      /* 6fd6 + drv 0x60 (CF clear): water, fields, flags */
void shimmer_tiles(int row, int col, int frame);   /* 1112 + drv 0x60 (CF set): moongate risen `frame` rows */
void free_tileset(void);                           /* 102e */
void patch_tiles(int n);                           /* 6f9e + drv 0x6C mode n + 3: 0 dungeon room colours, 1 the last scene */
void ladder_tiles_patch(int restore);              /* drv 0x6C modes 0 / 1 (patch_tiles_b) */
void patch_tiles_c(void);                          /* 71aa + drv 0x6C mode 2: the town clock's hands */

uint8_t tile_pixel(int tile, int x, int y);        /* colour of one pixel, for tests */

#endif
