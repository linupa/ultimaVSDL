/* video.h - the graphics driver (EGA.DRV, CGA.DRV or HER.DRV) as a plain framebuffer.
 *
 * The game draws through a loadable driver: far call to [ds:5350] with the function number as the
 * offset into the driver's jump table (see SESSION-NOTES "GRAPHICS DRIVER"). The port replaces the
 * driver with these functions; video_to_rgb() turns the screen into RGB for SDL or an image file.
 * The basic functions are here; pictures and effects are in picture.cpp, tiles in tiles.cpp. */
#ifndef U5_VIDEO_H
#define U5_VIDEO_H

#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 200      /* the game's coordinates */
#define VIDEO_ROWS 300    /* rows the pages hold: 200, or 300 on Hercules */

/* The driver being stood in for; the values are the game's video_type (ds:52C8).
 *   EGA   pixels are colour numbers 0-15.
 *   CGA   (CGA.DRV) mode 4, pixels 0-3: black, cyan, magenta, white.
 *   Hercules (HER.DRV) 640 x 300 in the middle of the 720 x 348 screen. The driver treats it as
 *         320 pixels of two bits (the left and right dot) and draws each of the game's rows y at
 *         row 3y/2, so a pixel here is 0-3 as well and the pages have 300 rows.
 * The Tandy driver (T1K.DRV) isn't ported. */
enum { VIDEO_CGA = 0, VIDEO_EGA = 1, VIDEO_TANDY = 2, VIDEO_HERC = 3 };

/* What differs between the drivers. The video_* functions below work on the pages and go through
 * the driver in use for anything that depends on it. */
class VideoDriver {
public:
    virtual ~VideoDriver() = default;
    virtual int type() const = 0;                       /* the game's video_type */
    virtual bool two_bit() const { return true; }       /* CGA or Hercules */
    uint8_t color_mask() const { return two_bit() ? 3 : 0x0F; }
    virtual int rows() const { return SCREEN_H; }       /* 200, or 300 on Hercules */
    virtual int out_width() const { return SCREEN_W; }  /* of the picture to_rgb makes */
    /* Hercules: row y of the game is drawn on rows row(y) and the one below, so odd and even rows
     * overlap; where that would matter (XOR, pictures) even rows get one row and odd rows two. */
    virtual int row(int y) const { return y; }
    virtual int row_count(int y, int exclusive) const { (void)y; (void)exclusive; return 1; }
    /* The page rows a rectangle from game row y1 to y2 covers, and a distance of n game rows. */
    virtual void rect_rows(int *y1, int *y2) const { (void)y1; (void)y2; }
    virtual int  distance(int n) const { return n; }
    /* EGA.DRV copies whole bytes of 8 pixels; the other two mask the ends. */
    virtual void copy_columns(int *x1, int *x2) const { (void)x1; (void)x2; }
    virtual void draw_char(uint8_t (*page)[SCREEN_W], int col, int row, const uint8_t *glyph, uint8_t fg, uint8_t bg) const;
    virtual void to_rgb(const uint8_t (*page)[SCREEN_W], uint8_t *rgb) const = 0;
};

class EgaDriver : public VideoDriver {
public:
    int type() const override { return VIDEO_EGA; }
    bool two_bit() const override { return false; }
    void copy_columns(int *x1, int *x2) const override;
    void to_rgb(const uint8_t (*page)[SCREEN_W], uint8_t *rgb) const override;
};

class CgaDriver : public VideoDriver {
public:
    int type() const override { return VIDEO_CGA; }
    void to_rgb(const uint8_t (*page)[SCREEN_W], uint8_t *rgb) const override;
};

