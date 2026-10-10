/* picture.h - pictures, 1-bit bitmaps, page copies and screen effects: the main program's
 * wrappers (0000:0ace-0000:10dc) and the EGA.DRV functions behind them (0x18, 0x1B, 0x30, 0x42,
 * 0x4B, 0x4E, 0x63, 0x66, 0x69, 0x6F). Parameter order follows the Ghidra signatures.
 *
 * Picture file (.16, LZW): word frame count, then per frame (word data offset, word mask offset or
 * 0). Frame data: word width, word height, rows of 4-bit pixels (high nibble left), each row
 * padded to 8 pixels. Mask: 4 bytes, then 1 bit per pixel, set = transparent.
 * Bitmap file (.BIT, PROPORT.PCS): word glyph count, word offsets, then per glyph word width,
 * word height, 1 bit per pixel in rows of width / 8 bytes (at least 1). Glyphs draw white on the back page.
 *
 * In the original a handle is a DOS memory segment; here it is a small number (0 = failed). */
#ifndef U5_PICTURE_H
#define U5_PICTURE_H

#include <stdint.h>

uint16_t load_picture(const char *filename);      /* 0bae */
uint16_t load_bitmap(const char *filename);       /* 0fae */
void dos_free_mem(uint16_t handle);                /* 0be4 */
void dos_free_mem2(uint16_t handle);               /* 0fdc */

int  alloc_back_page(void);                        /* 0f2a: clears it; 1 = ok */
void set_draw_page(int page);                      /* 0c22: 0 screen, 1 back page */
void copy_page(int to, int from);                  /* 0f6e */
void copy_page_rect(int y2, int x2, int y1, int x1, int to, int from);   /* 0ace */
void copy_back_rect(int y2, int x2, int y1, int x1);   /* 0f46: back page -> screen, pixel dissolve */
void plot_pixel(int y, int x);                     /* 0c64 */

/* flags: 1 = upside down, 2 = mirrored (back page only). x is rounded down to a multiple of 8. */
void draw_picture_frame(int flags, int y, int x, int frame, uint16_t picture);   /* 0d4c */
int  picture_frame_size(uint16_t picture, int frame, int *w, int *h);            /* port helper */
void draw_picture_frame2(int flags, int y, int x, int frame, uint16_t picture);  /* 0d... drv 0x63: mirrored */
void set_clip_rect(int y2, int x2, int y1, int x1);                               /* clip rows for draw_picture_frame while clip_enabled */
void draw_font_glyph(int y, int x, int glyph, uint16_t bitmap);                  /* 1044 */

void reveal_strips(uint16_t bitmap);               /* INTRO a26e: the "Warriors of Destiny" reveal */
void wait_reveal_strips(void);                     /* INTRO a250: one tick, next frame of the banner */
int  show_title_picture(uint16_t bitmap);          /* 0d72 + drv 0x6F: the ORIGIN logo growing and turning; 1 if a key stopped it */
int  dissolve_tile(int row, int col, int tile);    /* 1068: a tile appears pixel by pixel */

#endif
