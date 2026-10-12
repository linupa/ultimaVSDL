/* map.cpp - map storage and loading. */
#include <stdio.h>
#include <string.h>
#include "files.h"
#include "gametime.h"
#include "gfx.h"
#include "map.h"
#include "misc.h"
#include "outdoor.h"
#include "party.h"
#include "savegame.h"
#include "sound.h"
#include "status.h"
#include "strings.h"
#include "tiles.h"
#include "town.h"
#include "view.h"
#include "world.h"

/* Outdoors, map_buffer holds a 32x32 window of Britannia at map_origin as four 16x16 chunks
 * (slots 0 1 / 2 3). In towns it is the 32x32 level; outside it reads map_buffer[0x3FF]. In combat
 * (location >= 0x80) the map is local_map. */
uint8_t *Map::get_map_tile_ptr(unsigned y, unsigned x)
{
    if (map.in_scene()) return &view.local_map[((y * 0x20 + x) & 0xFFFF) % sizeof view.local_map];
    if (map.is_outdoors()) {
        x = (x - map.origin_x) & 0x1F;
        y = (y - map.origin_y) & 0x1F;
        unsigned slot = 0;
        if (y > 0x0F) { slot = 2; y -= 0x10; }
        if (x > 0x0F) { slot++; x -= 0x10; }
        return &map.buffer[slot * 0x100 + y * 0x10 + x];
    }
    if ((int)x < 0 || (int)y < 0 || (int)x > 0x1F || (int)y > 0x1F) return &map.buffer[0x3FF];
    return &map.buffer[y * 0x20 + x];
}

/* The original also handles disk swapping and retries; the port has neither. */
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

/* A saved game (SAVED.GAM, INIT.GAM) in the game's files: through SaveGame, which knows where the
 * state lives. A short or missing file changes nothing. */
int load_game_file(const char *filename)
{
    uint8_t image[SaveGame::SIZE];
    int r = load_file_part(0, SaveGame::SIZE, image, filename);
    if (r == 0) savegame.unpack(image);
    return r;
}

int save_game_file(const char *filename)
{
    uint8_t image[SaveGame::SIZE];
    savegame.pack(image);
    return save_file_part(SaveGame::SIZE, image, filename);
}

/* Chunk numbers of the dungeon entrances (ds:3866) and shrines (ds:386E). Entrances are sealed
 * unless the matching dungeon_seals byte is 0; an entrance not in the table counts as sealed. */
int Map::dungeon_entrance_sealed(unsigned chunk)
{
    for (int i = 0; i < 8; i++)
        if (entrance_chunks_[i] == chunk >> 8) return map.dungeon_seals[i] == 0;
    return 1;
}

int Map::shrine_destroyed(unsigned chunk)
{
    for (int i = 0; i < 8; i++)
        if (shrine_chunks_[i] == chunk >> 8) return map.shrine_state[i] > 0x7F;
    return 0;
}

/* chunk = (chunk number << 8), which is also the offset in UNDER.DAT. BRIT.DAT stores only the
 * chunks listed in ds:3876 (0xFF = open ocean, filled with tile 1). */
void Map::load_outdoor_chunk(unsigned chunk, int slot, const char *filename)
{
    unsigned offset;
    if (filename[0] == 'B') {
        uint8_t index = brit_chunk_[(chunk >> 8) & 0xFF];
        if (index == 0xFF) {
            memset(&map.buffer[slot * 0x100], 1, 0x100);
            return;
        }
        offset = (unsigned)index << 8;
    } else {
        offset = chunk;
    }
    load_file_part(offset, 0x100, &map.buffer[slot * 0x100], filename);
    unsigned cx = (chunk >> 4) & 0xF0, cy = (chunk >> 8) & 0xF0;
    for (int y = 0; y < 0x10; y++)
        for (int x = 0; x < 0x10; x++) {
            uint8_t t = map.buffer[slot * 0x100 + y * 0x10 + x];
            if (t <= 0x15) continue;
            if (t < 0x19) {
                if (map.dungeon_entrance_sealed(chunk)) *map.get_map_tile_ptr(y + cy, x + cx) = 0xDF;
            } else if (t == 0x19 && map.shrine_destroyed(chunk)) {
                *map.get_map_tile_ptr(y + cy, x + cx) = 0x1A;
            }
        }
}

