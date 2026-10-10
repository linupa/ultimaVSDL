/* tiles.c - tile set loading, tile drawing and the driver's tile animation (EGA.DRV 0x48, 0x51,
 * 0x60). */
#include <stdlib.h>
#include <string.h>
#include "data.h"
#include "lzw.h"
#include "tiles.h"
#include "video.h"

uint8_t tileset[TILE_COUNT * TILE_BYTES];

static uint16_t drv_rand = 0x7664;   /* EGA.DRV cs:1F94, the driver's own generator (initial value from the file) */

static uint8_t drv_random(void)
{
    uint16_t a = (uint16_t)(drv_rand + 0x9248);
    a = (uint16_t)(a >> 3 | a << 13);   /* ror 3 */
    drv_rand = (uint16_t)((a ^ 0x9248) + 0x11);
    return (uint8_t)drv_rand;
}

/* The bytes of one tile: 128 in the EGA driver's planar layout, 64 for CGA and Hercules, which
 * use TILES.4 as it is (two bits per pixel, four pixels per byte, the left one highest). */
#define TWO_BIT video_2bit()
#define TB (TWO_BIT ? 64 : TILE_BYTES)

/* drv 0x48: TILES.16 is 4 bits per pixel, two pixels per byte, high nibble first. */
int load_tileset(const char *filename)
{
    size_t size;
    uint8_t *packed = lzw_load_file(filename, &size);
    if (!packed || size < (size_t)(TILE_COUNT * TB)) {
        free(packed);
        return 0;
    }
    memset(tileset, 0, sizeof tileset);
    if (TWO_BIT) {
        memcpy(tileset, packed, TILE_COUNT * 64);
        free(packed);
        return 1;
    }
    for (int t = 0; t < TILE_COUNT; t++)
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++) {
                uint8_t b = packed[t * TILE_BYTES + y * 8 + x / 2];
                int c = (x & 1) ? (b & 0x0F) : (b >> 4);
                for (int p = 0; p < 4; p++)
                    if (c & (1 << p))
                        tileset[t * TILE_BYTES + y * 8 + (x / 8) * 4 + p] |= (uint8_t)(0x80 >> (x % 8));
            }
    free(packed);
    return 1;
}

uint8_t tile_pixel(int tile, int x, int y)
{
    if (TWO_BIT) return (uint8_t)((tileset[(tile & (TILE_COUNT - 1)) * 64 + y * 4 + x / 4] >> (6 - 2 * (x & 3))) & 3);
    const uint8_t *row = &tileset[(tile & (TILE_COUNT - 1)) * TILE_BYTES + y * 8 + (x / 8) * 4];
    uint8_t c = 0;
    for (int p = 0; p < 4; p++)
        if (row[p] & (0x80 >> (x % 8))) c |= (uint8_t)(1 << p);
    return c;
}

/* drv 0x51 draws only 16x16 tiles (it returns if tile_width/height aren't 16), at
 * (tile_origin_x + col * 16, tile_origin_y + row * 16). The tile number is a full word.
 * CGA.DRV and HER.DRV round the origin down to 4 pixels. HER.DRV (0x10a3) makes 24 rows of the
 * 16: each pair of rows becomes the first, both OR-ed together, and the second. */
void draw_tile_at(int row, int col, int tile)
{
    if (dg.tile_width != 16 || dg.tile_height != 16) return;
    int x0 = dg.tile_origin_x + col * 16, y0 = dg.tile_origin_y + row * 16;
    if (TWO_BIT) x0 = (dg.tile_origin_x & ~3) + col * 16;
    if (video_mode() == VIDEO_HERC) {
        y0 = row * 24 + ((dg.tile_origin_y * 3) >> 1);
        for (int y = 0; y < 24; y++) {
            if (y0 + y < 0 || y0 + y >= VIDEO_ROWS) continue;
            int pair = y / 3 * 2, part = y % 3;
            for (int x = 0; x < 16; x++) {
                if (x0 + x < 0 || x0 + x >= SCREEN_W) continue;
                uint8_t a = tile_pixel(tile, x, pair), b = tile_pixel(tile, x, pair + 1);
                video_draw[y0 + y][x0 + x] = part == 0 ? a : part == 2 ? b : (uint8_t)(a | b);
            }
        }
        return;
    }
    for (int y = 0; y < 16; y++) {
        if (y0 + y < 0 || y0 + y >= SCREEN_H) continue;
        for (int x = 0; x < 16; x++)
            if (x0 + x >= 0 && x0 + x < SCREEN_W) video_draw[y0 + y][x0 + x] = tile_pixel(tile, x, y);
    }
}

