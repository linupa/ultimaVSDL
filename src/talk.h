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

int  str_equal_nocase(const char *a, const char *b);      /* bf80 */
int  is_talk_through_tile(unsigned y, unsigned x);         /* bfd4: counters you can talk across */
int  ask_pay_yes_no(void);                                 /* c02c: 1 = No */
void open_shop(int merchant);                              /* c066: the merchant's shop (shops.c) */
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

#endif
