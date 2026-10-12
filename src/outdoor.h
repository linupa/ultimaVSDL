/* outdoor.h - Britannia and the Underworld: MAINOUT.OVL (movement, sailing, wandering monsters, the
 * outdoor command loop) and the rest of OUTSUBS.OVL. Parameter order follows the Ghidra signatures.
 * The map window itself (enter_outdoors, outdoor_step, chunks) is in map.cpp. */
#ifndef U5_OUTDOOR_H
#define U5_OUTDOOR_H

#include <stdint.h>

/* The outdoor game. `outdoor` is the one Outdoor object; the functions below the class are what
 * the game calls, and forward to it. */
class Outdoor {
public:
    static Outdoor &instance();

    /* Turns the monsters sit out: each flips every turn, and the monsters move only on one side. */
    uint8_t quickness_toggle = 0;    /* ds:2C55  under the Quickness spell */
    uint8_t riding_toggle = 0;       /* ds:2C57  riding a horse or flying the carpet */
    uint16_t cape_speed_toggle = 0;  /* ds:A524  sailing with the HMS Cape plans */

    /* The square the last monster to move came from: monster_step won't take it straight back
     * there (not_last_position). */
    uint8_t last_monster_x = 0;      /* ds:A526 */
    uint8_t last_monster_y = 0;      /* ds:A527 */

    void wait_if_object1_near(void);             /* 824a */
    int  outdoor_set_facing(int dir);            /* 82aa: dir 0-3 = N E S W; 1 = the turn used the move */
    int  outdoor_can_move(int dy, int dx);       /* 83ce */
    void terrain_slowdown(void);                 /* 85b0 */
    int  outdoor_move(int fast, unsigned key);   /* 8660: key 1-4 = W E N S */
    unsigned outdoor_read_key(void);             /* 8768: under sail the ship keeps going */
    int  cmd_attack_outdoor(void);               /* 88bc */
    int  enter_dungeon(const char *prompt);      /* 8960 */
    int  cmd_enter(void);                        /* 8aae */
    int  outdoor_idle_check(int ticked);         /* 8bea */
    void random_earthquake(void);                /* 8c30 */
    void outdoor_main_loop(void);                /* 8c54 */
    void outdoor_start(void);                    /* 8ef2 */
    int  encounter_chance(void);                 /* 8f5c */
    int  weighted_random_index(const uint8_t *weights);   /* 8fd4 */
    uint8_t pick_monster_for_terrain(int tile);  /* 901e */
    void random_spawn_pos(unsigned *y, unsigned *x);   /* 911e */
    void spawn_random_monster(void);             /* 9194 */
    int  is_active_monster_tile(int tile);       /* 922c */
    void damage_vehicle(void);                   /* 926e */
    void pirate_broadside(int dist_y, int dist_x, int object);   /* 9338 */
    void monster_contact(int object);            /* 9418 */
    int  monster_act(int object);                /* 94ea */
    int  monster_can_step(unsigned y, unsigned x, int object);   /* 9652 */
    int  not_last_position(unsigned y, unsigned x);              /* 9698 */
    uint8_t shadowlord_chase_weight(int object); /* 96ba */
    void monster_step(int dy, int dx, int object);               /* 9748 */
    void monster_random_walk(int object);        /* 98cc */
    void monster_chase(int chase, int object);   /* 99a4 */
    void monster_move_outdoor(int object);       /* 9b5c */
    int  outdoor_monsters_turn(void);            /* 9c30: number of monsters that acted */
    void troll_toll(void);                       /* 9d0e */
    void troll_bridge(void);                     /* 9db8 */

    void waterfall_fall(void);                   /* OUTSUBS a6e8 */
    void place_underworld_specials(void);        /* OUTSUBS a7f6 */
    void lava_burn(void);                        /* OUTSUBS a87e */
    void swamp_poison(void);                     /* OUTSUBS a88c */
    void apparition_level_up(void);              /* OUTSUBS a8e8 */

    Outdoor(const Outdoor &) = delete;
    Outdoor &operator=(const Outdoor &) = delete;

private:
    Outdoor();

