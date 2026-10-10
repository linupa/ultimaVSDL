/* platform_script.c - platform.h without a window, for tests: keys come from a script set with
 * platform_script_keys(). When the script runs out the platform reports quit, so input loops end. */
#include "platform.h"

static const int *script;
static int script_len, script_pos, quit;
static long ticks;

void platform_script_keys(const int *keys, int n)
{
    script = keys;
    script_len = n;
    script_pos = 0;
    quit = 0;
}

long platform_script_ticks(void) { return ticks; }

/* Test hook: if set, keys come from this function instead of the script (-1 = no more keys). */
int (*platform_script_next_key)(void);

int platform_poll_key(int *shifted)
{
    if (shifted) *shifted = 0;
    if (quit) return 0x0D;
    if (platform_script_next_key) {
        int k = platform_script_next_key();
        if (k >= 0) return k;
        quit = 1;
        return 0x0D;
    }
    if (script_pos >= script_len) {
        quit = 1;
        return 0x0D;
    }
    return script[script_pos++];
}

/* The next scripted key, if it is one (script entries of 0 are polls that find nothing). */
int platform_key_waiting(void)
{
    if (quit) return 1;
    if (platform_script_next_key || script_pos >= script_len) return 0;
    return script[script_pos] != 0;
}

/* Test hook: called after every tick with the running tick count (e.g. to save screenshots). */
void (*platform_script_on_tick)(long ticks);

void platform_delay_ticks(int n)
{
    for (; n > 0; n--) {
        ticks++;
        if (platform_script_on_tick) platform_script_on_tick(ticks);
    }
}
void platform_delay_ms(int ms) { (void)ms; }
void platform_audio_queue(const short *pcm, int count) { (void)pcm; (void)count; }
void platform_audio_wait(void) {}
void platform_present(void) {}
void platform_flush_keys(void) {}
int platform_quit_requested(void) { return quit; }
