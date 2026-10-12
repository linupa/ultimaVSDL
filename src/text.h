/* text.h - text windows, fonts and printing (main program 0000:1184-0000:1fa0).
 *
 * The screen is 40 x 25 cells of 8 x 8 pixels. There are four text windows (TextWindows::windows); the
 * current one holds the cursor, colours and attribute flags.
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

#define TEXT_COLS 40
#define TEXT_ROWS 25

/* A text window: its rectangle in cells, its cursor and its attributes. */
#pragma pack(push, 1)
struct TextWindow {
    uint8_t left, top, right, bottom;
    uint8_t cur_col, cur_row;
    uint8_t attr;          /* colour (low nibble) */
    uint8_t flags;
};
#pragma pack(pop)
static_assert(sizeof(TextWindow) == 8, "TextWindow size");

/* The text screen: four windows, the fonts and the current attributes. `text_windows` is the one
 * TextWindows object. None of this is part of a saved game, so it all lives here.
 *
 * The functions below the class (print_text, put_char, set_cursor, ...) are what the game calls:
 * each just passes on to the method of the same name. */
class TextWindows {
public:
    static TextWindows &instance();

    TextWindow windows[4] = {};
    uint16_t cur_window_index = 0;
    uint16_t auto_advance = 1;      /* put_char moves the cursor after a glyph only when this is set */
    uint16_t underline = 0;         /* 0xFE toggles: glyph row 7 drawn solid */
    uint16_t centred = 0;           /* 0xFC sets, 0xFB clears: print_text centres each line */
    uint16_t inverse = 0;           /* 0xFD toggles: glyph bits inverted */
    uint8_t fg = 0, bg = 0;         /* the current window's colours */
    /* The original keeps each font in its own DOS memory block, with the segment in font_table
     * (0 = not loaded) and the selected one in cur_font; here those hold slot + 1. */
    uint16_t font_table[4] = {};
    uint16_t cur_font = 0, cur_font_index = 0;

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

    TextWindow *cur_text_window(void);   /* port helper: the current window */

    TextWindows(const TextWindows &) = delete;
    TextWindows &operator=(const TextWindows &) = delete;

private:
    TextWindows() = default;

    /* IBM.CH, RUNES.CH: 128 glyphs, 8 rows of 8 pixels; the Hercules fonts IBM.HCS, RUNES.HCS:
     * 12 rows of 16 dots. */
    enum { FONT_BYTES = 128 * 24 };
    uint8_t fonts_[4][FONT_BYTES] = {};
    uint16_t glyph_save[4] = {};    /* put_char's copy of a glyph while it underlines or inverts it */
};

extern TextWindows &text_windows;

inline void reset_text_windows(void) { text_windows.reset_text_windows(); }
inline void reset_text_attrs(void) { text_windows.reset_text_attrs(); }
inline void select_text_window(uint16_t window) { text_windows.select_text_window(window); }
inline void define_text_window(uint8_t bottom, uint8_t right, uint8_t top, uint8_t left, uint16_t window) { text_windows.define_text_window(bottom, right, top, left, window); }
inline void set_cursor(int row, int col) { text_windows.set_cursor(row, col); }
inline uint8_t get_cursor_col(void) { return text_windows.get_cursor_col(); }
inline uint8_t get_cursor_row(void) { return text_windows.get_cursor_row(); }
inline void set_text_color(uint8_t color) { text_windows.set_text_color(color); }
inline void set_text_bg_color(uint8_t color) { text_windows.set_text_bg_color(color); }
inline int load_font(uint16_t slot, const char *filename) { return text_windows.load_font(slot, filename); }
inline void select_font(uint16_t font) { text_windows.select_font(font); }
inline void put_char(uint8_t c) { text_windows.put_char(c); }
inline void print_text(const char *text) { text_windows.print_text(text); }
inline void print_number(char pad, int width, int value) { text_windows.print_number(pad, width, value); }
inline void erase_chars(int count) { text_windows.erase_chars(count); }
inline TextWindow * cur_text_window(void) { return text_windows.cur_text_window(); }

/* Port helper: selects a text window for the rest of the scope, then goes back to the one that
 * was current (the original saves cur_window_index and selects it again at the end). */
class WindowScope {
public:
    explicit WindowScope(uint16_t window) : saved_(text_windows.cur_window_index) { select_text_window(window); }
    ~WindowScope() { select_text_window(saved_); }
    WindowScope(const WindowScope &) = delete;
    WindowScope &operator=(const WindowScope &) = delete;

private:
    uint16_t saved_;
};

/* Port hook: if set, receives every character put_char prints (not the
 * in-place cursor drawing), and '\n' for line breaks
 * (explicit or wrapped). For tests and logging. */
extern void (*text_echo)(uint8_t c, int rune_font);

#endif
