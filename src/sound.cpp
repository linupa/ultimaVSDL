/* sound.cpp - the PC speaker routines. See sound.h. */
#include <stdint.h>
#include "gamedata.h"
#include "gfx.h"
#include "map.h"
#include "misc.h"
#include "platform.h"
#include "savegame.h"
#include "sound.h"
#include "view.h"

#define RATE 44100.0
#define LEVEL 5000
#define SWEEP_RATE 16492.0   /* iterations a second with repeat 1: note 3497 = 880 Hz */

Sound::Sound()
{
    SaveGame::instance().add(0x5884, ambient_cooldown, "sound.ambient_cooldown");   /* not `savegame`: this runs before main */

    Data &data = Data::instance();   /* not a global: this runs before main, in no set order */
    data.add(0x5426, half_delay_, "sound.half_delay");
    data.add(0x6A34, tune_pitch_, "sound.tune_pitch");
    data.add(0x6A48, tune_, "sound.tune");
}

Sound &Sound::instance()
{
    static Sound the_sound;
    return the_sound;
}

Sound &sound = Sound::instance();

uint8_t Sound::half_delay_[8];   /* ds:5426 */
uint16_t Sound::tune_pitch_[10];   /* ds:6A34 */
uint8_t Sound::tune_[0x36];   /* ds:6A48 */

void Sound::flush(void)
{
    if (pcm_n_) platform->audio_queue(pcm_, pcm_n_);
    pcm_n_ = 0;
}

void Sound::put(int level)
{
    pcm_[pcm_n_++] = (int16_t)level;
    if (pcm_n_ == (int)(sizeof pcm_ / sizeof pcm_[0])) flush();
}

/* seconds of a square wave at freq (0 = silence), carrying the phase over. */
void Sound::tone(double freq, double seconds)
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
        phase_ += freq / RATE;
        if (phase_ >= 1.0) phase_ -= (int)phase_;
        put(phase_ < 0.5 ? LEVEL : -LEVEL);
    }
}

void Sound::play(void)
{
    flush();
    platform->audio_wait();
}

/* Timer 2 takes the divisor 0x1234DE / freq, so the pitch is slightly quantised. */
void Sound::speaker_on(unsigned freq)
{
    if (enabled == 0 || freq == 0) return;
    unsigned divisor = (0x1234DEu / freq) & 0xFFFF;
    speaker_freq_ = divisor ? 0x1234DEu / divisor : 0;
}

void Sound::speaker_off(void) { speaker_freq_ = 0; }

/* The busy-wait: count x cpu_speed loops (cpu_speed halved for speed indexes 2 and 3). Whatever
 * the speaker is doing goes on meanwhile. */
void Sound::delay_loop(int count, int speed_index)
{
    double ms = (uint16_t)count;
    if ((half_delay_[speed_index & 7] | half_delay_[(speed_index + 1) & 7] << 8) != 0) ms /= 2;
    if (speaker_freq_) {
        tone(speaker_freq_, ms / 1000.0);
        play();
    } else {
        platform->delay_ms((int)(ms + 0.5));
    }
}

void Sound::sound_crackle(int microseconds)
{
    tone(speaker_freq_, microseconds / 1000000.0);
}

void Sound::sound_queue_tone(unsigned freq, double seconds) { tone(freq, seconds); }
void Sound::sound_queue_flush(void) { flush(); }

void Sound::beep(int duration, int freq)
{
    if (enabled != 0) speaker_on((unsigned)freq);
    delay_loop(duration, 1);
    speaker_off();
}

void Sound::sound_slide(int length, int step, int freq_a, unsigned freq_b)
{
    int range = freq_a - (int)freq_b;
    if (step <= 0 || length <= 0) return;
    for (int i = 0; i < length; i += step) {
        speaker_on(freq_b);
        if (speaker_freq_) tone(speaker_freq_, step / 1000.0);
        else tone(0, step / 1000.0);
        freq_b = (unsigned)((int)freq_b + (range * step) / length);
    }
    speaker_off();
    play();
}

