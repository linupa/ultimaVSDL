/* text.c - text windows, fonts and printing. Port of main program 0000:1184-0000:1fa0
 * (put_char, glyph_colour_masks and glyph_cell_pixels are assembly; print_text, print_number and
 * erase_chars are C). See text.h for the interface. */
#include <stdio.h>
#include <string.h>
#include "files.h"
#include "text.h"
#include "video.h"

/* The original loads each font into its own DOS memory block and keeps the segment in
 * dg.font_table[slot] (0 = not loaded); dg.cur_font is the selected segment. The port keeps the
 * glyphs here and stores slot + 1 in those words instead of a segment. */
#define FONT_BYTES (128 * 24)  /* IBM.CH, RUNES.CH: 128 glyphs, 8 rows of 8 pixels; the Hercules fonts
                                * IBM.HCS, RUNES.HCS: 12 rows of 16 dots */
static uint8_t fonts[4][FONT_BYTES];

void (*text_echo)(uint8_t c, int rune_font);

TextWindow *cur_text_window(void)
{
    return (TextWindow *)ds_ptr(dg.cur_window);
}

void reset_text_windows(void)
{
    for (int i = 0; i < 4; i++) {
        TextWindow *w = &dg.text_windows[i];
        w->left = 0; w->top = 0; w->right = TEXT_COLS - 1; w->bottom = TEXT_ROWS - 1;
        w->cur_col = 0; w->cur_row = 0;
        w->attr = 0x0F;   /* white on black */
        w->flags = 0;
    }
    dg.cur_window = (uint16_t)offsetof(struct DGroup, text_windows);
    dg.cur_window_index = 0;
}

void reset_text_attrs(void)
{
    dg.text_underline = 0;
    dg.text_centred = 0;
    dg.text_inverse = 0;
}

void select_text_window(uint16_t window)
{
    if (window > 3) return;
    dg.cur_window_index = window;
    dg.cur_window = (uint16_t)(offsetof(struct DGroup, text_windows) + window * sizeof(TextWindow));
    TextWindow *w = cur_text_window();
    dg.text_fg = w->attr & 0x0F;
    dg.text_bg = (uint8_t)(w->attr >> 4);
    dg.text_underline = w->flags & 1;
    dg.text_centred = (w->flags >> 1) & 1;
    dg.text_inverse = (w->flags >> 2) & 1;
}

/* window_extent (1c5b): clamp to the screen and order the corners. */
void define_text_window(uint8_t bottom, uint8_t right, uint8_t top, uint8_t left, uint16_t window)
{
    if (window >= 4) return;
    int8_t l = (int8_t)left, t = (int8_t)top, r = (int8_t)right, b = (int8_t)bottom;  /* signed compares */
    l = l < 0 ? 0 : l > 39 ? 39 : l;
    t = t < 0 ? 0 : t > 24 ? 24 : t;
    r = r < 0 ? 0 : r > 39 ? 39 : r;
    b = b < 0 ? 0 : b > 24 ? 24 : b;
    if (l > r) {   /* the original only checks the rows when it had to swap the columns */
        int8_t x = l; l = r; r = x;
        if (t > b) { x = t; t = b; b = x; }
    }
    TextWindow *w = &dg.text_windows[window];
    w->left = (uint8_t)l; w->top = (uint8_t)t; w->right = (uint8_t)r; w->bottom = (uint8_t)b;
}

void set_cursor(int row, int col)
{
    TextWindow *w = cur_text_window();
    /* byte arithmetic and unsigned compares, as the original: negative values are off screen */
    if ((uint8_t)(col + w->left) > TEXT_COLS - 1 || (uint8_t)(row + w->top) > TEXT_ROWS - 1) return;
    w->cur_col = (uint8_t)col;
    w->cur_row = (uint8_t)row;
}

uint8_t get_cursor_col(void) { return cur_text_window()->cur_col; }
uint8_t get_cursor_row(void) { return cur_text_window()->cur_row; }

