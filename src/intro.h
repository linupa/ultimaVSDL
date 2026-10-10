/* intro.h - INTRO.OVL: title sequence, main menu, the "Ultima V Introduction" story.
 * including Transfer from Ultima IV. */
#ifndef U5_INTRO_H
#define U5_INTRO_H

void draw_menu_banner(void);                 /* 81d0 */
int  intro_trace_path(int y, int x);         /* 8210: draws part of Lord British's signature */
void intro_story(void);                      /* 830e */
void print_centered(const char *text);       /* 85fe: label on the bottom frame line */
void draw_menu_border(void);                 /* 86a0 */
void show_title_screen(unsigned quick);      /* 8770 */
void draw_menu_item(const char *text, int col, int row, int selected);   /* 8836 */
void draw_main_menu(int selected);           /* 887c */
void menu_scroll_effect(void);               /* 88ee: Acknowledgements scroll */
int  wait_key_or_ticks(int ticks);           /* 8b0e */

/* 8b46. Returns 1 when "Journey Onward" has loaded the saved game (the original then returns to
 * main), 0 if the window was closed. skip_title: start at the menu, as when a key is held. */
int  intro_main(int skip_title);
int  read_ultima4_party(void);               /* 91d6: 1 = bad data */
void print_ultima4_class(void);              /* 9438 */
int  convert_ultima4_stat(int stat);         /* 94aa */
void transfer_from_ultima4(void);            /* 94ea */
void print_transfer_label(int row);          /* 9fe2 */
void draw_transfer_frame(void);              /* a022 */
void draw_transfer_panel(int x);             /* a0e6 */
void draw_transfer_screen(void);             /* a1e4 */

#endif