/* drv 0x60 with carry set (EGA.DRV 0x24d6): a moongate rising. Scratch tile 0x116 becomes grass
 * (tile 5; floor 0x44 if location is 0xFF) with the top `frame` rows of the moongate (0xDC) at
 * its bottom, is drawn, and is restored. */
void shimmer_tiles(int row, int col, int frame)
{
    uint8_t saved[TILE_BYTES], *scratch = &tileset[0x116 * TB];
    int row_bytes = TB / 16;
    if (frame < 0) frame = 0;
    if (frame > 16) frame = 16;
    memcpy(saved, scratch, (size_t)TB);
    memcpy(scratch, &tileset[(dg.location == 0xFF ? 0x44 : 5) * TB], (size_t)TB);
    memcpy(scratch + TB - frame * row_bytes, &tileset[0xDC * TB], (size_t)(frame * row_bytes));
    draw_tile_at(row, col, 0x116);
    memcpy(scratch, saved, (size_t)TB);
}

static uint8_t *tb(int tile) { return &tileset[tile * TB]; }

/* Scroll a tile down one pixel row, the last row wrapping to the top. */
static void scroll_down(int tile)
{
    uint8_t last[8];
    int rb = TB / 16;
    memcpy(last, tb(tile) + TB - rb, (size_t)rb);
    memmove(tb(tile) + rb, tb(tile), (size_t)(TB - rb));
    memcpy(tb(tile), last, (size_t)rb);
}

/* Where mask's plane 3 is set (colours 8-15), replace the tile's pixels with the background's.
 * The two-bit drivers use the mask tile's bits as they are (its colour 3 is the opening). */
static void composite(int tile, int mask, int back)
{
    uint8_t *s = tb(tile), *m = tb(mask), *w = tb(back);
    if (TWO_BIT) {
        for (int i = 0; i < 64; i++) s[i] = (uint8_t)((s[i] & ~m[i]) | (w[i] & m[i]));
        return;
    }
    for (int i = 0; i < TILE_BYTES; i += 4) {
        uint8_t keep = (uint8_t)~m[i + 3];
        for (int p = 0; p < 4; p++) s[i + p] = (uint8_t)((s[i + p] & keep) | (w[i + p] & m[i + 3]));
    }
}

/* Same, with the mask's plane 3 inverted (the original inverts it in place and back again). */
static void composite_inverted(int tile, int mask, int back)
{
    uint8_t *s = tb(tile), *m = tb(mask), *w = tb(back);
    if (TWO_BIT) {
        for (int i = 0; i < 64; i++) s[i] = (uint8_t)((s[i] & m[i]) | (w[i] & ~m[i]));
        return;
    }
    for (int i = 0; i < TILE_BYTES; i += 4) {
        uint8_t m3 = (uint8_t)~m[i + 3];
        for (int p = 0; p < 4; p++) s[i + p] = (uint8_t)((s[i + p] & ~m3) | (w[i + p] & m3));
    }
}

/* tile ^= field & mask, over all the tile's bytes. */
static void xor_masked(int tile, int mask, int field)
{
    uint8_t *s = tb(tile), *m = tb(mask), *f = tb(field);
    for (int i = 0; i < TB; i++) s[i] ^= (uint8_t)(f[i] & m[i]);
}

/* Swap 4 bytes (all planes of one half row) with the same half two rows below (EGA.DRV 0x24b4). */
static void swap_half(int off)
{
    for (int i = 0; i < 4; i++) {
        uint8_t t = tileset[off + i];
        tileset[off + i] = tileset[off + 0x10 + i];
        tileset[off + 0x10 + i] = t;
    }
}

