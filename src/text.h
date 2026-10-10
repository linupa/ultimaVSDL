/* text.h - text windows, fonts and printing (main program 0000:1184-0000:1fa0).
 *
 * The screen is 40 x 25 cells of 8 x 8 pixels. There are four text windows (dg.text_windows); the
 * current one holds the cursor, colours and attribute flags. All state lives in the data segment, as
 * in the original, so code ported from the src/ files sees the same globals.
 *
 * Parameter order follows the Ghidra signatures (scripts/signatures.tsv), so calls in the decompiled
 * source can be copied as they are. For these routines that is the REVERSE of the original source
 * order (they pop their own arguments): set_cursor(row, col) is pushed col first.
 *
 * Control codes for put_char / print_text:
 *   0x0A  new line          0x0D  carriage return (column 0, no line feed)
 *   0xFF  clear the window (to its background colour) and home the cursor
 *   0xFE  toggle underline  0xFD  toggle inverse
 *   0xFC  centring on       0xFB  centring off (print_text centres each line)
 *   other bytes >= 0x80 print as (c & 0x7F) */
#ifndef U5_TEXT_H
#define U5_TEXT_H

#include <stdint.h>
#include "data.h"

#define TEXT_COLS 40
#define TEXT_ROWS 25

/* Startup: reset_text_windows, then load_font(0, "ibm.ch"), load_font(1, "runes.ch"), select_font(0). */
void reset_text_windows(void);   /* 1184: all four windows = full screen, colour 15 on 0, window 0 */
void reset_text_attrs(void);     /* 1226: underline, centring and inverse off */

void select_text_window(uint16_t window);                                   /* 1b94 */
void define_text_window(uint8_t bottom, uint8_t right, uint8_t top, uint8_t left, uint16_t window); /* 1c22 */
void set_cursor(int row, int col);   /* 1bf2: relative to the window; ignored if off the screen */
uint8_t get_cursor_col(void);        /* 1f12 */
uint8_t get_cursor_row(void);        /* 1cee */
void set_text_color(uint8_t color);      /* 1cca: foreground, kept in the window's attr low nibble */
void set_text_bg_color(uint8_t color);   /* 1f26: background, attr high nibble */

int  load_font(uint16_t slot, const char *filename);   /* 1d02: bytes read, -1 file error, -2 bad slot */
void select_font(uint16_t font);                       /* 1c9e: ignored if the slot isn't loaded */

void put_char(uint8_t c);                          /* 16ba */
void print_text(const char *text);                 /* 1850: word-wrapped, optional centring */
void print_number(char pad, int width, int value); /* 1a3e: right-aligned in width, padded with pad */
void erase_chars(int count);                       /* 1fa0: backspace over count cells, across lines */

TextWindow *cur_text_window(void);   /* port helper: the window dg.cur_window points at */

/* Port hook: if set, receives every character put_char prints (not the
 * in-place cursor drawing), and '\n' for line breaks
 * (explicit or wrapped). For tests and logging. */
extern void (*text_echo)(uint8_t c, int rune_font);

#endif