void set_text_color(uint8_t color)
{
    TextWindow *w = cur_text_window();
    dg.text_fg = color & 0x0F;
    w->attr = (uint8_t)((w->attr & 0xF0) | dg.text_fg);
}

void set_text_bg_color(uint8_t color)
{
    TextWindow *w = cur_text_window();
    dg.text_bg = color & 0x0F;
    w->attr = (uint8_t)((w->attr & 0x0F) | dg.text_bg << 4);
}

int load_font(uint16_t slot, const char *filename)
{
    if (slot > 3) return -2;
    FILE *f = game_fopen(filename, "rb");
    if (!f) return -1;
    memset(fonts[slot], 0, FONT_BYTES);
    size_t n = fread(fonts[slot], 1, FONT_BYTES, f);
    fclose(f);
    if (n == 0) return -1;
    dg.font_table[slot] = (uint16_t)(slot + 1);
    return (int)n;
}

void select_font(uint16_t font)
{
    if (font >= 4 || dg.font_table[font] == 0) return;
    dg.cur_font = dg.font_table[font];
    dg.cur_font_index = font;
}

/* Pixel rectangle of the current window (glyph_cell_pixels, 1f77). */
static void window_pixels(const TextWindow *w, int *x1, int *y1, int *x2, int *y2)
{
    *x1 = w->left * 8;
    *y1 = w->top * 8;
    *x2 = w->right * 8 + 7;
    *y2 = w->bottom * 8 + 7;
}

void put_char(uint8_t c)
{
    TextWindow *w = cur_text_window();
    int x1, y1, x2, y2;

    if (c > 0x7F) {
        switch (c) {
        case 0xFF:   /* clear the window */
            w->cur_col = 0;
            w->cur_row = 0;
            video_set_color((w->attr >> 4) & 0x0F);
            window_pixels(w, &x1, &y1, &x2, &y2);
            video_fill_rect(x1, y1, x2, y2, 0);
            video_set_color((uint8_t)dg.draw_color);
            return;
        case 0xFE: dg.text_underline ^= 1; w->flags ^= 1; return;
        case 0xFD: dg.text_inverse ^= 1; w->flags ^= 4; return;
        case 0xFC: dg.text_centred = 1; w->flags |= 2; return;
        case 0xFB: dg.text_centred = 0; w->flags &= (uint8_t)~2; return;
        default:
            /* The original masks the code for the 0x0A/0x0D tests but still indexes the font with the
             * unmasked byte (reading past the 128 glyphs). The port uses the masked glyph. */
            c &= 0x7F;
            break;
        }
    }

    if (text_echo && dg.text_auto_advance && (c == '\n' || c >= 0x20)) text_echo(c, dg.cur_font_index != 0);
    if (c == '\n') {
        w->cur_row++;
        w->cur_col = 0;
    } else if (c == '\r') {
        w->cur_col = 0;
    } else {
        /* glyph_colour_masks (17f4): the original patches the glyph in the font and restores it from
         * dg.glyph_save after drawing; drawing from a patched copy is equivalent. */
        uint8_t glyph[24];
        int n = dg.video_type == 3 ? 24 : 8;
        memcpy(glyph, &fonts[(dg.cur_font ? dg.cur_font : 1) - 1][c * n], (size_t)n);
        if (dg.text_underline) {
            glyph[n - 1] = 0xFF;
            if (n == 24) glyph[22] = 0xFF;
        }
        if (dg.text_inverse)
            for (int i = 0; i < n; i++) glyph[i] = (uint8_t)~glyph[i];
        video_draw_char(w->cur_col + w->left, w->cur_row + w->top, glyph, dg.text_fg, dg.text_bg);

        if (!dg.text_auto_advance) return;
        w->cur_col++;
        if ((int8_t)(w->cur_col + w->left) <= (int8_t)w->right) return;
        if (text_echo) text_echo('\n', 0);
        w->cur_row++;
        w->cur_col = 0;
    }

    if ((int8_t)(w->cur_row + w->top) > (int8_t)w->bottom) {   /* scroll the window up one line */
        window_pixels(w, &x1, &y1, &x2, &y2);
        w->cur_row--;
        video_scroll_rect(x1, y1, x2, y2, -8);
    }
}

