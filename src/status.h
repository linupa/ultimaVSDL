/* status.h - the party panel on the right (rows 1-9 of text window 1) and the panel title bars.
 * Main program 0000:2726-0000:2e8e, 0000:39fc, 0000:400c, 0000:4c2a-0000:4f3c. */
#ifndef U5_STATUS_H
#define U5_STATUS_H

int  check_party_state(void);              /* 39fc: -1 all dead, 1 all asleep, 0 ok (dir_x = first able) */
void draw_member_status_row(unsigned member);   /* 2726 */
void draw_gold(void);                      /* 2884 */
void draw_status_bar(void);                /* 2900: party rows, food, gold or hull, date, active spell */
void highlight_member_row(int member);     /* 2a28: XOR bar over the row */
int  select_member(int allow_all);         /* 2d7a: index, -1 none, -2 all (allow_all) */
int  ask_member_number(void);              /* 2e8e */
void regen_ring_tick(void);                /* 400c */
void draw_label_left(void);                /* 4c2a */
void draw_label_right(void);               /* 4cce */
void draw_panel_top_bar(void);             /* 4e20 */
void draw_panel_title(const char *title);  /* 4e50 */
void clear_side_panel(void);               /* 4efc */
void draw_panel_bottom_bar(void);          /* 4f3c */
void draw_list_header_bar(void);            /* 4daa */
void draw_list_marker(uint8_t c);          /* 4dea */

#endif
