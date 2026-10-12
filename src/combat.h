/* combat.h - combat: the core in the main program (0000:5f86-6f1c), COMBAT.OVL (the battle loop,
 * turns, monster behaviour, damage) and the combat half of SJOG.OVL. The attack commands and
 * missiles (COMSUBS.OVL) are in comsubs.cpp. Parameter order follows the Ghidra signatures.
 *
 * A battle runs on an 11 x 11 map from BRIT.CBT loaded into local_map (32-byte rows); location is
 * 0xFF meanwhile and the map's objects are kept in objects_backup. Every fighter is a Combatant
 * (slots 0-5 the party, 6-31 monsters) with a MapObject that shows it; the object's `skiffs` byte
 * remembers the slot last aimed at and `hull` holds a monster's hit points for display.
 * Combatant.flags: 0x80 party member, 0x40 monster, 0x20 dead / gone, 0x10 invisible, 8 asleep,
 * 4 swallowed, 2 fleeing, 1 changed sides (possessed or charmed).
 * Monster types are (tile - 0x40) / 4; their 8-byte records at ds:13BC are strength, dexterity,
 * intelligence, armour, damage, hit points, group size, treasure level.
 *
 * Dungeon rooms (combat_flags 0x82) and corridor fights (2) are set up in dungeon.cpp. */
#ifndef U5_COMBAT_H
#define U5_COMBAT_H

#include <stdint.h>

/* A square of the 11 x 11 view, which is also a battle's map. Outside the view both are 0xFFFF. */
struct Square {
    uint16_t x, y;
    bool outside() const { return x == 0xFFFF; }
};

/* A step on the battle map: -1, 0 or 1 each way, kept as the original's words (-1 is 0xFFFF). */
struct Step {
    uint16_t dx, dy;
};

/* A fighter in a battle. */
#pragma pack(push, 1)
struct Combatant {
    uint8_t hp;
    uint8_t speed;
    uint8_t flags;         /* 0x80 party 0x40 monster 0x20 dead 8 asleep 4 swallowed 2 fleeing 1 acted */
    uint8_t kind;          /* monster type or party member index */
    uint8_t object;        /* index into objects */
    uint8_t timer;         /* counts down to next turn */
    uint8_t x, y;
};
#pragma pack(pop)
static_assert(sizeof(Combatant) == 8, "Combatant size");

/* The state of a battle. `combat` is the one Combat object.
 *
 * All of it is the object's own. Some of it is part of a saved game: Combat registers those
 * members with SaveGame, which puts them at their old places in SAVED.GAM. */
class Combat {
public:
    static Combat &instance();

    Combatant combatants[32] = {};       /* slots 0-5 the party, 6-31 monsters */
    /* Start squares, read from the combat map (load_combat_map). */
    uint8_t monster_start_x[16] = {}, monster_start_y[16] = {};
    uint8_t party_start_x[6] = {}, party_start_y[6] = {};
    uint8_t on_ladder = 0;               /* a corridor fight on a ladder square (build_corridor_map) */
    /* A missile's flight, in pixels, 0xFF ends: x in the first half, y in the second. */
    enum { PATH_SIZE = 330 };
    uint8_t path[2 * PATH_SIZE] = {};

    /* Part of the saved game: SaveGame (savegame.h) reads and writes these at their places in
     * SAVED.GAM, which are the ds offsets given. */
    uint8_t turn = 0;            /* ds:589E  slot of the combatant whose turn it is; 0xFF none */
    uint8_t flags = 0;           /* ds:58A1  0x80 dungeon room, 2 corridor, 4 camp ambush */
    uint8_t event = 0;           /* ds:58A2  2 vanished, 8 poisoned, 0x10 escaped, 0x20 killed / missed */
    uint8_t exit_dir = 0;        /* ds:58A0  how the battle ended: 1-4 map side, 5 up, 6 down, 'M' endgame */
    uint8_t victory = 0;         /* ds:58A3  "VICTORY!" was shown */
    uint8_t redraw = 0;          /* ds:589F */
    uint8_t minute_ctr = 0;      /* ds:5882  every 10 rounds a minute passes */
    uint8_t attack_item = 0;     /* ds:589D  weapon or missile in use; 0xFF bare hands */
    uint8_t cast_circle = 0;     /* ds:588F  circle of the spell being cast; 0 for weapons */
    uint8_t magic_attack = 0;    /* ds:5890  1 while a spell or magic weapon attacks */
    uint8_t aim_x = 0, aim_y = 0;   /* ds:5899  where aim_cursor ended */
    uint8_t last_attacker[32] = {};   /* ds:58A8  per slot (check_interference) */
    uint8_t prev_location = 0;   /* ds:5894  the location the battle or room was entered from */

    /* The line figures of missile kinds 2 and 7 (ds:2188, ds:2198). */
    static int8_t missile_shapes[846];

    /* ---- the battle itself (combat.cpp) ---- */

    /* main program */
    void run_combat(int arg, int object, unsigned flags);     /* 5f86 */
    void load_combat_map(int map_no);                            /* 60ec */
    void combat_with_object(int object);                      /* 6150 */
    void camp_ambush_combat(int hours, int guard);            /* 6360 */
    int  add_combatant(uint8_t z, uint8_t y, uint8_t x, int kind, int type);   /* 6506: kind 0 monster, 1 party, 2 object */
    void apply_ring_effects(int slot);                        /* 6794 */
    void wake_combatant(int slot);                            /* 6800 */
    void sleep_unless_poisoned(unsigned slot);                /* 6880 */
    void sleep_combatant(unsigned slot);                      /* 68ae */
    void clear_combat_tables(void);                           /* 6936: also places the party */
    void place_monster_group(unsigned monster, unsigned flags);   /* 6bc2 */
    int  in_combat_bounds(int y, int x);                      /* 6d82 */
    int  get_armour_bonus(int member);                        /* 6da8 */
    int  unequip_item(unsigned item, int member);             /* 6e60 */
    int  combatant_side(int slot);                            /* 5646: 0 the party's side, 1 the other */
    void flash_combatant(unsigned slot);                      /* 3564 */
    void victory_fanfare(void);                               /* 4368 */

