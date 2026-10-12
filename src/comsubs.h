/* comsubs.h - COMSUBS.OVL: attacks, aiming, missiles and the messages of a hit. Parameter order
 * follows the Ghidra signatures. Weapons are item ids (0xFF = bare hands); ids 0x23 and up count
 * as magic. item_range gives the reach (0 = hand to hand) and item_missile the missile drawn. */
#ifndef U5_COMSUBS_H
#define U5_COMSUBS_H

#include <stdint.h>
#include "combat.h"

/* The attack, aiming and missile routines are methods of Combat (combat.h); these are not. */
long distance_squared(int y1, int x1, int y2, int x2);    /* e638 */
int  distance(int y1, int x1, int y2, int x2);            /* e66a: square root, rounded down */
Square pixel_to_square(int py, int px);                   /* 3fb4: the view square a pixel is in */
int  is_square_visible(int y, int x);                     /* 3f6e */

#endif
