/* gfx.cpp - drawing wrappers, random numbers and the screen frame. */
#include "gfx.h"
#include "pictures.h"
#include "strings.h"
#include "text.h"
#include "video.h"

void Gfx::set_draw_color(uint16_t color)
{
    if (color != 0xFFFF) color &= video_type == 0 ? 3 : 0x0F;
    draw_color = color;
    if (color != 0xFFFF) video_set_color((uint8_t)color);
}

/* The driver sorts the corners (0000:08e6); video_fill_rect does the same. */
void Gfx::fill_rect(int y2, int x2, int y1, int x1) { video_fill_rect(x1, y1, x2, y2, 0); }
void Gfx::xor_fill_rect(int y2, int x2, int y1, int x1) { video_fill_rect(x1, y1, x2, y2, 1); }

/* Clipping, used while clip_enabled is set (only the dungeon view sets it, standing in a
 * doorway). The rectangle is clip_x1..clip_x2, clip_y1..clip_y2. */
int Gfx::clip_outcode(int x, int y)   /* 0a22 */
{
    int code = 0;
    if (x < (int16_t)clip_x1) code |= 1;
    if (x > (int16_t)clip_x2) code |= 2;
    if (y > (int16_t)clip_y2) code |= 4;
    if (y < (int16_t)clip_y1) code |= 8;
    return code;
}

/* clip_line_step (0991): the point where the line from A (inside, or nearer) to B (outside)
 * leaves the rectangle, found by halving: the midpoint replaces B if it is outside, A if inside,
 * until it lies on an edge or the two are a pixel apart. */
void Gfx::clip_line_step(int *ax, int *ay, int bx, int by)
{
    int x = *ax, y = *ay;
    for (;;) {
        int mx = (x + bx) / 2, my = (y + by) / 2;   /* idiv for negative sums, shr otherwise */
        if (x + bx >= 0) mx = (x + bx) >> 1;
        if (y + by >= 0) my = (y + by) >> 1;
        int dx = x - mx, dy = y - my;
        if (clip_outcode(mx, my) != 0) {
            bx = mx;
            by = my;
            continue;
        }
        x = mx;
        y = my;
        if ((x == (int16_t)clip_x1 || x == (int16_t)clip_x2) && (y == (int16_t)clip_y1 || y == (int16_t)clip_y2)) break;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if ((dx > dy ? dx : dy) < 1) break;
    }
    *ax = x;
    *ay = y;
}

/* clip_line (0935): 0 if the line is wholly on one outer side of the rectangle. Otherwise each
 * end that is outside is moved to where the line enters. (A line passing wholly outside but
 * through two different sides still gets drawn, as some stub near the rectangle.) */
int Gfx::clip_line(int *x1, int *y1, int *x2, int *y2)
{
    int c1 = clip_outcode(*x1, *y1), c2 = clip_outcode(*x2, *y2);
    if (c1 == 0 && c2 == 0) return 1;
    if (c1 & c2) return 0;
    if (c1 != 0) {
        int ax = *x2, ay = *y2;
        clip_line_step(&ax, &ay, *x1, *y1);
        *x1 = ax;
        *y1 = ay;
    }
    if (c2 != 0) {
        int ax = *x1, ay = *y1;
        clip_line_step(&ax, &ay, *x2, *y2);
        *x2 = ax;
        *y2 = ay;
    }
    return 1;
}

/* draw_line_impl (0b2d): remembers the end point in line_last_x/y, clips, then draws a
 * horizontal, vertical, single-pixel or general line. */
void Gfx::line_impl(int x1, int y1, int x2, int y2)
{
    line_last_x = (uint16_t)x2;
    line_last_y = (uint16_t)y2;
    if (clip_enabled != 0 && !clip_line(&x1, &y1, &x2, &y2)) return;
    video_line(x1, y1, x2, y2);
}

void Gfx::draw_line(int y2, int x2, int y1, int x1) { line_impl(x1, y1, x2, y2); }
void Gfx::draw_line_b(int y, int x) { line_impl((int16_t)line_last_x, (int16_t)line_last_y, x, y); }

