/* picture.cpp - pictures, bitmaps, page copies and screen effects. See picture.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "files.h"
#include "gamedata.h"
#include "gfx.h"
#include "input.h"
#include "intro.h"
#include "map.h"
#include "picture.h"
#include "platform.h"
#include "sound.h"
#include "story.h"
#include "tiles.h"
#include "video.h"
#include "view.h"

/* A freed block keeps its contents until the slot is used again, as freed DOS memory does: the
 * game draws the title banner once more from the title picture just after freeing it (INTRO.OVL,
 * back from character creation), and that only works because nothing has reused the memory. */
#define MAX_HANDLES 16
static struct { uint8_t *data; size_t size; int freed; } handles[MAX_HANDLES];

static uint16_t new_handle(const char *filename)
{
    int slot = -1;
    for (int i = 0; i < MAX_HANDLES && slot < 0; i++)
        if (!handles[i].data) slot = i;
    for (int i = 0; i < MAX_HANDLES && slot < 0; i++)
        if (handles[i].freed) slot = i;
    if (slot < 0) return 0;
    free(handles[slot].data);
    handles[slot].freed = 0;
    handles[slot].data = game_load_file(filename, &handles[slot].size);
    return handles[slot].data ? (uint16_t)(slot + 1) : 0;
}

static const uint8_t *data_of(uint16_t handle, size_t *size)
{
    if (handle == 0 || handle > MAX_HANDLES || !handles[handle - 1].data) return NULL;
    *size = handles[handle - 1].size;
    return handles[handle - 1].data;
}

static unsigned u16(const uint8_t *p) { return (unsigned)(p[0] | p[1] << 8); }

/* The original converts the picture to planar form here (drv 0x42); the port keeps the file's
 * packed pixels. */
uint16_t load_picture(const char *filename) { return new_handle(filename); }
uint16_t load_bitmap(const char *filename) { return new_handle(filename); }

void dos_free_mem(uint16_t handle)
{
    if (handle == 0 || handle > MAX_HANDLES) return;
    handles[handle - 1].freed = 1;
}

void dos_free_mem2(uint16_t handle) { dos_free_mem(handle); }

int alloc_back_page(void)
{
    video_clear_back();
    return 1;
}

void set_draw_page(int page) { video_set_draw_page(page); }

void copy_page(int to, int from) { video_copy_page(from, to); }

void copy_page_rect(int y2, int x2, int y1, int x1, int to, int from)
{
    if (to == from || (unsigned)to > 1 || (unsigned)from > 1) return;
    video_copy_rect(x1, y1, x2, y2, from, to);
}

void plot_pixel(int y, int x)
{
    gfx.line_last_x = (uint16_t)x;
    gfx.line_last_y = (uint16_t)y;
    if (gfx.draw_color == 0xFFFF) return;
    if (gfx.clip_enabled != 0 && ((int16_t)x < (int16_t)gfx.clip_x1 || (int16_t)x > (int16_t)gfx.clip_x2 ||
                                 (int16_t)y < (int16_t)gfx.clip_y1 || (int16_t)y > (int16_t)gfx.clip_y2)) return;   /* clip_point (08ca) */
    video_plot(x, y);
}

/* The drivers' own noise (EGA.DRV 0x27af, CGA.DRV 0x1acc, HER.DRV 0x1e66): the speaker's timer
 * gets `count` random pitches between 100 Hz and top / 2, a sixteenth of a delay unit apart, and
 * the speaker stays on at the last. The port gives the last one `seconds`. */
static uint16_t noise_seed = 0x7664;
static unsigned noise_top = 0xF0, noise_count = 1;

static void driver_noise(double seconds)
{
    for (unsigned i = 0; i < noise_count; i++) {
        uint16_t a = (uint16_t)(noise_seed + 0x9248);
        a = (uint16_t)(a >> 3 | a << 13);
        noise_seed = (uint16_t)((a ^ 0x9248) + 0x11);
        unsigned span = ((noise_top >> 1) - 100 + 1) & 0xFFFF;
        unsigned freq = (span ? noise_seed % span : 0) + 100;
        sound_queue_tone(freq, i + 1 == noise_count ? seconds : 1 / 16000.0);
    }
}

/* One pixel of the back page to the screen: `y` is the game's row. */
static void reveal_pixel(int x, int y)
{
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    for (int r = video_row(y), n = video_row_count(y, 0); n > 0 && r < video_rows(); n--, r++)
        video_fb[r][x] = video_back[r][x];
}

