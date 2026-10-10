/* commands.h - command input and movement: get_command_key, passability, entering and leaving
 * towns. The movement here is the viewer's simplified version (see commands.c). */
#ifndef U5_COMMANDS_H
#define U5_COMMANDS_H

#include <stdint.h>

/* Direction codes returned by get_command_key: the arrow keys (poll_key's KEY_LEFT..KEY_DOWN, with
 * numpad_shift set) and Shift + keypad 4/6/8/2. Plain codes 1-4 (Ctrl+A to Ctrl+D) become 0xFB-0xFE. */
enum { CMD_WEST = 1, CMD_EAST = 2, CMD_NORTH = 3, CMD_SOUTH = 4 };

unsigned to_upper(uint8_t c);                  /* 2032 */
int  get_command_key(void);                    /* 266c: animates the view while waiting */
int  land_passable(int tile, uint8_t mover);   /* 2bd4 */
int  water_passable(int tile);                 /* 2c2e */
int  tile_passable(int tile, int mover);       /* 2c4c: can mover (a party/object tile) enter tile? */
int  enter_town_from_map(const char *prompt);  /* OUTSUBS a618: 0 if entered, 1 if no town here */
int  read_number(int max_digits);              /* 3b9e: typed number, optional sign */
void wait_turns(int turns);                    /* 3ae6: n x (world_tick + one tick) */
void read_line(int max_len, char *buf);       /* 3b1c: typed line, animating while waiting */
int  ask_direction(void);                      /* 35ec: 0 = "Pass", else sets dg.dir_x / dg.dir_y */
void restore_open_door(uint8_t y, uint8_t x, uint8_t tile);   /* 39cc */

/* Game messages in the data segment. */
#define MSG_NORTH   0xA2A6
#define MSG_SOUTH   0xA2AE
#define MSG_WEST    0xA2B6
#define MSG_EAST    0xA2BC
#define MSG_BLOCKED 0x26D8
#define MSG_NOT_HERE 0x8DDC
#define MSG_ENTER_WHAT 0xA158
#define MSG_OPEN    0x6E22

#endif