/* CGA.DRV 0x1884: swap one byte (four pixels) with the one two rows below. */
static void swap_byte(int off)
{
    uint8_t t = tileset[off];
    tileset[off] = tileset[off + 8];
    tileset[off + 8] = t;
}

/* drv 0x60 with CF clear in CGA.DRV (0x1451) and HER.DRV (0x17ab, the same code): the EGA
 * driver's steps on two-bit tiles. The fields get one random word per position: 0x1E8 and
 * 0x1EA its high bits (colour 2), 0x1E9 and 0x1EB its low bits (colour 1). */
static void animate_tiles_2bit(void)
{
    for (int i = 0; i < 64; i += 2) {
        drv_random();
        uint8_t lo = (uint8_t)drv_rand, hi = (uint8_t)(drv_rand >> 8);
        tb(0x1E8)[i] = tb(0x1EA)[i] = lo & 0xAA;
        tb(0x1E8)[i + 1] = tb(0x1EA)[i + 1] = hi & 0xAA;
        tb(0x1E9)[i] = tb(0x1EB)[i] = lo & 0x55;
        tb(0x1E9)[i + 1] = tb(0x1EB)[i + 1] = hi & 0x55;
    }
    scroll_down(1);
    scroll_down(2);
    scroll_down(3);
    scroll_down(0x8F);
    for (int i = 0; i < 16; i++) composite(0x60 + i, 0x70 + i, 3);
    composite(0x108, 0x100, 0x1EB);
    composite(0x1B4, 0x100, 0x1E9);
    for (int i = 0; i < 4; i++) composite(0x34 + i, 0xD0 + i, 3);
    for (int i = 0; i < 4; i++) composite_inverted(0xE4 + i, 0xD0 + i, 3);
    for (int i = 0; i < 4; i++) xor_masked(0xB0 + i, 0xC0 + i, 0x1EA);
    for (int i = 0; i < 4; i++) xor_masked(0xBC + i, 0xCC + i, 0x1EA);
    xor_masked(0xDE, 0xC2, 0x1EB);
    if (drv_rand & 0x01) { swap_byte(0x481); swap_byte(0x482); }
    if (drv_rand & 0x02) swap_byte(0x509);
    if (drv_rand & 0x04) { swap_byte(0x541); swap_byte(0x542); }
    if (drv_rand & 0x08) { swap_byte(0xF81); swap_byte(0xF82); }
    if (drv_rand & 0x10) swap_byte(0x4846);
    if (drv_rand & 0x20) swap_byte(0x48C5);
    if (drv_rand & 0x40) swap_byte(0x4B46);
    if (drv_rand & 0x01) swap_byte(0x4BC5);
}

