/* dungeon.h - the dungeons: DUNGEON.OVL (the first-person view, movement, traps, the wandering
 * monster, rooms) and DNGLOOK.OVL (Look, the map, corridor fights, room set-up). Parameter order
 * follows the Ghidra signatures.
 *
 * A dungeon (locations 0x21-0x28) is 8 levels of 8 x 8 squares in dungeon_map, wrapping at the
 * edges. A square's high nibble is its kind: 0 passage, 1 up ladder, 2 down ladder, 3 both,
 * 4 chest (low bits: trapped / locked), 5 fountain (0 cure, 1 heal, 2 poison, 3 bad), 6 pit and
 * traps (0 pit, 1 hidden pit, 2 bomb), 7 open chest, 8 field (0 sleep, 1 poison, 2 fire,
 * 3 electric), A room behind a door, B wall (low nibble: a sign), C special wall (stalactite,
 * cave-in or skeleton, by dungeon_style), D secret door, E doorway, F room. Bit 3 of kinds 0-8 is
 * a hole in the ceiling. Rooms are 11 x 11 combat maps in DUNGEON.CBT (16 per dungeon).
 * One monster wanders each level: objects[1] (tile 0-7 = picture MONn.16, hull = monster type).
 *
 * The view is built on the back page from wall pieces (DNG1-3.16 by style, doors in DNG0.16)
 * drawn left and mirrored right, four squares deep. */
#ifndef U5_DUNGEON_H
#define U5_DUNGEON_H

#include <stdint.h>
#include "strings.h"

/* The dungeons, as the Dungeon class: the first-person view, movement, traps, the wandering
 * monster, rooms, Look and the map (DUNGEON.OVL, DNGLOOK.OVL). `dungeon` is the one Dungeon
 * object. */
class Dungeon {
public:
    static Dungeon &instance();

    /* Part of the saved game: registered with SaveGame (savegame.h), which puts these at their old
     * places in SAVED.GAM. */
    /* The dungeon the party is in: 8 levels of 8 x 8 squares, [z * 0x40 + y * 8 + x], as loaded
     * from DUNGEON.DAT (ds:595A). */
    uint8_t grid[0x200] = {};
    uint8_t style = 0;               /* ds:6604  1 normal, 2, 3 caves: which walls are drawn */
    uint8_t facing = 0;              /* ds:6603  0 north, 1 east, 2 south, 3 west */
    static int16_t facing_dx[4], facing_dy[4];   /* ds:24D6, ds:24DE: a step that way */
    uint8_t entry_dir = 0;           /* ds:6602  the direction the party came from (rooms, attacks) */
    uint8_t cleared_rooms[14] = {};  /* ds:58E0  a bit per room emptied */

    /* The pictures of the first-person view, and what a corridor fight is fought on. */
    uint16_t wall_pic = 0, door_pic = 0, monster_pic = 0;
    uint8_t gfx_flags = 0;           /* 1 pictures loaded, 2 / 4 ? */
    uint8_t corridor_wall_tile = 0, corridor_floor_tile = 0;

    uint8_t footstep_toggle = 0, footstep_volume = 0;
    uint8_t fountain_frame = 0;      /* the fountain's animation */
    uint8_t map_queue_x[256] = {}, map_queue_y[256] = {};   /* show_dungeon_map's flood queue */

    /* ---- the dungeons (dungeon.cpp) ---- */

