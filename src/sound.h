/* sound.h - the PC speaker: the main program's sound routines (0000:20c8-230e, 433e-43ae) and
 * ambient_sounds (0000:41c2...). Parameter order follows the Ghidra signatures.
 *
 * The original makes sound three ways: timer 2 driving the speaker at a frequency (speaker_on,
 * with busy-wait delays for the length), rapid random frequencies (sound_noise), and switching
 * the speaker on and off directly in a counted loop with a phase accumulator (sound_sweep, used
 * for everything musical). Its delays are busy loops scaled by a start-up calibration
 * (cpu_speed), so their real length is only approximately fixed: one delay_loop unit is about a
 * millisecond, one sound_noise step unit 1/16 ms, and one sound_sweep iteration about 61
 * microseconds - the rate that puts the harpsichord's notes (ds:2746) at concert pitch.
 * The port renders the same sequences as 44.1 kHz samples and blocks while they play, as the
 * original blocks while it makes them. With sound switched off (Ctrl-S) the waits remain. */
#ifndef U5_SOUND_H
#define U5_SOUND_H

void delay_loop(int count, int speed_index);            /* 20c8: about count ms (half for index 2, 3) */
void sound_sweep(int step, unsigned from, int count, int repeat, unsigned increment);   /* 2192 */
void sound_noise(int pitch_range, int duration, int step);   /* 223c */
void beep(int duration, int freq);                      /* 22c0 */
void speaker_on(unsigned freq);                         /* 22e2 */
void speaker_off(void);                                 /* 230e */
void step_sound(void);                                  /* 433e */
void sound_slide(int length, int step, int freq_a, unsigned freq_b);   /* 43ae: from freq_b to freq_a over length ms */
void ambient_sounds(void);                              /* 41c2...: clocks, waterfalls, fountains, a bard's tune */

void sound_crackle(int microseconds);   /* port: a moment of the current speaker tone, queued without waiting, for
                                         * loops the original paces by CPU time; the next delay_loop plays it */

/* port: for the graphics drivers' own noises (picture.c): a square wave (0 = silence) added to
 * the queue, and the queue handed to the platform without waiting for it to play. */
void sound_queue_tone(unsigned freq, double seconds);
void sound_queue_flush(void);

#endif
