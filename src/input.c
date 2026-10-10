/* input.c - keyboard and timing routines (main program 1b38, 1d5e, 1dda, 1e38, 20fa). */
#include "data.h"
#include "input.h"
#include "text.h"

int poll_key(void)
{
    int shifted = 0;
    dg.numpad_shift = 0;
    int key = platform_poll_key(&shifted);
    /* The original sets numpad_shift for the translated cursor keys and for Shift + 1-9.
     * The "Buffer On/Off" type-ahead setting (dg.typeahead_on, BIOS buffer flush) isn't modelled. */
    if ((key >= KEY_LEFT && key <= KEY_DOWN) || (key >= KEY_HOME && key <= KEY_PGDN)) dg.numpad_shift++;
    else if (key >= '1' && key <= '9' && shifted) dg.numpad_shift++;
    return key;
}

void delay_ticks(int ticks)
{
    platform_delay_ticks(ticks);
}

/* The cursor is a glyph drawn in place (auto-advance off): IBM.CH glyphs ds:5390 + frame, with the
 * frame wrapping at ds:5392 (glyphs 5-8). A key blanks it again. */
int wait_key_cursor(void)
{
    uint16_t saved = dg.text_auto_advance;
    dg.text_auto_advance = 0;
    uint8_t frame = (uint8_t)dg.cursor_frame++;
    put_char((uint8_t)(ds_u16(0x5390) + frame));
    int key = poll_key();
    if ((int16_t)dg.cursor_frame >= (int16_t)ds_u16(0x5392)) dg.cursor_frame = 0;
    if (key == 0) delay_ticks(1);
    else put_char(' ');
    dg.text_auto_advance = saved;
    return key;
}

int wait_key_timeout(int tries)
{
    int key, count = tries != 0 ? 0 : -1;
    uint16_t saved = dg.cpu_speed;
    if (tries > 1) dg.cpu_speed = 500;   /* fixed blink speed for timed waits */
    do {
        key = wait_key_cursor();
        if (tries != 0) count++;
    } while (count < tries && key == 0);
    if (tries > 1) dg.cpu_speed = saved;
    return key;
}

void read_name(int max_len, char *buf)
{
    int key, count = 0;
    do {
        key = wait_key_timeout(0);
        if ((key == 8 || key == KEY_LEFT) && count != 0) {
            erase_chars(1);
            count--;
        } else if (key == 0x1B && count != 0) {
            erase_chars(count);
            count = 0;
        } else if (key > 0x1F && key < 0x80 && count < max_len) {
            buf[count++] = (char)key;
            put_char((uint8_t)key);
        }
    } while (key != 0x0D);
    buf[count] = '\0';
}

void flush_keyboard(void) { platform_flush_keys(); }