/* Loads the chunks on the side the party moved to (dy, dx); (1,0) + (-1,0) loads all four. */
void Map::load_outdoor_chunks(int dy, int dx)
{
    unsigned chunk = (unsigned)(map.origin_x * 0x10 + map.origin_y * 0x100) & 0xFFFF;
    unsigned right = map.origin_x == 0xF0 ? (unsigned)-0xF00 : 0x100;   /* wraps east to west */
    const char *file = map.z < 0x80 ? strings[Strings::BRIT_DAT_3980] : strings[Strings::UNDER_DAT_3976];   /* BRIT.DAT / UNDER.DAT */
    if (dx == -1 || dy == -1) map.load_outdoor_chunk(chunk, 0, file);
    if (dx == 1 || dy == -1) map.load_outdoor_chunk((chunk + right) & 0xFFFF, 1, file);
    if (dx == -1 || dy == 1) map.load_outdoor_chunk((chunk + 0x1000) & 0xFFFF, 2, file);
    if (dx == 1 || dy == 1) map.load_outdoor_chunk((chunk + right + 0x1000) & 0xFFFF, 3, file);

    /* The lighthouse light (tile 0x1B) for animate_light_beams. */
    uint8_t *p = (uint8_t *)memchr(map.buffer, 0x1B, 0x400);
    if (!p) {
        map.beam1_y = 0xFFFF;
        map.beam1_x = 0xFFFF;
    } else {
        unsigned off = (unsigned)(p - map.buffer);
        map.beam1_x = off & 0x0F;
        map.beam1_y = (off >> 4) & 0x0F;
        if (off & 0x100) map.beam1_x += 0x10;
        if (off & 0x200) map.beam1_y += 0x10;
    }
}

/* Keeps the two chunks that stay in the window, moving them to their new slots. */
void Map::shift_outdoor_chunks(int dy, int dx)
{
    int to = (dx < 0 || dy < 0) ? 3 : 0;
    int from = (dx < 0 || dy > 0) ? 2 : 1;
    memcpy(&map.buffer[to * 0x100], &map.buffer[from * 0x100], 0x100);
    memcpy(&map.buffer[(to + dy + dx * 2) * 0x100], &map.buffer[(from + dy + dx * 2) * 0x100], 0x100);
}

void Map::enter_outdoors(void)
{
    clear_location_label();
    pass_time(0);
    view.rebuild = true;
    party.need_prompt = 1;
    view.world_tick_enabled = 1;
    party.sail_direction = 0;
    map.origin_x = map.x & 0xF0;
    if ((map.x & 0x0F) < 8) map.origin_x -= 0x10;
    map.origin_y = map.y & 0xF0;
    if ((map.y & 0x0F) < 8) map.origin_y -= 0x10;
    map.load_outdoor_chunks(1, 0);
    map.load_outdoor_chunks(-1, 0);
    pass_time(0);
    update_light_map();
    set_wind_direction(-1);
    draw_sky_bar();
    place_underworld_specials();
}

/* Moves the party; near the window edge, scrolls the window by one chunk. */
void Map::outdoor_step(int dy, int dx)
{
    map.x = (uint8_t)(map.x + dx);
    map.y = (uint8_t)(map.y + dy);
    view.rebuild = true;
    unsigned rx = (unsigned)(map.x - map.origin_x) & 0x1F;
    unsigned ry = (unsigned)(map.y - map.origin_y) & 0x1F;
    if (rx < 5 || rx > 0x1A || ry < 5 || ry > 0x1A) {
        map.shift_outdoor_chunks(dy, dx);
        map.origin_x = (uint8_t)((dx * 0x10 + map.origin_x) & 0xF0);
        map.origin_y = (uint8_t)((dy * 0x10 + map.origin_y) & 0xF0);
        map.load_outdoor_chunks(dy, dx);
        update_light_map();
    }
}

