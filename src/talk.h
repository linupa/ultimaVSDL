/* talk.h - conversations: the .TLK script interpreter and the talk command. Port of TALK.OVL
 * (TALK::bf80-d28f). The script format is in docs/conversations.md.
 *
 * The script (0x400 bytes) is loaded into text_buffer and read through talk_ptr, a ds offset.
 * Printing goes through a word buffer (talk_word_buf) so words wrap whole in the 16-column
 * message window; bytes with bit 7 clear are drawn in the rune font. Most functions return 1 when
 * the conversation has ended. The conversation nests: a question (label) runs talk_main_loop again
 * from inside the string being executed, and the "1" result unwinds everything. */
#ifndef U5_TALK_H
#define U5_TALK_H

#include <stdint.h>
#include "strings.h"

/* A conversation with an NPC: the .TLK script interpreter and its state. `conversation` is the
 * one Conversation object.
 *
 * The script itself is loaded into the data segment's text buffer and read through ptr, a ds
 * offset. Two things belong to the saved game; they are registered with SaveGame (savegame.h),
 * which puts them at their old places in SAVED.GAM. */
class Conversation {
public:
    static Conversation &instance();

    uint16_t npc = 0;               /* NPC slot being talked to */
    uint16_t ptr = 0;               /* script read pointer (ds offset into the text buffer) */
    uint8_t label = 0;              /* label to jump to (0x91-0x9F) */
    uint16_t keyword_index = 0;
    char input[16] = {};            /* the keyword the player typed (the shops read theirs here too) */

    uint8_t pending_op = 0;         /* op waiting for operands */
    uint8_t operand_count = 0;
    uint8_t operands[3] = {};       /* operand bytes for ops 0x85 / 0x86 / 0xFE */
    uint8_t font_mask = 0xFF;       /* op 0x8E toggles the rune font (bit 7) */
    uint8_t question_pending = 0;   /* set while a question is asked */

    uint8_t word_buf[16] = {};      /* word being printed: words wrap whole */
    uint8_t word_len = 0;
    uint8_t last_col = 0;
    uint8_t last_op = 0;            /* last printed byte (avoids a double quote) */
    uint8_t need_space = 0;

    /* Part of the saved game (registered with SaveGame). */
    uint8_t alms_counter = 0;       /* ds:588B  turns since alms were last given */
    /* A bit per NPC met, by location 1-0x20; entry 0 is unused. In the file they are the words from
     * ds:5BDA on: the original's table starts at 5BD6, but its entry 0 is the same word as the
     * last of the dead NPC bits before it (Npcs::dead_bits[0x20]). */
    uint32_t met_bits[33] = {};

    /* Expansions of the script's bytes below 0x81 ("the", "thou", ...; ds:24E8) and the words
     * every NPC knows, NAME / JOB / BYE and the swear words (ds:4AA8). */
    enum { WORDS = 129, BUILTIN_KEYWORDS = 34 };
    const char *word(unsigned n) const { return strings[n < WORDS ? words_[n] : Strings::NONE]; }
    const char *builtin_keyword_text(unsigned n) const { return strings[n < BUILTIN_KEYWORDS ? builtin_keywords_[n] : Strings::NONE]; }

    int  is_talk_through_tile(unsigned y, unsigned x);         /* bfd4: counters you can talk across */
    int  ask_pay_yes_no(void);                                 /* c02c: 1 = No */
    void open_shop(int merchant);                              /* c066: the merchant's shop (shops.cpp) */
    int  guard_toll(void);                                     /* c162: 1 = refused (the guard arrests) */
    int  talk_to_npc(int npc);                                 /* c29e */
    int  cmd_talk(void);                                       /* c39c: T */
    void talk_emit_newline(void);                              /* c452 */
    void talk_emit_quote(void);                                /* c45a */
    void talk_flush_word(void);                                /* c462 */
    void talk_buffer_char(uint8_t c);                          /* c4f4 */
    int  talk_op_pay_gold(void);                               /* c536 */
    void talk_op_give_item(uint8_t item);                      /* c602 */
    int  talk_skip_to(uint8_t stop, uint8_t match);            /* c6a8 */
    void talk_seek_string(int index);                          /* c6da */
    int  talk_run_string(void);                                /* c708 */
    int  talk_say_string(int index);                           /* c72a */
    int  talk_op_or(void);                                     /* c73e */
    void talk_op_avatar_name(void);                            /* c764 */
    int  talk_op_join_party(void);                             /* c78a */
    int  talk_goto_label(void);                                /* c8ba */
    int  talk_run_next_string(void);                           /* c8e0 */
    int  talk_goto_label_default(void);                        /* c8ee */
    int  talk_seek_keyword(int index);                         /* c91a */
    int  talk_match_keyword(void);                             /* c958 */
    void talk_read_input(void);                                /* c9ac */
    int  talk_say_bye(void);                                   /* c9bc */
    int  talk_builtin_keyword(uint8_t index);                  /* c9d4 */
    int  talk_main_loop(void);                                 /* ca84 */
    int  talk_match_label_answer(void);                        /* cb54 */
    int  talk_ask_question(void);                              /* cbdc */
    void talk_mark_met(int npc);                               /* ccc2 */
    int  talk_has_met(int npc);                                /* ccfa */
    int  talk_op_operands(uint8_t b);                          /* cd3e */
    void talk_op_ask_name(void);                               /* cdf8 */
    int  talk_exec_op(uint8_t op);                             /* ceb2 */
    int  talk_greeting(void);                                  /* d09c */
    void talk_maybe_steal(void);                               /* d100 */
    void talk_run_conversation(int script);                    /* d1fe */

    Conversation(const Conversation &) = delete;
    Conversation &operator=(const Conversation &) = delete;

private:
    Conversation();

    int word_start(int pos);
    int find_word(const char *needle);
    const char * script_str(void);
    void seek_label(void);
    void rebuke_pause(void);
    int check_builtins(void);
    void clear_pending(void);

    static Strings::Id words_[WORDS], builtin_keywords_[BUILTIN_KEYWORDS];
};

extern Conversation &conversation;

int  str_equal_nocase(const char *a, const char *b);      /* bf80 */

#endif
