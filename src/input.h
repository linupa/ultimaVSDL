/* input.h - keyboard and timing routines of the main program, on top of platform.h. */
#ifndef U5_INPUT_H
#define U5_INPUT_H

#include "platform.h"

int  poll_key(void);                       /* 1d5e: key code or 0; sets dg.numpad_shift */
int  wait_key_cursor(void);                /* 1b38: one blink of the cursor, then poll */
int  wait_key_timeout(int tries);          /* 1dda: up to tries blinks, 0 = forever */
void read_name(int max_len, char *buf);    /* 1e38: line input with backspace / Esc */
void delay_ticks(int ticks);               /* 20fa */
void flush_keyboard(void);                 /* 1b16 */

#endif
