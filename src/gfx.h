/* gfx.h - the main program's drawing wrappers around the graphics driver (0000:0a70-0000:0fae),
 * plus the random number generator and the screen frame. Ghidra parameter order. */
#ifndef U5_GFX_H
#define U5_GFX_H

#include <stdint.h>

void set_draw_color(uint16_t color);                 /* 0a70: masks to 0-15 (0-3 on CGA); 0xFFFF = none */
void fill_rect(int y2, int x2, int y1, int x1);       /* 0aa6: pixels, inclusive */
void xor_fill_rect(int y2, int x2, int y1, int x1);   /* 0b86 */
void draw_line(int y2, int x2, int y1, int x1);       /* 0b10 */
void draw_line_b(int y, int x);                       /* 0f90: from the end of the last line */
void draw_hline(int x2, int y, int x1);              /* 0c9c */
void draw_vline(int y2, int y1, int x);              /* 0cf2 */
int  random_between(int hi, int lo);                  /* 2092: lo..hi inclusive */
void draw_screen_frame(void);                         /* 637e: border around the view and side panels */
void set_ega_colors(void);                            /* intro_main's 16-colour role colours */
int  video_start(void);                 /* intro_main's start: picture names, colours and fonts for dg.video_type */

#endif
