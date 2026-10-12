/* input.h - keyboard and timing routines of the main program, on top of platform.h, as the
 * Input class. `input` is the one Input object.
 *
 * The functions below the class (poll_key, wait_key_cursor, ...) are what the game calls; they
 * forward to the object. */
#ifndef U5_INPUT_H
#define U5_INPUT_H

#include <stdint.h>
#include "platform.h"

class Input {
public:
    static Input &instance();

    uint16_t numpad_shift = 0;       /* ds:538A  poll_key: a cursor key, or Shift with a keypad digit */
    uint16_t typeahead_on = 0;       /* ds:538C  the "Buffer On/Off" toggle (Ctrl key 0xFC); not modelled */
    uint16_t cursor_frame = 0;       /* ds:540C  wait_key_cursor's animation frame */
    uint16_t cpu_speed = 0;          /* ds:5356  the original's calibrated delay scale; wait_key_timeout
                                      * still swaps it, but the port's delays don't use it */

    int  poll_key(void);                       /* 1d5e: key code or 0; sets numpad_shift */
    int  wait_key_cursor(void);                /* 1b38: one blink of the cursor, then poll */
    int  wait_key_timeout(int tries);          /* 1dda: up to tries blinks, 0 = forever */
    void read_name(int max_len, char *buf);    /* 1e38: line input with backspace / Esc */
    void delay_ticks(int ticks);               /* 20fa */
    void flush_keyboard(void);                 /* 1b16 */

    Input(const Input &) = delete;
    Input &operator=(const Input &) = delete;

private:
    Input() = default;

    /* The cursor's IBM.CH glyphs: first_ + frame, frame wrapping at frames_ (ds:5390, ds:5392). */
    static const uint16_t cursor_first_ = 5, cursor_frames_ = 4;
};

/* The one Input object (Input::instance()). */
extern Input &input;

inline int  poll_key(void) { return input.poll_key(); }
inline int  wait_key_cursor(void) { return input.wait_key_cursor(); }
inline int  wait_key_timeout(int tries) { return input.wait_key_timeout(tries); }
inline void read_name(int max_len, char *buf) { input.read_name(max_len, buf); }
inline void delay_ticks(int ticks) { input.delay_ticks(ticks); }
inline void flush_keyboard(void) { input.flush_keyboard(); }

#endif