/* drv 0x66 (carry clear): the pixels of the rectangle, rows y1 to y2 - 1, are copied from the back
 * page to the screen in the order of a maximal-length shift register over width * height, so the
 * picture dissolves in. The port shows about 20 steps a second while it runs.
 *
 * Until the driver has drawn its first character of text (so: for the title) every other pixel
 * also gives the speaker a new random pitch, from a range that widens as it goes - a rising
 * hiss - and a waiting key stops the dissolve (the key is left for the game to read). */
void copy_back_rect(int y2, int x2, int y1, int x1)
{
    static const uint16_t taps[] = {0x0003, 0x0006, 0x000C, 0x0014, 0x0030, 0x0060, 0x00B8, 0x0110,
                                    0x0240, 0x0500, 0x0CA0, 0x1B00, 0x3500, 0x6000, 0xB400};
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    unsigned w = (unsigned)(x2 - x1 + 1), h = (unsigned)(y2 - y1);
    unsigned n = (uint16_t)(w * h - 1), bits = 0;
    for (unsigned v = n; v; v >>= 1) bits++;
    if (bits < 2) bits = 2;
    uint16_t tap = taps[bits - 2 > 14 ? 14 : bits - 2], state = 1;
    int base = x1 & (video_2bit() ? ~3 : ~7);
    unsigned done = 0, step = (1u << bits) / 20 + 1;
    int noisy = video_no_text_yet, every_other = 0, stopped = 0;
    unsigned quiet = 0;   /* steps since the pitch last changed */
    do {
        unsigned x = state % w, y = state / w;
        quiet++;
        if (y < h) {
            reveal_pixel(base + (int)x, y1 + (int)y);
            if (noisy && (every_other ^= 1)) {
                driver_noise(quiet * (0.055 / step));
                quiet = 0;
                noise_top = (noise_top + 1) & 0xFFFF;
                if (platform->key_waiting()) {
                    stopped = 1;
                    break;
                }
            }
        }
        int carry = state & 1;
        state >>= 1;
        if (carry) state ^= tap;
        if (++done % step == 0) {
            if (noisy) sound_queue_flush();
            delay_ticks(1);
        }
    } while (state != 1);
    if (!stopped && h > 0) reveal_pixel(base, y1);
    if (noisy) sound_queue_flush();
}

static const uint8_t *frame_data(uint16_t picture, int frame, const uint8_t **mask, int *w, int *h, int *row_bytes)
{
    size_t size;
    const uint8_t *d = data_of(picture, &size);
    if (!d || frame < 0 || (unsigned)frame >= u16(d)) return NULL;
    unsigned off = u16(d + 2 + frame * 4), moff = u16(d + 4 + frame * 4);
    if (off + 4 > size) return NULL;
    *w = (int)u16(d + off);
    *h = (int)u16(d + off + 2);
    if (video_2bit()) *row_bytes = (*w + 3) / 4;   /* the .4 files: 2-bit pixels, rows padded to 4 pixels */
    else *row_bytes = ((*w + 1) / 2 + 3) & ~3;     /* 4-bit pixels, rows padded to 8 pixels */
    if (off + 4 + (size_t)*row_bytes * (size_t)*h > size) return NULL;
    if (mask) *mask = moff ? d + moff + 4 : NULL;
    return d + off + 4;
}

int picture_frame_size(uint16_t picture, int frame, int *w, int *h)
{
    int rb;
    return frame_data(picture, frame, NULL, w, h, &rb) != NULL;
}

/* drv 0x4B. x is rounded down to a multiple of 8. Rows y to y + h - 1 are clipped to 0-199, or
 * to clip_y1-clip_y2 while clip_enabled is set (flag 1, upside down, draws upwards from y + h).
 * With a mask, pixels whose mask bit is set are left alone. On the screen page whole bytes are
 * copied, so the width is rounded up to 8 pixels and flag 2 is ignored; on the back page the
 * last byte keeps what was there beyond the frame's width.
 *
 * Flag 2 (back page only) mirrors the frame within its byte-padded width W. The driver reverses
 * each byte with a table and patches the seams: without a mask the frame is OR-ed onto the page,
 * and the last pixel of each byte comes from one source pixel further left (the very last is
 * left out, and one more lands just left of the frame); with a mask that pixel is the AND of
 * the two source pixels, opaque only if both are. The port does the same per pixel. */
