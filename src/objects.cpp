/* objects.cpp - map object table helpers. objects[0] is the party; 1-31 are vehicles, people,
 * monsters and fields. */
#include "gfx.h"
#include "map.h"
#include "misc.h"
#include "objects.h"

void set_object_entry(int index, uint8_t hull, uint8_t z, uint8_t y, uint8_t x, uint8_t tile2, uint8_t tile)
{
    MapObject *o = &map.objects[index];
    o->tile = tile;
    o->tile2 = tile2;
    o->x = x;
    o->y = y;
    o->z = z;
    o->hull = hull;
}

/* First object 1-23 whose tile is in [min, max] (never 0xB5); far_only: only ones outside the
 * 11x11 view around the party. 0 if none. */
int find_object_in_tile_range(int far_only, unsigned max_tile, unsigned min_tile)
{
    for (int i = 1; i <= 0x17; i++) {
        MapObject *o = &map.objects[i];
        if (o->tile < min_tile || o->tile > max_tile || o->tile == 0xB5) continue;
        if (!far_only) return i;
        if ((uint8_t)(o->x - map.x + 5) > 10 || (uint8_t)(o->y - map.y + 5) > 10) return i;
    }
    return 0;
}

/* A free slot, or else one to reuse: out-of-sight objects first (1-0x0F, 0x80+, horses, 0x30-0x7F),
 * then the same anywhere, then any. */
int alloc_object_slot(void)
{
    static const uint8_t tries[][3] = {   /* far_only, max, min */
        {0, 0, 0}, {1, 0x0F, 1}, {1, 0xFF, 0x80}, {1, 0x11, 0x10}, {1, 0x7F, 0x30},
        {0, 0x0F, 1}, {0, 0xFF, 0x80}, {0, 0x11, 0x10}, {0, 0x7F, 0x30}, {0, 0xFF, 0},
    };
    for (unsigned i = 0; i < sizeof tries / sizeof tries[0]; i++) {
        int slot = find_object_in_tile_range(tries[i][0], tries[i][1], tries[i][2]);
        if (slot) return slot;
    }
    return 0;
}

/* From object 31 down to 0. z only counts outside combat. Empty slots match too (tile 0). */
uint8_t object_tile_at(unsigned z, unsigned y, unsigned x, uint16_t *slot)
{
    for (int i = 31; i >= 0; i--) {
        MapObject *o = &map.objects[i];
        if (o->x == x && o->y == y && (map.in_scene() || o->z == z)) {
            if (slot) *slot = (uint16_t)i;
            return o->tile;
        }
    }
    if (slot) *slot = 0xFFFF;
    return 0;
}

int find_object_at(uint8_t z, uint8_t y, uint8_t x, uint16_t *slot)
{
    for (int i = 1; i < 32; i++) {
        MapObject *o = &map.objects[i];
        if (o->x == x && o->y == y && (map.in_scene() || o->z == z)) {
            if (slot) *slot = (uint16_t)i;
            return o->tile;
        }
    }
    if (slot) *slot = 31;   /* where the original's search ends */
    return 0;
}

int random_upto(int max) { return random_between(max, 0); }