void Map::load_town_level(int place_npcs_too)
{
    clear_location_label();
    draw_sky_bar();
    set_wind_direction(-1);
    map.open_door_tile = 0;
    map.origin_y = 0;
    map.origin_x = 0;
    int level = map.level_index() + map.z;
    if (map.z > 0x7F) level -= 0x100;
    load_file_part((uint32_t)level << 10, 0x400, map.buffer, map.dat_file());

    town.lamp_count = 0;
    map.beam2_y = map.beam2_x = map.beam1_y = map.beam1_x = 0xFFFF;
    for (int x = 0; x < 0x20; x++)
        for (int y = 0; y < 0x20; y++) {
            uint8_t t = *map.get_map_tile_ptr((unsigned)y, (unsigned)x);
            if ((t & 0xFE) == 0x48 && town.lamp_count < 32) {
                town.lamp_x[town.lamp_count] = (uint8_t)x;
                town.lamp_y[town.lamp_count] = (uint8_t)y;
                town.lamp_tile[town.lamp_count] = t;
                town.lamp_count++;
            }
            if (t == 0x2A) {   /* the first two 0x2A tiles get light beams */
                if (map.beam1_x == 0xFFFF) { map.beam1_x = (uint16_t)x; map.beam1_y = (uint16_t)y; }
                else { map.beam2_x = (uint16_t)x; map.beam2_y = (uint16_t)y; }
            }
        }
    if (game_time.hour < 5 || game_time.hour > 0x13) town.update_lamps();
    update_light_map();
    pass_time(0);
    town.shadowlord_blight();
    if (place_npcs_too) town.place_npcs();
    view.rebuild = true;
}

/* From pass_time: night, underground and location 0x19 get radius 2; full day 50; the hours 5 and
 * 19 ramp by ds:6A80[minutes / 10]. A light spell gives at least 18, a torch at least 10.
 * Values above 0x32 are left alone (set elsewhere for special places). */
void update_light_radius(void)
{
    uint8_t old = view.light_radius;
    if (view.light_radius > 0x32) return;
    if (map == Map::ARARAT || map.z > 0x7F || game_time.hour < 5 || game_time.hour > 0x13) {
        view.light_radius = 2;
    } else if (game_time.hour == 5) {
        view.light_radius = view.dawn_radius(game_time.minute / 10);
    } else if (game_time.hour == 0x13) {
        view.light_radius = view.dawn_radius((0x3B - game_time.minute) / 10);
    } else {
        view.light_radius = 0x32;
    }
    if (party.light_spell && view.light_radius < 0x12) view.light_radius = 0x12;
    if (party.torch_light && view.light_radius < 10) view.light_radius = 10;
    if (old != view.light_radius) view.rebuild = true;
}

void pass_time(int minutes)
{
    if (minutes != 0) {
        if (party.active_spell == 0x51 && (minutes >>= 1) == 0) minutes = 1;   /* 'Q' quickness */
        game_time.prev_hour = game_time.hour;
        if (party.active_spell != 0x54) {   /* 'T' time stop */
            game_time.minute = (uint8_t)(game_time.minute + minutes);
            sub_byte_floor0((uint8_t)minutes, &party.torch_light);
            sub_byte_floor0((uint8_t)minutes, &party.light_spell);
        }
        if (game_time.minute > 0x3B) {
            game_time.minute -= 0x3C;
            sub_byte_floor0(1, &game_time.rest_cooldown);
            if (++game_time.hour > 0x17) {
                game_time.hour = 0;
                /* each Shadowlord still about moves to a random town 1-8: not this one, not
                 * one another Shadowlord is in */
                for (int i = 0; i < 3; i++) {
                    if (map.shadowlord_locations[i] >= 0x80) continue;
                    unsigned town;
                    do {
                        town = (unsigned)random_between(8, 1);
                        if (map.id() == town) town = 0;
                        for (int j = 0; j < 3; j++)
                            if (map.shadowlord_locations[j] == town) town = 0;
                    } while (town == 0);
                    map.shadowlord_locations[i] = (uint8_t)town;
                }
                if (++game_time.day > 0x1C) {   /* 28 days a month, 13 months a year */
                    game_time.reagent_spot_day[0] = game_time.reagent_spot_day[1] = game_time.reagent_spot_day[2] = 0;
                    game_time.hidden_item_day = 0;
                    game_time.day = 1;
                    game_time.no_ambush = 0;
                    for (int m = 0; m < 16; m++)
                        if (party[m].inn_months < 0x19) party[m].inn_months++;
                    if (++game_time.month > 0x0D) {
                        game_time.month = 1;
                        game_time.year++;
                    }
                }
                draw_status_bar();
            }
        }
    }
    update_light_radius();
    if (game_time.hour != game_time.prev_hour) {
        draw_sky_bar();
        sound.ambient_cooldown = game_time.hour == 0 ? 0x0C : game_time.hour > 0x0C ? (uint8_t)(game_time.hour - 0x0C) : game_time.hour;
    }
    if (map.is_town()) patch_tiles_c();
}
