/* platform.h - what the ported game needs from the host (SDL, or a test stub): the
 * Platform interface.
 *
 * The original code is written as blocking loops: it polls the keyboard and busy-waits on the timer.
 * The port keeps that structure. The platform shows the frame and handles window events whenever the
 * game polls a key or waits, so ported loops run unchanged. */
#ifndef U5_PLATFORM_H
#define U5_PLATFORM_H

/* Key codes as the game sees them (poll_key, 0000:1d5e): ASCII for ordinary keys (8 backspace,
 * 0x0D return, 0x1B escape), plus these for extended keys (table ds:540E -> ds:5416, F-keys + 0x8E). */
enum {
    KEY_LEFT = 0x01, KEY_RIGHT = 0x02, KEY_UP = 0x03, KEY_DOWN = 0x04,
    KEY_F1 = 0xC9,   /* F1-F10 = 0xC9-0xD2 */
    KEY_HOME = 0xD3, KEY_END = 0xD4, KEY_PGUP = 0xD5, KEY_PGDN = 0xD6,
};

class Platform {
public:
    virtual ~Platform() = default;

    /* Next key from the queue, or 0. *shifted is set when Shift was held (the game uses it for
     * keypad digits). Also handles window events and shows the frame if it changed. */
    virtual int  poll_key(int *shifted) = 0;
    /* Non-zero if a key is waiting; it stays in the queue (BIOS int 16h, AH = 1). */
    virtual int  key_waiting() = 0;

    /* Wait n timer ticks (the PC timer, 18.2 per second), showing frames meanwhile. */
    virtual void delay_ticks(int n) = 0;

    /* Discard keys typed ahead (flush_keyboard / flush_bios_keys). A scripted platform ignores it. */
    virtual void flush_keys() = 0;

    /* Show the frame (video_fb) now. */
    virtual void present() = 0;

    /* Wait about ms milliseconds (the game's short busy-wait delays). */
    virtual void delay_ms(int ms) = 0;
    /* Sound: queue 44.1 kHz mono samples; wait until everything queued has been played. */
    virtual void audio_queue(const short *pcm, int count) = 0;
    virtual void audio_wait() = 0;
    /* Non-zero once the user closed the window. poll_key then returns 0x0D so input loops end. */
    virtual int  quit_requested() = 0;
};

/* The host the game is running on. Each program defines it along with its Platform. */
extern Platform *platform;

#endif
