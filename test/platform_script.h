/* platform_script.h - Platform without a window, for tests: keys come from a script set with
 * keys(). When the script runs out the platform reports quit, so input loops end. */
#ifndef U5_PLATFORM_SCRIPT_H
#define U5_PLATFORM_SCRIPT_H

#include "platform.h"

class ScriptPlatform : public Platform {
public:
    void keys(const int *keys, int n);
    long ticks() const { return ticks_; }

    /* Test hook: if set, keys come from this function instead of the script (-1 = no more keys). */
    int (*next_key)() = nullptr;
    /* Test hook: called after every tick with the running tick count (e.g. to save screenshots). */
    void (*on_tick)(long ticks) = nullptr;

    int  poll_key(int *shifted) override;
    int  key_waiting() override;
    void delay_ticks(int n) override;
    void flush_keys() override {}
    void present() override {}
    void delay_ms(int) override {}
    void audio_queue(const short *, int) override {}
    void audio_wait() override {}
    int  quit_requested() override { return quit_; }

private:
    const int *script_ = nullptr;
    int script_len_ = 0, script_pos_ = 0, quit_ = 0;
    long ticks_ = 0;
};

extern ScriptPlatform script_platform;

#endif