/* drv 0x60 with CF clear (EGA.DRV 0x1f9d), once per turn from animate_objects. */
void animate_tiles_alt(void)
{
    if (TWO_BIT) {
        animate_tiles_2bit();
        return;
    }
    /* Fields 0x1E8-0x1EB: new random pixels each turn in fixed planes, one random byte shared
     * by the four tiles at each position (colours 10, 13, 12 and 9). */
    for (int i = 0; i < TILE_BYTES; i += 4) {
        uint8_t r = drv_random();
        uint8_t *a = tb(0x1E8) + i, *b = tb(0x1E9) + i, *c = tb(0x1EA) + i, *d = tb(0x1EB) + i;
        a[3] = r; b[3] = r; c[3] = r; d[3] = r;
        b[2] = r; c[2] = r;
        a[1] = r;
        b[0] = r; d[0] = r;
    }

    /* Water (tiles 1-3) and tile 0x8F flow down one pixel. */
    scroll_down(1);
    scroll_down(2);
    scroll_down(3);
    scroll_down(0x8F);

    /* Shores 0x60-0x6F show the moving water (tile 3) through masks 0x70-0x7F. */
    for (int i = 0; i < 16; i++) composite(0x60 + i, 0x70 + i, 3);
    composite(0x108, 0x100, 0x1EB);
    composite(0x1B4, 0x100, 0x1E9);
    for (int i = 0; i < 4; i++) composite(0x34 + i, 0xD0 + i, 3);
    for (int i = 0; i < 4; i++) composite_inverted(0xE4 + i, 0xD0 + i, 3);

    /* Fire/energy flicker on 0xB0-0xB3, 0xBC-0xBF and 0xDE (XOR, so it accumulates turn to turn). */
    for (int i = 0; i < 4; i++) xor_masked(0xB0 + i, 0xC0 + i, 0x1EA);
    for (int i = 0; i < 4; i++) xor_masked(0xBC + i, 0xCC + i, 0x1EA);
    xor_masked(0xDE, 0xC2, 0x1EB);

    /* Flags flutter: swap rows 0/2 or 1/3 of some tiles, chosen by bits of the generator state. */
    if (drv_rand & 0x01) { swap_half(0x900); swap_half(0x904); }
    if (drv_rand & 0x02) swap_half(0xA10);
    if (drv_rand & 0x04) { swap_half(0xA80); swap_half(0xA84); }
    if (drv_rand & 0x08) { swap_half(0x1F00); swap_half(0x1F04); }
    if (drv_rand & 0x10) swap_half(0x908C);
    if (drv_rand & 0x20) swap_half(0x9188);
    if (drv_rand & 0x40) swap_half(0x968C);
    if (drv_rand & 0x01) swap_half(0x9788);
}

void free_tileset(void) {}   /* the port keeps the tile set in memory */

/* drv 0x6C mode 2 (EGA.DRV 0x2b10): the town clock. Tiles 0xFA and 0xFB are the clock's two
 * frames; the hands are wiped from rows 2-7 (planes 0, 1 and 3; plane 2 is left alone) and one
 * white pixel is set for the hour, then one for the minute (in twelfths), around the rim, at
 * the same place in both frames. */
static void clock_wipe(uint8_t *t)
{
    static const uint8_t keep[6] = {0xFE, 0x3F, 0xFE, 0xBF, 0xFE, 0x3F};
    if (TWO_BIT) {   /* CGA.DRV 0x1df2: the same six pixels (and the one between), as colour 2 */
        for (int r = 0; r < 3; r++) t[r * 4 + 1] = (uint8_t)((t[r * 4 + 1] & 0xFC) | 2);
        t[2] = 0xA0;
        t[6] = (uint8_t)((t[6] & 0xCF) | 0x20);
        t[10] = 0xA0;
        return;
    }
    for (int p = 0; p < 4; p++) {
        if (p == 2) continue;
        for (int i = 0; i < 6; i++) t[i * 4 + p] &= keep[i];
    }
}

static void clock_hand(int h)
{
    static const uint8_t row[12]  = {0x10, 0x10, 0x10, 0x18, 0x20, 0x20, 0x20, 0x20, 0x20, 0x18, 0x10, 0x10};
    static const uint8_t half[12] = {4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0};
    static const uint8_t bits[12] = {0x80, 0x40, 0x40, 0x40, 0x40, 0x40, 0x80, 0x01, 0x01, 0x01, 0x01, 0x01};
    static const uint8_t bits2[12] = {0xC0, 0x30, 0x30, 0x30, 0x30, 0x30, 0xC0, 0x03, 0x03, 0x03, 0x03, 0x03};
    if (TWO_BIT) {   /* tables at CGA.DRV 0x1e42, 0x1e5a, 0x1e72: the same pixels */
        uint8_t *t = tb(0xFA) + row[h] / 2 + (half[h] ? 2 : 1);
        t[0] |= bits2[h];
        t[64] |= bits2[h];
        return;
    }
    uint8_t *t = tb(0xFA) + row[h] + half[h];
    for (int p = 0; p < 4; p++) {
        t[p] |= bits[h];
        t[p + TILE_BYTES] |= bits[h];
    }
}

void patch_tiles_c(void)
{
    int hour = dg.hour, minute = dg.minute;
    int rows2 = TWO_BIT ? 8 : 0x10;
    clock_wipe(tb(0xFA) + rows2);
    clock_wipe(tb(0xFB) + rows2);
    if (hour >= 12) hour -= 12;
    clock_hand(hour % 12);            /* (the original trusts the hour to be below 24) */
    clock_hand(minute / 5 % 12);
}

