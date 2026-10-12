/* sdl_platform.cpp - SdlPlatform: see sdl_platform.h. */
#include "sdl_platform.h"
#include "video.h"

SdlPlatform sdl_platform;
Platform *platform = &sdl_platform;

void SdlPlatform::push_key(int key, int shifted)
{
    int next = (q_tail_ + 1) % QUEUE_SIZE;
    if (next == q_head_) return;   /* full: drop, like a full BIOS buffer */
    queue_[q_tail_].key = key;
    queue_[q_tail_].shifted = shifted;
    q_tail_ = next;
}

int SdlPlatform::init(const char *title, int scale, int aspect_4_3)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0 && SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return -1;
    }
    /* The PC showed 320x200 on a 4:3 monitor; the Hercules picture (640x300 of the card's
     * 720x348) was about as wide for its height as 640x465. */
    int w, h, lines;
    video_out_size(&w, &lines);
    h = !aspect_4_3 ? lines : lines == SCREEN_H ? 240 : 465;
    if (w > SCREEN_W) scale = (scale + 1) / 2;
    tex_w_ = w;
    window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              w * scale, h * scale, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        return -1;
    }
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer_) {
        SDL_Log("SDL_CreateRenderer: %s", SDL_GetError());
        return -1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, aspect_4_3 ? "1" : "0");
    SDL_RenderSetLogicalSize(renderer_, w, h);
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, w, lines);
    if (!texture_) {
        SDL_Log("SDL_CreateTexture: %s", SDL_GetError());
        return -1;
    }
    SDL_AudioSpec want = {};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    audio_ = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);   /* 0 if there is no sound: the game runs silent */
    if (audio_) SDL_PauseAudioDevice(audio_, 0);
    SDL_StartTextInput();
    return 0;
}

void SdlPlatform::shutdown()
{
    if (audio_) SDL_CloseAudioDevice(audio_);
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void SdlPlatform::present()
{
    static Uint8 rgb[SCREEN_W * 2 * VIDEO_ROWS * 3];
    video_to_rgb(rgb);
    SDL_UpdateTexture(texture_, NULL, rgb, tex_w_ * 3);
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, NULL, NULL);
    SDL_RenderPresent(renderer_);
    last_present_ = SDL_GetTicks();
}

void SdlPlatform::key_down(const SDL_KeyboardEvent *k)
{
    SDL_Keycode sym = k->keysym.sym;
    int shift = (k->keysym.mod & KMOD_SHIFT) != 0, ctrl = (k->keysym.mod & KMOD_CTRL) != 0;
    switch (sym) {
    case SDLK_BACKSPACE: push_key(8, shift); return;
    case SDLK_RETURN: case SDLK_KP_ENTER: push_key(0x0D, shift); return;
    case SDLK_ESCAPE: push_key(0x1B, shift); return;
    case SDLK_TAB: push_key(9, shift); return;
    case SDLK_LEFT: push_key(KEY_LEFT, shift); return;
    case SDLK_RIGHT: push_key(KEY_RIGHT, shift); return;
    case SDLK_UP: push_key(KEY_UP, shift); return;
    case SDLK_DOWN: push_key(KEY_DOWN, shift); return;
    case SDLK_HOME: push_key(KEY_HOME, shift); return;
    case SDLK_END: push_key(KEY_END, shift); return;
    case SDLK_PAGEUP: push_key(KEY_PGUP, shift); return;
    case SDLK_PAGEDOWN: push_key(KEY_PGDN, shift); return;
    default: break;
    }
    if (sym >= SDLK_F1 && sym <= SDLK_F10) {
        push_key(KEY_F1 + (int)(sym - SDLK_F1), shift);
        return;
    }
    if (sym >= SDLK_KP_1 && sym <= SDLK_KP_9) {   /* keypad digits, with Shift reported */
        push_key('1' + (int)(sym - SDLK_KP_1), shift);
        skip_text_ = 1;
        return;
    }
    if (ctrl && sym >= SDLK_a && sym <= SDLK_z) {   /* Ctrl+letter = 1-26, as from the BIOS */
        push_key(1 + (int)(sym - SDLK_a), shift);
        return;
    }
    skip_text_ = 0;
}

void SdlPlatform::pump_events()
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            quit_ = 1;
            break;
        case SDL_KEYDOWN:
            key_down(&e.key);
            break;
        case SDL_TEXTINPUT:
            if (skip_text_) { skip_text_ = 0; break; }
            for (const char *p = e.text.text; *p; p++)
                if ((unsigned char)*p >= 0x20 && (unsigned char)*p < 0x7F) push_key(*p, 0);
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_EXPOSED) present();
            break;
        }
    }
}

int SdlPlatform::poll_key(int *shifted)
{
    pump_events();
    if (SDL_GetTicks() - last_present_ >= 16) present();
    if (quit_) return 0x0D;
    if (q_head_ == q_tail_) return 0;
    int key = queue_[q_head_].key;
    if (shifted) *shifted = queue_[q_head_].shifted;
    q_head_ = (q_head_ + 1) % QUEUE_SIZE;
    return key;
}

int SdlPlatform::key_waiting()
{
    pump_events();
    return quit_ || q_head_ != q_tail_;
}

void SdlPlatform::delay_ticks(int n)
{
    Uint32 end = SDL_GetTicks() + (Uint32)n * 55;   /* 18.2 ticks per second */
    present();
    while (!quit_ && (Sint32)(end - SDL_GetTicks()) > 0) {
        pump_events();
        SDL_Delay(5);
    }
}

void SdlPlatform::delay_ms(int ms)
{
    Uint32 end = SDL_GetTicks() + (Uint32)(ms > 0 ? ms : 0);
    if (ms >= 10 || SDL_GetTicks() - last_present_ >= 16) present();
    while (!quit_ && (Sint32)(end - SDL_GetTicks()) > 0) {
        pump_events();
        SDL_Delay(1);
    }
}

/* With no audio_ device the samples' length is waited out instead, so the game's pace is the same. */
void SdlPlatform::audio_queue(const short *pcm, int count)
{
    if (audio_) SDL_QueueAudio(audio_, pcm, (Uint32)count * sizeof(short));
    else silent_ms_x1000_ += (Uint32)count * 1000000u / 44100u;
}

void SdlPlatform::audio_wait()
{
    if (!audio_) {
        delay_ms((int)(silent_ms_x1000_ / 1000));
        silent_ms_x1000_ %= 1000;
        return;
    }
    if (SDL_GetQueuedAudioSize(audio_) > 44100 / 50 * sizeof(short)) present();
    while (!quit_ && SDL_GetQueuedAudioSize(audio_) > 256) {
        pump_events();
        SDL_Delay(1);
    }
}

void SdlPlatform::flush_keys()
{
    pump_events();
    q_head_ = q_tail_;
}