/* The horizontal and vertical line calls don't trim: with clipping on they are dropped unless
 * (hline, 0ccd) the line reaches into the rectangle's columns - the row isn't looked at - or
 * (vline, 0d2b) lies wholly within its rows. */
void Gfx::draw_hline(int x2, int y, int x1)
{
    line_last_x = (uint16_t)x2;
    if (clip_enabled != 0) {
        int hi = x1 > x2 ? x1 : x2, lo = x1 > x2 ? x2 : x1;
        if (hi < (int16_t)clip_x1 || lo > (int16_t)clip_x2 || hi < 0 || hi > 0x13F || lo > 0x13F) return;
    }
    video_line(x1, y, x2, y);
}

void Gfx::draw_vline(int y2, int y1, int x)
{
    line_last_y = (uint16_t)y2;
    if (clip_enabled != 0) {
        int lo = y1 < y2 ? y1 : y2, hi = y1 < y2 ? y2 : y1;
        if (lo < (int16_t)clip_y1 || hi > (int16_t)clip_y2 || lo < 0 || lo > 0xC7 || hi > 0xC7) return;
    }
    video_line(x, y1, x, y2);
}

/* The start of intro_main (INTRO.OVL), after the driver is loaded: the four-colour drivers (CGA,
 * Hercules) use the picture files ending in ".4" (Pictures) and keep the colour numbers in
 * DATA.OVL; the sixteen-colour ones get theirs set here. Then the fonts: 16 x 12 ones for Hercules. Returns 0 if a font is missing. */
int Gfx::video_start(void)
{
    video_set_mode(video_type);
    if (video_type == 0 || video_type == 3) {
        pictures.use_four_colour_files();
    } else {
        set_ega_colors();
    }
    if (video_type == 3) return load_font(0, strings[Strings::ibm_hcs_3182]) >= 0 && load_font(1, strings[Strings::runes_hcs_318A]) >= 0;   /* ibm.hcs, runes.hcs */
    return load_font(0, strings[Strings::ibm_ch_3194]) >= 0 && load_font(1, strings[Strings::runes_ch_319B]) >= 0;                             /* ibm.ch, runes.ch */
}

void Gfx::set_ega_colors(void)
{
    color_frame = 1;
    color_fire = 4;
    color_text = 0x0F;
    color_sleep = 5;
    color_poison = 2;
    color_chest = 0x0E;
    color_ladder = 7;
}

void Gfx::draw_screen_frame(void)
{
    set_draw_color(0);
    fill_rect(199, 0x13F, 0, 0);
    set_draw_color(color_frame);
    fill_rect(6, 0x13F, 0, 0);
    fill_rect(0xBF, 0xBF, 0xB9, 0);
    fill_rect(0xBF, 6, 0, 0);
    fill_rect(0xBF, 0xBF, 0, 0xB9);
    fill_rect(0x57, 0x13F, 0, 0x139);
    fill_rect(0x57, 0x138, 0x50, 0xC0);
    fill_rect(0x3F, 0x138, 0x39, 0xC0);
    set_text_color((uint8_t)color_frame);
    set_cursor(0, 0);
    put_char(0x7B);
    set_cursor(0, 0x27);
    put_char(0x7C);
    set_cursor(0x17, 0);
    put_char(0x7D);
    set_draw_color(color_text);
    draw_line(0xB8, 7, 7, 7);
    draw_line_b(0xB8, 0xB8);
    draw_line_b(7, 0xB8);
    draw_line_b(7, 7);
    draw_line(0x57, 0xBF, 0xBF, 0xBF);
    draw_line_b(0x57, 0x13F);
    draw_line(7, 0x138, 7, 0xBF);
    draw_line_b(0x38, 0x138);
    draw_line_b(0x38, 0xBF);
    draw_line_b(7, 0xBF);
    draw_line(0x3F, 0x138, 0x3F, 0xBF);
    draw_line_b(0x50, 0x138);
    draw_line_b(0x50, 0xBF);
    draw_line_b(0x3F, 0xBF);
    set_text_color((uint8_t)color_text);
}