/* drv 0x6C modes 0 and 1 (patch_tiles_b): mode 0 saves row 14 of tiles 0xC8 and 0x117 and
 * replaces it with row 0; mode 1 puts the saved rows back. Used while a fight is on a two-way
 * ladder. */
void ladder_tiles_patch(int restore)
{
    static uint8_t saved[2][8];
    static const int tiles[2] = {0xC8, 0x117};
    int rb = TB / 16;
    for (int i = 0; i < 2; i++) {
        uint8_t *t = tb(tiles[i]);
        if (restore) {
            memcpy(t + 14 * rb, saved[i], (size_t)rb);
        } else {
            memcpy(saved[i], t + 14 * rb, (size_t)rb);
            memcpy(t + 14 * rb, t, (size_t)rb);
        }
    }
}

/* drv 0x6C modes 3 and 4 (the caller passes n + 3).
 * Mode 3 (EGA.DRV 0x2bda) swaps colour planes 1 and 2 of the tiles dungeon rooms are built from,
 * which trades green for red (and their mixes): tiles 5, 0x1E, 0x1F, 0x4C, 0xCA, 0x20-0x26,
 * 0x30-0x37 and 0x60-0x6F. Calling it again swaps them back.
 * Mode 4 (0x2c4e) recolours 22 tiles for the last scene through a colour table; the original
 * unpacks the whole tile set to one nibble per pixel for it and packs it again.
 * CGA.DRV (0x1ea2, 0x1f22) and HER.DRV swap the two bits of every pixel of the same tiles in
 * both modes: colours 1 and 2 change places. */
static void swap_pixel_bits(int tile, int count)
{
    uint8_t *t = tb(tile);
    for (int i = 0; i < count * 64; i++) t[i] = (uint8_t)((t[i] & 0x55) << 1 | (t[i] & 0xAA) >> 1);
}

void patch_tiles(int n)
{
    static const uint16_t last_scene[22] = {
        0x44, 0x5C, 0x5D, 0x90, 0x92, 0x94, 0x96, 0x9B, 0xAB, 0xAC, 0xAF,
        0xB0, 0xB1, 0xBF, 0xDC, 0x108, 0x10E, 0x11A, 0x138, 0x139, 0x13A, 0x13B,
    };
    static const struct { uint16_t first, count; } runs[8] = {
        {5, 1}, {0x1E, 1}, {0x1F, 1}, {0x4C, 1}, {0xCA, 1}, {0x20, 7}, {0x30, 8}, {0x60, 16},
    };
    if (n == 1) {
        static const uint8_t colour[16] = {0, 5, 4, 4, 2, 1, 2, 7, 8, 0xC, 0xC, 0xC, 0xA, 9, 0xE, 0xF};
        for (int i = 0; i < 22; i++) {
            uint8_t *t = tb(last_scene[i]);
            if (TWO_BIT) {
                swap_pixel_bits(last_scene[i], 1);
                continue;
            }
            for (int g = 0; g < TILE_BYTES; g += 4) {
                uint8_t out[4] = {0, 0, 0, 0};
                for (int bit = 0x80; bit; bit >>= 1) {
                    int c = 0;
                    for (int p = 0; p < 4; p++)
                        if (t[g + p] & bit) c |= 1 << p;
                    c = colour[c];
                    for (int p = 0; p < 4; p++)
                        if (c & (1 << p)) out[p] |= (uint8_t)bit;
                }
                memcpy(t + g, out, 4);
            }
        }
        return;
    }
    for (int r = 0; r < 8; r++) {
        uint8_t *t = tb(runs[r].first);
        if (TWO_BIT) {
            swap_pixel_bits(runs[r].first, runs[r].count);
            continue;
        }
        for (int i = 0; i < runs[r].count * TILE_BYTES; i += 4) {
            uint8_t a = t[i + 1];
            t[i + 1] = t[i + 2];
            t[i + 2] = a;
        }
    }
}