static void draw_frame_2bit(int flags, int y, int x, int w, int h, int rb, const uint8_t *src, const uint8_t *mask,
                            const uint8_t *end);

void draw_picture_frame(int flags, int y, int x, int frame, uint16_t picture)
{
    int w, h, row_bytes;
    const uint8_t *mask, *src = frame_data(picture, frame, &mask, &w, &h, &row_bytes);
    if (!src) return;
    if (video_2bit()) {
        size_t size;
        const uint8_t *d = data_of(picture, &size);
        draw_frame_2bit(flags, y, x, w, h, row_bytes, src, mask, d + size);
        return;
    }
    int full = row_bytes * 2, mask_bytes = row_bytes / 4;
    int back = video_get_draw_page() != 0;
    int limit = (back && !mask) ? w : full;
    int top = 0, bottom = 199;
    if (gfx.clip_enabled) {
        top = (int16_t)gfx.clip_y1;
        bottom = (int16_t)gfx.clip_y2;
    }
    x &= ~7;
#define SRC(row, px) (((px) & 1) ? ((row)[(px) >> 1] & 0x0F) : ((row)[(px) >> 1] >> 4))
#define MSK(mrow, px) (((mrow)[(px) >> 3] & (0x80 >> ((px) & 7))) != 0)
    for (int i = 0; i < h; i++) {
        int py = (flags & 1) ? y + h - i : y + i;
        if (py < top || py >= bottom + ((flags & 1) ? 1 : 0) || py < 0 || py >= SCREEN_H) continue;
        const uint8_t *row = src + (size_t)i * (size_t)row_bytes;
        const uint8_t *mrow = mask ? mask + (size_t)i * (size_t)mask_bytes : NULL;
        if (!(flags & 2) || !back) {
            for (int px = 0; px < limit; px++) {
                if (x + px < 0 || x + px >= SCREEN_W) continue;
                if (mrow && MSK(mrow, px)) continue;
                video_draw[py][x + px] = SRC(row, px);
            }
            continue;
        }
        for (int dx = -1; dx < full; dx++) {
            if (x + dx < 0 || x + dx >= SCREEN_W) continue;
            uint8_t *d = &video_draw[py][x + dx];
            int sp = full - 1 - dx;   /* the mirrored source pixel */
            if (dx == -1) {           /* the stray pixel left of the frame: the last source pixel */
                if (!mrow || !MSK(mrow, full - 1)) *d = SRC(row, full - 1);
            } else if ((dx & 7) != 7) {
                if (!mrow) *d |= SRC(row, sp);
                else if (!MSK(mrow, sp)) *d = SRC(row, sp);
            } else if (!mrow) {
                if (sp - 1 >= 0 && dx != full - 1) *d |= SRC(row, sp - 1);
            } else {
                uint8_t c = SRC(row, sp);
                int m = MSK(mrow, sp);
                if (sp - 1 >= 0 && dx != full - 1) {
                    c &= SRC(row, sp - 1);
                    m = m && MSK(mrow, sp - 1);
                }
                if (!m) *d = c;
            }
        }
    }
#undef SRC
#undef MSK
}

/* drv 0x4B in CGA.DRV (0xa26) and HER.DRV (0xc66, the same code with each row put on one or two
 * of its 300). Four pixels to a byte; x is rounded down to a multiple of 4. The mask has one bit
 * per pixel (set = leave the screen alone), a byte for every two bytes of picture.
 *
 * Rows are clipped as in the EGA driver. What differs:
 *  - Without a mask the last byte of a row whose width isn't a multiple of 4 is meant to be
 *    merged through a mask, but the register that should hold it holds something else (the
 *    high byte of an earlier product, zero unless rows were clipped off): normally those last
 *    pixels just aren't drawn.
 *  - Clipped rows are skipped in the mask by the picture's row length, not the mask's.
 *  - Mirrored (flag 2), each byte's first three pixels are reversed into the byte and the
 *    fourth goes to the byte on the left, so the image sits one pixel left of its box. The mask
 *    is then read half a byte at a time straight through, not row by row.
 *  - HER.DRV, mirrored without a mask, puts row 1 on one row only. */
