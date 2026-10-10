/* view.c - the 11x11 map view. See view.h.
 *
 * The line-of-sight code works on raw ds offsets (the mask argument is one), as the original
 * does: it can write a little outside its 11x11 area (row -1, columns -1 and 11), and those writes
 * land in the neighbouring globals exactly as in the game. */
#include <string.h>
#include "combat.h"
#include "data.h"
#include "gfx.h"
#include "map.h"
#include "tiles.h"
#include "view.h"
#include "world.h"

#define VIEW_MASK  ((int)offsetof(struct DGroup, view_mask))
#define VIEW_TILES ((int)offsetof(struct DGroup, view_tiles))
#define LOCAL_MAP  ((int)offsetof(struct DGroup, local_map))

#define dsp(off) ds_at(off)

#define MASK(r, c)  (*dsp(VIEW_MASK + (r) * 0x20 + (c)))
#define TILES(r, c) (*dsp(VIEW_TILES + (r) * 0x10 + (c)))

void reset_tile_anim_map(void)
{
    for (int i = 0; i < 256; i++) dg.tile_anim_map[i] = (uint8_t)i;
}

/* ds:6AA8 is a 6x6 quarter of the view: 0 at the centre, 0x32 in the corners. */
uint8_t view_distance_level(int y, int x)
{
    if (x >= 0x0B || y >= 0x0B) return 0;
    if (x > 5) x = 10 - x;
    if (y > 5) y = 10 - y;
    return *dsp(0x6AA8 + x + y * 6);
}

/* Opaque tiles are listed at ds:6A86 (19 bytes). Tiles 0x4A, 0x4B, 0xBA, 0xBB and 0x98 can be
 * seen through only from the next square (level 1). */
int is_transparent(int level, uint8_t tile)
{
    if (tile == 0x4B || tile == 0x4A || tile == 0xBA || tile == 0xBB || tile == 0x98) return level == 1;
    return memchr(ds_ptr(0x6A86), tile, 0x13) == NULL;
}

/* Breadth-first from the centre (5,5) of an 11x11 area. Squares are numbered from (x0, y0), which
 * is relative to map_origin.
 *  - View mode (row == -0x6F, mask = view_mask): marks what the party sees. Within the light
 *    radius every reached square shows its tile. Beyond it a square shows only if the light map
 *    lights it (an opaque one also needs its lit, visible neighbour); otherwise 0xFF / 0.
 *  - Light mode (mask = local_map at the source): marks the squares a light source reaches
 *    (radius 10), using view_mask as the visited set. */
void compute_line_of_sight(uint16_t mask, uint16_t stride, int col, int row, int x0, int y0, int radius)
{
    (void)stride;   /* rows are always 0x20 bytes */
    int queue[257], head = 0, tail = 2;
    if (radius <= 0) return;
    int view_mode = row == -0x6F;
    if (view_mode) row = 0;
    queue[0] = 5;
    queue[1] = 5;
    *dsp(mask + row * 0x20 + col + 0xA5) = *get_map_tile_ptr((unsigned)(y0 + dg.map_origin_y + 5),
                                                             (unsigned)(x0 + dg.map_origin_x + 5));
    while (head != tail) {
        int pr = queue[head], pc = queue[head + 1];
        head += 2;
        int r = pr, c = pc;
        for (int dir = 7; dir >= 0; dir--) {   /* walk round the 8 neighbours */
            switch (dir) {
            case 0: case 1: case 7: c--; break;
            case 2: case 3: r--; break;
            case 4: case 5: c++; break;
            case 6: r++; break;
            }
            int ok = c >= 0 && c <= 10 && r >= 0 && r <= 10;
            uint8_t *p = dsp(mask + (row + r) * 0x20 + col + c);
            if (view_mode) {
                if (*p != 0xFF) ok = 0;
            } else if (MASK(r, c) == 0 || row + r > 0x1F) {
                ok = 0;
            } else {
                MASK(r, c) = 0;
            }
            if (!ok) continue;

            uint8_t t = *get_map_tile_ptr((unsigned)(y0 + r + dg.map_origin_y), (unsigned)(x0 + c + dg.map_origin_x));
            if (view_distance_level(r, c) < radius + 1) {
                *p = t;
            } else if (view_mode) {
                int lit_here = *dsp(LOCAL_MAP + c + (y0 + r) * 0x20 + x0) != 0;
                if (!is_transparent(view_distance_level(r, c), t)) {
                    if (*dsp(mask + pr * 0x20 + pc) == 0 || *dsp(LOCAL_MAP + pc + (y0 + pr) * 0x20 + x0) == 0 || !lit_here) {
                        *p = 0xFF;
                        t = 0xFF;
                    } else {
                        *p = t;
                    }
                } else if (y0 + r >= 0 && x0 + c >= 0 && x0 + c < 0x20 && y0 + r < 0x20 && lit_here) {
                    *p = t;
                } else {
                    *p = 0;   /* dark but see-through: keep looking past it */
                }
            } else {
                t = 0xFF;
            }
            if (is_transparent(view_distance_level(r, c), t) && tail < 256) {
                queue[tail++] = r;
                queue[tail++] = c;
            }
        }
    }
}

