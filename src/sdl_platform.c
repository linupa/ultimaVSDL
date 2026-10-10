/* sdl_platform.c - platform.h on SDL2: a window showing video_fb, keyboard to game key codes. */
#include <SDL.h>
#include "platform.h"
#include "video.h"

static SDL_Window *window;
static SDL_Renderer *renderer;
static int tex_w;
static SDL_Texture *texture;
static int quit_requested;
static Uint32 last_present;
static SDL_AudioDeviceID audio;

#define QUEUE_SIZE 64
static struct { int key, shifted; } queue[QUEUE_SIZE];
static int q_head, q_tail;
static int skip_text;   /* drop the SDL_TEXTINPUT that follows a key already handled by SDL_KEYDOWN */

static void push_key(int key, int shifted)
{
    int next = (q_tail + 1) % QUEUE_SIZE;
    if (next == q_head) return;   /* full: drop, like a full BIOS buffer */
    queue[q_tail].key = key;
    queue[q_tail].shifted = shifted;
    q_tail = next;
}

int platform_init(const char *title, int scale, int aspect_4_3)
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
    tex_w = w;
    window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              w * scale, h * scale, SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        return -1;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer: %s", SDL_GetError());
        return -1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, aspect_4_3 ? "1" : "0");
    SDL_RenderSetLogicalSize(renderer, w, h);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, w, lines);
    if (!texture) {
        SDL_Log("SDL_CreateTexture: %s", SDL_GetError());
        return -1;
    }
    SDL_AudioSpec want = {0};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    audio = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);   /* 0 if there is no sound: the game runs silent */
    if (audio) SDL_PauseAudioDevice(audio, 0);
    SDL_StartTextInput();
    return 0;
}

void platform_shutdown(void)
{
    if (audio) SDL_CloseAudioDevice(audio);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

void platform_present(void)
{
    static Uint8 rgb[SCREEN_W * 2 * VIDEO_ROWS * 3];
    video_to_rgb(rgb);
    SDL_UpdateTexture(texture, NULL, rgb, tex_w * 3);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    last_present = SDL_GetTicks();
}

static void key_down(const SDL_KeyboardEvent *k)
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
        skip_text = 1;
        return;
    }
    if (ctrl && sym >= SDLK_a && sym <= SDLK_z) {   /* Ctrl+letter = 1-26, as from the BIOS */
        push_key(1 + (int)(sym - SDLK_a), shift);
        return;
    }
    skip_text = 0;
}

static void pump_events(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            quit_requested = 1;
            break;
        case SDL_KEYDOWN:
            key_down(&e.key);
            break;
        case SDL_TEXTINPUT:
            if (skip_text) { skip_text = 0; break; }
            for (const char *p = e.text.text; *p; p++)
                if ((unsigned char)*p >= 0x20 && (unsigned char)*p < 0x7F) push_key(*p, 0);
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_EXPOSED) platform_present();
            break;
        }
    }
}

int platform_poll_key(int *shifted)
{
    pump_events();
    if (SDL_GetTicks() - last_present >= 16) platform_present();
    if (quit_requested) return 0x0D;
    if (q_head == q_tail) return 0;
    int key = queue[q_head].key;
    if (shifted) *shifted = queue[q_head].shifted;
    q_head = (q_head + 1) % QUEUE_SIZE;
    return key;
}

int platform_key_waiting(void)
{
    pump_events();
    return quit_requested || q_head != q_tail;
}

void platform_delay_ticks(int n)
{
    Uint32 end = SDL_GetTicks() + (Uint32)n * 55;   /* 18.2 ticks per second */
    platform_present();
    while (!quit_requested && (Sint32)(end - SDL_GetTicks()) > 0) {
        pump_events();
        SDL_Delay(5);
    }
}

void platform_delay_ms(int ms)
{
    Uint32 end = SDL_GetTicks() + (Uint32)(ms > 0 ? ms : 0);
    if (ms >= 10 || SDL_GetTicks() - last_present >= 16) platform_present();
    while (!quit_requested && (Sint32)(end - SDL_GetTicks()) > 0) {
        pump_events();
        SDL_Delay(1);
    }
}

/* With no audio device the samples' length is waited out instead, so the game's pace is the same. */
static Uint32 silent_ms_x1000;

void platform_audio_queue(const short *pcm, int count)
{
    if (audio) SDL_QueueAudio(audio, pcm, (Uint32)count * sizeof(short));
    else silent_ms_x1000 += (Uint32)count * 1000000u / 44100u;
}

void platform_audio_wait(void)
{
    if (!audio) {
        platform_delay_ms((int)(silent_ms_x1000 / 1000));
        silent_ms_x1000 %= 1000;
        return;
    }
    if (SDL_GetQueuedAudioSize(audio) > 44100 / 50 * sizeof(short)) platform_present();
    while (!quit_requested && SDL_GetQueuedAudioSize(audio) > 256) {
        pump_events();
        SDL_Delay(1);
    }
}

int platform_quit_requested(void) { return quit_requested; }

void platform_flush_keys(void)
{
    pump_events();
    q_head = q_tail;
}
