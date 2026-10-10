/* endings.h - BLCKTHRN.OVL (capture by Blackthorn, death and resurrection) and ENDGAME.OVL
 * (rescuing Lord British, the ending pictures and proclamation). These scenes play on the 11 x 11
 * combat-style map (local_map) with location 0xFF and actors in the object table. */
#ifndef U5_ENDINGS_H
#define U5_ENDINGS_H

#include <stdint.h>

/* main program helpers used by the scenes */
void flash_square(int y, int x);          /* 3522 */
void screen_shake(void);                  /* 3072 */
int  str_index(uint8_t c, const char *s); /* 4d76: position of c in s, or strlen(s) */

/* BLCKTHRN */
void cutscene_wait(int n);                /* a290 */
int  cutscene_actor(unsigned cmd);        /* a2be: object slot; direction to dir_x / dir_y */
void run_cutscene(uint16_t script);       /* a34e: script at a ds offset */
void blackthorn_demand(int shrine, int attempt);       /* a508 */
int  check_mantra_answer(int shrine);     /* a57a */
void companion_executed(int say);         /* a63e */
void blackthorn_release(void);            /* a7a0 */
void blackthorn_threat(void);             /* a7ac */
void blackthorn_interrogation(int alive, int shrine);  /* a7da */
void captured_by_blackthorn(void);        /* a89e: ends in Blackthorn's castle (location 0x12) */
void party_death(void);                   /* aba0: ends in Lord British's castle at 6:00 */

/* ENDGAME */
void ending_pictures(void);               /* a290 */
void print_ending_word(const char *word); /* a4ca */
void print_number_words(int n);           /* a51c */
void print_ordinal_words(int n);          /* a566 */
void print_proclamation(void);            /* a5b6: never returns in the original; here on quit */
void ending_tick(void);                   /* a78e */
int  walk_ending_actor(unsigned y, unsigned x, int slot);   /* a7a0: 1 while it is still walking */
void ending_actor_leave(int slot);        /* a832 */
void rescue_lord_british(void);           /* a8d8 */

#endif
