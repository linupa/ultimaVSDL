/* gfx.h - the main program's drawing wrappers around the graphics driver (0000:0a70-0000:0fae),
 * plus the random number generator and the screen frame. Ghidra parameter order. */
#ifndef U5_GFX_H
#define U5_GFX_H

#include <stdint.h>

/* The drawing state. `gfx` is the one Gfx object.
 *
 * The functions below the class (set_draw_color, fill_rect, draw_line, ...) are what the game
 * calls: each just passes on to the method of the same name. */
class Gfx {
public:
    static Gfx &instance();

    uint16_t video_type = 0;         /* 0 CGA, 1 EGA, 2 Tandy, 3 Hercules: the driver in use */

    /* The colours things are drawn in, by what they are for. These are the four-colour values of
     * DATA.OVL; set_ega_colors replaces them for the 16-colour drivers. */
    uint16_t color_text = 3;         /* text, lines, flashes */
    uint16_t color_frame = 1;        /* the screen's frame; energy fields */
    uint16_t color_fire = 2;         /* fire fields and missiles */
    uint16_t color_poison = 1;       /* poison fields */
    uint16_t color_sleep = 2;        /* sleep fields */
    uint16_t color_chest = 3;        /* chests and doors on the maps; the moons */
    uint16_t color_ladder = 3;       /* ladders on the maps; the sun */

    uint16_t draw_color = 0;         /* the colour being drawn in; 0xFFFF = none */
    uint16_t clip_enabled = 0;       /* lines and fills keep inside the rectangle below when set */
    uint16_t clip_x1 = 0, clip_x2 = 319, clip_y1 = 0, clip_y2 = 199;
    uint16_t line_last_x = 0, line_last_y = 0;   /* where the last line ended: draw_line_b goes on from there */

    void set_draw_color(uint16_t color);                 /* 0a70: masks to 0-15 (0-3 on CGA); 0xFFFF = none */
    void fill_rect(int y2, int x2, int y1, int x1);       /* 0aa6: pixels, inclusive */
    void xor_fill_rect(int y2, int x2, int y1, int x1);   /* 0b86 */
    void draw_line(int y2, int x2, int y1, int x1);       /* 0b10 */
    void draw_line_b(int y, int x);                       /* 0f90: from the end of the last line */
    void draw_hline(int x2, int y, int x1);              /* 0c9c */
    void draw_vline(int y2, int y1, int x);              /* 0cf2 */
    void draw_screen_frame(void);                         /* 637e: border around the view and side panels */
    void set_ega_colors(void);                            /* intro_main's 16-colour role colours */
    int  video_start(void);                 /* intro_main's start: picture names, colours and fonts for video_type */

    Gfx(const Gfx &) = delete;
    Gfx &operator=(const Gfx &) = delete;

private:
    Gfx() = default;

    int  clip_outcode(int x, int y);
    void clip_line_step(int *ax, int *ay, int bx, int by);
    int  clip_line(int *x1, int *y1, int *x2, int *y2);
    void line_impl(int x1, int y1, int x2, int y2);
};

extern Gfx &gfx;

inline void set_draw_color(uint16_t color) { gfx.set_draw_color(color); }
inline void fill_rect(int y2, int x2, int y1, int x1) { gfx.fill_rect(y2, x2, y1, x1); }
inline void xor_fill_rect(int y2, int x2, int y1, int x1) { gfx.xor_fill_rect(y2, x2, y1, x1); }
inline void draw_line(int y2, int x2, int y1, int x1) { gfx.draw_line(y2, x2, y1, x1); }
inline void draw_line_b(int y, int x) { gfx.draw_line_b(y, x); }
inline void draw_hline(int x2, int y, int x1) { gfx.draw_hline(x2, y, x1); }
inline void draw_vline(int y2, int y1, int x) { gfx.draw_vline(y2, y1, x); }
inline void draw_screen_frame(void) { gfx.draw_screen_frame(); }
inline void set_ega_colors(void) { gfx.set_ega_colors(); }
inline int video_start(void) { return gfx.video_start(); }


#endif