/* y, x: the party relative to map_origin. radius < 0: no line of sight, copy the whole 11x11. */
void compute_view_mask(uint16_t mode, int y, int x, int radius)
{
    (void)mode;
    for (int r = 0; r < 11; r++) memset(&MASK(r, 0), 0xFF, 11);
    if (radius > 0) {
        compute_line_of_sight((uint16_t)VIEW_MASK, 0x20, 0, -0x6F, x - 5, y - 5, radius);
        for (int r = 0; r < 11; r++)
            for (int c = 0; c < 11; c++)
                if (MASK(r, c) == 0) MASK(r, c) = 0xFF;
    }
    if (radius < 0)
        for (int r = 0; r < 11; r++)
            for (int c = 0; c < 11; c++)
                MASK(r, c) = *get_map_tile_ptr((unsigned)(dg.map_origin_y + y - 5 + r), (unsigned)(x - 5 + c + dg.map_origin_x));
}

/* Light sources in the 32x32 window: tiles listed at ds:6A9A (10 bytes). The original keeps up to
 * 32 of them on its stack without checking; the port stops at 32. */
void update_light_map(void)
{
    int sources[64], n = 0;
    memset(dg.local_map, 0xFF, sizeof dg.local_map);
    for (int y = 0; y < 0x20; y++)
        for (int x = 0; x < 0x20; x++) {
            uint8_t t = *get_map_tile_ptr((unsigned)(y + dg.map_origin_y), (unsigned)(dg.map_origin_x + x));
            if (memchr(ds_ptr(0x6A9A), t, 10) && n < 64) {
                sources[n++] = y;
                sources[n++] = x;
                dg.local_map[y * 0x20 + x] = t;
            }
        }
    for (int i = 0; i < n; i += 2) {
        int sy = sources[i] - 5, sx = sources[i + 1] - 5;
        for (int r = 0; r < 11; r++) memset(&MASK(r, 0), 0xFF, 11);
        compute_line_of_sight((uint16_t)LOCAL_MAP, 0x20, sx, sy, sx, sy, 10);
    }
    for (int i = 0; i < (int)sizeof dg.local_map; i++)
        if (dg.local_map[i] == 0xFF) dg.local_map[i] = 0;
}

int random_anim_frame(void)
{
    return dg.active_spell == 0x54 ? 0 : random_between(3, 0);   /* 'T' time stop freezes */
}

/* People shown sitting in chairs, lying in beds, at the forge and so on: the object's tile is
 * replaced depending on the terrain under it. Only for the party on foot (0x1C), carpets
 * (0x12-0x15), skiffs (0x28-0x2B) and creatures (>= 0x40). */
void view_object_fixup(int tile, unsigned y, unsigned x, int row, int col)
{
    if (tile == 0x1C || (tile >= 0x12 && tile <= 0x15) || tile >= 0x40 || (tile >= 0x28 && tile <= 0x2B)) {
        unsigned u = *get_map_tile_ptr(y, x);
        if (u == 0xEC || u == 0x0A) return;                       /* hidden */
        if (u == 0x57) { MASK(row, col) = 0x38; return; }
        if (u == 0x6A || u == 0x6B) {
            if ((tile & 0xF0) == 0x80 || (tile & 0xFC) == 0x28) return;
        } else if (tile <= 0x7F) {
            int done = 1;
            switch (u) {
            case 0x92:
                tile = (*get_map_tile_ptr(y + 1, x) == 0x9A || *get_map_tile_ptr(y + 1, x) == 0x9C)
                       ? random_anim_frame() + 0x34 : 0x32;
                break;
            case 0x84: tile = random_anim_frame() + 0x60; break;
            case 0x85: tile = random_anim_frame() + 0x64; break;
            case 0x90:
                tile = (*get_map_tile_ptr(y - 1, x) == 0x9B || *get_map_tile_ptr(y - 1, x) == 0x9C)
                       ? random_anim_frame() + 0x38 : 0x30;
                break;
            case 0x91: case 0x93: tile = (int)(u & 3) + 0x30; break;
            case 0x9D: case 0x9E: tile = random_anim_frame() + 0x3C; break;
            case 0xAB: tile = 0x1A; break;
            case 0xC8: tile = 0x17; break;
            case 0xC9: tile = 0x18; break;
            default: done = 0; break;
            }
            /* Under the top half of a bed (0x9D) the square above shows 0x9E. The original writes
             * view_mask[row - 1][col] through objects_backup + 0xE6. */
            if (!done && *get_map_tile_ptr(y - 1, x) == 0x9D && row != 0) MASK(row - 1, col) = 0x9E;
        }
    }
    TILES(row, col) = (uint8_t)tile;
    MASK(row, col) = 0;
}

