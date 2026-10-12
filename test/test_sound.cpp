/* test_sound.cpp - the speaker emulation and the ORIGIN logo effect, with its own recording
 * platform: sounds are captured and measured, frame delays counted. Usage: test_sound <game dir> */
#include <stdio.h>
#include <string.h>
#include "combat.h"
#include "files.h"
#include "gamedata.h"
#include "gfx.h"
#include "picture.h"
#include "platform.h"
#include "sound.h"
#include "video.h"

static short pcm[44100 * 20];
static int pcm_n, frames;
static long waited_ms;

class RecordingPlatform : public Platform {   /* keeps what is played, counts the waits */
public:
    int  poll_key(int *shifted) override { if (shifted) *shifted = 0; return 0; }
    void delay_ticks(int n) override { waited_ms += n * 55; }
    int  key_waiting() override { return 0; }
    void flush_keys() override {}
    void present() override {}
    int  quit_requested() override { return 0; }
    void delay_ms(int ms) override { waited_ms += ms; frames++; }
    void audio_wait() override {}
    void audio_queue(const short *p, int count) override
    {
        if (pcm_n + count > (int)(sizeof pcm / sizeof pcm[0])) return;
        memcpy(pcm + pcm_n, p, (size_t)count * sizeof(short));
        pcm_n += count;
    }
};
static RecordingPlatform recording_platform;
Platform *platform = &recording_platform;

static int fails;
static void check(int ok, const char *what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

static double ms, hz;
static void measure(const char *name)
{
    int rising = 0, loud = 0;
    for (int i = 1; i < pcm_n; i++) {
        rising += pcm[i - 1] <= 0 && pcm[i] > 0;
        loud += pcm[i] > 1000;
    }
    ms = pcm_n * 1000.0 / 44100.0;
    hz = ms > 0 ? rising * 1000.0 / ms : 0;
    printf("  %-26s %7.1f ms, about %5.0f Hz, %ld ms waited%s\n", name, ms, hz, waited_ms, loud ? "" : " (silent)");
    pcm_n = 0;
    waited_ms = 0;
}

int main(int argc, char **argv)
{
    files_set_game_dir(argc > 1 ? argv[1] : ".");
    if (!Data::instance().load()) return 1;   /* the game's tables and text */
    sound.enabled = 1;
    gfx.video_type = 1;
    set_ega_colors();

    sound_sweep(-4, 20000, 4000, 1, 3497);
    measure("harpsichord key 2");
    check(hz > 870 && hz < 890 && ms > 200 && ms < 280, "a harpsichord note is the A at 880 Hz, a quarter of a second");
    sound_sweep(-4, 20000, 4000, 1, 6995);
    measure("harpsichord key 9");
    check(hz > 1650 && hz < 1780, "the highest key is the A an octave up");
    beep(200, 0xA5);
    measure("the Blocked! beep");
    check(ms > 199 && ms < 201 && hz > 150 && hz < 175, "200 ms at 165 Hz");
    step_sound();
    measure("a footstep");
    check(ms > 2 && ms < 5, "two clicks of 1.6 ms (20 ms apart)");
    sound_noise(2000, 0x640, 10);
    measure("the noise of a wound");
    check(ms > 99 && ms < 101 && hz > 300 && hz < 2000, "100 ms of noise between 100 and 2000 Hz");
    sound_slide(0x28, 1, 2000, 0x4B0);
    measure("a slide");
    check(ms > 39 && ms < 41 && hz > 1300 && hz < 1900, "40 ms rising from 1200 to 2000 Hz");
    combat.victory_fanfare();
    measure("the victory fanfare");
    check(ms > 2000 && ms < 5000, "the fanfare lasts a few seconds");
    sound.enabled = 0;
    sound_noise(2000, 0x640, 10);
    int silent = 1;
    for (int i = 0; i < pcm_n; i++) silent &= pcm[i] == 0;
    measure("the same with sound off");
    check(silent && ms > 99 && ms < 101, "with sound off the wait is the same and nothing is heard");
    speaker_on(440);
    delay_loop(50, 1);
    measure("speaker_on with sound off");
    check(pcm_n == 0 && ms == 0, "the speaker stays off; the delay is a plain wait");
    sound.enabled = 1;

    alloc_back_page();
    uint16_t title = load_bitmap("TITLE.BIT");
    check(title != 0, "TITLE.BIT");
    frames = 0;
    int r = show_title_picture(title);
    int rows = 0, first = -1, last = -1, blue = 1;
    for (int y = 0; y < SCREEN_H; y++) {
        int any = 0;
        for (int x = 0; x < SCREEN_W; x++) {
            any |= video_fb[y][x] != 0;
            blue &= video_fb[y][x] == 0 || video_fb[y][x] == 9;
        }
        if (any) {
            rows++;
            if (first < 0) first = y;
            last = y;
        }
    }
    printf("  logo: %d frames, rows %d-%d\n", frames, first, last);
    check(r == 0 && frames == 85, "the logo effect runs its 85 frames (7 sizes unfold, 6 fold up again)");
    check(first >= 0x2E && last <= 0x2E + 60 && rows > 50 && blue, "it ends on the largest logo, light blue, in rows 46-106");
    if (video_save_ppm("test_sound_logo.ppm") == 0) printf("wrote test_sound_logo.ppm\n");
    printf("%d failure(s)\n", fails);
    return fails != 0;
}
