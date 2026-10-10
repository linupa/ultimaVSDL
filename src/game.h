/* game.h - the top of the game after the intro: main's loop over outdoors / town / dungeon, the
 * command dispatcher, and the main program's own command handlers. Parameter order follows the
 * Ghidra signatures.
 */
#ifndef U5_GAME_H
#define U5_GAME_H

/* main (0000:0000), from after the intro: runs until Exit to DOS or the window is closed. */
void game_run(void);

void exit_game(int code);                    /* 02f4: here it only makes game_quit() true */
int  game_quit(void);                        /* port: Exit to DOS was chosen or the window closed */
void game_clear_exit(void);                  /* port: forget an Exit to DOS (game_run does it too) */

int  dispatch_command(int key);              /* 3178: 0 no turn, 1 a turn, 2 a talk that ended in arrest */
int  cmd_set_active_player(int key);         /* 4080: '0'-'6'; 0 = done (no turn), 1 = invalid */
void cmd_hole_up(void);                      /* 3c9a: H outdoors: repair the ship, or camp */
void cmd_quit_save(void);                    /* CAST2 f2de: Q */

#endif
