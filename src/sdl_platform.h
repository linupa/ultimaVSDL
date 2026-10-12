/* sdl_platform.h - Platform on SDL2: a window showing video_fb, keyboard to game key codes. */
#ifndef U5_SDL_PLATFORM_H
#define U5_SDL_PLATFORM_H

#include <SDL.h>
#include "platform.h"

class SdlPlatform : public Platform {
public:
    /* Opens the window (and the audio device, if there is one). Returns -1 on failure. */
    int  init(const char *title, int scale, int aspect_4_3);
    void shutdown();

    int  poll_key(int *shifted) override;
    int  key_waiting() override;
    void delay_ticks(int n) override;
    void flush_keys() override;
    void present() override;
    void delay_ms(int ms) override;
    void audio_queue(const short *pcm, int count) override;
    void audio_wait() override;
    int  quit_requested() override { return quit_; }

private:
    enum { QUEUE_SIZE = 64 };

    void push_key(int key, int shifted);
    void key_down(const SDL_KeyboardEvent *k);
    void pump_events();

    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *texture_ = nullptr;
    SDL_AudioDeviceID audio_ = 0;
    int tex_w_ = 0;
    int quit_ = 0;
    Uint32 last_present_ = 0;
    struct { int key, shifted; } queue_[QUEUE_SIZE] = {};
    int q_head_ = 0, q_tail_ = 0;
    int skip_text_ = 0;            /* drop the SDL_TEXTINPUT that follows a key already handled by SDL_KEYDOWN */
    Uint32 silent_ms_x1000_ = 0;   /* with no audio device: sample time still to be waited out */
};

extern SdlPlatform sdl_platform;

#endif
