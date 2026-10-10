/* map.c - map storage and loading. */
#include <stdio.h>
#include <string.h>
#include "data.h"
#include "files.h"
#include "gfx.h"
#include "map.h"
#include "misc.h"
#include "outdoor.h"
#include "status.h"
#include "tiles.h"
#include "town.h"
#include "view.h"
#include "world.h"

/* Outdoors, map_buffer holds a 32x32 window of Britannia at map_origin as four 16x16 chunks
 * (slots 0 1 / 2 3). In towns it is the 32x32 level; outside it reads map_buffer[0x3FF]. In combat
 * (location >= 0x80) the map is local_map. */
uint8_t *get_map_tile_ptr(unsigned y, unsigned x)
{
    if (dg.location >= 0x80) return &dg.local_map[((y * 0x20 + x) & 0xFFFF) % sizeof dg.local_map];
    if (dg.location == 0) {
        x = (x - dg.map_origin_x) & 0x1F;
        y = (y - dg.map_origin_y) & 0x1F;
        unsigned slot = 0;
        if (y > 0x0F) { slot = 2; y -= 0x10; }
        if (x > 0x0F) { slot++; x -= 0x10; }
        return &dg.map_buffer[slot * 0x100 + y * 0x10 + x];
    }
    if ((int)x < 0 || (int)y < 0 || (int)x > 0x1F || (int)y > 0x1F) return &dg.map_buffer[0x3FF];
    return &dg.map_buffer[y * 0x20 + x];
}

/* The original also handles disk swapping (select_disk) and retries. */
int load_file_part(uint32_t offset, unsigned size, uint8_t *buf, const char *filename)
{
    FILE *f = game_fopen(filename, "rb");
    if (!f) return -1;
    size_t n = 0;
    if (fseek(f, (long)offset, SEEK_SET) == 0) n = fread(buf, 1, size, f);
    fclose(f);
    return n == size ? 0 : -1;
}

int save_file_part(unsigned size, const uint8_t *buf, const char *filename)
{
    FILE *f = game_fopen(filename, "wb");
    if (!f) return -1;
    size_t n = fwrite(buf, 1, size, f);
    fclose(f);
    return n == size ? 0 : -1;
}

/* Chunk numbers of the dungeon entrances (ds:3866) and shrines (ds:386E). Entrances are sealed
 * unless the matching dungeon_seals byte is 0; an entrance not in the table counts as sealed. */
int dungeon_entrance_sealed(unsigned chunk)
{
    for (int i = 0; i < 8; i++)
        if (ds_u8(0x3866 + i) == chunk >> 8) return dg.dungeon_seals[i] == 0;
    return 1;
}

int shrine_destroyed(unsigned chunk)
{
    for (int i = 0; i < 8; i++)
        if (ds_u8(0x386E + i) == chunk >> 8) return dg.shrine_state[i] > 0x7F;
    return 0;
}

/* chunk = (chunk number << 8), which is also the offset in UNDER.DAT. BRIT.DAT stores only the
 * chunks listed in ds:3876 (0xFF = open ocean, filled with tile 1). */
void load_outdoor_chunk(unsigned chunk, int slot, const char *filename)
{
    unsigned offset;
    if (filename[0] == 'B') {
        uint8_t index = ds_u8(0x3876 + (chunk >> 8));
        if (index == 0xFF) {
            memset(&dg.map_buffer[slot * 0x100], 1, 0x100);
            return;
        }
        offset = (unsigned)index << 8;
    } else {
        offset = chunk;
    }
    load_file_part(offset, 0x100, &dg.map_buffer[slot * 0x100], filename);
    unsigned cx = (chunk >> 4) & 0xF0, cy = (chunk >> 8) & 0xF0;
    for (int y = 0; y < 0x10; y++)
        for (int x = 0; x < 0x10; x++) {
            uint8_t t = dg.map_buffer[slot * 0x100 + y * 0x10 + x];
            if (t <= 0x15) continue;
            if (t < 0x19) {
                if (dungeon_entrance_sealed(chunk)) *get_map_tile_ptr(y + cy, x + cx) = 0xDF;
            } else if (t == 0x19 && shrine_destroyed(chunk)) {
                *get_map_tile_ptr(y + cy, x + cx) = 0x1A;
            }
        }
}

