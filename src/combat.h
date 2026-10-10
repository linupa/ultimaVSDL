/* combat.h - combat: the core in the main program (0000:5f86-6f1c), COMBAT.OVL (the battle loop,
 * turns, monster behaviour, damage) and the combat half of SJOG.OVL. The attack commands and
 * missiles (COMSUBS.OVL) are in comsubs.c. Parameter order follows the Ghidra signatures.
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
 * Dungeon rooms (combat_flags 0x82) and corridor fights (2) are set up in dungeon.c. */
#ifndef U5_COMBAT_H
#define U5_COMBAT_H

#include <stdint.h>

/* main program */
void run_combat(int arg, int object, unsigned flags);     /* 5f86 */
void load_combat_map(int map);                            /* 60ec */
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
int  find_nearest_enemy(int slot);                        /* afc0: slot or -1; direction to dir_x / dir_y */
int  monster_move(int slot);                              /* b174 */
int  combat_room_trigger(unsigned y, unsigned x);         /* b3aa */
int  random_combat_pos(void);                             /* b49e */
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
void count_combat_sides(void);                            /* daec: dir_y = party's side, dir_x = the other */
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

#define MONSTER_STAT(type, field) ds_u8((uint16_t)(0x13BC + (type) * 8 + (field)))
#define MONSTER_FLAGS(type) ds_u16((uint16_t)(0x153C + (type) * 2))
#define MONSTER_NAME(type) ds_str(ds_u16((uint16_t)(0x1856 + (type) * 2)))

#endif