static void draw_frame_2bit(int flags, int y, int x, int w, int h, int rb, const uint8_t *src, const uint8_t *mask,
                            const uint8_t *end)
{
    int top = 0, bottom = 199, skip = 0;
    if (gfx.clip_enabled) {
        top = (int16_t)gfx.clip_y1;
        bottom = (int16_t)gfx.clip_y2;
    }
    int ya = y, yb = y + h;
    if (ya < top) {
        if (!(flags & 1)) skip = top - ya;
        ya = top;
    }
    if (yb > bottom) {
        if (flags & 1) skip = yb - bottom;
        yb = bottom;
    }
    int rows = yb - ya, py = (flags & 1) ? yb : ya, x0 = (x >> 2) * 4;
    if (rows <= 0) return;
    uint8_t stale = (uint8_t)(skip ? ((unsigned)skip * (unsigned)rb) >> 8 : 0);
    src += (size_t)skip * (size_t)rb;
    if (mask) mask += (size_t)skip * (size_t)rb;
    int phase = 0;   /* mirrored + mask: which half of the mask byte comes next */
    int herc = video_mode() == VIDEO_HERC;

    for (; rows > 0; rows--, py += (flags & 1) ? -1 : 1, src += rb) {
        int passes = (herc && (py & 1)) ? 2 : 1;
        if (herc && (flags & 2) && !mask && py == 1) passes = 1;
        const uint8_t *row_mask = mask;
        if (src + rb > end) return;
        for (int pass = 0; pass < passes; pass++) {
            int r = video_row(py) + pass;
            uint8_t *d = (r >= 0 && r < video_rows()) ? video_draw[r] : NULL;
            mask = row_mask;   /* (the second pass starts again in the mask, but keeps `phase`) */
#define PUT(px, v) do { int x_ = (px); if (d && x_ >= 0 && x_ < SCREEN_W) d[x_] = (uint8_t)(v); } while (0)
            for (int i = 0; i < rb; i++) {
                uint8_t b = src[i], m = 0;
                if (mask && !(flags & 2)) {
                    if (mask + i / 2 < end) m = (i & 1) ? (mask[i / 2] & 0x0F) : (mask[i / 2] >> 4);
                } else if (mask) {
                    if (mask < end) m = phase ? (*mask & 0x0F) : (*mask >> 4);
                    if (phase) mask++;
                    phase ^= 1;
                } else if (!(flags & 2) && i == rb - 1 && (w & 3)) {
                    for (int p = 0; p < 4; p++)   /* the stale byte as the mask: its set bits are drawn */
                        if ((stale >> (6 - 2 * p)) & 3)
                            PUT(x0 + i * 4 + p, (d && x0 + i * 4 + p < SCREEN_W ? d[x0 + i * 4 + p] & ~(stale >> (6 - 2 * p)) & 3 : 0)
                                                    | (((b & stale) >> (6 - 2 * p)) & 3));
                    continue;
                }
                for (int p = 0; p < 4; p++) {
                    if (m & (8 >> p)) continue;
                    int px = (flags & 2) ? x0 + (rb - 1 - i) * 4 + 2 - p : x0 + i * 4 + p;
                    PUT(px, (b >> (6 - 2 * p)) & 3);
                }
            }
#undef PUT
        }
        if (mask && !(flags & 2)) mask = row_mask + (rb + 1) / 2;
    }
}

/* drv 0x63: the same, mirrored. */
void draw_picture_frame2(int flags, int y, int x, int frame, uint16_t picture)
{
    draw_picture_frame(flags | 2, y, x, frame, picture);
}

void set_clip_rect(int y2, int x2, int y1, int x1)
{
    gfx.clip_x1 = (uint16_t)x1;
    gfx.clip_y1 = (uint16_t)y1;
    gfx.clip_x2 = (uint16_t)x2;
    gfx.clip_y2 = (uint16_t)y2;
}

/* drv 0x4E: set bits become colour 15 on the back page, clear bits leave it alone.
 * CGA.DRV (0xe0c) and HER.DRV (0x118c) draw on the current page, in colour 3, and round the
 * width up to whole bytes (HER.DRV: half bytes) where the EGA driver rounds it down. */