/* A new random pitch between 100 Hz and pitch_range every `step` sixteenths of a millisecond,
 * until `duration` of them have passed. The generator (noise_rand) is the original's. */
void Sound::sound_noise(int pitch_range, int duration, int step)
{
    int total = 0;
    unsigned range = (unsigned)(pitch_range - 99) & 0xFFFF;
    if (range == 0) range = 1;
    do {
        noise_rand = (uint16_t)((((uint16_t)(noise_rand + 0x9248) >> 3 | noise_rand << 13) ^ 0x9248) + 0x11);
        unsigned freq = noise_rand % range + 100;
        tone(enabled ? freq : 0, (uint16_t)step / 16000.0);
        total += step;
    } while (total < duration && step > 0);
    play();
}

/* `count` iterations; in each a 16-bit accumulator advances by `increment` and the speaker is on
 * while it is above a threshold that starts at `from` and moves by `step`. So the pitch is
 * increment / 65536 of the iteration rate and the threshold shapes the tone: a narrowing or
 * widening pulse, silent once the threshold wraps past the accumulator's range. `repeat`
 * lengthens each iteration's delay. */
void Sound::sound_sweep(int step, unsigned from, int count, int repeat, unsigned increment)
{
    uint16_t acc = 0, threshold = (uint16_t)from, n = (uint16_t)count;
    double period = (120.0 + 172.0 * (repeat > 0 ? repeat : 1)) / 292.0 / SWEEP_RATE;
    double t = 0, next = 0;   /* time of the current iteration's end, of the next sample */
    do {
        acc = (uint16_t)(acc + increment);
        int on = enabled != 0 && acc > threshold;
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

void Sound::step_sound(void)
{
    sound_noise(1000, 0x19, 1);
    delay_loop(0x14, 1);
    sound_noise(0x5DC, 0x19, 1);
}

/* Once per view update: the nearest sounding thing within about 7 squares - a clock (ticks, and
 * chimes the hour on the hour), a waterfall, a fountain, or a bard (tile 0x5C-0x5F), who plays the
 * 53-note tune at ds:6A48 with the note table at ds:6A34. The eight-step cycle (ambient_state)
 * times the clock's tick and tock. */
void Sound::ambient_sounds(void)
{
    int kind = 0, best = 0x33, px = 5, py = 5;
    if (!map.in_scene()) {
        px = map.x;
        py = map.y;
    }
    for (int col = 0; col < 11; col++)
        for (int row = 0; row < 11; row++) {
            int x = px - 5 + col, y = py - 5 + row;
            int d = (y - py) * (y - py) + (x - px) * (x - px);
            if (d >= best) continue;
            uint8_t t = *map.get_map_tile_ptr((unsigned)y, (unsigned)x);
            if ((t & 0xFE) == 0xFA) kind = 1;
            else if ((t & 0xFC) == 0xD4) kind = 2;
            else if ((t & 0xFC) == 0xD8) kind = 3;
            else if (view.mask[row * 0x20 + col] == 0 && (view.tiles[row * 0x10 + col] & 0xFC) == 0x5C) kind = 4;
            else continue;
            best = d;
        }
    if (kind == 1) {
        if (ambient_cooldown == 0 || (ambient_state != 0 && ambient_state != 4)) {
            if (ambient_state == 0) beep(3, 3000);
            else if (ambient_state == 4) beep(3, 2000);
        } else {
            sound_sweep(-10, 20000, 2000, 1, 0xC2C);   /* a chime */
        }
    } else if (kind == 2) {
        sound_noise(0x2710, 0x3C, 0x14);
    } else if (kind == 3) {
        sound_noise(0x61A8, 0x1E, 10);
    } else if (kind == 4) {
        uint8_t note = tune_[tune_pos < sizeof tune_ ? tune_pos : 0];
        if (note != 0) sound_sweep(-10, 20000, 2000, 1, tune_pitch_[note < 10 ? note : 0]);
        if (++tune_pos > 0x34) tune_pos = 0;
    }
    if (ambient_cooldown != 0 && (ambient_state == 0 || ambient_state == 4)) ambient_cooldown--;
    if (++ambient_state > 7) ambient_state = 0;
}
