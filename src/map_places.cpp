/* map_places.cpp - the Map object, its saved pieces and the tables about places. See map.h; the
 * map loaders are in map.cpp. */
#include <string.h>
#include "gamedata.h"
#include "map.h"
#include "savegame.h"

Map &Map::instance()
{
    static Map the_map;
    return the_map;
}

Map &map = Map::instance();

Map::Map()
{
    SaveGame &save = SaveGame::instance();   /* not `savegame`: this runs before main, in no set order */
    save.add(0x5893, id_, "map.id");
    save.add(0x5892, wind_direction, "map.wind_direction");
    save.add(0x5895, z, "map.z");
    save.add(0x5896, x, "map.x");
    save.add(0x5897, y, "map.y");
    save.add(0x589B, origin_x, "map.origin_x");
    save.add(0x589C, origin_y, "map.origin_y");
    save.add(0x5830, moonstone_x, "map.moonstone_x");
    save.add(0x5838, moonstone_y, "map.moonstone_y");
    save.add(0x5840, moonstone_loc, "map.moonstone_loc");
    save.add(0x5848, moonstone_z, "map.moonstone_z");
    save.add(0x585C, search_found_bits, "map.search_found_bits");
    save.add(0x58C8, shadowlord_locations, "map.shadowlord_locations");
    save.add(0x58D0, dungeon_seals, "map.dungeon_seals");
    save.add(0x5C5A, objects.items, "map.objects");
    save.add(0x58D8, shrine_state, "map.shrine_state");
    save.add(0x594F, open_door_tile, "map.open_door_tile");
    save.add(0x5950, open_door_x, "map.open_door_x");
    save.add(0x5951, open_door_y, "map.open_door_y");
    save.add(0x5952, open_door_timer, "map.open_door_timer");

    Data &data = Data::instance();   /* not a global: this runs before main, in no set order */
    data.add(0x13A5, shadowlord_row_, "map.shadowlord_row", Data::SHARED);
    data.add(0x1E19, level_index_, "map.level_index", Data::SHARED);
    data.add(0x1E3A, names_, "map.names", Data::SHARED);
    data.add(0x1E89, exit_x_, "map.exit_x", Data::SHARED);
    data.add(0x1EB1, exit_y_, "map.exit_y", Data::SHARED);
    data.add(0x1F4E, virtue_names_, "map.virtue_names");
    data.add(0x1F5E, mantras_, "map.mantras");
    data.add(0x1F6E, shrine_x_, "map.shrine_x");
    data.add(0x1F76, shrine_y_, "map.shrine_y");
    data.add(0x2652, dat_files_, "map.dat_files");
    data.add(0x4502, words_of_power_, "map.words_of_power");
    data.add(0x4512, seal_tiles_, "map.seal_tiles");
    data.add(0x4AA0, tlk_files_, "map.tlk_files");
    data.add(0x1F7E, 16 * 16 * 2, [this](const uint8_t *b, unsigned n) { memcpy(beam_shapes, b, n); }, "map.beam_shapes");
    data.add(0x3866, entrance_chunks_, "map.entrance_chunks");
    data.add(0x386E, shrine_chunks_, "map.shrine_chunks");
    data.add(0x3876, brit_chunk_, "map.brit_chunk");
}

uint8_t Map::exit_x_[256];   /* ds:1E89 */

uint8_t Map::exit_y_[256];   /* ds:1EB1 */

uint8_t Map::level_index_[256];   /* ds:1E19 */

uint8_t Map::shadowlord_row_[256];   /* ds:13A5 */

uint8_t Map::shrine_x_[Map::SHRINES];   /* ds:1F6E */
uint8_t Map::shrine_y_[Map::SHRINES];   /* ds:1F76 */

Strings::Id Map::names_[Map::NAMES];   /* ds:1E3A */

Strings::Id Map::dat_files_[4];   /* ds:2652 */

Strings::Id Map::tlk_files_[4];   /* ds:4AA0 */

/* Not a table in DATA.OVL: the original picks the name in code (NPC.OVL). */
const Strings::Id Map::npc_files_[4] = {
    Strings::towne_npc_6D46,
    Strings::dwelling_npc_6D50,
    Strings::castle_npc_6D5E,
    Strings::keep_npc_6D6A,
};

Strings::Id Map::virtue_names_[Map::SHRINES];   /* ds:1F4E */

Strings::Id Map::mantras_[Map::SHRINES];   /* ds:1F5E */

Strings::Id Map::words_of_power_[Map::DUNGEONS];   /* ds:4502 */

uint8_t Map::seal_tiles_[Map::DUNGEONS];   /* ds:4512 */