void draw_font_glyph(int y, int x, int glyph, uint16_t bitmap)
{
    size_t size;
    const uint8_t *d = data_of(bitmap, &size);
    if (!d || glyph < 0 || glyph >= (int16_t)u16(d)) return;
    unsigned off = u16(d + 2 + glyph * 2);
    if (off + 4 > size) return;
    int width = (int)u16(d + off), h = (int)u16(d + off + 2);
    int bytes = width >> 3, pixels;
    uint8_t (*page)[SCREEN_W] = video_back;
    uint8_t colour = 15;
    if (video_2bit()) {
        page = video_draw;
        colour = 3;
        if (width & 7) bytes++;
    }
    if (bytes < 1) bytes = 1;   /* the driver's row loop runs at least once (the story font) */
    pixels = bytes * 8;
    if (video_mode() == VIDEO_HERC) {
        pixels = width > 0 ? (width + 3) / 4 * 4 : 4;
        bytes = (pixels + 7) / 8;
    }
    const uint8_t *row = d + off + 4;
    for (int i = 0; i < h; i++, row += bytes) {
        if ((size_t)(row - d) + (size_t)bytes > size) return;
        int py = y + i;
        if (py < 0 || py >= SCREEN_H) continue;
        for (int r = video_row(py), n = video_row_count(py, 0); n > 0 && r < video_rows(); n--, r++)
            for (int px = 0; px < pixels; px++)
                if ((row[px >> 3] & (0x80 >> (px & 7))) && x + px >= 0 && x + px < SCREEN_W) page[r][x + px] = colour;
    }
}

/* drv 0x69 (carry clear): the title banner is four animation frames kept in the back page at rows
 * 0, 50, 100 and 150 (49 rows each); each call copies the next one to rows 65-113 of the screen.
 * HER.DRV (0x1ee6) takes 74 rows from every 75th - the same frames - and puts them at row 97. */
static int banner_frame;
static void banner_step(void)
{
    int herc = video_mode() == VIDEO_HERC;
    int rows = herc ? 74 : 49, pitch = herc ? 75 : 50, to = herc ? 97 : 65;
    for (int r = 0; r < rows; r++) memcpy(video_fb[to + r], video_back[banner_frame * pitch + r], SCREEN_W);
    banner_frame = (banner_frame + 1) & 3;
}

void wait_reveal_strips(void)
{
    delay_ticks(1);
    banner_step();
}

/* drv 0x69 (carry set): the four banner frames dissolve in around the letters of the 288 x 49
 * bitmap (WD.BIT, "Warriors of Destiny"), which stay black, then the letters fill in. Works in the
 * back page while the banner keeps animating on the screen; a waiting key stops it (and stays
 * for the game to read).
 *
 * After every 128 pixels (256 if the machine is slow: the delay calibration below 250; the port
 * counts as fast) the banner moves on a frame and the driver waits 50 delay units - or crackles
 * first: 25 random pitches up to 1500 Hz in a millisecond and a half, then 45 units. The chance
 * of a crackle starts at 400 in 512 and drops by 3 each time.
 * The banner is 16 pixels in from the left in the EGA driver, 12 in the others. */
static uint8_t strips_saved[VIDEO_ROWS][SCREEN_W];

static void strips_pixel(const uint8_t *wd, size_t size, unsigned x, unsigned y, int letters_stay)
{
    int herc = video_mode() == VIDEO_HERC, left = video_2bit() ? 12 : 16;
    int letter = wd && 8 + y * 36 + (x >> 3) < size && (wd[8 + y * 36 + (x >> 3)] & (0x80 >> (x & 7)));
    if (letters_stay && letter) return;
    for (unsigned n = 0, ly = y; n < 4; n++, ly += 50) {
        unsigned r1 = herc ? (ly * 3) >> 1 : ly, r2 = herc ? (ly * 3 + 1) >> 1 : ly;
        video_back[r1][left + x] = strips_saved[r1][left + x];
        video_back[r2][left + x] = strips_saved[r2][left + x];
    }
}