    /* Read-only tables, from DATA.OVL (gamedata.h). */
    int8_t wind_dx_[4] = {}, wind_dy_[4] = {};   /* ds:29F6, ds:29FA: the way wind 1-4 blows */
    /* Wandering monsters by terrain, each with its chances out of 256: Britannia's land
     * (ds:2BC0 / 2BDC), the Underworld's land (2BCC / 2BE8), sea (2BD4 / 2BF0) and the
     * Underworld's sea (2BDA / 2BF6). */
    uint8_t land_monsters_[12] = {}, under_land_monsters_[8] = {}, sea_monsters_[6] = {}, under_sea_monsters_[2] = {};
    uint8_t land_chances_[12] = {}, under_land_chances_[8] = {}, sea_chances_[6] = {}, under_sea_chances_[2] = {};
    /* How many turns a pirate ship (tile 0x2C-0x2F, the way it faces) waits in wind 1-4; 4 =
     * never moves (ds:2BF8; the original reaches it as ds:2BF6 + wind * 2). */
    uint16_t pirate_wait_[4][4] = {};
    uint8_t chase_weight_[61] = {};              /* ds:2C18: 11 a row, distance x + 11 * distance y, both < 6 */
    uint8_t shadowlord_x_[3] = {}, shadowlord_y_[3] = {}, shadowlord_hull_[3] = {};   /* ds:3A06, 3A0A, 3A0E: where they start in the Underworld */
    uint16_t apparition_tune_[6] = {};           /* ds:3A26 */
};

/* The one Outdoor object (Outdoor::instance()). */
extern Outdoor &outdoor;

inline void wait_if_object1_near(void) { outdoor.wait_if_object1_near(); }
inline int outdoor_set_facing(int dir) { return outdoor.outdoor_set_facing(dir); }
inline int outdoor_can_move(int dy, int dx) { return outdoor.outdoor_can_move(dy, dx); }
inline void terrain_slowdown(void) { outdoor.terrain_slowdown(); }
inline int outdoor_move(int fast, unsigned key) { return outdoor.outdoor_move(fast, key); }
inline unsigned outdoor_read_key(void) { return outdoor.outdoor_read_key(); }
inline int cmd_attack_outdoor(void) { return outdoor.cmd_attack_outdoor(); }
inline int enter_dungeon(const char *prompt) { return outdoor.enter_dungeon(prompt); }
inline int cmd_enter(void) { return outdoor.cmd_enter(); }
inline int outdoor_idle_check(int ticked) { return outdoor.outdoor_idle_check(ticked); }
inline void random_earthquake(void) { outdoor.random_earthquake(); }
inline void outdoor_main_loop(void) { outdoor.outdoor_main_loop(); }
inline void outdoor_start(void) { outdoor.outdoor_start(); }
inline int encounter_chance(void) { return outdoor.encounter_chance(); }
inline int weighted_random_index(const uint8_t *weights) { return outdoor.weighted_random_index(weights); }
inline uint8_t pick_monster_for_terrain(int tile) { return outdoor.pick_monster_for_terrain(tile); }
inline void random_spawn_pos(unsigned *y, unsigned *x) { outdoor.random_spawn_pos(y, x); }
inline void spawn_random_monster(void) { outdoor.spawn_random_monster(); }
inline int is_active_monster_tile(int tile) { return outdoor.is_active_monster_tile(tile); }
inline void damage_vehicle(void) { outdoor.damage_vehicle(); }
inline void pirate_broadside(int dist_y, int dist_x, int object) { outdoor.pirate_broadside(dist_y, dist_x, object); }
inline void monster_contact(int object) { outdoor.monster_contact(object); }
inline int monster_act(int object) { return outdoor.monster_act(object); }
inline int monster_can_step(unsigned y, unsigned x, int object) { return outdoor.monster_can_step(y, x, object); }
inline int not_last_position(unsigned y, unsigned x) { return outdoor.not_last_position(y, x); }
inline uint8_t shadowlord_chase_weight(int object) { return outdoor.shadowlord_chase_weight(object); }
inline void monster_step(int dy, int dx, int object) { outdoor.monster_step(dy, dx, object); }
inline void monster_random_walk(int object) { outdoor.monster_random_walk(object); }
inline void monster_chase(int chase, int object) { outdoor.monster_chase(chase, object); }
inline void monster_move_outdoor(int object) { outdoor.monster_move_outdoor(object); }
inline int outdoor_monsters_turn(void) { return outdoor.outdoor_monsters_turn(); }
inline void troll_toll(void) { outdoor.troll_toll(); }
inline void troll_bridge(void) { outdoor.troll_bridge(); }
inline void waterfall_fall(void) { outdoor.waterfall_fall(); }
inline void place_underworld_specials(void) { outdoor.place_underworld_specials(); }
inline void lava_burn(void) { outdoor.lava_burn(); }
inline void swamp_poison(void) { outdoor.swamp_poison(); }
inline void apparition_level_up(void) { outdoor.apparition_level_up(); }

#endif