    /* DUNGEON.OVL */
    void dungeon_enter_room(uint8_t square);     /* 81d0 */
    void dungeon_spawn_monster(int new_monster); /* 8304 */
    void draw_dungeon_hud(void);                 /* 83a2 */
    unsigned place_dungeon_monster(void);        /* 8422 */
    void draw_dungeon_frame(void);               /* 8502 */
    unsigned dungeon_read_key(void);             /* 85a6 */
    void electric_field_shock(int dir);          /* 8640 */
    int  dungeon_move(uint8_t square, int key);  /* 86d2: 1 left, 2 right, 3 advance, 4 back, else turn around */
    int  dungeon_handle_key(unsigned key);       /* 8894 */
    int  dungeon_monster_move(void);             /* 89b2: 1 = it reached the party */
    void sleep_trap(void);                       /* 8b18 */
    void poison_trap(void);                      /* 8bb6 */
    void pit_trap(uint8_t square);               /* 8c1c */
    void dungeon_monster_attack(void);           /* 8d4e */
    void dungeon_turn_effects(int monster_moves, uint8_t square);   /* 8e46 */
    void dungeon_main_loop(int reset_monster);   /* 8ffe */
    void footstep_sound(void);                   /* 91f0 */
    void draw_dungeon_sign(unsigned sign);       /* 921c */
    uint8_t dungeon_cell_at(unsigned y, unsigned x);   /* 92ac */
    void draw_dungeon_monster(int distance);     /* 92ee */
    void draw_field_sparkles(int field, int distance);   /* 944e */
    void draw_wall_piece(int side, int kind, unsigned piece);   /* 951a */
    int  draw_drip(int distance, int frame, int y, int x);      /* 962c */
    int  draw_dungeon_center(int distance, unsigned y, unsigned x);   /* 96da: 0 = the view ends here */
    void draw_dungeon_side(int distance, int side, unsigned y, unsigned x);   /* 9852 */
    void draw_fountain(int frame, int distance); /* 9956 */
    void draw_dungeon_contents(int distance, unsigned y, unsigned x);   /* 9b22 */
    void draw_dungeon_view(int full);            /* 9c60: onto the back page */
    void redraw_dungeon(void);                   /* 9db0 */
    int  can_klimb_into(int check, int level);   /* 9ddc */
    int  dungeon_change_level(int check, int dz);   /* 9e3a: 1 = out of the dungeon */
    void exit_dungeon(void);                     /* 9ed8 */
    int  cmd_attack_dungeon(void);               /* 9f1a */
    int  cmd_klimb_dungeon(void);                /* 9fe0 */

    /* DNGLOOK.OVL */
    void cmd_look_dungeon(void);                 /* a290 */
    void draw_map_field(int y, int x);           /* a514 */
    int  draw_map_square(int row, int col);      /* a5d0 */
    void show_dungeon_map(void);                 /* a938 */
    void mark_room_cleared(int room);            /* aad4 */
    int  is_room_cleared(int room);              /* ab64 */
    void apply_cleared_rooms(void);              /* abca */
    void corridor_edge_wall(int side, uint8_t square_type);   /* ac0e */
    void corridor_edge_open(int side);           /* acd8 */
    void corridor_edge_passage(int side);        /* ad7e */
    void build_corridor_edge(int side);          /* ae2e */
    void build_corridor_map(void);               /* aefc */
    void setup_corridor_combat(void);            /* afce */
    void apply_combat_exit(void);                /* b26a */
    void load_dungeon_graphics(int reset_monster);   /* b32e */
    void unload_dungeon_graphics(void);          /* b3c0 */
    void place_combatants(int mode, int room_tile);  /* b40e */

    /* SJOG.OVL (dungeon halves) */
    /* A square of the dungeon level. */
    struct Spot {
        uint16_t x, y;
    };
    Spot step_in_facing(unsigned y, unsigned x, int facing);   /* bfaa: the next square that way */
    int  ask_dungeon_direction(int facing, Spot *spot);   /* bfec: Ahead, Left, Right, Here; 0 = Pass, else the square in *spot */
    void dungeon_search(void);                   /* c5c6 */
    void dungeon_jimmy(void);                    /* cbbe */
    void dungeon_open_chest(void);               /* d254 */
    void dungeon_get(void);                      /* d71e */

    Dungeon(const Dungeon &) = delete;
    Dungeon &operator=(const Dungeon &) = delete;

private:
    Dungeon();

    static uint8_t fixed_rooms_[6];

