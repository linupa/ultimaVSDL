/* map.h - where the party is and the map around it, as the Map class. `map` is the one Map object.
 *
 * Three things, which the original keeps apart in its data segment:
 *   - the party's square (x, y, z) and the map in memory, with the loaders that fill it: ports of
 *     get_map_tile_ptr (main program) and of the loaders in the OUTSUBS, MAINOUT and TOWN
 *     overlays. Coordinates are absolute map coordinates (outdoors 0-255, towns 0-31);
 *   - which place this is: the game's location number, with is_town() and the like. The numbers
 *     are the original's and are used as indices (exit_x, the NPC bit tables, the files by
 *     kind()), so they must stay as they are;
 *   - the read-only tables about places: entrances, names, files, shrines. The names of 1-0x28
 *     are the game's own (table at ds:1E3A), except the three huts 0x0E-0x10, which it never names.
 *
 * The square, the place and the changes to the map are part of the saved game: Map registers
 * them with SaveGame (savegame.h), which puts them at their old places in SAVED.GAM. */
#ifndef U5_MAP_H
#define U5_MAP_H

#include <stdint.h>
#include "strings.h"

/* A thing on the map: 8 bytes, as in SAVED.GAM and the .OOL files. */
#pragma pack(push, 1)
struct MapObject {
    uint8_t tile;          /* 0 = empty slot */
    uint8_t tile2;         /* displayed (animated) tile */
    uint8_t x, y, z;
    uint8_t hull;          /* ship hull / hp / extra */
    uint8_t flags;
    uint8_t skiffs;        /* skiffs carried by a ship */
};
#pragma pack(pop)
static_assert(sizeof(MapObject) == 8, "MapObject size");

/* The things on the map: 32 slots, 0 the party itself. This is the table BRIT.OOL and UNDER.OOL
 * hold, 0x100 bytes. The original sometimes indexes it with a slot it failed to find (32, or
 * 0xFFFF) and hits whatever follows; here such an index gives a spare entry. */
struct MapObjects {
    enum { COUNT = 32 };
    MapObject items[COUNT] = {};

    MapObject &operator[](unsigned i) { return i < COUNT ? items[i] : spare_; }
    uint8_t *bytes() { return (uint8_t *)items; }

private:
    MapObject spare_ = {};
};

class Map {
public:
    enum Id : uint8_t {
        BRITANNIA = 0x00,   /* outdoors: the surface, or the Underworld (z_level) */
        /* towns (TOWNE.DAT) */
        MOONGLOW = 0x01, BRITAIN, JHELOM, YEW, MINOC, TRINSIC, SKARA_BRAE, NEW_MAGINCIA,
        /* dwellings (DWELLING.DAT) */
        FOGSBANE = 0x09, STORMCROW, GREYHAVEN, WAVEGUIDE, IOLOS_HUT, SUTEKS_HUT, SIN_VRAALS_HUT, GRENDELS_HUT,
        /* castles and villages (CASTLE.DAT) */
        LORD_BRITISHS_CASTLE = 0x11, PALACE_OF_BLACKTHORN, WEST_BRITANNY, NORTH_BRITANNY, EAST_BRITANNY,
        PAWS, COVE, BUCCANEERS_DEN,
        /* keeps (KEEP.DAT) */
        ARARAT = 0x19, BORDERMARCH, FARTHING, WINDEMERE, STONEGATE, LYCAEUM, EMPATH_ABBEY, SERPENTS_HOLD,
        /* dungeons */
        DECEIT = 0x21, DESPISE, DESTARD, WRONG, COVETOUS, SHAME, HYTHLOTH, DOOM,
        /* outside the game proper */
        TITLE = 0x40,        /* title screen, no tile set loaded */
        TITLE_MENU = 0x41,   /* the menu, tile set loaded */
        DEMO = 0x42,         /* the demo and character creation scenes (tiles and MISCMAPS.DAT) */
        /* a scene on its own small map (local_map): combat, a dungeon room, a shrine, a cut-scene */
        SCENE = 0xFF,
    };

    static Map &instance();

    /* ---- where the party stands, and the map around it ---- */

    uint8_t x = 0, y = 0;            /* ds:5896, ds:5897  the party's square */
    uint8_t z = 0;                   /* ds:5895  0 the surface, 0xFF the Underworld; a building's or dungeon's level */