    /* COMBAT.OVL */
    int  combat_tile_free(unsigned y, unsigned x, uint8_t mover);      /* a290 */
    int  monster_ranged_attack(int force_miss, int target, int attacker);   /* a3de */
    int  monster_attack(int slot);                            /* a4b6 */
    void monster_turn(void);                                  /* a684 */
    int  combat_item_command(int which, const char *prompt);  /* a7d4 */
    int  append_weapon_name(int count, int item);             /* a846 */
    void party_member_turn(void);                             /* a8ce */
    int  combat_main_loop(void);                              /* ae24 */
    int  find_nearest_enemy(int slot, Step *toward = nullptr);   /* afc0: slot or -1; the step towards it (away, if fleeing) */
    int  monster_move(int slot);                              /* b174 */
    int  combat_room_trigger(unsigned y, unsigned x);         /* b3aa */
    int  random_combat_pos(Square *pos);                      /* b49e: a random square in *pos; 0 if it is off the map */
    void combat_remove(int which);                            /* b4c6: -1 - slot, or object + 1 */
    int  calc_damage(int defender, int attacker);             /* b540 */
    uint8_t get_combatant_speed(int slot);                    /* b62a */
    int  get_combatant_stat(int which, int slot);             /* b672 */
    int  roll_to_hit(int weapon, int magic, int attacker, int defender);   /* b766 */
    int  apply_damage(unsigned damage, int slot);             /* b804: experience earned */
    void apply_poison_hit(int attacker, int defender);        /* bb4a */
    void resolve_hit(int attacker, int defender);             /* bbda */
    int  monster_morale(int slot);                            /* bcec: 1 critical ... 4 barely wounded */
    void apply_tile_hazard(unsigned slot);                    /* bdae */
    void check_regurgitate(int slot);                         /* bef6 */

    /* SJOG.OVL */
    int  count_members_where(unsigned item);                  /* dab4 */
    struct Sides {
        unsigned party, others;
    };
    Sides count_combat_sides(void);                           /* daec: who is still fighting, on each side */
    int  combat_leave_map(unsigned exit_dir, int slot);       /* db32 */
    int  combat_move_member(int dir, int slot);               /* dbd6 */
    int  combat_klimb(void);                                  /* dcea */
    void check_absorbed(void);                                /* de24 */
    int  combat_cant_do(int kind, const char *prompt);        /* dea6 */
    int  combat_set_active_player(unsigned member);           /* defa */
    void combat_end_of_turn(void);                            /* df92 */
    void combat_cleanup(int object);                          /* dfbe */
    int  combat_is_blocked(int slot, unsigned y, unsigned x); /* e058 */
    int  combat_is_surrounded(int slot);                      /* e0c8 */
    int  combat_check_pass_out(void);                         /* e14e */

    /* ---- attacks, aiming and missiles (comsubs.cpp; see comsubs.h) ---- */

    int  magic_resist_roll(int missile, int target, int attacker);   /* e1e0: 1 = resisted */
    void decay_combat_fields(void);                           /* e236 */
    void print_combatant_name(int slot);                      /* e274 */
    void print_missed(int slot);                              /* e2b2 */
    int  monster_special(int slot);                           /* e2d4: possess, turn invisible, gate in a daemon */
    void report_hit(int attacker, int target);                /* e4f2 */
    int  combatant_distance(int a, int b);                    /* e6b4 */
    int  aim_cursor(int range, unsigned slot);                /* e6e4: 0 cancelled, else the distance; aim_x / aim_y */
    int  combatant_at(unsigned y, unsigned x);                /* e928: slot or -1 */
    Square random_near(int y, int x);                         /* e9b4: a square next to (y, x), on the map */
    int  fire_missile(int anim, int hit, unsigned y, unsigned x, int attacker, Square *landed = nullptr);   /* ea02: slot hit or -1; where it came down */
    void consume_ammo(int hit, unsigned weapon);              /* eb5c */
    int  check_interference(int slot);                        /* ebdc */
    void ranged_attack(int anim, int weapon, unsigned slot);  /* ec48 */
    void melee_attack(int weapon, unsigned target, int slot); /* edd8 */
    void attack_with(int weapon, unsigned slot);              /* ee32 */
    void attack_with_item(int weapon_count, int item);        /* ef1c */
    void cmd_attack_combat(int weapon_count, int slot);       /* ef76 */
    void trace_line(uint8_t *path_y, uint8_t *path_x, int to_y, int to_x, int from_y, int from_x);   /* f006 */
    void draw_missile_frame(uint8_t *path_y, uint8_t *path_x, int kind, int frame, uint8_t y, uint8_t x);   /* f12a */
    int  animate_missile(int kind, int to_y, int to_x, int from_y, int from_x, Square *stopped = nullptr);   /* f4be: 0 if stopped short, at *stopped */


    Combat(const Combat &) = delete;
    Combat &operator=(const Combat &) = delete;

private:
    Combat();
};

extern Combat &combat;

#endif
