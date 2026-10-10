/* stats.h - ZSTATS.OVL: the Ztats pages, Ready and the item lists (also used by Use and the
 * armoury's sell list), and resurrection from CAST2.OVL (used by healers and magic). */
#ifndef U5_STATS_H
#define U5_STATS_H

#include <stdint.h>

int  is_equipped_by(unsigned item, int member);                              /* ZSTATS e6f8 */
int  list_prev_item(int member, const uint8_t *counts, int size, int index);  /* ZSTATS e74c: -1 none */
int  list_next_item(int member, const uint8_t *counts, int size, int index);  /* ZSTATS e784: -1 none */
void print_list_entry(unsigned marker, uint16_t names, const uint8_t *counts, int index);  /* ZSTATS e7c2 */
void collect_special_items(void);                                             /* ZSTATS eb7a */
void set_list_window(int top_row);                                            /* ZSTATS e63e */
int  ready_item_list(int mode, int member, int first);                        /* ZSTATS f10e: mode 'R' or 'U' */
int  ready_item(int item, int member);                                        /* ZSTATS ee3c: 1 = close the list */
int  ask_player_number(int allow_party);                                      /* ZSTATS e1e0 */
void show_member_stats(int member);                                           /* ZSTATS e262 */
int  print_equipped_name(int item);                                           /* ZSTATS e458 */
void show_member_arms(int member);                                            /* ZSTATS e488 */
void show_equipment_page(void);                                               /* ZSTATS e57c */
int  browse_item_list(uint16_t names, const uint8_t *counts, int size, const char *title);   /* ZSTATS e8c8 */
void cmd_zstats(void);                                                        /* ZSTATS ec1a */
void print_item_prompt(const char *text);                                     /* ZSTATS edce */
int  free_hands(int member);                                                  /* ZSTATS edea */
void cmd_ready(void);                                                         /* ZSTATS f476 */
int  resurrect_member(int from_scroll, int member);                           /* CAST2 e7c0 */

#endif