void print_text(const char *text)
{
    uint8_t line[TEXT_COLS];
    int pos = 0, done = 0, newline_after = 0;

    if (*text == '\0') return;
    TextWindow *w = &dg.text_windows[dg.cur_window_index];
    int centre = w->flags & 2;
    int width = w->right - w->left;          /* columns in the window - 1 */

    do {
        if (newline_after) put_char('\n');
        int start = 0, skip_spaces = 0;
        newline_after = 0;
        int avail = width - get_cursor_col();    /* cells left on this row - 1 */

        /* Collect up to avail + 1 characters, stopping at a line end or the end of the text. */
        int n = 0, i = pos;
        while (text[i] != '\n' && text[i] != '\r' && text[i] != '\0' && n <= avail)
            line[n++] = (uint8_t)text[i++];
        pos = i;
        int last = n;   /* becomes the index of the last character to print */

        if (n == 0) {   /* at a line end or the end of the text */
            if (text[pos] == '\0') done++;
            else put_char((uint8_t)text[pos++]);
            continue;
        }

        if (n > avail) {   /* doesn't fit: break at the last space */
            int k = n, j = pos;
            while (text[j] != ' ') {
                if (k == 0 || text[j] == '\n' || text[j] == '\0' || text[j] == '\r') break;
                k--;
                j--;
            }
            pos = j;
            if (k == 0) {
                /* one word longer than the row: print it as it is, on a fresh line if not at its start */
                last = n;
                pos = i;
                if (w->cur_col != 0) {
                    put_char('\n');
                    skip_spaces = 1;
                }
            } else {
                last = k - 1;   /* drop the trailing spaces and line ends */
                while (last >= 0 && (line[last] == ' ' || line[last] == '\r' || line[last] == '\n'))
                    last--;
                if (text[pos] != '\0') pos++;   /* skip the space it broke at */
                if (last < avail) newline_after++;   /* a full row wraps by itself */
            }
            if (last > avail) last--;
        } else {
            last--;
        }

        if (centre) {
            if (last > width) put_char('\n');
            set_cursor(get_cursor_row(), (avail - last) / 2);
        }
        if (skip_spaces)
            while (line[start] == ' ') start++;
        for (int k = start; k <= last; k++) put_char(line[k]);
    } while (!done);
}

void print_number(char pad, int width, int value)
{
    static const int16_t pow10[] = {1, 10, 100, 1000, 10000};   /* ds:5402 (ds:5400 is unused) */
    char buf[40];
    int len = 0;

    if (width > 39) width = 39;
    int negative = value < 0;
    if (negative) value = -value;
    int digits = 1;
    for (int k = 1; k < 5; k++)
        if (pow10[k] <= value) digits++;
    if (negative) digits++;
    int padding = width - digits;
    if (padding < 1) padding = 0;
    for (; len < padding; len++) buf[len] = pad;
    if (negative) {
        buf[len++] = '-';
        digits--;
    }
    do {
        int p = pow10[digits - 1];
        buf[len++] = (char)('0' + value / p);
        value %= p;
    } while (--digits != 0);
    buf[len] = '\0';
    print_text(buf);
}

void erase_chars(int count)
{
    uint16_t saved = dg.text_auto_advance;
    if ((get_cursor_col() != 0 || get_cursor_row() != 0) && count != 0) {
        dg.text_auto_advance = 0;
        TextWindow *w = &dg.text_windows[dg.cur_window_index];
        int width = w->right - w->left;
        for (; count > 0; count--) {
            int col = get_cursor_col();
            put_char(' ');
            if (col == 0) set_cursor(get_cursor_row() - 1, width);
            else set_cursor(get_cursor_row(), col - 1);
        }
    }
    dg.text_auto_advance = saved;
}
