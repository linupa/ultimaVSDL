/* endings.h - BLCKTHRN.OVL (capture by Blackthorn, death and resurrection) and ENDGAME.OVL
 * (rescuing Lord British, the ending pictures and proclamation). These scenes play on the 11 x 11
 * combat-style map (local_map) with location 0xFF and actors in the object table. */
#ifndef U5_ENDINGS_H
#define U5_ENDINGS_H

#include <stdint.h>
#include "strings.h"

/* The game's set pieces: capture by Blackthorn, the death of the party, and the ending
 * (BLCKTHRN.OVL and ENDGAME.OVL). `endings` is the one Endings object. They play on a small map
 * of their own (location SCENE) with the actors in the object table, which like the party and the
 * clock belong to the game, not to this class; only the proclamation's state is its own. */
class Endings {
public:
    static Endings &instance();

    /* The cut-scenes run_cutscene plays. */
    enum Cutscene {
        SCENE_RELEASE = 0x00,          /* the guards let the prisoners go */
        SCENE_THREAT = 0x3C,           /* a guard steps up to a companion */
        SCENE_GUARDS_ENTER = 0x64,     /* two guards come in */
        SCENE_GUARDS_UNCHAIN = 0x70,   /* "Release this good man" */
        SCENE_BLACKTHORN_LEAVES = 0x78,
    };

    /* A page of the ending: as Intro::StoryPage, with `chapter` 1 for a chapter title. */
    struct Page {
        uint8_t left[2];
        uint16_t right[2];
        uint16_t text;
        uint8_t line_min, line_max, x, y, frame, set, px, py, chapter;
    };

    /* The proclamation at the end: the line being put together, and how far it has got. */
    uint8_t proclamation_line[40] = {};
    uint8_t proclamation_col = 0;

    /* ---- capture, death and the ending (endings.cpp) ---- */

    /* main program helpers used by the scenes */

    /* BLCKTHRN */
    void cutscene_wait(int n);                /* a290 */
    int  cutscene_actor(unsigned cmd, int8_t *dx, int8_t *dy);   /* a2be: object slot, and its step */
    void run_cutscene(Cutscene script);       /* a34e */
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

    Endings(const Endings &) = delete;
    Endings &operator=(const Endings &) = delete;

private:
    Endings();

    static uint8_t cutscene_scripts_[0x82];
    static uint8_t prisoner_places_[7][8], prisoner_x_[6], prisoner_y_[6];
    static uint16_t death_notes_[4][6];
    static Page pages_[6];
    static Strings::Id number_words_[40];
    static uint8_t rescue_x_[6], rescue_y_[6];

};

extern Endings &endings;

void flash_square(int y, int x);          /* 3522 */
void screen_shake(void);                  /* 3072 */
int  str_index(uint8_t c, const char *s); /* 4d76: position of c in s, or strlen(s) */

#endif