void update_view_objects(void)
{
    if (dg.location < 0x80) {
        dg.objects[0].x = dg.party_x;
        dg.objects[0].y = dg.party_y;
        dg.objects[0].z = dg.z_level;
        dg.objects[0].tile = dg.transport;
        dg.objects[0].tile2 = dg.transport;
        /* 0xDD and 0x1C swap with distance from the party (5 = the boundary). */
        for (int r = 0; r < 11; r++)
            for (int c = 0; c < 11; c++) {
                if (MASK(r, c) == 0xDD && view_distance_level(r, c) > 5) MASK(r, c) = 0x1C;
                else if (MASK(r, c) == 0x1C && view_distance_level(r, c) < 6) MASK(r, c) = 0xDD;
            }
    }
    /* From the last object to the first, so object 0 (the party) ends up on top. */
    for (int i = 31; i >= 0; i--) {
        MapObject *o = &dg.objects[i];
        if (o->tile == 0) continue;
        uint8_t col = o->x, row = o->y;
        if (dg.location < 0x80) {
            col = (uint8_t)(o->x - (dg.party_x - 5));
            row = (uint8_t)(o->y - (dg.party_y - 5));
            if (o->z != dg.z_level || col > 10 || row > 10) continue;
        }
        if (o->tile2 == 0 || MASK(row, col) == 0xFF || MASK(row, col) == 0x87) continue;
        if ((o->tile & 0xFC) == 0xE8 || o->tile == 0x1E || o->tile == 0x1F) {   /* fields, ... */
            if (MASK(row, col) != 0) {
                TILES(row, col) = o->tile2;
                MASK(row, col) = 0;
            }
        } else if (o->tile2 == 0x1D || o->tile2 == 0x1E) {
            TILES(row, col) = o->tile2;
            MASK(row, col) = 0;
        } else if (o->tile == 0x5C && MASK(row, col) == 0x92) {
            TILES(row, col) = o->tile2;
            MASK(row, col) = 0;
        } else {
            view_object_fixup(o->tile == 0x5C ? o->tile2 - 8 : o->tile2, o->y, o->x, row, col);
        }
    }
}

void draw_view(void)
{
    for (int r = 0; r < 11; r++)
        for (int c = 0; c < 11; c++) {
            uint8_t m = MASK(r, c);
            if (m == 0) {
                if (TILES(r, c) != 0x16) draw_tile_at(r, c, 0x100 + TILES(r, c));
            } else if (m == 0xDC && dg.moongate_phase != 0 && dg.moongate_phase < 0x10) {
                shimmer_tiles(r, c, dg.moongate_phase);
            } else {
                draw_tile_at(r, c, dg.tile_anim_map[m]);
            }
        }
    /* In combat: every other draw, a frame around the fighter whose turn it is (if on the
     * party's side) and, while aiming, the cross-hairs. */
    if (dg.location <= 0x7F) return;
    dg.combat_redraw = dg.combat_redraw == 0;
    if (!dg.combat_redraw || dg.combat_turn == 0xFF || combatant_side(dg.combat_turn) != 0) return;
    Combatant *c = &dg.combatants[dg.combat_turn & 0x1F];
    int px = c->x * 0x10, py = c->y * 0x10;
    set_draw_color(dg.color_text);
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 0x0F; j += 0x0E) {
            draw_hline(px + 0x17, py + 8 + i + j, px + 8);
            draw_vline(py + 0x17, py + 8, px + 8 + i + j);
        }
    if (dg.aim_mode == 0) return;
    int ax = dg.aim_x * 0x10, ay = dg.aim_y * 0x10;
    for (int i = 0; i < 2; i++) {
        int y_out = ay + 10 + 11 * i, y_in = ay + 14 + 3 * i, y_gap = ay + 13 + 5 * i, row = ay + i + 15;
        for (int j = 0; j < 2; j++) {
            int x_out = ax + 10 + 11 * j, x_in = ax + 14 + 3 * j, x_gap = ax + 13 + 5 * j, col = ax + j + 15;
            set_draw_color(dg.color_text);
            draw_line(y_in, x_in, y_in, x_out);
            draw_line_b(y_out, x_in);
            set_draw_color(0);
            draw_line(y_gap, x_gap, y_gap, x_out);
            draw_line_b(y_out, x_gap);
            draw_line(row, x_in, row, x_out);
            draw_line(y_out, col, y_in, col);
        }
    }
}

