/* sound.c - the PC speaker routines. See sound.h. */
#include <stdint.h>
#include "data.h"
#include "gfx.h"
#include "map.h"
#include "misc.h"
#include "platform.h"
#include "sound.h"
#include "view.h"

#define RATE 44100.0
#define LEVEL 5000
#define SWEEP_RATE 16492.0   /* iterations a second with repeat 1: note 3497 = 880 Hz */

static int16_t pcm[4096];
static int pcm_n;
static double phase;          /* of the timer-driven square wave, 0-1 */
static unsigned speaker_freq; /* 0 = off */

static void flush(void)
{
    if (pcm_n) platform_audio_queue(pcm, pcm_n);
    pcm_n = 0;
}

static void put(int level)
{
    pcm[pcm_n++] = (int16_t)level;
    if (pcm_n == (int)(sizeof pcm / sizeof pcm[0])) flush();
}

/* seconds of a square wave at freq (0 = silence), carrying the phase over. */
static void tone(double freq, double seconds)
{
    static double carry;   /* fraction of a sample left over, so short pieces add up exactly */
    double n = seconds * RATE + carry;
    int count = (int)n;
    carry = n - count;
    for (int i = 0; i < count; i++) {
        if (freq <= 0) {
            put(0);
            continue;
        }
        phase += freq / RATE;
        if (phase >= 1.0) phase -= (int)phase;
        put(phase < 0.5 ? LEVEL : -LEVEL);
    }
}

static void play(void)
{
    flush();
    platform_audio_wait();
}

/* Timer 2 takes the divisor 0x1234DE / freq, so the pitch is slightly quantised. */
void speaker_on(unsigned freq)
{
    if (dg.sound_enabled == 0 || freq == 0) return;
    unsigned divisor = (0x1234DEu / freq) & 0xFFFF;
    speaker_freq = divisor ? 0x1234DEu / divisor : 0;
}

void speaker_off(void) { speaker_freq = 0; }

/* The busy-wait: count x cpu_speed loops (cpu_speed halved for speed indexes 2 and 3). Whatever
 * the speaker is doing goes on meanwhile. */
void delay_loop(int count, int speed_index)
{
    double ms = (uint16_t)count;
    if (ds_u16((uint16_t)(0x5426 + speed_index)) != 0) ms /= 2;
    if (speaker_freq) {
        tone(speaker_freq, ms / 1000.0);
        play();
    } else {
        platform_delay_ms((int)(ms + 0.5));
    }
}

void sound_crackle(int microseconds)
{
    tone(speaker_freq, microseconds / 1000000.0);
}

void sound_queue_tone(unsigned freq, double seconds) { tone(freq, seconds); }
void sound_queue_flush(void) { flush(); }

void beep(int duration, int freq)
{
    if (dg.sound_enabled != 0) speaker_on((unsigned)freq);
    delay_loop(duration, 1);
    speaker_off();
}

void sound_slide(int length, int step, int freq_a, unsigned freq_b)
{
    int range = freq_a - (int)freq_b;
    if (step <= 0 || length <= 0) return;
    for (int i = 0; i < length; i += step) {
        speaker_on(freq_b);
        if (speaker_freq) tone(speaker_freq, step / 1000.0);
        else tone(0, step / 1000.0);
        freq_b = (unsigned)((int)freq_b + (range * step) / length);
    }
    speaker_off();
    play();
}

/* A new random pitch between 100 Hz and pitch_range every `step` sixteenths of a millisecond,
 * until `duration` of them have passed. The generator (noise_rand) is the original's. */
void sound_noise(int pitch_range, int duration, int step)
{
    int total = 0;
    unsigned range = (unsigned)(pitch_range - 99) & 0xFFFF;
    if (range == 0) range = 1;
    do {
        dg.noise_rand = (uint16_t)((((uint16_t)(dg.noise_rand + 0x9248) >> 3 | dg.noise_rand << 13) ^ 0x9248) + 0x11);
        unsigned freq = dg.noise_rand % range + 100;
        tone(dg.sound_enabled ? freq : 0, (uint16_t)step / 16000.0);
        total += step;
    } while (total < duration && step > 0);
    play();
}

