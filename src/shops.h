/* shops.h - merchants: SHOPPES.OVL (common code, provisioner, reagents, stables, armoury,
 * healer), SHOPPES2.OVL (tavern, shipwright) and SHOPPES3.OVL (inn). Entered from open_shop
 * (talk.cpp) with shop_type, shop_town, shopkeeper_name and shop_name set. Parameter order
 * follows the Ghidra signatures.
 *
 * Shop text comes from SHOPPE.DAT (shop_say loads 1500 bytes at an offset). In that text, and in
 * the strings passed to shop_print, bytes >= 0x80 are words from the talk word table and
 * # $ % ^ & * @ are substitutions: keeper, shop name, price, quantity, item, item 2, time of day.
 *
 * Prices go up for low intelligence: price + price * (100 - 3 * INT) / 100. Most shops divide
 * unsigned, so from INT 34 on the negative product wraps into a huge number; ported as is. */
#ifndef U5_SHOPS_H
#define U5_SHOPS_H

#include <stdint.h>
#include "town.h"

/* These routines are methods of Town (town.h). */

#endif
