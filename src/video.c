/* video.c - the graphics drivers' basic functions on a framebuffer. See video.h. */
#include <stdio.h>
#include <string.h>
#include "video.h"

uint8_t video_fb[VIDEO_ROWS][SCREEN_W];
uint8_t video_back[VIDEO_ROWS][SCREEN_W];
uint8_t (*video_draw)[SCREEN_W] = video_fb;

static int mode = VIDEO_EGA;
#define HERC (mode == VIDEO_HERC)

void video_set_mode(int type) { mode = type; }
int video_mode(void) { return mode; }
int video_2bit(void) { return mode == VIDEO_CGA || mode == VIDEO_HERC; }
int video_rows(void) { return HERC ? 300 : SCREEN_H; }
int video_row(int y) { return HERC ? (y * 3) >> 1 : y; }
int video_row_count(int y, int exclusive) { return !HERC ? 1 : exclusive ? 1 + (y & 1) : 2; }

void video_out_size(int *w, int *h)
{
    *w = HERC ? SCREEN_W * 2 : SCREEN_W;
    *h = video_rows();
}

static uint8_t (*page(int n))[SCREEN_W] { return n ? video_back : video_fb; }

void video_set_draw_page(int n) { video_draw = page(n); }
int video_get_draw_page(void) { return video_draw == video_back; }
void video_clear_back(void) { memset(video_back, 0, sizeof video_back); }

void video_copy_page(int from, int to)
{
    if ((from != 0) != (to != 0)) memcpy(page(to), page(from), sizeof video_fb);
}

