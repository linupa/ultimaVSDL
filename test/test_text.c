/* test_text.c - exercises the text system against the real fonts and writes test_text.ppm.
 * Usage: test_text <game dir>
 * Besides the image it prints the screen as characters, recovered by matching each 8x8 cell against
 * the font, so wrapping, centring and scrolling can be checked as text. */
#include <stdio.h>
#include <string.h>
#include "files.h"
#include "text.h"
#include "video.h"

static uint8_t ibm[128 * 8];

/* Which IBM.CH glyph is drawn at cell (col,row)? Any two colours; '?' if none, '#' if inverse. */
static char cell_char(int col, int row)
{
    uint8_t bg = video_fb[row * 8][col * 8 + 7], rows[8];
    for (int y = 0; y < 8; y++) {
        rows[y] = 0;
        for (int x = 0; x < 8; x++)
            if (video_fb[row * 8 + y][col * 8 + x] != bg) rows[y] |= (uint8_t)(0x80 >> x);
    }
    for (int c = 0x20; c < 0x7F; c++)
        if (!memcmp(rows, &ibm[c * 8], 8)) return (char)c;
    for (int c = 0x20; c < 0x7F; c++) {   /* underlined: last row solid */
        if (!memcmp(rows, &ibm[c * 8], 7) && rows[7] == 0xFF) return (char)c;
    }
    return '?';
}

static void dump_screen(const char *title)
{
    printf("---- %s\n", title);
    for (int r = 0; r < TEXT_ROWS; r++) {
        char s[TEXT_COLS + 1];
        for (int c = 0; c < TEXT_COLS; c++) s[c] = cell_char(c, r);
        s[TEXT_COLS] = '\0';
        printf("%2d|%s|\n", r, s);
    }
}

static int fails;
static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

int main(int argc, char **argv)
{
    files_set_game_dir(argc > 1 ? argv[1] : ".");
    FILE *f = game_fopen("ibm.ch", "rb");
    if (!f || fread(ibm, 1, sizeof ibm, f) != sizeof ibm) { printf("cannot read IBM.CH\n"); return 1; }
    fclose(f);

    reset_text_windows();
    reset_text_attrs();
    check(load_font(0, "ibm.ch") == 1024, "load_font(0, ibm.ch)");
    check(load_font(1, "runes.ch") == 1024, "load_font(1, runes.ch)");
    check(load_font(4, "ibm.ch") == -2, "load_font rejects slot 4");
    select_font(0);
    select_text_window(0);
    dg.draw_color = 15;

    /* Full screen: centred title, then a number. */
    put_char(0xFF);
    put_char(0xFC);
    print_text("Ultima V\nWarriors of Destiny\n");
    put_char(0xFB);
    print_text("Gold:");
    print_number(' ', 6, 150);
    print_text(" Karma:");
    print_number('0', 4, -75);
    check(get_cursor_row() == 2 && get_cursor_col() == 22, "cursor after title and numbers");

    /* A 16 x 6 message window like the game's (lower right), word wrap and scrolling. */
    define_text_window(23, 39, 18, 24, 1);   /* (bottom, right, top, left, window) */
    select_text_window(1);
    set_text_bg_color(1);
    set_text_color(14);
    put_char(0xFF);
    print_text("Thou dost see a stately, white-haired man of many years.\n");
    print_text("A_very_long_unbreakable_word follows.\n");
    print_text("Your interest:\n");
    check(get_cursor_col() == 0 && get_cursor_row() == 5, "message window scrolled, cursor on last row");

    /* Attributes: inverse and underline toggles, in window 2. */
    define_text_window(10, 20, 6, 2, 2);
    select_text_window(2);
    put_char(0xFF);
    print_text("normal ");
    put_char(0xFD); print_text("inverse"); put_char(0xFD);
    put_char('\n');
    put_char(0xFE); print_text("underline"); put_char(0xFE);
    put_char('\n');
    print_text("12345");
    erase_chars(2);
    put_char(' ');
    check(get_cursor_col() == 4, "erase_chars moved back two cells");

    /* Runes. */
    define_text_window(13, 20, 12, 2, 3);
    select_text_window(3);
    select_font(1);
    print_text("LIVE BY THE EIGHT LAWS");
    select_font(0);

    dump_screen("screen (runes show as ?)");
    check(cell_char(16, 0) == 'U' && cell_char(10, 1) == 'W', "title centred");
    if (video_save_ppm("test_text.ppm") == 0) printf("wrote test_text.ppm\n");
    printf("%d failure(s)\n", fails);
    return fails != 0;
}