/* Loads the chunks on the side the party moved to (dy, dx); (1,0) + (-1,0) loads all four. */
void load_outdoor_chunks(int dy, int dx)
{
    unsigned chunk = (unsigned)(dg.map_origin_x * 0x10 + dg.map_origin_y * 0x100) & 0xFFFF;
    unsigned right = dg.map_origin_x == 0xF0 ? (unsigned)-0xF00 : 0x100;   /* wraps east to west */
    const char *file = dg.z_level < 0x80 ? ds_str(0x3980) : ds_str(0x3976);   /* BRIT.DAT / UNDER.DAT */
    if (dx == -1 || dy == -1) load_outdoor_chunk(chunk, 0, file);
    if (dx == 1 || dy == -1) load_outdoor_chunk((chunk + right) & 0xFFFF, 1, file);
    if (dx == -1 || dy == 1) load_outdoor_chunk((chunk + 0x1000) & 0xFFFF, 2, file);
    if (dx == 1 || dy == 1) load_outdoor_chunk((chunk + right + 0x1000) & 0xFFFF, 3, file);

    /* The lighthouse light (tile 0x1B) for animate_light_beams. */
    uint8_t *p = memchr(dg.map_buffer, 0x1B, 0x400);
    if (!p) {
        dg.beam1_y = 0xFFFF;
        dg.beam1_x = 0xFFFF;
    } else {
        unsigned off = (unsigned)(p - dg.map_buffer);
        dg.beam1_x = off & 0x0F;
        dg.beam1_y = (off >> 4) & 0x0F;
        if (off & 0x100) dg.beam1_x += 0x10;
        if (off & 0x200) dg.beam1_y += 0x10;
    }
}

/* Keeps the two chunks that stay in the window, moving them to their new slots. */
void shift_outdoor_chunks(int dy, int dx)
{
    int to = (dx < 0 || dy < 0) ? 3 : 0;
    int from = (dx < 0 || dy > 0) ? 2 : 1;
    memcpy(&dg.map_buffer[to * 0x100], &dg.map_buffer[from * 0x100], 0x100);
    memcpy(&dg.map_buffer[(to + dy + dx * 2) * 0x100], &dg.map_buffer[(from + dy + dx * 2) * 0x100], 0x100);
}

void enter_outdoors(void)
{
    clear_location_label();
    pass_time(0);
    dg.redraw_flags = 1;
    dg.need_prompt = 1;
    dg.world_tick_enabled = 1;
    dg.sail_direction = 0;
    dg.map_origin_x = dg.party_x & 0xF0;
    if ((dg.party_x & 0x0F) < 8) dg.map_origin_x -= 0x10;
    dg.map_origin_y = dg.party_y & 0xF0;
    if ((dg.party_y & 0x0F) < 8) dg.map_origin_y -= 0x10;
    load_outdoor_chunks(1, 0);
    load_outdoor_chunks(-1, 0);
    pass_time(0);
    update_light_map();
    set_wind_direction(-1);
    draw_sky_bar();
    place_underworld_specials();
}

/* Moves the party; near the window edge, scrolls the window by one chunk. */
void outdoor_step(int dy, int dx)
{
    dg.party_x = (uint8_t)(dg.party_x + dx);
    dg.party_y = (uint8_t)(dg.party_y + dy);
    dg.redraw_flags = 1;
    unsigned rx = (unsigned)(dg.party_x - dg.map_origin_x) & 0x1F;
    unsigned ry = (unsigned)(dg.party_y - dg.map_origin_y) & 0x1F;
    if (rx < 5 || rx > 0x1A || ry < 5 || ry > 0x1A) {
        shift_outdoor_chunks(dy, dx);
        dg.map_origin_x = (uint8_t)((dx * 0x10 + dg.map_origin_x) & 0xF0);
        dg.map_origin_y = (uint8_t)((dy * 0x10 + dg.map_origin_y) & 0xF0);
        load_outdoor_chunks(dy, dx);
        update_light_map();
    }
}