class HerculesDriver : public VideoDriver {
public:
    int type() const override { return VIDEO_HERC; }
    int rows() const override { return 300; }
    int out_width() const override { return SCREEN_W * 2; }
    int row(int y) const override { return (y * 3) >> 1; }
    int row_count(int y, int exclusive) const override { return exclusive ? 1 + (y & 1) : 2; }
    void rect_rows(int *y1, int *y2) const override;
    int  distance(int n) const override { return (n * 3) >> 1; }
    void draw_char(uint8_t (*page)[SCREEN_W], int col, int row, const uint8_t *glyph, uint8_t fg, uint8_t bg) const override;
    void to_rgb(const uint8_t (*page)[SCREEN_W], uint8_t *rgb) const override;
};

void video_set_mode(int type);          /* picks the driver; anything but CGA or Hercules is EGA */
const VideoDriver &video_driver(void);
int  video_mode(void);
int  video_2bit(void);
int  video_rows(void);
int  video_row(int y);
int  video_row_count(int y, int exclusive);
void video_out_size(int *w, int *h);    /* of the picture video_to_rgb makes: 320x200 or 640x300 */

extern uint8_t video_fb[VIDEO_ROWS][SCREEN_W];   /* the screen (page 0) */
extern uint8_t video_back[VIDEO_ROWS][SCREEN_W]; /* the back page (page 1; EGA.DRV keeps it at A800) */
extern uint8_t (*video_draw)[SCREEN_W];        /* the page all drawing goes to */

/* drv 0x0F: choose the draw page (0 screen, 1 back). drv 0x06 clears the back page. */
void video_set_draw_page(int page);
int  video_get_draw_page(void);
void video_clear_back(void);
/* drv 0x1B / 0x18: copy a whole page, or the rectangle (x1,y1)-(x2,y2), from one page to the other.
 * The EGA rectangle copy works on whole bytes of 8 pixels, like the driver. */
void video_copy_page(int from, int to);
void video_copy_rect(int x1, int y1, int x2, int y2, int from, int to);
extern const uint8_t video_palette[16][3];     /* standard EGA 16-colour palette, RGB */

/* drv 0x2D: set the drawing colour used by the fill, line and plot functions. */
void video_set_color(uint8_t color);
uint8_t video_get_color(void);

/* drv 0x3F: fill the rectangle (x1,y1)-(x2,y2) inclusive, in pixels, with the current colour.
 * xor != 0 is the carry-flag-set mode: XOR the colour into the screen. Clipped to the screen. */
void video_fill_rect(int x1, int y1, int x2, int y2, int xor_mode);

/* drv 0x30: plot one pixel; drv 0x33/0x39/0x3C: line from (x1,y1) to (x2,y2) inclusive, in the
 * current colour (horizontal and vertical lines are the driver's 0x39 and 0x3C). */
void video_plot(int x, int y);
void video_line(int x1, int y1, int x2, int y2);

/* drv 0x5D: draw an 8x8 1-bit glyph (8 bytes, top row first, bit 7 = left pixel) at text cell
 * (col, row): set bits in fg, clear bits in bg. Opaque, like the driver. On Hercules the glyph
 * is 12 rows of 16 dots (24 bytes) and the colours choose which of each pair of dots show. */
void video_draw_char(int col, int row, const uint8_t *glyph, uint8_t fg, uint8_t bg);

/* drv 0x27: move the rectangle (x1,y1)-(x2,y2) by dy pixels (negative = up) and fill the uncovered
 * strip with colour 0 (the driver temporarily sets colour 0 and calls its fill). */
void video_scroll_rect(int x1, int y1, int x2, int y2, int dy);

/* Set until the first character is drawn (the drivers' flag, EGA.DRV cs:253D): while it is, the
 * dissolve (drv 0x66) crackles and a key stops it. */
extern int video_no_text_yet;

void video_clear(void);
extern int video_herc_raw;                       /* 1: Hercules dots exactly as lit; 0: blended like a monitor */
extern uint8_t video_herc_phosphor[3];           /* RGB of a lit Hercules dot: green by default */
void video_to_rgb(uint8_t *rgb);                 /* w * h * 3 bytes (video_out_size) */
int  video_save_ppm(const char *path);          /* 0 on success; for headless tests */

#endif