void reveal_strips(uint16_t bitmap)
{
    static uint16_t chance_seed = 0x7664;   /* EGA.DRV cs:28C2 */
    size_t size;
    const uint8_t *wd = data_of(bitmap, &size);
    memcpy(strips_saved, video_back, sizeof strips_saved);
    video_clear_back();
    int stopped = 0;
    for (int pass = 1; pass >= 0 && !stopped; pass--) {
        uint16_t state = 1;
        int count = 0x80, chance = 400;
        do {
            unsigned x = state % 288, y = state / 288;
            if (y < 49) {
                strips_pixel(wd, size, x, y, pass);
                if (--count == 0) {
                    count = 0x80;
                    banner_step();
                    chance -= 3;
                    if ((int)(chance_seed & 0x1FF) < chance) {
                        noise_top = 3000;
                        noise_count = 25;
                        driver_noise(1 / 16000.0);
                        sound_queue_tone(0, 0.001);
                        sound_queue_flush();
                        platform->delay_ms(45);
                    } else {
                        platform->delay_ms(50);
                    }
                }
            }
            if (platform->key_waiting() || platform->quit_requested()) {
                stopped = 1;
                break;
            }
            int carry = state & 1;
            state >>= 1;
            if (carry) state ^= 0x3500;
            uint16_t a = (uint16_t)(chance_seed + 0x9248);
            a = (uint16_t)(a >> 3 | a << 13);
            chance_seed = (uint16_t)((a ^ 0x9248) + 0x11);
        } while (state != 1);
        if (!stopped) strips_pixel(wd, size, 0, 0, pass);
    }
    memcpy(video_back, strips_saved, sizeof strips_saved);
}

/* drv 0x6F: the ORIGIN logo that grows while it turns over. The back page holds seven sizes of
 * it stacked from row 0; the script (EGA.DRV 0x1E8C-0x1F93) gives for each size its row on
 * the back page, its row on the screen and its height, then lists of line numbers separated by
 * 0xFF. Reading a list backwards switches lines on, a frame at each 0xFF (seven frames: the
 * size unfolds); reading it forwards switches them off (six frames: it folds up again) before
 * the next size starts. A frame draws the lines that are on packed together in the middle of
 * the size's band, the rest of the band black - and bottom-up for every other size, so the
 * image flips as it grows. Only planes 0 and 3 are written: the logo is light blue (9).
 * A key ends it with the full last size (returns 1). Each frame waits 12 delay units. */
enum { LOGO_SCRIPT = 0x1E8C, LOGO_SCRIPT_SIZE = 0x108, LOGO_LAST_SIZE = 0x1F4B - LOGO_SCRIPT };

/* The script, read from EGA.DRV when the effect runs. 0 if the file can't be read. */
static int load_logo_script(uint8_t *script)
{
    size_t size = 0;
    uint8_t *drv = game_load_file("EGA.DRV", &size);
    int ok = drv && size >= LOGO_SCRIPT + LOGO_SCRIPT_SIZE;
    if (ok) memcpy(script, drv + LOGO_SCRIPT, LOGO_SCRIPT_SIZE);
    else fprintf(stderr, "cannot read the logo script from EGA.DRV\n");
    free(drv);
    return ok;
}

static struct {
    int src, dst, height, flip, key, skipped;
    uint8_t on[256];
} logo;

static void logo_next_row(int *y)
{
    *y += logo.flip ? -1 : 1;
    if (!logo.skipped && poll_key()) logo.key = 1;
}

/* CGA.DRV (0x1079, 0x1092) and HER.DRV: the whole row is cleared, and the logo is colour 1.
 * HER.DRV uses the script's screen rows as rows of its 300 - the logo is drawn that much higher
 * and flatter - and takes back-page row 3r/2 (rounded up) for the game's row r. */
static void logo_clear_row(int y)
{
    if (y < 0 || y >= video_rows()) return;
    if (video_2bit()) memset(video_fb[y], 0, SCREEN_W);
    else for (int x = 0; x < SCREEN_W; x++) video_fb[y][x] &= 6;
}

static void logo_frame(void)
{
    int hidden = 0, shown = 0, y, next = 0;
    for (int i = 0; i < logo.height; i++) {
        if (logo.on[i]) shown++;
        else hidden++;
    }
    delay_loop(12, 1);
    y = logo.flip ? logo.dst + logo.height : logo.dst;
    for (int n = hidden / 2; n > 0; n--) {
        logo_clear_row(y);
        logo_next_row(&y);
        if (logo.key) return;
    }
    for (int n = shown; n > 0; n--) {
        while (!logo.on[next]) next++;
        int row = logo.src + next++;
        if (video_mode() == VIDEO_HERC) row = (row * 3 + 1) >> 1;
        if (y >= 0 && y < video_rows() && row < video_rows())
            for (int x = 0; x < SCREEN_W; x++) {
                if (video_2bit()) video_fb[y][x] = video_back[row][x] & 1;
                else video_fb[y][x] = (uint8_t)((video_fb[y][x] & 6) | ((video_back[row][x] & 1) ? 9 : 0));
            }
        logo_next_row(&y);
        if (logo.key) return;
    }
    for (int n = (hidden + 1) / 2; n > 0; n--) {
        logo_clear_row(y);
        logo_next_row(&y);
        if (logo.key) return;
    }
}