void cycle_animated_tiles(void)
{
    uint8_t *m = dg.tile_anim_map;
    for (int i = 0xD4; i < 0xD8; i++) if (++m[i] == 0xD8) m[i] = 0xD4;
    for (int i = 0xD8; i < 0xDC; i++) if (++m[i] == 0xDC) m[i] = 0xD8;
    if (dg.anim_tick & 1) {
        for (int i = 0x80; i < 0x84; i++) m[i] ^= 1;
        for (int i = 0xEC; i < 0xF0; i++) if (++m[i] == 0xF0) m[i] = 0xEC;
        if (dg.anim_tick & 2)
            for (int i = 0xFA; i < 0xFE; i++) m[i] ^= 1;
    }
    dg.anim_tick++;
}

/* Each object's flags: low nibble = turns to wait (0xF = never animate), high nibble = position
 * in its animation script, 16 bytes per script at ds:1B18, script number by tile group at
 * ds:1BC8 + (tile - 0x34) / 4. Script bytes: 1-4 show frame n-1 of the 4-tile group; 5 / 6 random
 * branches; 0 restart; 7 go to step 2; 0x8n wait n turns. */
void animate_objects(void)
{
    for (int i = 0; i < 32; i++) {
        MapObject *o = &dg.objects[i];
        if (o->tile == 0) continue;
        uint8_t wait = o->flags & 0x0F;
        unsigned step = o->flags >> 4;
        unsigned base = o->tile & 0xFC;
        uint8_t script = *dsp(0x1BC8 + (((int)base - 0x34) >> 2));
        if (wait == 0x0F) continue;
        if (wait != 0) {
            o->flags = (uint8_t)((o->flags & 0xF0) + wait - 1);
            continue;
        }
        if (o->tile2 == 0 || o->tile2 == 0x1D || o->tile2 == 0x1E || base <= 0x33 || base == 0xE8 || base == 0xB4)
            continue;
        if (!(base == 0x5C || base == 0xA8 || random_between(0xFF, 0) > 0x7F)) continue;
        int done = 0;
        do {
            uint8_t op = *dsp(0x1B18 + (int)step + script * 0x10);
            switch (op) {
            case 1: case 2: case 3: case 4:
                o->tile2 = (uint8_t)(base + op - 1);
                step++;
                done = 1;
                break;
            case 5:
                if (random_between(0xFF, 0) > 0x3F) { step++; break; }
                o->tile2 = o->tile;
                if (base == 0x5C) step++;
                else wait = 6;
                done = 1;
                break;
            case 6:
                if (random_between(0xFF, 0) > 0xBF) { step++; break; }
                step = 0;
                break;
            case 0:
                step = 0;
                break;
            case 7:
                step = 2;
                break;
            default:
                wait = (uint8_t)(op + 0x80);
                step++;
                done = 1;
                break;
            }
            o->flags = (uint8_t)(step * 0x10 + wait);
        } while (!done);
    }
    cycle_animated_tiles();
    animate_tiles_alt();
}


void world_tick(void)
{
    dg.last_disk_checked = 0xFF;
    if (dg.active_spell == 0x54) dg.anim_flag = 0;
    if (dg.world_tick_enabled) {
        if (dg.anim_flag) {
            if (dg.anim_flag != 0xFF) animate_objects();
            random_wind_change();
            if (dg.location < 0x80) {
                update_moongates();
                animate_light_beams();
            }
        }
        if (dg.location < 0x80) {
            if (dg.redraw_flags == 0) {
                /* squares that showed an object get their terrain back before objects are redone */
                for (int r = 0; r < 11; r++)
                    for (int c = 0; c < 11; c++)
                        if (MASK(r, c) == 0)
                            MASK(r, c) = *get_map_tile_ptr((unsigned)(r + dg.party_y - 5), (unsigned)(c + dg.party_x - 5));
            } else {
                compute_view_mask(0x0B, dg.party_y - dg.map_origin_y, dg.party_x - dg.map_origin_x, dg.light_radius);
                dg.redraw_flags = 0;
            }
        } else {
            memcpy(dg.view_mask, dg.local_map, 0x160);   /* combat: the 11x11 map is the view */
        }
        update_view_objects();
        draw_view();
        if (dg.anim_flag) ambient_sounds();
    }
    dg.anim_flag = 1;
}