const uint8_t video_palette[16][3] = {
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
    {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
    {0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
    {0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF},
};

static uint8_t cur_color;   /* the driver's own copy (cs:312), set by drv 0x2D */

void video_set_color(uint8_t color) { cur_color = color & (video_2bit() ? 3 : 0x0F); }
uint8_t video_get_color(void) { return cur_color; }

static int clip(int *x1, int *y1, int *x2, int *y2)
{
    if (*x1 > *x2) { int t = *x1; *x1 = *x2; *x2 = t; }
    if (*y1 > *y2) { int t = *y1; *y1 = *y2; *y2 = t; }
    if (*x1 < 0) *x1 = 0;
    if (*y1 < 0) *y1 = 0;
    if (*x2 >= SCREEN_W) *x2 = SCREEN_W - 1;
    if (*y2 >= SCREEN_H) *y2 = SCREEN_H - 1;
    return *x1 <= *x2 && *y1 <= *y2;
}

/* HER.DRV 0x51e: a filled row is two rows deep; XOR-ed, one row, or two for odd y, so that
 * neighbouring rows don't undo each other. */
void video_fill_rect(int x1, int y1, int x2, int y2, int xor)
{
    if (!clip(&x1, &y1, &x2, &y2)) return;
    for (int y = y1; y <= y2; y++)
        for (int r = video_row(y), n = video_row_count(y, xor); n > 0 && r < video_rows(); n--, r++)
            for (int x = x1; x <= x2; x++)
                video_draw[r][x] = xor ? (uint8_t)(video_draw[r][x] ^ cur_color) : cur_color;
}

void video_plot(int x, int y)
{
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;
    for (int r = video_row(y), n = video_row_count(y, 0); n > 0 && r < video_rows(); n--, r++)
        video_draw[r][x] = cur_color;
}

/* drv 0x33: with L the longer and m the shorter of |dx|, |dy| (x counts as the longer when they
 * are equal), d starts at 2m - L; L + 1 pixels are plotted from (x1, y1); after each, d < 0 steps
 * along the longer axis only and adds 2m, otherwise steps diagonally and adds 2m - 2L. */
void video_line(int x1, int y1, int x2, int y2)
{
    int sx = x2 < x1 ? -1 : 1, sy = y2 < y1 ? -1 : 1;
    int dx = x2 < x1 ? x1 - x2 : x2 - x1, dy = y2 < y1 ? y1 - y2 : y2 - y1;
    int x_major = dx >= dy, longer = x_major ? dx : dy, shorter = x_major ? dy : dx;
    int d = 2 * shorter - longer;
    for (int n = longer + 1; n > 0; n--) {
        video_plot(x1, y1);
        if (d < 0) {
            if (x_major) x1 += sx;
            else y1 += sy;
            d += 2 * shorter;
        } else {
            x1 += sx;
            y1 += sy;
            d += 2 * shorter - 2 * longer;
        }
    }
}

int video_no_text_yet = 1;

void video_draw_char(int col, int row, const uint8_t *glyph, uint8_t fg, uint8_t bg)
{
    video_no_text_yet = 0;
    if (col < 0 || col >= SCREEN_W / 8 || row < 0 || row >= SCREEN_H / 8) return;
    if (HERC) {   /* HER.DRV 0x1282: each dot of the glyph takes that dot of the colour's pair */
        for (int y = 0; y < 12; y++) {
            uint8_t *p = &video_draw[row * 12 + y][col * 8];
            for (int x = 0; x < 8; x++) {
                int g = (glyph[y * 2 + (x >> 2)] >> (6 - 2 * (x & 3))) & 3;
                p[x] = (uint8_t)(((g & fg) | (~g & bg)) & 3);
            }
        }
        return;
    }
    int mask = video_2bit() ? 3 : 0x0F;
    for (int y = 0; y < 8; y++) {
        uint8_t bits = glyph[y], *p = &video_draw[row * 8 + y][col * 8];
        for (int x = 0; x < 8; x++)
            p[x] = (uint8_t)(((bits & (0x80 >> x)) ? fg : bg) & mask);
    }
}

/* HER.DRV 0x8b6: the rectangle becomes rows 3*y1/2 to 3*y2/2 rounded up, the distance 3*dy/2. */
void video_scroll_rect(int x1, int y1, int x2, int y2, int dy)
{
    if (!clip(&x1, &y1, &x2, &y2) || dy == 0) return;
    int n = dy < 0 ? -dy : dy;
    if (HERC) {
        y1 = (y1 * 3) >> 1;
        y2 = (y2 * 3 + 1) >> 1;
        n = (n * 3) >> 1;
    }
    int w = x2 - x1 + 1, h = y2 - y1 + 1;
    if (n > h) n = h;
    if (dy < 0)
        for (int y = y1; y <= y2 - n; y++) memcpy(&video_draw[y][x1], &video_draw[y + n][x1], (size_t)w);
    else
        for (int y = y2; y >= y1 + n; y--) memcpy(&video_draw[y][x1], &video_draw[y - n][x1], (size_t)w);
    int fy1 = dy < 0 ? y2 - n + 1 : y1;
    for (int y = fy1; y < fy1 + n; y++) memset(&video_draw[y][x1], 0, (size_t)w);
}

void video_clear(void) { memset(video_fb, 0, sizeof video_fb); }

/* EGA.DRV copies whole bytes of 8 pixels; the other two mask the ends. Hercules rows as above. */
void video_copy_rect(int x1, int y1, int x2, int y2, int from, int to)
{
    if ((from != 0) == (to != 0) || !clip(&x1, &y1, &x2, &y2)) return;
    if (!video_2bit()) {
        x1 &= ~7;
        x2 |= 7;
    }
    if (HERC) {
        y1 = (y1 * 3) >> 1;
        y2 = (y2 * 3 + 1) >> 1;
    }
    for (int y = y1; y <= y2; y++) memcpy(&page(to)[y][x1], &page(from)[y][x1], (size_t)(x2 - x1 + 1));
}

/* CGA.DRV sets palette 1 and then background 0 with the intensity bit clear. */
static const uint8_t cga_palette[4][3] = {{0, 0, 0}, {0x00, 0xAA, 0xAA}, {0xAA, 0x00, 0xAA}, {0xAA, 0xAA, 0xAA}};

/* Hercules: the driver shows colours 1 and 2 as one lit dot of each pair, so anything grey is
 * fine vertical stripes. A monochrome monitor ran them together; exact dots on a modern screen
 * look like a barcode. Unless video_herc_raw is set each dot is blended with its neighbours
 * (1:2:1), which turns the stripes into an even half tone and leaves text sharp enough. */
int video_herc_raw;
uint8_t video_herc_phosphor[3] = {0x40, 0xFF, 0x60};   /* a green screen (P1/P39); see sdl_main -p */

void video_to_rgb(uint8_t *rgb)
{
    for (int y = 0; y < video_rows(); y++) {
        if (HERC) {
            uint8_t dot[SCREEN_W * 2 + 2];
            dot[0] = dot[SCREEN_W * 2 + 1] = 0;
            for (int x = 0; x < SCREEN_W; x++) {
                dot[1 + x * 2] = (video_fb[y][x] >> 1) & 1;
                dot[2 + x * 2] = video_fb[y][x] & 1;
            }
            for (int i = 1; i <= SCREEN_W * 2; i++, rgb += 3) {
                int level = video_herc_raw ? dot[i] * 4 : dot[i - 1] + 2 * dot[i] + dot[i + 1];
                for (int c = 0; c < 3; c++) rgb[c] = (uint8_t)(level * video_herc_phosphor[c] / 4);
            }
            continue;
        }
        for (int x = 0; x < SCREEN_W; x++, rgb += 3)
            memcpy(rgb, mode == VIDEO_CGA ? cga_palette[video_fb[y][x] & 3] : video_palette[video_fb[y][x] & 0x0F], 3);
    }
}

int video_save_ppm(const char *path)
{
    static uint8_t rgb[SCREEN_W * 2 * VIDEO_ROWS * 3];
    int w, h;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    video_out_size(&w, &h);
    video_to_rgb(rgb);
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    size_t n = fwrite(rgb, 1, (size_t)(w * h * 3), f);
    fclose(f);
    return n == (size_t)(w * h * 3) ? 0 : -1;
}
