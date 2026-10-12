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

/* The view's buffers. `view` is the one View object.
 *
 * mask, tiles and local_map lie one after the other in one block, as they do in the original:
 * the line-of-sight code works on either the mask or the local map by an offset into the block,
 * and near the edges it steps outside the one it was given. at() takes such an offset; one
 * outside the block gives a spare byte. */
class View {
public:
    enum { MASK = 0, TILES = 0x162, LOCAL_MAP = 0x212, SIZE = 0x612 };

    static View &instance();

    uint8_t block[SIZE] = {};
    uint8_t (&mask)[0x160];        /* 11 rows of 32: the terrain tile to draw, 0xFF unseen, 0 an object */
    uint8_t (&tiles)[0xB0];        /* 11 rows of 16: the object's tile where the mask is 0 */
    uint8_t (&local_map)[0x400];   /* 32 x 32: a scene's own map (11 x 11 used), or the light map */

    uint8_t *at(long offset) { return offset >= 0 && offset < SIZE ? &block[offset] : &outside_; }

    uint8_t tile_anim_map[256] = {};   /* tile -> the tile shown for it now (cycle_animated_tiles) */

    /* Set when something changes what the party can see (a door, a wall, a move, a light): the
     * next world_tick works out line of sight and light afresh instead of only putting back the
     * terrain under the objects. (The original's byte for this, ds:24E6, was set to 1 or ORed
     * with 2, but only ever tested for 0.) */
    bool rebuild = true;
    uint8_t anim_tick = 0;           /* cycle_animated_tiles' counter */

    /* Saved state (registered with SaveGame in the constructor). */
    uint8_t anim_flag = 0;           /* ds:5891: 0 = no animation step this tick, 0xFF = never (Hercules) */
    uint8_t aim_mode = 0;            /* ds:5898: set while aim_cursor runs (draw_view shows the cursor) */
    uint8_t world_tick_enabled = 0;  /* ds:58A4: world_tick does nothing when 0 */
    uint8_t light_radius = 0;        /* ds:58A5: view radius in view_distance_level units: 50 day, 2 night/
                                      * underground, dawn/dusk from ds:6A80, >= 18 light spell, >= 10 torch;
                                      * > 0x32 = fixed (pass_time) */

    /* ---- the view: line of sight, light, objects, drawing and animation ---- */

    /* Read-only tables, from DATA.OVL. */
    int seen_across(uint8_t tile) const { return (0x80 >> (tile & 7)) & see_across_bits_[tile >> 3]; }   /* ds:6A14 */
    uint8_t dawn_radius(unsigned tens_of_minutes) const { return dawn_radius_[tens_of_minutes < 6 ? tens_of_minutes : 5]; }   /* ds:6A80 */

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

    View(const View &) = delete;
    View &operator=(const View &) = delete;

private:
    View();

    uint8_t see_across_bits_[32] = {};   /* ds:6A14  a bit per tile that can be seen and shot across */
    uint8_t dawn_radius_[6] = {};        /* ds:6A80  the light radius by ten minutes at dawn (5:00-5:59) */
    uint8_t opaque_tiles_[19] = {};      /* ds:6A86  tiles that block sight */
    uint8_t light_tiles_[10] = {};       /* ds:6A9A  tiles that give light */
    uint8_t distance_[6][6] = {};        /* ds:6AA8  a quarter of the view: 0 at the centre, 0x32 in the corners */
    uint8_t anim_script_of_[51] = {};    /* ds:1BC8  the animation script of each object tile group from 0x34 */
    uint8_t anim_scripts_[0xB0] = {};    /* ds:1B18  the scripts, 16 bytes each (a long one runs on into the next) */

    uint8_t outside_ = 0;
};

extern View &view;

inline uint8_t view_distance_level(int y, int x) { return view.view_distance_level(y, x); }
inline int is_transparent(int level, uint8_t tile) { return view.is_transparent(level, tile); }
inline void compute_line_of_sight(uint16_t mask, uint16_t stride, int col, int row, int x0, int y0, int radius) { view.compute_line_of_sight(mask, stride, col, row, x0, y0, radius); }
inline void compute_view_mask(uint16_t mode, int y, int x, int radius) { view.compute_view_mask(mode, y, x, radius); }
inline void update_light_map(void) { view.update_light_map(); }
inline void update_view_objects(void) { view.update_view_objects(); }
inline void view_object_fixup(int tile, unsigned y, unsigned x, int row, int col) { view.view_object_fixup(tile, y, x, row, col); }
inline void draw_view(void) { view.draw_view(); }
inline int random_anim_frame(void) { return view.random_anim_frame(); }
inline void cycle_animated_tiles(void) { view.cycle_animated_tiles(); }
inline void animate_objects(void) { view.animate_objects(); }
inline void world_tick(void) { view.world_tick(); }
inline void reset_tile_anim_map(void) { view.reset_tile_anim_map(); }

#endif
