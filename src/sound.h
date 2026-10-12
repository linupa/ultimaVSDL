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

#include <stdint.h>

/* The speaker and what it is doing. `sound` is the one Sound object.
 *
 * The functions below the class (sound_sweep, sound_slide, delay_loop, ...) are what the game
 * calls: each just passes on to the method of the same name. */
class Sound {
public:
    static Sound &instance();

    uint8_t enabled = 0;             /* Ctrl-S, "Sound on / off"; the intro switches it on */
    uint16_t noise_rand = 0x7664;    /* sound_noise's random numbers */
    /* The sounds of a place (ambient_sounds): which of its eight turns this is, how many strokes
     * a clock still has to strike, and where a bard's tune has got to. */
    uint8_t ambient_state = 0;
    uint8_t tune_pos = 0;
    uint8_t ambient_cooldown = 0;    /* ds:5884: part of the saved game, registered with SaveGame (savegame.h) */

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

    /* port: for the graphics drivers' own noises (picture.cpp): a square wave (0 = silence) added to
     * the queue, and the queue handed to the platform without waiting for it to play. */
    void sound_queue_tone(unsigned freq, double seconds);
    void sound_queue_flush(void);

    Sound(const Sound &) = delete;
    Sound &operator=(const Sound &) = delete;

private:
    Sound();

    void flush(void);
    void put(int level);
    void tone(double freq, double seconds);
    void play(void);

    int16_t pcm_[4096] = {};         /* samples not yet handed to the platform */
    int pcm_n_ = 0;
    double phase_ = 0;               /* of the timer-driven square wave, 0-1 */
    unsigned speaker_freq_ = 0;      /* 0 = off */

    static uint8_t half_delay_[8];     /* ds:5426: which delay_loop speeds run at half length */
    static uint16_t tune_pitch_[10];   /* ds:6A34: the pitches of a bard's tune, 1-9 */
    static uint8_t tune_[0x36];        /* ds:6A48: the tune, a note a turn; 0 = a rest */
};

extern Sound &sound;

inline void delay_loop(int count, int speed_index) { sound.delay_loop(count, speed_index); }
inline void sound_sweep(int step, unsigned from, int count, int repeat, unsigned increment) { sound.sound_sweep(step, from, count, repeat, increment); }
inline void sound_noise(int pitch_range, int duration, int step) { sound.sound_noise(pitch_range, duration, step); }
inline void beep(int duration, int freq) { sound.beep(duration, freq); }
inline void speaker_on(unsigned freq) { sound.speaker_on(freq); }
inline void speaker_off(void) { sound.speaker_off(); }
inline void step_sound(void) { sound.step_sound(); }
inline void sound_slide(int length, int step, int freq_a, unsigned freq_b) { sound.sound_slide(length, step, freq_a, freq_b); }
inline void ambient_sounds(void) { sound.ambient_sounds(); }
inline void sound_crackle(int microseconds) { sound.sound_crackle(microseconds); }
inline void sound_queue_tone(unsigned freq, double seconds) { sound.sound_queue_tone(freq, seconds); }
inline void sound_queue_flush(void) { sound.sound_queue_flush(); }

#endif