    /* The read-only tables, by their offsets in the original data segment (see dungeon.cpp). */
    enum Table {
        MONSTER_TYPE = 0x173C,   /* the wandering monster: its type by picture (0-7) */
        MONSTER_ANIM = 0x1744,   /* and how that picture is animated */
        CORRIDOR_FEATURE = 0x244A,   /* corridor fight: the tile shown for the square's kind (ladder, chest, ...) */
        CAMP_PARTY_X = 0x2452,   /* a camp's party squares */
        CAMP_PARTY_Y = 0x2458,
        CORRIDOR_PARTY_1 = 0x245E,   /* corridor fight: the party's start squares, four runs of six used as x or y by the side faced */
        CORRIDOR_PARTY_2 = 0x2464,
        CORRIDOR_PARTY_3 = 0x246A,
        CORRIDOR_PARTY_4 = 0x2470,
        CORRIDOR_MONSTERS_1 = 0x2476,   /* corridor fight: the monsters' squares, four runs of sixteen used as x or y by the facing */
        CORRIDOR_MONSTERS_2 = 0x2486,
        CORRIDOR_MONSTERS_3 = 0x2496,
        CORRIDOR_MONSTERS_4 = 0x24A6,
        CAMP_MONSTER_X = 0x24B6,   /* a camp's monster squares */
        CAMP_MONSTER_Y = 0x24C6,
        ROOM_FACING = 0x2C76,   /* the way the party faces in a room, by the direction it came from */
        SIGN_FIRST = 0x2DC7,   /* signs: by location id, the dungeon's first sign */
        SIGN_LAST = 0x2DCF,   /* and its last */
        SIGN_LEFT = 0x2DF7,   /* a sign's text window */
        SIGN_TOP = 0x2E03,
        SIGN_TEXT = 0x2E10,   /* and its text (string ids, words) */
        MONSTER_ARMS = 0x2E26,   /* the monster's picture: arm positions by phase */
        MONSTER_X = 0x2E2A,   /* its x by distance (words) */
        MONSTER_Y = 0x2E32,   /* its y, standing or hanging, by distance (words) */
        SPARKLE_LOW = 0x2E42,   /* a field's sparkles by distance (words): the area */
        SPARKLE_HIGH = 0x2E4A,
        SPARKLE_COUNT = 0x2E52,   /* how many */
        SPARKLE_LENGTH = 0x2E5A,   /* how long */
        WALL_X = 0x2E62,   /* wall pieces: x by side and piece (words) */
        FEATURE_X = 0x2E72,   /* things in a square: x by depth (words) */
        FEATURE_Y = 0x2E7A,   /* their y by floor / ceiling and depth (words) */
        CEILING_Y = 0x2E82,   /* the ceiling half of FEATURE_Y */
        WALL_PIECE = 0x2E80,   /* the wall piece for a square's kind (0xA-0xF) */
        DRIP_Y_AHEAD = 0x2E8B,   /* a stalactite's drip: y by frame and distance, ahead */
        DRIP_Y_SIDE = 0x2E9A,   /* and at the side */
        FOUNTAIN_0 = 0x2EA4,   /* a fountain's spray, (x, y) pairs by frame: at distance 0 */
        FOUNTAIN_1 = 0x2ED4,   /* 1 */
        FOUNTAIN_2 = 0x2EF2,   /* 2 */
        FOUNTAIN_3 = 0x2F0A,   /* 3 */
        FOUNTAIN_0_TOP = 0x2F10,   /* and the top drop at distance 0 */
        CEILING_PIECE = 0x2F16,   /* the piece drawn on the ceiling for a square's kind */
        FLOOR_PIECE = 0x2F1E,   /* and on the floor */
        HOLE_PIECE = 0x2F24,   /* a hole in the ceiling */
        SIDE_DX = 0x2F28,   /* the square to the left, by facing */
        SIDE_DY = 0x2F2C,
        ROOM_OBJECT_RANGE = 0x383F,   /* a room's objects: the random part of the amount, by type */
        ROOM_OBJECT_BASE = 0x384D,   /* and the fixed part */
        RANDOM_MONSTERS = 0x385E,   /* the types a room's "random monster" squares draw from */
        CHEST_LEVEL = 0x41BC,   /* an opened chest: how deep each kind of find starts to appear */
        CHEST_AMOUNT = 0x41C4,   /* the most of it */
        CHEST_KIND = 0x41CC,   /* and what it is */
    };
    static uint8_t table_byte(unsigned ds_offset);
    static int16_t table_word(unsigned ds_offset);   /* little-endian, signed */

    int dungeon_index(void);   /* 0x21 and 0x22 share index 0 */
};

extern Dungeon &dungeon;

#define DCELL(z, y, x) dungeon.grid[(((z) & 7) * 0x40 + ((y) & 7) * 8 + ((x) & 7))]
#define DHERE DCELL(map.z, map.y, map.x)

#endif
