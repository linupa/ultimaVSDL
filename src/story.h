/* story.h - FONT.OVL: proportional story text, the attract demo ("Return to the View") and
 * character creation. Parameter order follows the Ghidra signatures. */
#ifndef U5_STORY_H
#define U5_STORY_H

#include <stdint.h>

/* Draws text with the proportional font (PROPORT.PCS) into the back page at story_x / story_y,
 * word-wrapped and justified between two pairs of margins: lines whose y lies between
 * story_line_min and story_line_max use the second pair (text beside a picture). In the text,
 * '_' marks where a word may be hyphenated and '{' leaves a 15-pixel gap; a line is 9 pixels. */
void draw_story_text(const uint8_t *text, uint16_t font);   /* e1e0 */

void demo_draw_cell(int row, int col);     /* e482 */
int  demo_step(int frames);                /* e4dc: 1 if a key was pressed */
void demo_caption(int n);                  /* e5f8 */
void attract_demo(void);                   /* e684 */

int  random_virtue(void);                  /* eb78 */
void ask_virtue_question(uint16_t picture, uint16_t font);   /* eba8 */
void create_character(void);               /* ecea: writes SAVED.GAM and SAVED.OOL */

/* Port helper: wait for a key (the original spins on poll_key). Returns it, or 0 on quit. */
int  wait_any_key(void);

/* The story text margins: dg.story_left[i] / dg.story_right[i] as ds offsets. */
#define STORY_LEFT(i)   (0x5146 + (i) * 2)
#define STORY_RIGHT(i)  (0x514C + (i) * 2)

#endif