/* `count` iterations; in each a 16-bit accumulator advances by `increment` and the speaker is on
 * while it is above a threshold that starts at `from` and moves by `step`. So the pitch is
 * increment / 65536 of the iteration rate and the threshold shapes the tone: a narrowing or
 * widening pulse, silent once the threshold wraps past the accumulator's range. `repeat`
 * lengthens each iteration's delay. */
void sound_sweep(int step, unsigned from, int count, int repeat, unsigned increment)
{
    uint16_t acc = 0, threshold = (uint16_t)from, n = (uint16_t)count;
    double period = (120.0 + 172.0 * (repeat > 0 ? repeat : 1)) / 292.0 / SWEEP_RATE;
    double t = 0, next = 0;   /* time of the current iteration's end, of the next sample */
    do {
        acc = (uint16_t)(acc + increment);
        int on = dg.sound_enabled != 0 && acc > threshold;
        threshold = (uint16_t)(threshold + step);
        t += period;
        while (next < t) {
            put(on ? LEVEL : -LEVEL / 4);
            next += 1.0 / RATE;
        }
    } while (--n != 0);
    put(0);
    play();
}

void step_sound(void)
{
    sound_noise(1000, 0x19, 1);
    delay_loop(0x14, 1);
    sound_noise(0x5DC, 0x19, 1);
}

/* Once per view update: the nearest sounding thing within about 7 squares - a clock (ticks, and
 * chimes the hour on the hour), a waterfall, a fountain, or a bard (tile 0x5C-0x5F), who plays the
 * 53-note tune at ds:6A48 with the note table at ds:6A34. The eight-step cycle (ambient_state)
 * times the clock's tick and tock. */
void ambient_sounds(void)
{
    int kind = 0, best = 0x33, px = 5, py = 5;
    if (dg.location < 0x80) {
        px = dg.party_x;
        py = dg.party_y;
    }
    for (int col = 0; col < 11; col++)
        for (int row = 0; row < 11; row++) {
            int x = px - 5 + col, y = py - 5 + row;
            int d = (y - py) * (y - py) + (x - px) * (x - px);
            if (d >= best) continue;
            uint8_t t = *get_map_tile_ptr((unsigned)y, (unsigned)x);
            if ((t & 0xFE) == 0xFA) kind = 1;
            else if ((t & 0xFC) == 0xD4) kind = 2;
            else if ((t & 0xFC) == 0xD8) kind = 3;
            else if (dg.view_mask[row * 0x20 + col] == 0 && (dg.view_tiles[row * 0x10 + col] & 0xFC) == 0x5C) kind = 4;
            else continue;
            best = d;
        }
    if (kind == 1) {
        if (dg.ambient_cooldown == 0 || (dg.ambient_state != 0 && dg.ambient_state != 4)) {
            if (dg.ambient_state == 0) beep(3, 3000);
            else if (dg.ambient_state == 4) beep(3, 2000);
        } else {
            sound_sweep(-10, 20000, 2000, 1, 0xC2C);   /* a chime */
        }
    } else if (kind == 2) {
        sound_noise(0x2710, 0x3C, 0x14);
    } else if (kind == 3) {
        sound_noise(0x61A8, 0x1E, 10);
    } else if (kind == 4) {
        uint8_t note = ds_u8((uint16_t)(0x6A48 + dg.tune_pos));
        if (note != 0) sound_sweep(-10, 20000, 2000, 1, ds_u16((uint16_t)(0x6A34 + note * 2)));
        if (++dg.tune_pos > 0x34) dg.tune_pos = 0;
    }
    if (dg.ambient_cooldown != 0 && (dg.ambient_state == 0 || dg.ambient_state == 4)) dg.ambient_cooldown--;
    if (++dg.ambient_state > 7) dg.ambient_state = 0;
}
