/* platform_script.cpp - ScriptPlatform: see platform_script.h. */
#include "platform_script.h"

ScriptPlatform script_platform;
Platform *platform = &script_platform;

void ScriptPlatform::keys(const int *keys, int n)
{
    script_ = keys;
    script_len_ = n;
    script_pos_ = 0;
    quit_ = 0;
}

int ScriptPlatform::poll_key(int *shifted)
{
    if (shifted) *shifted = 0;
    if (quit_) return 0x0D;
    if (next_key) {
        int k = next_key();
        if (k >= 0) return k;
        quit_ = 1;
        return 0x0D;
    }
    if (script_pos_ >= script_len_) {
        quit_ = 1;
        return 0x0D;
    }
    return script_[script_pos_++];
}

/* The next scripted key, if it is one (script entries of 0 are polls that find nothing). */
int ScriptPlatform::key_waiting()
{
    if (quit_) return 1;
    if (next_key || script_pos_ >= script_len_) return 0;
    return script_[script_pos_] != 0;
}

void ScriptPlatform::delay_ticks(int n)
{
    for (; n > 0; n--) {
        ticks_++;
        if (on_tick) on_tick(ticks_);
    }
}
