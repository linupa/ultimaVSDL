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
void step_in_facing(unsigned y, unsigned x, int facing);   /* bfaa: to dir_x / dir_y */
int  ask_dungeon_direction(int facing);      /* bfec: Ahead, Left, Right, Here; 0 = Pass */
void dungeon_search(void);                   /* c5c6 */
void dungeon_jimmy(void);                    /* cbbe */
void dungeon_open_chest(void);               /* d254 */
void dungeon_get(void);                      /* d71e */


#define DCELL(z, y, x) dg.dungeon_map[(((z) & 7) * 0x40 + ((y) & 7) * 8 + ((x) & 7))]
#define DHERE DCELL(dg.z_level, dg.party_y, dg.party_x)

#endif