static int title_effect(void)
{
    uint8_t script[LOGO_SCRIPT_SIZE];
    if (!load_logo_script(script)) return 0;
    const uint8_t *p = script;
    memset(&logo, 0, sizeof logo);
    logo.flip = 1;
    for (int stages = *p++; ; ) {
        logo.src = *p++;
        logo.dst = *p++;
        logo.height = *p++;
        int len = *p++;
        const uint8_t *list = p;
        p += len;
        logo.flip ^= 1;
        memset(logo.on, 0, sizeof logo.on);
        for (int i = len - 2, frames = 7; frames > 0 && i >= 0; i--) {
            if (list[i] != 0xFF) {
                logo.on[list[i]] = 1;
                continue;
            }
            logo_frame();
            frames--;
        }
        if (logo.key || platform->quit_requested()) break;
        if (--stages == 0) return 0;
        for (int i = 1, frames = 6; frames > 0 && i < len; i++) {
            if (list[i] != 0xFF) {
                logo.on[list[i]] = 0;
                continue;
            }
            logo_frame();
            frames--;
        }
        if (logo.key || platform->quit_requested()) break;
    }
    logo.key = 0;
    logo.skipped = 1;
    logo.src = script[LOGO_LAST_SIZE];   /* the last size, whole (the script's entry at 0x1F4B) */
    logo.dst = script[LOGO_LAST_SIZE + 1];
    logo.height = script[LOGO_LAST_SIZE + 2];
    logo.flip = 0;
    memset(logo.on, 1, sizeof logo.on);
    logo_frame();
    return 1;
}

/* The widths and heights of the logo's seven sizes (ds:5306, ds:5314, from DATA.OVL). */
static int16_t title_widths[7], title_heights[7];
[[maybe_unused]] static const bool title_sizes_added = (Data::instance().add(0x5306, title_widths, "picture.title_widths"),
                                                        Data::instance().add(0x5314, title_heights, "picture.title_heights"), true);

/* The seven sizes (TITLE.BIT glyphs 0-6; widths at ds:5306, heights at ds:5314) are drawn one
 * under another on the back page, centred, and the effect plays. 1 if a key cut it short. */
int show_title_picture(uint16_t bitmap)
{
    int y = 0;
    set_draw_page(1);
    set_draw_color(0);
    fill_rect(199, 0x13F, 0, 0);
    for (int i = 0; i < 7; i++) {
        draw_font_glyph(y, (0x140 - title_widths[i]) >> 1, i, bitmap);
        y += title_heights[i];
    }
    set_draw_page(0);
    return title_effect();
}

/* drv 0x66 (carry set), 256 calls: the tile's pixels are drawn straight to the screen in the
 * order of an 8-bit shift register (x = state / 16, y = state % 16). Every 8 pixels the world
 * (or, in the demo, the demo) advances a step. Returns non-zero if the demo was interrupted. */
int dissolve_tile(int row, int col, int tile)
{
    uint8_t state = 1;
    int x0 = tiles.origin_x + col * 16, y0 = tiles.origin_y + row * 16;
    if (video_2bit()) x0 = (tiles.origin_x & ~3) + col * 16;
    for (int step = 0; step < 0x100; step++) {
        int x = 0, y = 0;
        if (step != 0) {
            x = state >> 4;
            y = state & 0x0F;
            int carry = state & 1;
            state >>= 1;
            if (carry) state ^= 0xB8;
        }
        if (y0 + y >= 0 && y0 + y < SCREEN_H && x0 + x >= 0 && x0 + x < SCREEN_W)
            for (int r = video_row(y0 + y), n = video_row_count(y0 + y, 0); n > 0 && r < video_rows(); n--, r++)
                video_fb[r][x0 + x] = tile_pixel(tile, x, y);
        if ((step & 7) == 7 && step != 0xFF) {
            if (map == Map::DEMO) {
                int r = intro.demo_step(1);
                if (r) return r;
            } else {
                world_tick();
            }
        }
    }
    return 0;
}