    /* The map in memory: outdoors a 32 x 32 window of Britannia at the origin, as four 16 x 16
     * chunks (slots 0 1 / 2 3); in a settlement the 32 x 32 level. */
    uint8_t buffer[1024] = {};
    uint8_t origin_x = 0, origin_y = 0;   /* ds:589B  the outdoor window's origin, a multiple of 16 */

    /* What has been changed on the map. */
    uint8_t dungeon_seals[8] = {};   /* ds:58D0  the word of power toggles bit 7 */
    uint8_t shrine_state[8] = {};    /* ds:58D8  bit 7: destroyed by a Shadowlord */
    uint8_t open_door_tile = 0, open_door_x = 0, open_door_y = 0;   /* ds:594F  the door that is open */
    uint8_t open_door_timer = 0;     /* ds:5952  turns until it closes again */

    /* Vehicles, monsters, people and the party on this map (ds:5C5A), and where the table is
     * put aside while a scene (a battle, a shrine) uses it for its own. */
    MapObjects objects;
    uint8_t objects_backup[0x100] = {};

    uint8_t wind_direction = 0;      /* ds:5892  0 calm, 1 north, 2 south, 3 east, 4 west */

    /* The light sources whose beams animate_light_beams draws: outdoors the lighthouse light
     * (tile 0x1B), in a town the first two 0x2A tiles; found when the map loads, 0xFFFF when
     * there is none. And the beams' step, 0xFF to start them afresh. (Not saved.) */
    uint16_t beam1_x = 0xFFFF, beam1_y = 0xFFFF;   /* ds:217E */
    uint16_t beam2_x = 0xFFFF, beam2_y = 0xFFFF;   /* ds:2182 */
    uint8_t beam_frame = 0xFF;                     /* ds:2186 */
    /* The sixteen positions of a lighthouse beam as it sweeps: 16 squares each, as pairs dx, dy
     * (ds:1F7E, from DATA.OVL). */
    int8_t beam_shapes[16][16][2] = {};

    /* Where things are in the world. */
    uint8_t shadowlord_locations[3] = {};   /* ds:58C8  the town Falsehood, Hatred and Cowardice are each in; bit 7: gone */
    uint8_t search_found_bits[15] = {};     /* ds:585C  a bit per hidden thing already found by searching */
    uint8_t saved_location = 0;             /* the place a shrine was entered from, while the shrine is a scene (not saved) */

    /* The eight moonstones: where each lies buried (x, y, z), and which map that is. moonstone_loc
     * is 0xFF while the party carries the stone (ds:5830, 5838, 5840, 5848). */
    uint8_t moonstone_x[8] = {}, moonstone_y[8] = {}, moonstone_z[8] = {};
    uint8_t moonstone_loc[8] = {};

    /* The tile at map square (y, x). Outside a settlement's level it gives the level's last
     * square; in a scene the map is the scene's own (local_map). */
    uint8_t *get_map_tile_ptr(unsigned y, unsigned x);                              /* 4402 */
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

    /* ---- which place this is ---- */

    Id   id() const { return Id(id_); }
    void set(Id id) { id_ = id; }
    bool operator==(Id other) const { return id() == other; }
    bool operator!=(Id other) const { return id() != other; }

    bool is_outdoors() const { return id() == BRITANNIA; }
    bool is_town() const { return id() >= MOONGLOW && id() <= SERPENTS_HOLD; }   /* any settlement: 1-0x20 */
    bool is_dungeon() const { return id() >= DECEIT && id() <= DOOM; }
    /* Outdoors or in a settlement (< 0x21): the party walks a tile map seen from above. */
    bool on_tile_map() const { return id() <= SERPENTS_HOLD; }
    /* In a scene (>= 0x80; the game only ever sets SCENE). */
    bool in_scene() const { return id() >= 0x80; }
    /* A settlement's data files: 0 TOWNE, 1 DWELLING, 2 CASTLE, 3 KEEP ((id - 1) >> 3). */
    unsigned kind() const { return ((unsigned)id() - 1) >> 3; }

    /* ---- what the game knows about each place: read-only tables of the original data segment.
     * The byte tables are read with any byte as the index, so each is the 256 bytes that follow
     * its start in DATA.OVL. ---- */

