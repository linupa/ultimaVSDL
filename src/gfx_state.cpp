/* gfx_state.cpp - the Gfx object itself. See gfx.h; the drawing routines are in gfx.cpp. (Apart,
 * so that a program which only needs the drawing state does not have to link the rest.) */
#include "gfx.h"

Gfx &Gfx::instance()
{
    static Gfx the_gfx;
    return the_gfx;
}

Gfx &gfx = Gfx::instance();