void load_town_level(int place_npcs_too)
{
    clear_location_label();
    draw_sky_bar();
    set_wind_direction(-1);
    dg.open_door_tile = 0;
    dg.map_origin_y = 0;
    dg.map_origin_x = 0;
    int level = dg.town_level_index[dg.location] + dg.z_level;
    if (dg.z_level > 0x7F) level -= 0x100;
    load_file_part((uint32_t)level << 10, 0x400, dg.map_buffer, ds_str(dg.town_dat_files[(dg.location - 1) >> 3]));

    dg.lamp_count = 0;
    dg.beam2_y = dg.beam2_x = dg.beam1_y = dg.beam1_x = 0xFFFF;
    for (int x = 0; x < 0x20; x++)
        for (int y = 0; y < 0x20; y++) {
            uint8_t t = *get_map_tile_ptr((unsigned)y, (unsigned)x);
            if ((t & 0xFE) == 0x48 && dg.lamp_count < 32) {
                dg.lamp_x[dg.lamp_count] = (uint8_t)x;
                dg.lamp_y[dg.lamp_count] = (uint8_t)y;
                dg.lamp_tile[dg.lamp_count] = t;
                dg.lamp_count++;
            }
            if (t == 0x2A) {   /* the first two 0x2A tiles get light beams */
                if (dg.beam1_x == 0xFFFF) { dg.beam1_x = (uint16_t)x; dg.beam1_y = (uint16_t)y; }
                else { dg.beam2_x = (uint16_t)x; dg.beam2_y = (uint16_t)y; }
            }
        }
    if (dg.hour < 5 || dg.hour > 0x13) update_lamps();
    update_light_map();
    pass_time(0);
    shadowlord_blight();
    if (place_npcs_too) place_npcs();
    dg.redraw_flags = 1;
}

/* From pass_time: night, underground and location 0x19 get radius 2; full day 50; the hours 5 and
 * 19 ramp by ds:6A80[minutes / 10]. A light spell gives at least 18, a torch at least 10.
 * Values above 0x32 are left alone (set elsewhere for special places). */
void update_light_radius(void)
{
    uint8_t old = dg.light_radius;
    if (dg.light_radius > 0x32) return;
    if (dg.location == 0x19 || dg.z_level > 0x7F || dg.hour < 5 || dg.hour > 0x13) {
        dg.light_radius = 2;
    } else if (dg.hour == 5) {
        dg.light_radius = ds_u8(0x6A80 + dg.minute / 10);
    } else if (dg.hour == 0x13) {
        dg.light_radius = ds_u8(0x6A80 + (0x3B - dg.minute) / 10);
    } else {
        dg.light_radius = 0x32;
    }
    if (dg.light_spell && dg.light_radius < 0x12) dg.light_radius = 0x12;
    if (dg.torch_light && dg.light_radius < 10) dg.light_radius = 10;
    if (old != dg.light_radius) dg.redraw_flags = 1;
}

void pass_time(int minutes)
{
    if (minutes != 0) {
        if (dg.active_spell == 0x51 && (minutes >>= 1) == 0) minutes = 1;   /* 'Q' quickness */
        dg.prev_hour = dg.hour;
        if (dg.active_spell != 0x54) {   /* 'T' time stop */
            dg.minute = (uint8_t)(dg.minute + minutes);
            sub_byte_floor0((uint8_t)minutes, &dg.torch_light);
            sub_byte_floor0((uint8_t)minutes, &dg.light_spell);
        }
        if (dg.minute > 0x3B) {
            dg.minute -= 0x3C;
            sub_byte_floor0(1, &dg.rest_cooldown);
            if (++dg.hour > 0x17) {
                dg.hour = 0;
                /* each Shadowlord still about moves to a random town 1-8: not this one, not
                 * one another Shadowlord is in */
                for (int i = 0; i < 3; i++) {
                    if (dg.shadowlord_locations[i] >= 0x80) continue;
                    unsigned town;
                    do {
                        town = (unsigned)random_between(8, 1);
                        if (dg.location == town) town = 0;
                        for (int j = 0; j < 3; j++)
                            if (dg.shadowlord_locations[j] == town) town = 0;
                    } while (town == 0);
                    dg.shadowlord_locations[i] = (uint8_t)town;
                }
                if (++dg.day > 0x1C) {   /* 28 days a month, 13 months a year */
                    dg.reagent_spot_day[0] = dg.reagent_spot_day[1] = dg.reagent_spot_day[2] = 0;
                    dg.hidden_item_day = 0;
                    dg.day = 1;
                    dg.no_ambush = 0;
                    for (int m = 0; m < 16; m++)
                        if (dg.party[m].inn_months < 0x19) dg.party[m].inn_months++;
                    if (++dg.month > 0x0D) {
                        dg.month = 1;
                        dg.year++;
                    }
                }
                draw_status_bar();
            }
        }
    }
    update_light_radius();
    if (dg.hour != dg.prev_hour) {
        draw_sky_bar();
        dg.ambient_cooldown = dg.hour == 0 ? 0x0C : dg.hour > 0x0C ? (uint8_t)(dg.hour - 0x0C) : dg.hour;
    }
    if (dg.location != 0 && dg.location < 0x21) patch_tiles_c();
}