    /* Where the party stands outdoors after leaving a place, which is also where its entrance
     * is: ids 1-0x20 the settlements, 0x21-0x28 the dungeons (ds:1E89, ds:1EB1). */
    uint8_t exit_x(uint8_t id) const { return exit_x_[id]; }
    uint8_t exit_y(uint8_t id) const { return exit_y_[id]; }
    uint8_t exit_x() const { return exit_x(id()); }
    uint8_t exit_y() const { return exit_y(id()); }
    /* The game's name for a place, "MOONGLOW" ... "DOOM" (ds:1E3A); the two castles and three
     * huts have none. */
    Strings::Id name_id(unsigned id) const { return id >= 1 && id <= NAMES ? names_[id - 1] : Strings::NONE; }
    const char *name(unsigned id) const { return strings[name_id(id)]; }
    /* First 1K block of this settlement's ground floor in its .DAT file (ds:1E19). */
    uint8_t level_index() const { return level_index_[id()]; }
    /* This settlement's data files by kind(): TOWNE/DWELLING/CASTLE/KEEP .DAT (ds:2652), .TLK
     * (ds:4AA0) and .NPC. */
    const char *dat_file() const { return strings[kind() < 4 ? dat_files_[kind()] : Strings::NONE]; }
    const char *tlk_file() const { return strings[kind() < 4 ? tlk_files_[kind()] : Strings::NONE]; }
    const char *npc_file() const { return strings[npc_files_[kind() & 3]]; }
    /* Row a Shadowlord stands on in this town (ds:13A5; towns 1-8). */
    uint8_t shadowlord_row() const { return shadowlord_row_[id()]; }

    /* The eight shrines, 0 Honesty ... 7 Humility: where each is outdoors (ds:1F6E, ds:1F76),
     * its virtue (ds:1F4E) and its mantra (ds:1F5E). */
    enum { SHRINES = 8, DUNGEONS = 8 };
    uint8_t shrine_x(unsigned shrine) const { return shrine < SHRINES ? shrine_x_[shrine] : 0; }
    uint8_t shrine_y(unsigned shrine) const { return shrine < SHRINES ? shrine_y_[shrine] : 0; }
    const char *virtue_name(unsigned shrine) const { return strings[shrine < SHRINES ? virtue_names_[shrine] : Strings::NONE]; }
    const char *mantra(unsigned shrine) const { return strings[shrine < SHRINES ? mantras_[shrine] : Strings::NONE]; }
    /* The word of power that opens or seals dungeon n, 0 Deceit ... 7 Doom (ds:4502). */
    const char *word_of_power(unsigned n) const { return strings[n < DUNGEONS ? words_of_power_[n] : Strings::NONE]; }
    /* The map tile of dungeon n's open entrance; the word of power swaps it with the seal (ds:4512). */
    uint8_t seal_tile(unsigned n) const { return n < DUNGEONS ? seal_tiles_[n] : 0; }

    Map(const Map &) = delete;
    Map &operator=(const Map &) = delete;

private:
    Map();

    /* From DATA.OVL: the outdoor chunks holding the dungeon entrances (ds:3866) and the shrines
     * (ds:386E), and where each of the 256 chunks is in BRIT.DAT (ds:3876; 0xFF = open sea). */
    uint8_t entrance_chunks_[8] = {}, shrine_chunks_[8] = {}, brit_chunk_[256] = {};

    uint8_t id_ = BRITANNIA;   /* ds:5893 */

    enum { NAMES = 0x28 };
    static uint8_t exit_x_[256], exit_y_[256], level_index_[256], shadowlord_row_[256];
    static uint8_t shrine_x_[SHRINES], shrine_y_[SHRINES], seal_tiles_[DUNGEONS];
    static Strings::Id names_[NAMES], dat_files_[4], tlk_files_[4];
    static const Strings::Id npc_files_[4];
    static Strings::Id virtue_names_[SHRINES], mantras_[SHRINES], words_of_power_[DUNGEONS];
};

/* The one Map object (Map::instance()). */
/* The one Map object (Map::instance()). */
extern Map &map;

/* Files in parts (these are not the map's, but have always been declared here). */
int  load_file_part(uint32_t offset, unsigned size, uint8_t *buf, const char *filename);   /* 256e */
int  save_file_part(unsigned size, const uint8_t *buf, const char *filename);   /* writes the file from offset 0 */
int  load_game_file(const char *filename);   /* a saved game, through SaveGame (savegame.h): 0 on success */
int  save_game_file(const char *filename);

/* pass_time (4f7c): the clock, torch and light spell countdown, rest cooldown, the Shadowlords'
 * daily moves, the calendar, then the light radius. Quickness halves time, time stop freezes it. */
void pass_time(int minutes);
void update_light_radius(void);   /* the light-radius part of pass_time */

#endif
