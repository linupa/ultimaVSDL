/* look.h - LOOKOBJ.OVL: Look (tile and object descriptions from LOOK2.DAT, signs from SIGNS.DAT,
 * wells, fountains, clocks, the night sky) and the area map shown by a gem, the In Quas Wis
 * spell, the View scroll and crystal balls. Parameter order follows the Ghidra signatures.
 *
 * LOOK2.DAT: 256 word offsets for the terrain tiles, 256 for the object tiles (from 0x200), then
 * the texts. SIGNS.DAT: 33 word offsets by location (0 = no signs), then entries of (byte, level,
 * x, y, text); in the text, bytes with bit 7 are plain letters, the rest are rune-font frame and
 * letter glyphs, 0x29-0x31 whole frame rows (table ds:37BE), 0x0D waits for a key. */
#ifndef U5_LOOK_H
#define U5_LOOK_H

#include <stdint.h>

void print_tile_description(int tile);       /* a290 */
void look_well(uint8_t z, uint8_t y, uint8_t x);   /* a2d2 */
void look_town_fountain(void);               /* a3f2 */
void draw_sky_moon(int col, int phase);      /* a43c */
void draw_sky_shadowlord(int col, int phase);   /* a4dc */
void look_sky(void);                         /* a5f6 */
void look_tile(unsigned y, unsigned x, unsigned tile);   /* a792 */
void print_object_description(int tile);     /* a934 */
void print_sign_text(int offset);            /* a988: offset in text_buffer, -1 = the default sign */
void read_sign(uint8_t y, uint8_t x, uint8_t z);   /* aa74 */
void cmd_look(void);                         /* ac2c */
void map_cell_pos(int row, int col);         /* ad2c: where the area map's next glyph is drawn */
void draw_map_glyph(int row, int col, int tile);   /* b20e */
void show_area_map(int y, int x);            /* b38c */

#endif
